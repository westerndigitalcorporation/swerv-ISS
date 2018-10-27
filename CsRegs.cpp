#include <iostream>
#include <sstream>
#include <assert.h>
#include "CsRegs.hpp"


using namespace WdRiscv;


template <typename URV>
CsRegs<URV>::CsRegs()
{
  // Allocate CSR vector.  All entries are invalid.
  regs_.clear();
  regs_.resize(size_t(CsrNumber::MAX_CSR_) + 1);

  // Define CSR entries.
  defineMachineRegs();
  defineSupervisorRegs();
  defineUserRegs();
  defineDebugRegs();
  defineNonStandardRegs();
}


template <typename URV>
CsRegs<URV>::~CsRegs()
{
  regs_.clear();
  nameToNumber_.clear();
}


template <typename URV>
Csr<URV>*
CsRegs<URV>::defineCsr(const std::string& name, CsrNumber csrn, bool mandatory,
		       bool implemented, URV resetValue, URV writeMask)
{
  size_t ix = size_t(csrn);

  if (ix >= regs_.size())
    return nullptr;

  if (nameToNumber_.count(name))
    {
      std::cerr << "Error: Csr " << name << " already defined\n";
      return nullptr;
    }

  auto& csr = regs_.at(ix);
  csr.config(name, csrn, mandatory, implemented, resetValue, writeMask);

  nameToNumber_[name] = csrn;
  return &csr;
}


template <typename URV>
const Csr<URV>*
CsRegs<URV>::findCsr(const std::string& name) const
{
  const auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return nullptr;

  size_t num = size_t(iter->second);
  if (num >= regs_.size())
    return nullptr;

  return &regs_.at(num);
}


template <typename URV>
const Csr<URV>*
CsRegs<URV>::findCsr(CsrNumber number) const
{
  size_t ix = size_t(number);
  if (ix >= regs_.size())
    return nullptr;
  return &regs_.at(ix);
}


template <typename URV>
bool
CsRegs<URV>::read(CsrNumber number, PrivilegeMode mode,
		  bool debugMode, URV& value) const
{
  const Csr<URV>* csr = getImplementedCsr(number);
  if (not csr)
    return false;

  if (mode < csr->privilegeMode())
    return false;

  if (csr->isDebug() and not debugMode)
    return false;

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    return readTdata(number, mode, debugMode, value);

  value = csr->read();
  return true;
}
  

template <typename URV>
bool
CsRegs<URV>::write(CsrNumber number, PrivilegeMode mode, bool debugMode,
		   URV value)
{
  Csr<URV>* csr = getImplementedCsr(number);
  if (not csr or mode < csr->privilegeMode() or csr->isReadOnly())
    return false;

  // fflags and frm are part of fcsr
  if (number <= CsrNumber::FCSR)  // FFLAGS, FRM or FCSR.
    {
      csr->write(value);
      recordWrite(number);
      updateFcsrGroupForWrite(number, value);
      return true;
    }

  if (csr->isDebug() and not debugMode)
    return false;

  if (number == CsrNumber::MRAC)
    {
      // A value of 11 (io/cacheable) for the ith region is invalid:
      // Make it 10 (io/non-cacheable).
      URV mask = 1;
      for (unsigned i = 0; i < sizeof(URV)*8; i += 2)
	{
	  if ((value & mask) and (value & (mask << 1)))
	    value = value & ~mask;
	  mask = mask << 2;
	}
    }

  if (number >= CsrNumber::MHPMEVENT3 and number <= CsrNumber::MHPMEVENT31)
    {
      if (value > maxEventId_)
	value = maxEventId_;
      unsigned counterIx = unsigned(number) - unsigned(CsrNumber::MHPMEVENT3);
      assignEventToCounter(value, counterIx);
    }

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    {
      if (not writeTdata(number, mode, debugMode, value))
	return false;
    }
  else
    csr->write(value);

  recordWrite(number);

  // Writing MDEAU unlocks mdseac.
  if (number == CsrNumber::MDEAU)
    lockMdseac(false);

  // Cache interrupt enable.
  if (number == CsrNumber::MSTATUS)
    {
      MstatusFields<URV> fields(csr->read());
      interruptEnable_ = fields.bits_.MIE;
    }

  // Writing of the MEIVT changes the base address in MEIHAP.
  if (number == CsrNumber::MEIVT)
    {
      value = (value >> 10) << 10;  // Clear least sig 10 bits keeping base.
      size_t meihapIx = size_t(CsrNumber::MEIHAP);
      URV meihap = regs_.at(meihapIx).read();
      meihap &= 0x3ff;  // Clear base address bits.
      meihap |= value;  // Copy base address bits from MEIVT.
      regs_.at(meihapIx).poke(value);
      recordWrite(CsrNumber::MEIHAP);
    }

  return true;
}


