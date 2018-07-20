#include <iostream>
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

  regs_.at(ix) = Csr<URV>(name, csrn, mandatory, implemented, resetValue,
			  writeMask);
  nameToNumber_[name] = csrn;

  return &regs_.at(ix);
}


template <typename URV>
bool
CsRegs<URV>::findCsr(const std::string& name, Csr<URV>& reg) const
{
  const auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  size_t num = size_t(iter->second);
  if (num >= regs_.size())
    return false;

  reg = regs_.at(num);
  return true;
}


template <typename URV>
bool
CsRegs<URV>::findCsr(CsrNumber number, Csr<URV>& reg) const
{
  size_t ix = size_t(number);

  if (ix >= regs_.size())
    return false;

  reg = regs_.at(ix);
  return true;
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

  if (csr->isDebug() and not debugMode)
    return false;

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    {
      if (not writeTdata(number, mode, debugMode, value))
	return false;
    }
  else if (number == CsrNumber::MDSEAL)
    {
      // Least sig bit of MDSEAL_CSR can only be cleared.
      if ((value & 1) == 0)
	csr->write(value);
    }
  else
    csr->write(value);

  recordWrite(number);

  // fflags and frm are parts of fcsr
  if (number <= CsrNumber::FCSR)  // FFLAGS, FRM or FCSR.
    updateFcsrGroupForWrite(number, value);

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

  auto& csr = regs_.at(num);
  if (csr.isMandatory() and not implemented)
    {
      std::cerr << "CSR " << name << " is mandatory and is being configured "
		<< " as non-implemented -- configuration ignored.\n";
      return false;
    }

  csr.setValid(implemented);
  csr.setInitialValue(resetValue);
  csr.setWriteMask(mask);
  csr.setPokeMask(pokeMask);

  csr.pokeNoMask(resetValue);

  // Cahche interrupt enable.
  if (CsrNumber(num) == CsrNumber::MSTATUS)
    {
      MstatusFields<URV> fields(csr.read());
      interruptEnable_ = fields.bits_.MIE;
    }

  return true;
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

  // Machine Counter/Timers
  defineCsr("mcycle", CsrNumber::MCYCLE, mand, imp, 0);
  defineCsr("minstret", CsrNumber::MINSTRET, mand, imp, 0);
  defineCsr("mhpmcounter3", CsrNumber::MHPMCOUNTER3, mand, imp, 0);
  defineCsr("mhpmcounter4", CsrNumber::MHPMCOUNTER4, mand, imp, 0);
  defineCsr("mhpmcounter5", CsrNumber::MHPMCOUNTER5, mand, imp, 0);
  defineCsr("mhpmcounter6", CsrNumber::MHPMCOUNTER6, mand, imp, 0);
  defineCsr("mhpmcounter7", CsrNumber::MHPMCOUNTER7, mand, imp, 0);
  defineCsr("mhpmcounter8", CsrNumber::MHPMCOUNTER8, mand, imp, 0);
  defineCsr("mhpmcounter9", CsrNumber::MHPMCOUNTER9, mand, imp, 0);
  defineCsr("mhpmcounter10", CsrNumber::MHPMCOUNTER10, mand, imp, 0);
  defineCsr("mhpmcounter11", CsrNumber::MHPMCOUNTER11, mand, imp, 0);
  defineCsr("mhpmcounter12", CsrNumber::MHPMCOUNTER12, mand, imp, 0);
  defineCsr("mhpmcounter13", CsrNumber::MHPMCOUNTER13, mand, imp, 0);
  defineCsr("mhpmcounter14", CsrNumber::MHPMCOUNTER14, mand, imp, 0);
  defineCsr("mhpmcounter15", CsrNumber::MHPMCOUNTER15, mand, imp, 0);
  defineCsr("mhpmcounter16", CsrNumber::MHPMCOUNTER16, mand, imp, 0);
  defineCsr("mhpmcounter17", CsrNumber::MHPMCOUNTER17, mand, imp, 0);
  defineCsr("mhpmcounter18", CsrNumber::MHPMCOUNTER18, mand, imp, 0);
  defineCsr("mhpmcounter19", CsrNumber::MHPMCOUNTER19, mand, imp, 0);
  defineCsr("mhpmcounter20", CsrNumber::MHPMCOUNTER20, mand, imp, 0);
  defineCsr("mhpmcounter21", CsrNumber::MHPMCOUNTER21, mand, imp, 0);
  defineCsr("mhpmcounter22", CsrNumber::MHPMCOUNTER22, mand, imp, 0);
  defineCsr("mhpmcounter23", CsrNumber::MHPMCOUNTER23, mand, imp, 0);
  defineCsr("mhpmcounter24", CsrNumber::MHPMCOUNTER24, mand, imp, 0);
  defineCsr("mhpmcounter25", CsrNumber::MHPMCOUNTER25, mand, imp, 0);
  defineCsr("mhpmcounter26", CsrNumber::MHPMCOUNTER26, mand, imp, 0);
  defineCsr("mhpmcounter27", CsrNumber::MHPMCOUNTER27, mand, imp, 0);
  defineCsr("mhpmcounter28", CsrNumber::MHPMCOUNTER28, mand, imp, 0);
  defineCsr("mhpmcounter29", CsrNumber::MHPMCOUNTER29, mand, imp, 0);
  defineCsr("mhpmcounter30", CsrNumber::MHPMCOUNTER30, mand, imp, 0);
  defineCsr("mhpmcounter31", CsrNumber::MHPMCOUNTER31, mand, imp, 0);

  defineCsr("mcycleh", CsrNumber::MCYCLEH, mand, imp, 0);
  defineCsr("minstreth", CsrNumber::MINSTRETH, mand, imp, 0);

  defineCsr("mhpmcounter3h", CsrNumber::MHPMCOUNTER3H, mand, imp, 0);
  defineCsr("mhpmcounter4h", CsrNumber::MHPMCOUNTER4H, mand, imp, 0);
  defineCsr("mhpmcounter5h", CsrNumber::MHPMCOUNTER5H, mand, imp, 0);
  defineCsr("mhpmcounter6h", CsrNumber::MHPMCOUNTER6H, mand, imp, 0);
  defineCsr("mhpmcounter7h", CsrNumber::MHPMCOUNTER7H, mand, imp, 0);
  defineCsr("mhpmcounter8h", CsrNumber::MHPMCOUNTER8H, mand, imp, 0);
  defineCsr("mhpmcounter9h", CsrNumber::MHPMCOUNTER9H, mand, imp, 0);
  defineCsr("mhpmcounter10h", CsrNumber::MHPMCOUNTER10H, mand, imp, 0);
  defineCsr("mhpmcounter11h", CsrNumber::MHPMCOUNTER11H, mand, imp, 0);
  defineCsr("mhpmcounter12h", CsrNumber::MHPMCOUNTER12H, mand, imp, 0);
  defineCsr("mhpmcounter13h", CsrNumber::MHPMCOUNTER13H, mand, imp, 0);
  defineCsr("mhpmcounter14h", CsrNumber::MHPMCOUNTER14H, mand, imp, 0);
  defineCsr("mhpmcounter15h", CsrNumber::MHPMCOUNTER15H, mand, imp, 0);
  defineCsr("mhpmcounter16h", CsrNumber::MHPMCOUNTER16H, mand, imp, 0);
  defineCsr("mhpmcounter17h", CsrNumber::MHPMCOUNTER17H, mand, imp, 0);
  defineCsr("mhpmcounter18h", CsrNumber::MHPMCOUNTER18H, mand, imp, 0);
  defineCsr("mhpmcounter19h", CsrNumber::MHPMCOUNTER19H, mand, imp, 0);
  defineCsr("mhpmcounter20h", CsrNumber::MHPMCOUNTER20H, mand, imp, 0);
  defineCsr("mhpmcounter21h", CsrNumber::MHPMCOUNTER21H, mand, imp, 0);
  defineCsr("mhpmcounter22h", CsrNumber::MHPMCOUNTER22H, mand, imp, 0);
  defineCsr("mhpmcounter23h", CsrNumber::MHPMCOUNTER23H, mand, imp, 0);
  defineCsr("mhpmcounter24h", CsrNumber::MHPMCOUNTER24H, mand, imp, 0);
  defineCsr("mhpmcounter25h", CsrNumber::MHPMCOUNTER25H, mand, imp, 0);
  defineCsr("mhpmcounter26h", CsrNumber::MHPMCOUNTER26H, mand, imp, 0);
  defineCsr("mhpmcounter27h", CsrNumber::MHPMCOUNTER27H, mand, imp, 0);
  defineCsr("mhpmcounter28h", CsrNumber::MHPMCOUNTER28H, mand, imp, 0);
  defineCsr("mhpmcounter29h", CsrNumber::MHPMCOUNTER29H, mand, imp, 0);
  defineCsr("mhpmcounter30h", CsrNumber::MHPMCOUNTER30H, mand, imp, 0);
  defineCsr("mhpmcounter31h", CsrNumber::MHPMCOUNTER31H, mand, imp, 0);

  // Machine counter setup.
  defineCsr("mhpmevent3", CsrNumber::MHPMEVENT3, mand, imp, 0);
  defineCsr("mhpmevent4", CsrNumber::MHPMEVENT4, mand, imp, 0);
  defineCsr("mhpmevent5", CsrNumber::MHPMEVENT5, mand, imp, 0);
  defineCsr("mhpmevent6", CsrNumber::MHPMEVENT6, mand, imp, 0);
  defineCsr("mhpmevent7", CsrNumber::MHPMEVENT7, mand, imp, 0);
  defineCsr("mhpmevent8", CsrNumber::MHPMEVENT8, mand, imp, 0);
  defineCsr("mhpmevent9", CsrNumber::MHPMEVENT9, mand, imp, 0);
  defineCsr("mhpmevent10", CsrNumber::MHPMEVENT10, mand, imp, 0);
  defineCsr("mhpmevent11", CsrNumber::MHPMEVENT11, mand, imp, 0);
  defineCsr("mhpmevent12", CsrNumber::MHPMEVENT12, mand, imp, 0);
  defineCsr("mhpmevent13", CsrNumber::MHPMEVENT13, mand, imp, 0);
  defineCsr("mhpmevent14", CsrNumber::MHPMEVENT14, mand, imp, 0);
  defineCsr("mhpmevent15", CsrNumber::MHPMEVENT15, mand, imp, 0);
  defineCsr("mhpmevent16", CsrNumber::MHPMEVENT16, mand, imp, 0);
  defineCsr("mhpmevent17", CsrNumber::MHPMEVENT17, mand, imp, 0);
  defineCsr("mhpmevent18", CsrNumber::MHPMEVENT18, mand, imp, 0);
  defineCsr("mhpmevent19", CsrNumber::MHPMEVENT19, mand, imp, 0);
  defineCsr("mhpmevent20", CsrNumber::MHPMEVENT20, mand, imp, 0);
  defineCsr("mhpmevent21", CsrNumber::MHPMEVENT21, mand, imp, 0);
  defineCsr("mhpmevent22", CsrNumber::MHPMEVENT22, mand, imp, 0);
  defineCsr("mhpmevent23", CsrNumber::MHPMEVENT23, mand, imp, 0);
  defineCsr("mhpmevent24", CsrNumber::MHPMEVENT24, mand, imp, 0);
  defineCsr("mhpmevent25", CsrNumber::MHPMEVENT25, mand, imp, 0);
  defineCsr("mhpmevent26", CsrNumber::MHPMEVENT26, mand, imp, 0);
  defineCsr("mhpmevent27", CsrNumber::MHPMEVENT27, mand, imp, 0);
  defineCsr("mhpmevent28", CsrNumber::MHPMEVENT28, mand, imp, 0);
  defineCsr("mhpmevent29", CsrNumber::MHPMEVENT29, mand, imp, 0);
  defineCsr("mhpmevent30", CsrNumber::MHPMEVENT30, mand, imp, 0);
  defineCsr("mhpmevent31", CsrNumber::MHPMEVENT31, mand, imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineSupervisorRegs()
{
  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Supervisor trap SETUP_CSR.
  defineCsr("sstatus", CsrNumber::SSTATUS, !mand, !imp, 0);
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
  defineCsr("ustatus", CsrNumber::USTATUS, !mand, !imp, 0);
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

  defineCsr("hpmcounter3", CsrNumber::HPMCOUNTER3, !mand, !imp, 0);
  defineCsr("hpmcounter4", CsrNumber::HPMCOUNTER4, !mand, !imp, 0);
  defineCsr("hpmcounter5", CsrNumber::HPMCOUNTER5, !mand, !imp, 0);
  defineCsr("hpmcounter6", CsrNumber::HPMCOUNTER6, !mand, !imp, 0);
  defineCsr("hpmcounter7", CsrNumber::HPMCOUNTER7, !mand, !imp, 0);
  defineCsr("hpmcounter8", CsrNumber::HPMCOUNTER8, !mand, !imp, 0);
  defineCsr("hpmcounter9", CsrNumber::HPMCOUNTER9, !mand, !imp, 0);
  defineCsr("hpmcounter10", CsrNumber::HPMCOUNTER10, !mand, !imp, 0);
  defineCsr("hpmcounter11", CsrNumber::HPMCOUNTER11, !mand, !imp, 0);
  defineCsr("hpmcounter12", CsrNumber::HPMCOUNTER12, !mand, !imp, 0);
  defineCsr("hpmcounter13", CsrNumber::HPMCOUNTER13, !mand, !imp, 0);
  defineCsr("hpmcounter14", CsrNumber::HPMCOUNTER14, !mand, !imp, 0);
  defineCsr("hpmcounter15", CsrNumber::HPMCOUNTER15, !mand, !imp, 0);
  defineCsr("hpmcounter16", CsrNumber::HPMCOUNTER16, !mand, !imp, 0);
  defineCsr("hpmcounter17", CsrNumber::HPMCOUNTER17, !mand, !imp, 0);
  defineCsr("hpmcounter18", CsrNumber::HPMCOUNTER18, !mand, !imp, 0);
  defineCsr("hpmcounter19", CsrNumber::HPMCOUNTER19, !mand, !imp, 0);
  defineCsr("hpmcounter20", CsrNumber::HPMCOUNTER20, !mand, !imp, 0);
  defineCsr("hpmcounter21", CsrNumber::HPMCOUNTER21, !mand, !imp, 0);
  defineCsr("hpmcounter22", CsrNumber::HPMCOUNTER22, !mand, !imp, 0);
  defineCsr("hpmcounter23", CsrNumber::HPMCOUNTER23, !mand, !imp, 0);
  defineCsr("hpmcounter24", CsrNumber::HPMCOUNTER24, !mand, !imp, 0);
  defineCsr("hpmcounter25", CsrNumber::HPMCOUNTER25, !mand, !imp, 0);
  defineCsr("hpmcounter26", CsrNumber::HPMCOUNTER26, !mand, !imp, 0);
  defineCsr("hpmcounter27", CsrNumber::HPMCOUNTER27, !mand, !imp, 0);
  defineCsr("hpmcounter28", CsrNumber::HPMCOUNTER28, !mand, !imp, 0);
  defineCsr("hpmcounter29", CsrNumber::HPMCOUNTER29, !mand, !imp, 0);
  defineCsr("hpmcounter30", CsrNumber::HPMCOUNTER30, !mand, !imp, 0);
  defineCsr("hpmcounter31", CsrNumber::HPMCOUNTER31, !mand, !imp, 0);

  defineCsr("cycleh", CsrNumber::CYCLEH, !mand, !imp, 0);
  defineCsr("timeh", CsrNumber::TIMEH, !mand, !imp, 0);
  defineCsr("instreth", CsrNumber::INSTRETH, !mand, !imp, 0);

  defineCsr("hpmcounter3h", CsrNumber::HPMCOUNTER3H, !mand, !imp, 0);
  defineCsr("hpmcounter4h", CsrNumber::HPMCOUNTER4H, !mand, !imp, 0);
  defineCsr("hpmcounter5h", CsrNumber::HPMCOUNTER5H, !mand, !imp, 0);
  defineCsr("hpmcounter6h", CsrNumber::HPMCOUNTER6H, !mand, !imp, 0);
  defineCsr("hpmcounter7h", CsrNumber::HPMCOUNTER7H, !mand, !imp, 0);
  defineCsr("hpmcounter8h", CsrNumber::HPMCOUNTER8H, !mand, !imp, 0);
  defineCsr("hpmcounter9h", CsrNumber::HPMCOUNTER9H, !mand, !imp, 0);
  defineCsr("hpmcounter10h", CsrNumber::HPMCOUNTER10H, !mand, !imp, 0);
  defineCsr("hpmcounter11h", CsrNumber::HPMCOUNTER11H, !mand, !imp, 0);
  defineCsr("hpmcounter12h", CsrNumber::HPMCOUNTER12H, !mand, !imp, 0);
  defineCsr("hpmcounter13h", CsrNumber::HPMCOUNTER13H, !mand, !imp, 0);
  defineCsr("hpmcounter14h", CsrNumber::HPMCOUNTER14H, !mand, !imp, 0);
  defineCsr("hpmcounter15h", CsrNumber::HPMCOUNTER15H, !mand, !imp, 0);
  defineCsr("hpmcounter16h", CsrNumber::HPMCOUNTER16H, !mand, !imp, 0);
  defineCsr("hpmcounter17h", CsrNumber::HPMCOUNTER17H, !mand, !imp, 0);
  defineCsr("hpmcounter18h", CsrNumber::HPMCOUNTER18H, !mand, !imp, 0);
  defineCsr("hpmcounter19h", CsrNumber::HPMCOUNTER19H, !mand, !imp, 0);
  defineCsr("hpmcounter20h", CsrNumber::HPMCOUNTER20H, !mand, !imp, 0);
  defineCsr("hpmcounter21h", CsrNumber::HPMCOUNTER21H, !mand, !imp, 0);
  defineCsr("hpmcounter22h", CsrNumber::HPMCOUNTER22H, !mand, !imp, 0);
  defineCsr("hpmcounter23h", CsrNumber::HPMCOUNTER23H, !mand, !imp, 0);
  defineCsr("hpmcounter24h", CsrNumber::HPMCOUNTER24H, !mand, !imp, 0);
  defineCsr("hpmcounter25h", CsrNumber::HPMCOUNTER25H, !mand, !imp, 0);
  defineCsr("hpmcounter26h", CsrNumber::HPMCOUNTER26H, !mand, !imp, 0);
  defineCsr("hpmcounter27h", CsrNumber::HPMCOUNTER27H, !mand, !imp, 0);
  defineCsr("hpmcounter28h", CsrNumber::HPMCOUNTER28H, !mand, !imp, 0);
  defineCsr("hpmcounter29h", CsrNumber::HPMCOUNTER29H, !mand, !imp, 0);
  defineCsr("hpmcounter30h", CsrNumber::HPMCOUNTER30H, !mand, !imp, 0);
  defineCsr("hpmcounter31h", CsrNumber::HPMCOUNTER31H, !mand, !imp, 0);
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
  data1Val.mcontrol_.type_ = unsigned(TriggerType::Address);
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
  URV dcsrMask = 0x00008e07;
  URV dcsrPokeMask = dcsrMask | 0x1c0; // Cause field modifiable
  Reg* dcsr = defineCsr("dcsr", CsrNumber::DCSR, !mand, imp, dcsrVal, dcsrMask);
  dcsr->setIsDebug(true);
  dcsr->setPokeMask(dcsrPokeMask);

  // Least sig bit of dpc is not writeable.
  URV dpcMask = ~URV(1);
  Reg* dpc = defineCsr("dpc", CsrNumber::DPC, !mand, imp, 0, dpcMask);
  dpc->setIsDebug(true);

  Reg* dscratch = defineCsr("dscratch", CsrNumber::DSCRATCH, !mand, !imp, 0);
  dscratch->setIsDebug(true);
}


template <typename URV>
void
CsRegs<URV>::defineNonStandardRegs()
{
  URV romask = 0;  // Mask for read-only regs.

  bool mand = true; // Mandatory.
  bool imp = true;  // Implemented.

  defineCsr("mrac",   CsrNumber::MRAC, !mand, imp, 0);

  // mdseac is read-only to CSR instructions but is modifiable with
  // poke.
  auto mdseac = defineCsr("mdseac", CsrNumber::MDSEAC, !mand, imp, 0, romask);
  mdseac->setPokeMask(~romask);

  URV mask = 1;  // Only least sig bit writeable.
  defineCsr("mdseal", CsrNumber::MDSEAL, !mand, imp, 0, mask);

  // Least sig 10 bits of interrupt vector table (meivt) are read only.
  mask = (~URV(0)) << 10;
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
  defineCsr("mmst", CsrNumber::MMST, !mand, imp, 0, 0);
}


template <typename URV>
uint64_t
CsRegs<URV>::getRetiredInstCount() const
{
  const Csr<URV>* csr = getImplementedCsr(CsrNumber::MINSTRET);
  if (not csr)
    return 0;

  if (sizeof(URV) == 8)  // 64-bit machine
    return csr->read();

  const Csr<URV>* csrh = getImplementedCsr(CsrNumber::MINSTRETH);
  if (not csrh)
    return 0;

  uint64_t count = uint64_t(csrh->read()) << 32;
  count |= csr->read();
  return count;
}


template <typename URV>
bool
CsRegs<URV>::setRetiredInstCount(uint64_t count)
{
  Csr<URV>* csr = getImplementedCsr(CsrNumber::MINSTRET);
  if (not csr)
    return false;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      csr->write(count);
      return true;
    }

  Csr<URV>* csrh = getImplementedCsr(CsrNumber::MINSTRETH);
  if (not csrh)
    return false;
  csrh->write(count >> 32);
  csr->write(count);
  return true;
}



template <typename URV>
uint64_t
CsRegs<URV>::getCycleCount() const
{
  const Csr<URV>* csr = getImplementedCsr(CsrNumber::MCYCLE);
  if (not csr)
    return 0;

  if (sizeof(URV) == 8)  // 64-bit machine
    return csr->read();

  const Csr<URV>* csrh = getImplementedCsr(CsrNumber::MCYCLEH);
  if (not csrh)
    return 0;

  uint64_t count = uint64_t(csrh->read()) << 32;
  count |= csr->read();
  return count;
}


template <typename URV>
bool
CsRegs<URV>::setCycleCount(uint64_t count)
{
  Csr<URV>* csr = getImplementedCsr(CsrNumber::MCYCLE);
  if (not csr)
    return 0;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      csr->write(count);
      return true;
    }

  Csr<URV>* csrh = getImplementedCsr(CsrNumber::MCYCLEH);
  if (not csrh)
    return false;

  csrh->write(count >> 32);
  csr->write(count);
  return true;
}


template <typename URV>
bool
CsRegs<URV>::poke(CsrNumber number, PrivilegeMode mode, URV value)
{
  Csr<URV>* csr = getImplementedCsr(number);
  if (not csr)
    return false;

  if (mode < csr->privilegeMode())
    return false;

  bool debugMode = true;

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    return writeTdata(number, mode, debugMode, value);

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
      bool ok = triggers_.writeData1(trigger, value);
      if (ok) 
	{
	  // TDATA1 modified, update cached values
	  hasActiveTrigger_ = triggers_.hasActiveTrigger();
	  hasActiveInstTrigger_ = triggers_.hasActiveInstTrigger();
	}
      return ok;
    }

  if (number == CsrNumber::TDATA2)
    return triggers_.writeData2(trigger, value);

  if (number == CsrNumber::TDATA3)
    return triggers_.writeData3(trigger, value);

  return false;
}


template class WdRiscv::CsRegs<uint32_t>;
template class WdRiscv::CsRegs<uint64_t>;