template <typename URV>
bool
CsRegs<URV>::isWriteable(CsrNumber number, PrivilegeMode mode,
			 bool debugMode) const
{
  const Csr<URV>* csr = getImplementedCsr(number);
  if (not csr)
    return false;

  if (mode < csr->privilegeMode())
    return false;

  if (csr->isReadOnly())
    return false;

  if (csr->isDebug() and not debugMode)
    return false;

  return true;
}


template <typename URV>
void
CsRegs<URV>::reset()
{
  for (auto& csr : regs_)
    if (csr.isImplemented())
      csr.reset();

  // Cache interrupt enable.
  Csr<URV>* mstatus = getImplementedCsr(CsrNumber::MSTATUS);
  if (mstatus)
    {
      MstatusFields<URV> fields(mstatus->read());
      interruptEnable_ = fields.bits_.MIE;
    }

  mdseacLocked_ = false;
}


template <typename URV>
bool
CsRegs<URV>::configCsr(const std::string& name, bool implemented,
		       URV resetValue, URV mask, URV pokeMask)
{
  auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  size_t num = size_t(iter->second);
  if (num >= regs_.size())
    return false;

  return configCsr(CsrNumber(num), implemented, resetValue, mask, pokeMask);
}


template <typename URV>
bool
CsRegs<URV>::configCsr(CsrNumber csrNum, bool implemented,
		       URV resetValue, URV mask, URV pokeMask)
{
  if (size_t(csrNum) >= regs_.size())
    {
      std::cerr << "ConfigCsr: CSR number " << size_t(csrNum)
		<< " out of bound\n";
      return false;
    }

  auto& csr = regs_.at(size_t(csrNum));
  if (csr.isMandatory() and not implemented)
    {
      std::cerr << "CSR " << csr.getName() << " is mandatory and is being "
		<< "configured as non-implemented -- configuration ignored.\n";
      return false;
    }

  csr.setValid(implemented);
  csr.setInitialValue(resetValue);
  csr.setWriteMask(mask);
  csr.setPokeMask(pokeMask);

  csr.pokeNoMask(resetValue);

  // Cahche interrupt enable.
  if (csrNum == CsrNumber::MSTATUS)
    {
      MstatusFields<URV> fields(csr.read());
      interruptEnable_ = fields.bits_.MIE;
    }

  return true;
}


template <typename URV>
bool
CsRegs<URV>::configMachineModePerfCounters(unsigned numCounters)
{
  if (numCounters > 29)
    {
      std::cerr << "No more than 29 machine mode performance counters "
		<< "can be defined\n";
      return false;
    }

  unsigned errors = 0;

  for (unsigned i = 0; i < 29; ++i)
    {
      URV resetValue = 0, mask = ~URV(0), pokeMask = ~URV(0);
      if (i >= numCounters)
	mask = pokeMask = 0;

      CsrNumber csrNum = CsrNumber(i + unsigned(CsrNumber::MHPMCOUNTER3));
      if (not configCsr(csrNum, true, resetValue, mask, pokeMask))
	errors++;

      if constexpr (sizeof(URV) == 4)
         {
	   csrNum = CsrNumber(i + unsigned(CsrNumber::MHPMCOUNTER3H));
	   if (not configCsr(csrNum, true, resetValue, mask, pokeMask))
	     errors++;
	 }

      csrNum = CsrNumber(i + unsigned(CsrNumber::MHPMEVENT3));
      if (not configCsr(csrNum, true, resetValue, mask, pokeMask))
	errors++;
    }

  if (errors == 0)
    {
      mPerfRegs_.config(numCounters);
      tieMachinePerfCounters(mPerfRegs_.counters_);
    }

  return errors == 0;
}


template <typename URV>
void
CsRegs<URV>::updateFcsrGroupForWrite(CsrNumber number, URV value)
{
  if (number == CsrNumber::FFLAGS)
    {
      auto fcsr = getImplementedCsr(CsrNumber::FCSR);
      if (fcsr)
	{
	  URV fcsrVal = fcsr->read();
	  fcsrVal = (fcsrVal & ~URV(0x1f)) | (value & 0x1f);
	  fcsr->write(fcsrVal);
	  recordWrite(CsrNumber::FCSR);
	}
      return;
    }

  if (number == CsrNumber::FRM)
    {
      auto fcsr = getImplementedCsr(CsrNumber::FCSR);
      if (fcsr)
	{
	  URV fcsrVal = fcsr->read();
	  fcsrVal = (fcsrVal & ~URV(0xe0)) | ((value << 5) & 0xe0);
	  fcsr->write(fcsrVal);
	  recordWrite(CsrNumber::FCSR);
	}
      return;
    }

  if (number == CsrNumber::FCSR)
    {
      URV newVal = value & 0x1f;  // New fflags value
      auto fflags = getImplementedCsr(CsrNumber::FFLAGS);
      if (fflags and fflags->read() != newVal)
	{
	  fflags->write(newVal);
	  recordWrite(CsrNumber::FFLAGS);
	}

      newVal = (value >> 5) & 7;
      auto frm = getImplementedCsr(CsrNumber::FRM);
      if (frm and frm->read() != newVal)
	{
	  frm->write(newVal);
	  recordWrite(CsrNumber::FRM);
	}
    }
}


template <typename URV>
void
CsRegs<URV>::updateFcsrGroupForPoke(CsrNumber number, URV value)
{
  if (number == CsrNumber::FFLAGS)
    {
      auto fcsr = getImplementedCsr(CsrNumber::FCSR);
      if (fcsr)
	{
	  URV fcsrVal = fcsr->read();
	  fcsrVal = (fcsrVal & ~URV(0x1f)) | (value & 0x1f);
	  fcsr->poke(fcsrVal);
	}
      return;
    }

  if (number == CsrNumber::FRM)
    {
      auto fcsr = getImplementedCsr(CsrNumber::FCSR);
      if (fcsr)
	{
	  URV fcsrVal = fcsr->read();
	  fcsrVal = (fcsrVal & ~URV(0xe0)) | ((value << 5) & 0xe0);
	  fcsr->poke(fcsrVal);
	}
      return;
    }

  if (number == CsrNumber::FCSR)
    {
      URV newVal = value & 0x1f;  // New fflags value
      auto fflags = getImplementedCsr(CsrNumber::FFLAGS);
      if (fflags and fflags->read() != newVal)
	fflags->poke(newVal);

      newVal = (value >> 5) & 7;
      auto frm = getImplementedCsr(CsrNumber::FRM);
      if (frm and frm->read() != newVal)
	frm->poke(newVal);
    }
}


template <typename URV>
void
CsRegs<URV>::defineMachineRegs()
{
  URV romask = 0;  // Mask for read-only regs.

  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Machine info.
  defineCsr("mvendorid", CsrNumber::MVENDORID, mand, imp, 0, romask);
  defineCsr("marchid", CsrNumber::MARCHID, mand, imp, 0, romask);
  defineCsr("mimpid", CsrNumber::MIMPID, mand, imp, 0, romask);
  defineCsr("mhartid", CsrNumber::MHARTID, mand, imp, 0, romask);

  // Machine trap setup.

  //                  S R        T T T M S M X  F  M  R  S M R S U M R S U
  //                  D E        S W V X U P S  S  P  E  P P E P P I E I I
  //                    S        R   M R M R       P  S  P I S I I E S E E
  //                                       V               E   E E
  URV mstatusMask = 0b0'00000000'1'1'1'1'1'1'11'11'11'00'1'1'0'1'1'1'0'1'1;
  URV mstatusVal = 0;
  if constexpr (sizeof(URV) == 8)
    mstatusMask |= (URV(0b1111) << 32);  // Mask for SXL and UXL.
  defineCsr("mstatus", CsrNumber::MSTATUS, mand, imp, mstatusVal, mstatusMask);
  defineCsr("misa", CsrNumber::MISA, mand,  imp, 0x40001104, romask);
  defineCsr("medeleg", CsrNumber::MEDELEG, !mand, !imp, 0);
  defineCsr("mideleg", CsrNumber::MIDELEG, !mand, !imp, 0);

  // Interrupt enable: Least sig 12 bits corresponding to the 12
  // interrupt causes are writable.
  URV mieMask = 0xfff; 
  defineCsr("mie", CsrNumber::MIE, mand, imp, 0, mieMask);

  // Initial value of 0: vectored interrupt. Mask of ~2 to make bit 1
  // non-writable.
  defineCsr("mtvec", CsrNumber::MTVEC, mand, imp, 0, ~URV(2));

  defineCsr("mcounteren", CsrNumber::MCOUNTEREN, !mand, !imp, 0);

  // Machine trap handling: mscratch and mepc.
  defineCsr("mscratch", CsrNumber::MSCRATCH, mand, imp, 0);
  URV mepcMask = ~URV(1);  // Bit 0 of MEPC is not writable.
  defineCsr("mepc", CsrNumber::MEPC, mand, imp, 0, mepcMask);

  // All bits of mcause writeable.
  defineCsr("mcause", CsrNumber::MCAUSE, mand, imp, 0);
  defineCsr("mtval", CsrNumber::MTVAL, mand, imp, 0);

  // MIP is read-only for CSR instructions but the bits corresponding
  // to defined interrupts are modifiable.
  Csr<URV>* mip = defineCsr("mip", CsrNumber::MIP, mand, imp, 0, romask);
  mip->setPokeMask(mieMask);

  // Machine protection and translation.
  defineCsr("pmpcfg0", CsrNumber::PMPCFG0, !mand, imp, 0);
  defineCsr("pmpcfg1", CsrNumber::PMPCFG1, !mand, imp, 0);
  defineCsr("pmpcfg2", CsrNumber::PMPCFG2, !mand, imp, 0);
  defineCsr("pmpcfg3", CsrNumber::PMPCFG3, !mand, imp, 0);
  defineCsr("pmpaddr0", CsrNumber::PMPADDR0, !mand, imp, 0);
  defineCsr("pmpaddr1", CsrNumber::PMPADDR1, !mand, imp, 0);
  defineCsr("pmpaddr2", CsrNumber::PMPADDR2, !mand, imp, 0);
  defineCsr("pmpaddr3", CsrNumber::PMPADDR3, !mand, imp, 0);
  defineCsr("pmpaddr4", CsrNumber::PMPADDR4, !mand, imp, 0);
  defineCsr("pmpaddr5", CsrNumber::PMPADDR5, !mand, imp, 0);
  defineCsr("pmpaddr6", CsrNumber::PMPADDR6, !mand, imp, 0);
  defineCsr("pmpaddr7", CsrNumber::PMPADDR7, !mand, imp, 0);
  defineCsr("pmpaddr8", CsrNumber::PMPADDR8, !mand, imp, 0);
  defineCsr("pmpaddr9", CsrNumber::PMPADDR9, !mand, imp, 0);
  defineCsr("pmpaddr10", CsrNumber::PMPADDR10, !mand, imp, 0);
  defineCsr("pmpaddr11", CsrNumber::PMPADDR11, !mand, imp, 0);
  defineCsr("pmpaddr12", CsrNumber::PMPADDR12, !mand, imp, 0);
  defineCsr("pmpaddr13", CsrNumber::PMPADDR13, !mand, imp, 0);
  defineCsr("pmpaddr14", CsrNumber::PMPADDR14, !mand, imp, 0);
  defineCsr("pmpaddr15", CsrNumber::PMPADDR15, !mand, imp, 0);

  // Machine Counter/Timers.
  defineCsr("mcycle", CsrNumber::MCYCLE, mand, imp, 0);
  defineCsr("minstret", CsrNumber::MINSTRET, mand, imp, 0);
  defineCsr("mcycleh", CsrNumber::MCYCLEH, mand, imp, 0);
  defineCsr("minstreth", CsrNumber::MINSTRETH, mand, imp, 0);

  // Define mhpmcounter3/mhpmcounter3h to mhpmcounter31/mhpmcounter31h
  // as write-anything/read-zero (user can change that in the config
  // file by setting the number of writeable counters). Same for
  // mhpmevent3/mhpmevent3h to mhpmevent3h/mhpmevent31h.
  for (unsigned i = 3; i <= 31; ++i)
    {
      CsrNumber csrNum = CsrNumber(unsigned(CsrNumber::MHPMCOUNTER3) + i - 3);
      std::ostringstream oss;
      oss << "mhpmcounter" << i;
      std::string name = oss.str();
      defineCsr(name, csrNum, mand, imp, 0, romask);

      // High register counterpart of mhpmcounter.
      name += "h";
      csrNum = CsrNumber(unsigned(CsrNumber::MHPMCOUNTER3H) + i - 3);
      defineCsr(name, csrNum, mand, imp, 0, romask);

      oss.str("");

      csrNum = CsrNumber(unsigned(CsrNumber::MHPMEVENT3) + i - 3);
      oss << "mhpmevent" << i;
      name = oss.str();
      defineCsr(name, csrNum, mand, imp, 0, romask);
    }
}


template <typename URV>
void
CsRegs<URV>::tieMachinePerfCounters(std::vector<uint64_t>& counters)
{
  if constexpr (sizeof(URV) == 4)
    {
      // Tie each mhpmcounter CSR value to the least significant 4
      // bytes of the corresponding counters_ entry. Tie each
      // mhpmcounterh CSR value to the most significan 4 bytes of the
      // corresponding counters_ entry.
      for (unsigned num = 3; num <= 31; ++num)
	{
	  unsigned ix = num - 3;
	  if (ix >= counters.size())
	    break;
	  unsigned lowIx = ix +  unsigned(CsrNumber::MHPMCOUNTER3);
	  Csr<URV>& csrLow = regs_.at(lowIx);
	  URV* loc = reinterpret_cast<URV*>(&counters.at(ix));
	  csrLow.tie(loc);

	  loc++;

	  unsigned highIx = ix +  unsigned(CsrNumber::MHPMCOUNTER3H);
	  Csr<URV>& csrHigh = regs_.at(highIx);
	  csrHigh.tie(loc);
	}
    }
  else
    {
      for (unsigned num = 3; num <= 31; ++num)
	{
	  unsigned ix = num - 3;
	  if (ix >= counters.size())
	    break;
	  unsigned csrIx = ix +  unsigned(CsrNumber::MHPMCOUNTER3);
	  Csr<URV>& csr = regs_.at(csrIx);
	  URV* loc = reinterpret_cast<URV*>(&counters.at(ix));
	  csr.tie(loc);
	}
    }
}


template <typename URV>
void
CsRegs<URV>::defineSupervisorRegs()
{
  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Supervisor trap SETUP_CSR.

  // Only bits spp, spie, upie, sie and uie of sstatus are writeable.
  URV mask = 0x233;
  defineCsr("sstatus", CsrNumber::SSTATUS, !mand, !imp, 0, mask);

  defineCsr("sedeleg", CsrNumber::SEDELEG, !mand, !imp, 0);
  defineCsr("sideleg", CsrNumber::SIDELEG, !mand, !imp, 0);
  defineCsr("sie", CsrNumber::SIE, !mand, !imp, 0);
  defineCsr("stvec", CsrNumber::STVEC, !mand, !imp, 0);
  defineCsr("scounteren", CsrNumber::SCOUNTEREN, !mand, !imp, 0);

  // Supervisor Trap Handling 
  defineCsr("sscratch", CsrNumber::SSCRATCH, !mand, !imp, 0);
  defineCsr("sepc", CsrNumber::SEPC, !mand, !imp, 0);
  defineCsr("scause", CsrNumber::SCAUSE, !mand, !imp, 0);
  defineCsr("stval", CsrNumber::STVAL, !mand, !imp, 0);
  defineCsr("sip", CsrNumber::SIP, !mand, !imp, 0);

  // Supervisor Protection and Translation 
  defineCsr("satp", CsrNumber::SATP, !mand, !imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineUserRegs()
{
  bool mand = true;  // Mandatory.
  bool imp = true; // Implemented.

  // User trap setup.
  URV mask = 0x11; // Only UPIE and UIE bits are writeable.
  defineCsr("ustatus", CsrNumber::USTATUS, !mand, !imp, 0, mask);
  defineCsr("uie", CsrNumber::UIE, !mand, !imp, 0);
  defineCsr("utvec", CsrNumber::UTVEC, !mand, !imp, 0);

  // User Trap Handling
  defineCsr("uscratch", CsrNumber::USCRATCH, !mand, !imp, 0);
  defineCsr("uepc", CsrNumber::UEPC, !mand, !imp, 0);
  defineCsr("ucause", CsrNumber::UCAUSE, !mand, !imp, 0);
  defineCsr("utval", CsrNumber::UTVAL, !mand, !imp, 0);
  defineCsr("uip", CsrNumber::UIP, !mand, !imp, 0);

  // User Floating-Point CSRs
  defineCsr("fflags", CsrNumber::FFLAGS, !mand, !imp, 0);
  defineCsr("frm", CsrNumber::FRM, !mand, !imp, 0);
  defineCsr("fcsr", CsrNumber::FCSR, !mand, !imp, 0, 0xff);

  // User Counter/Timers
  defineCsr("cycle", CsrNumber::CYCLE, !mand, imp, 0);
  defineCsr("time", CsrNumber::TIME, !mand, imp, 0);
  defineCsr("instret", CsrNumber::INSTRET, !mand, imp, 0);
  defineCsr("cycleh", CsrNumber::CYCLEH, !mand, !imp, 0);
  defineCsr("timeh", CsrNumber::TIMEH, !mand, !imp, 0);
  defineCsr("instreth", CsrNumber::INSTRETH, !mand, !imp, 0);

  // Define hpmcounter3/hpmcounter3h to hpmcounter31/hpmcounter31h
  // as write-anything/read-zero (user can change that in the config
  // file).  Same for mhpmevent3/mhpmevent3h to mhpmevent3h/mhpmevent31h.
  for (unsigned i = 3; i <= 31; ++i)
    {
      CsrNumber csrNum = CsrNumber(unsigned(CsrNumber::HPMCOUNTER3) + i - 3);
      std::ostringstream oss;
      oss << "hpmcounter" << i;
      std::string name = oss.str();
      defineCsr(name, csrNum, !mand, !imp, 0);

      // High register counterpart of mhpmcounter.
      name += "h";
      csrNum = CsrNumber(unsigned(CsrNumber::HPMCOUNTER3H) + i - 3);
      defineCsr(name, csrNum, !mand, !imp, 0);
    }
}


template <typename URV>
void
CsRegs<URV>::defineDebugRegs()
{
  typedef Csr<URV> Reg;

  bool mand = true;  // Mandatory.
  bool imp = true; // Implemented.

  // Debug/Trace registers.
  defineCsr("tselect", CsrNumber::TSELECT, !mand, imp, 0);
  defineCsr("tdata1", CsrNumber::TDATA1, !mand, imp, 0);
  defineCsr("tdata2", CsrNumber::TDATA2, !mand, imp, 0);
  defineCsr("tdata3", CsrNumber::TDATA3, !mand, !imp, 0);

  // Define triggers.
  URV triggerCount = 4;
  triggers_ = Triggers<URV>(triggerCount);

  Data1Bits<URV> data1Mask(0), data1Val(0);

  // Set the masks of the read-write fields of data1 to all 1.
  URV allOnes = ~URV(0);
  data1Mask.mcontrol_.dmode_   = allOnes;
  data1Mask.mcontrol_.hit_     = allOnes;
  data1Mask.mcontrol_.select_  = allOnes;
  data1Mask.mcontrol_.action_  = 1; // Only least sig bit writeable
  data1Mask.mcontrol_.chain_   = allOnes;
  data1Mask.mcontrol_.match_   = 1; // Only least sig bit of match is writeable.
  data1Mask.mcontrol_.m_       = allOnes;
  data1Mask.mcontrol_.execute_ = allOnes;
  data1Mask.mcontrol_.store_   = allOnes;
  data1Mask.mcontrol_.load_    = allOnes;

  // Set intitial values of fields of data1.
  data1Val.mcontrol_.type_ = unsigned(TriggerType::AddrData);
  data1Val.mcontrol_.maskMax_ = 8*sizeof(URV) - 1;  // 31 or 63.

  // Values, write-masks, and poke-masks of the three components of
  // the triggres.
  URV val1(data1Val.value_), val2(0), val3(0);
  URV wm1(data1Mask.value_), wm2(~URV(0)), wm3(0);
  URV pm1(wm1), pm2(wm2), pm3(wm3);

  triggers_.reset(0, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);
  triggers_.reset(1, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);
  triggers_.reset(2, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);

  Data1Bits<URV> icountMask(0), icountVal(0);

  icountMask.icount_.dmode_  = allOnes;
  icountMask.icount_.count_  = allOnes;
  icountMask.icount_.m_      = allOnes;
  icountMask.icount_.action_ = allOnes;

  icountVal.icount_.type_ = unsigned(TriggerType::InstCount);
  icountVal.icount_.count_ = 0;

  triggers_.reset(3, icountVal.value_, 0, 0, icountMask.value_, 0, 0,
		  icountMask.value_, 0, 0);

  hasActiveTrigger_ = triggers_.hasActiveTrigger();
  hasActiveInstTrigger_ = triggers_.hasActiveInstTrigger();

  // Debug mode registers.
  URV dcsrVal = 0x40000003;
  URV dcsrMask = 0x00008e04;
  URV dcsrPokeMask = dcsrMask | 0x1c8; // Cause field modifiable
  Reg* dcsr = defineCsr("dcsr", CsrNumber::DCSR, !mand, imp, dcsrVal, dcsrMask);
  dcsr->setIsDebug(true);
  dcsr->setPokeMask(dcsrPokeMask);

  // Least sig bit of dpc is not writeable.
  URV dpcMask = ~URV(1);
  Reg* dpc = defineCsr("dpc", CsrNumber::DPC, !mand, imp, 0, dpcMask);
  dpc->setPokeMask(dpcMask);
  dpc->setIsDebug(true);

  Reg* dscratch = defineCsr("dscratch", CsrNumber::DSCRATCH, !mand, !imp, 0);
  dscratch->setIsDebug(true);
}


template <typename URV>
void
CsRegs<URV>::defineNonStandardRegs()
{
  URV romask = 0;  // Mask for read-only regs that are immutable to
		   // change using csr instructions.

  bool mand = true; // Mandatory.
  bool imp = true;  // Implemented.

  defineCsr("mrac",   CsrNumber::MRAC, !mand, imp, 0);

  // mdseac is read-only to CSR instructions but is modifiable with
  // poke.
  auto mdseac = defineCsr("mdseac", CsrNumber::MDSEAC, !mand, imp, 0, romask);
  mdseac->setPokeMask(~romask);

  // mdeau is write-only, it unlocks mdseac when written, it always
  // reads zero.
  defineCsr("mdeau", CsrNumber::MDEAU, !mand, imp, 0, romask);

  // Least sig 10 bits of interrupt vector table (meivt) are read only.
  URV mask = (~URV(0)) << 10;
  defineCsr("meivt", CsrNumber::MEIVT, !mand, imp, 0, mask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  defineCsr("meipt", CsrNumber::MEIPT, !mand, imp, 0, mask);

  // The external interrupt claim-id/priority capture does not hold
  // any state. It always yield zero on read.
  defineCsr("meicpct", CsrNumber::MEICPCT, !mand, imp, 0, romask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  defineCsr("meicidpl", CsrNumber::MEICIDPL, !mand, imp, 0, mask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  defineCsr("meicurpl", CsrNumber::MEICURPL, !mand, imp, 0, mask);

  // None of the bits are writeable by CSR instructions. All but least
  // sig 2 bis are modifiable.
  mask = 0;

  auto meihap = defineCsr("meihap", CsrNumber::MEIHAP, !mand, imp, 0, mask);
  meihap->setPokeMask((~URV(0)) << 2);

  // Memory synchronization trigger register. Used in debug mode to
  // flush the cashes. It always reads zero. Writing 1 to least sig
  // bit flushes instruction cache. Writing 1 to bit 1 flushes data
  // cache.
  defineCsr("dmst", CsrNumber::DMST, !mand, imp, 0, 0);

  // Cache diagnositic
  mask = 0x0130fffc;
  defineCsr("dicawics", CsrNumber::DICAWICS, !mand, imp, 0, mask);

  mask = ~URV(0);
  defineCsr("dicad0", CsrNumber::DICAD0, !mand, imp, 0, mask);

  mask = 0x3;
  defineCsr("dicad1", CsrNumber::DICAD1, !mand, imp, 0, mask);

  mask = 0;  // Least sig bit is read0/write1
  auto dicgo = defineCsr("dicgo", CsrNumber::DICGO, !mand, imp, 0, mask);
  dicgo->setPokeMask(mask);

  mask = 1;  // Only least sig bit writeable
  auto mgpmc = defineCsr("mgpmc", CsrNumber::MGPMC, !mand, imp, 1, mask);
  mgpmc->setPokeMask(mask);

  // Internal timer/bound/control zero and one.
  mask = 0x00000017;
  defineCsr("mitcnt0", CsrNumber::MITCNT0, !mand, imp, 0);
  defineCsr("mitbnd0", CsrNumber::MITBND0, !mand, imp, 0);
  auto mitctl = defineCsr("mitctl0", CsrNumber::MITCTL0, !mand, imp, 1, mask);
  mitctl->setPokeMask(mask);

  defineCsr("mitcnt1", CsrNumber::MITCNT1, !mand, imp, 0);
  defineCsr("mitbnd1", CsrNumber::MITBND1, !mand, imp, 0);
  mitctl = defineCsr("mitctl1", CsrNumber::MITCTL1, !mand, imp, 1, mask);
  mitctl->setPokeMask(mask);

  // Core pause control regiser. Implemented as read only (once the hardware
  // writes it, the hart will pause until this counts down to zero). So, this
  // will always read zero.
  mask = 0;
  defineCsr("mcpc", CsrNumber::MCPC, !mand, imp, 0, mask);

  // Power managerment control register
  mask = 0;  // Least sig bit is read0/write1
  auto mpmc = defineCsr("mpmc", CsrNumber::MPMC, !mand, imp, 0, mask);
  mpmc->setPokeMask(mask);

  // Error correcting code.
  mask = ~URV(0);
  defineCsr("micect", CsrNumber::MICECT, !mand, imp, 0, mask);

  mask = ~URV(0);
  defineCsr("miccmect", CsrNumber::MICCMECT, !mand, imp, 0, mask);

  mask = ~URV(0);
  defineCsr("mdccmect", CsrNumber::MDCCMECT, !mand, imp, 0, mask);

  mask = 0xff;
  auto mcgc = defineCsr("mcgc", CsrNumber::MCGC, !mand, imp, 0, mask);
  mcgc->setPokeMask(mask);

  defineCsr("mfdc", CsrNumber::MFDC, !mand, imp, 0);
}


template <typename URV>
bool
CsRegs<URV>::peek(CsrNumber number, URV& value) const
{
  const Csr<URV>* csr = getImplementedCsr(number);
  if (not csr)
    return false;

  bool debugMode = true;

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    return readTdata(number, PrivilegeMode::Machine, debugMode, value);

  value = csr->read();
  return true;
}
  

template <typename URV>
bool
CsRegs<URV>::poke(CsrNumber number, URV value)
{
  Csr<URV>* csr = getImplementedCsr(number);
  if (not csr)
    return false;

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    return pokeTdata(number, value);

  if (number == CsrNumber::MRAC)
    {
      // A value of 11 (io/cacheable) for the ith region is invalid:
      // Make it 10 (io/non-cacheable).
      URV mask = 1;
      for (unsigned i = 0; i < sizeof(URV)*8; i += 2)
	{
	  if ((value & mask) and (value & (mask << 1)))
	    value = value & ~mask;
	  mask = mask << 2;
	}
    }

  if (number >= CsrNumber::MHPMEVENT3 and number <= CsrNumber::MHPMEVENT31)
    {
      if (value > maxEventId_)
	value = maxEventId_;
      unsigned counterIx = unsigned(number) - unsigned(CsrNumber::MHPMEVENT3);
      assignEventToCounter(value, counterIx);
    }

  csr->poke(value);

  // fflags and frm are parts of fcsr
  if (number <= CsrNumber::FCSR)  // FFLAGS, FRM or FCSR.
    updateFcsrGroupForPoke(number, value);

  // Cache interrupt enable.
  if (number == CsrNumber::MSTATUS)
    {
      MstatusFields<URV> fields(csr->read());
      interruptEnable_ = fields.bits_.MIE;
    }

  return true;
}


template <typename URV>
bool
CsRegs<URV>::readTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
		       URV& value) const
{
  // Determine currently selected trigger.
  URV trigger = 0;
  if (not read(CsrNumber::TSELECT, mode, debugMode, trigger))
    return false;

  if (number == CsrNumber::TDATA1)
    return triggers_.readData1(trigger, value);

  if (number == CsrNumber::TDATA2)
    return triggers_.readData2(trigger, value);

  if (number == CsrNumber::TDATA3)
    return triggers_.readData3(trigger, value);

  return false;
}


template <typename URV>
bool
CsRegs<URV>::writeTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
			URV value)
{
  // Determine currently selected trigger.
  URV trigger = 0;
  if (not read(CsrNumber::TSELECT, mode, debugMode, trigger))
    return false;

  if (number == CsrNumber::TDATA1)
    {
      bool ok = triggers_.writeData1(trigger, debugMode, value);
      if (ok) 
	{
	  // TDATA1 modified, update cached values
	  hasActiveTrigger_ = triggers_.hasActiveTrigger();
	  hasActiveInstTrigger_ = triggers_.hasActiveInstTrigger();
	}
      return ok;
    }

  if (number == CsrNumber::TDATA2)
    return triggers_.writeData2(trigger, debugMode, value);

  if (number == CsrNumber::TDATA3)
    return triggers_.writeData3(trigger, debugMode, value);

  return false;
}


template <typename URV>
bool
CsRegs<URV>::pokeTdata(CsrNumber number, URV value)
{
  // Determine currently selected trigger.
  URV trigger = 0;
  bool debugMode = true;
  if (not read(CsrNumber::TSELECT, PrivilegeMode::Machine, debugMode, trigger))
    return false;

  if (number == CsrNumber::TDATA1)
    {
      bool ok = triggers_.pokeData1(trigger, value);
      if (ok) 
	{
	  // TDATA1 modified, update cached values
	  hasActiveTrigger_ = triggers_.hasActiveTrigger();
	  hasActiveInstTrigger_ = triggers_.hasActiveInstTrigger();
	}
      return ok;
    }

  if (number == CsrNumber::TDATA2)
    return triggers_.pokeData2(trigger,value);

  if (number == CsrNumber::TDATA3)
    return triggers_.pokeData3(trigger, value);

  return false;
}



template class WdRiscv::CsRegs<uint32_t>;
template class WdRiscv::CsRegs<uint64_t>;
