#include <iostream>
#include <assert.h>
#include "CsRegs.hpp"


using namespace WdRiscv;


template <typename URV>
CsRegs<URV>::CsRegs() 
  : triggers_(0), traceWrites_(false)
{
  // Allocate CSR vector.  All entries are invalid.
  regs_.clear();
  regs_.resize(MAX_CSR_ + 1);

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
  if (csrn >= regs_.size())
    return nullptr;

  if (nameToNumber_.count(name))
    {
      std::cerr << "Error: Csr " << name << " already defined\n";
      return nullptr;
    }

  regs_.at(csrn) = Csr<URV>(name, csrn, mandatory, implemented, resetValue,
			    writeMask);
  nameToNumber_[name] = csrn;

  return &regs_.at(csrn);
}


template <typename URV>
bool
CsRegs<URV>::findCsr(const std::string& name, Csr<URV>& reg) const
{
  const auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  CsrNumber num = iter->second;
  if (num < 0 or num >= regs_.size())
      return false;

  reg = regs_.at(num);
  return true;
}


template <typename URV>
bool
CsRegs<URV>::findCsr(CsrNumber number, Csr<URV>& reg) const
{
  if (number < 0 or number >= regs_.size())
    return false;

  reg = regs_.at(number);
  return true;
}


template <typename URV>
bool
CsRegs<URV>::read(CsrNumber number, PrivilegeMode mode,
		  bool debugMode, URV& value) const
{
  if (number < 0 or number >= regs_.size())
    return false;

  const Csr<URV>& reg = regs_.at(number);
  if (mode < reg.privilegeMode())
    return false;

  if (not reg.isImplemented())
    return false;

  if (reg.isDebug() and not debugMode)
    return false;

  if (number >= TDATA1_CSR and number <= TDATA3_CSR)
    return readTdata(number, mode, debugMode, value);

  value = reg.read();
  return true;
}
  

template <typename URV>
bool
CsRegs<URV>::write(CsrNumber number, PrivilegeMode mode, bool debugMode,
		   URV value)
{
  if (number < 0 or number >= regs_.size())
    return false;

  Csr<URV>& reg = regs_.at(number);
  if (mode < reg.privilegeMode())
    return false;

  if (reg.isReadOnly() or not reg.isImplemented())
    return false;

  if (reg.isDebug() and not debugMode)
    return false;

  if (number >= TDATA1_CSR and number <= TDATA3_CSR)
    {
      if (not writeTdata(number, mode, debugMode, value))
	return false;
    }
  else if (number == MDSEAL_CSR)
    {
      // Least sig bit of MDSEAL_CSR can only be cleared.
      if ((value & 1) == 0)
	reg.write(value);
    }
  else
    reg.write(value);

  recordWrite(number);

  // Writing ot the MEIVT changes the base address in MEIHAP.
  if (number == MEIVT_CSR)
    {
      value = (value >> 10) << 10;  // Clear least sig 10 bits keeping base.
      URV meihap = regs_.at(MEIHAP_CSR).read();
      meihap &= 0x3ff;  // Clear base address bits.
      meihap |= value;  // Copy base address bits from MEIVT.
      regs_.at(MEIHAP_CSR).poke(value);
      recordWrite(MEIHAP_CSR);
    }

  return true;
}


template <typename URV>
bool
CsRegs<URV>::isWriteable(CsrNumber number, PrivilegeMode mode) const
{
  if (number < 0 or number >= regs_.size())
    return false;

  const Csr<URV>& reg = regs_.at(number);
  if (mode < reg.privilegeMode())
    return false;

  if (reg.isReadOnly() or not reg.isImplemented())
    return false;

  return true;
}


template <typename URV>
bool
CsRegs<URV>::configCsr(const std::string& name, bool implemented,
		       URV resetValue, URV mask, URV pokeMask)
{
  auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  CsrNumber num = iter->second;
  if (num < 0 or num >= regs_.size())
    return false;

  auto& csr = regs_.at(num);
  if (csr.isMandatory() and not implemented)
    {
      std::cerr << "CSR " << name << " is mandatory and is being configured "
		<< " as non-implemented -- configuration ignored.\n";
      return false;
    }

  csr = Csr<URV>(name, csr.getNumber(), csr.isMandatory(), implemented,
		 resetValue, mask);
  csr.setPokeMask(pokeMask);
  return true;
}


template <typename URV>
void
CsRegs<URV>::defineMachineRegs()
{
  URV romask = 0;  // Mask for read-only regs.

  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Machine info.
  defineCsr("mvendorid", MVENDORID_CSR, mand, imp, 0);
  defineCsr("marchid", MARCHID_CSR, mand, imp, 0, romask);
  defineCsr("mimpid", MIMPID_CSR, mand, imp, 0, romask);
  defineCsr("mhartid", MHARTID_CSR, mand, imp, 0, romask);

  // Machine trap setup.

  //                  S R        T T T M S M X  F  M  R  S M R S U M R S U
  //                  D E        S W V X U P S  S  P  E  P P E P P I E I I
  //                    S        R   M R M R       P  S  P I S I I E S E E
  //                                       V               E   E E
  URV mstatusMask = 0b0'00000000'1'1'1'1'1'1'11'11'11'00'1'1'0'1'1'1'0'1'1;
  URV mstatusVal = 0;
  if constexpr (sizeof(URV) == 8)
    mstatusMask |= (URV(0b1111) << 32);  // Mask for SXL and UXL.
  defineCsr("mstatus", MSTATUS_CSR, mand, imp, mstatusVal, mstatusMask);
  defineCsr("misa", MISA_CSR, mand,  imp, 0x40001104, romask);
  defineCsr("medeleg", MEDELEG_CSR, !mand, !imp, 0);
  defineCsr("mideleg", MIDELEG_CSR, !mand, !imp, 0);

  // Interrupt enable: Least sig 12 bits corresponding to the 12
  // interrupt causes are writable.
  URV mieMask = 0xfff; 
  defineCsr("mie", MIE_CSR, mand, imp, 0, mieMask);

  // Initial value of 0: vectored interrupt. Mask of ~2 to make bit 1
  // non-writable.
  defineCsr("mtvec", MTVEC_CSR, mand, imp, 0, ~URV(2));

  defineCsr("mcounteren", MCOUNTEREN_CSR, !mand, !imp, 0);

  // Machine trap handling: mscratch and mepc.
  defineCsr("mscratch", MSCRATCH_CSR, mand, imp, 0);
  URV mepcMask = ~URV(1);  // Bit 0 of MEPC is not writable.
  defineCsr("mepc", MEPC_CSR, mand, imp, 0, mepcMask);

  // All bits of mcause writeable.
  defineCsr("mcause", MCAUSE_CSR, mand, imp, 0);
  defineCsr("mtval", MTVAL_CSR, mand, imp, 0);

  // MIP is read-only for CSR instructions but the bits corresponding
  // to defined interrupts are modifiable.
  Csr<URV>* mip = defineCsr("mip", MIP_CSR, mand, imp, 0, romask);
  mip->setPokeMask(mieMask);

  // Machine protection and translation.
  defineCsr("pmpcfg0", PMPCFG0_CSR, mand, imp, 0);
  defineCsr("pmpcfg1", PMPCFG1_CSR, mand, imp, 0);
  defineCsr("pmpcfg2", PMPCFG2_CSR, mand, imp, 0);
  defineCsr("pmpcfg3", PMPCFG3_CSR, mand, imp, 0);
  defineCsr("pmpaddr0", PMPADDR0_CSR, mand, imp, 0);
  defineCsr("pmpaddr1", PMPADDR1_CSR, mand, imp, 0);
  defineCsr("pmpaddr2", PMPADDR2_CSR, mand, imp, 0);
  defineCsr("pmpaddr3", PMPADDR3_CSR, mand, imp, 0);
  defineCsr("pmpaddr4", PMPADDR4_CSR, mand, imp, 0);
  defineCsr("pmpaddr5", PMPADDR5_CSR, mand, imp, 0);
  defineCsr("pmpaddr6", PMPADDR6_CSR, mand, imp, 0);
  defineCsr("pmpaddr7", PMPADDR7_CSR, mand, imp, 0);
  defineCsr("pmpaddr8", PMPADDR8_CSR, mand, imp, 0);
  defineCsr("pmpaddr9", PMPADDR9_CSR, mand, imp, 0);
  defineCsr("pmpaddr10", PMPADDR10_CSR, mand, imp, 0);
  defineCsr("pmpaddr11", PMPADDR11_CSR, mand, imp, 0);
  defineCsr("pmpaddr12", PMPADDR12_CSR, mand, imp, 0);
  defineCsr("pmpaddr13", PMPADDR13_CSR, mand, imp, 0);
  defineCsr("pmpaddr14", PMPADDR14_CSR, mand, imp, 0);
  defineCsr("pmpaddr15", PMPADDR15_CSR, mand, imp, 0);

  // Machine Counter/Timers
  defineCsr("mcycle", MCYCLE_CSR, mand, imp, 0);
  defineCsr("minstret", MINSTRET_CSR, mand, imp, 0);
  defineCsr("mhpmcounter3", MHPMCOUNTER3_CSR, mand, imp, 0);
  defineCsr("mhpmcounter4", MHPMCOUNTER4_CSR, mand, imp, 0);
  defineCsr("mhpmcounter5", MHPMCOUNTER5_CSR, mand, imp, 0);
  defineCsr("mhpmcounter6", MHPMCOUNTER6_CSR, mand, imp, 0);
  defineCsr("mhpmcounter7", MHPMCOUNTER7_CSR, mand, imp, 0);
  defineCsr("mhpmcounter8", MHPMCOUNTER8_CSR, mand, imp, 0);
  defineCsr("mhpmcounter9", MHPMCOUNTER9_CSR, mand, imp, 0);
  defineCsr("mhpmcounter10", MHPMCOUNTER10_CSR, mand, imp, 0);
  defineCsr("mhpmcounter11", MHPMCOUNTER11_CSR, mand, imp, 0);
  defineCsr("mhpmcounter12", MHPMCOUNTER12_CSR, mand, imp, 0);
  defineCsr("mhpmcounter13", MHPMCOUNTER13_CSR, mand, imp, 0);
  defineCsr("mhpmcounter14", MHPMCOUNTER14_CSR, mand, imp, 0);
  defineCsr("mhpmcounter15", MHPMCOUNTER15_CSR, mand, imp, 0);
  defineCsr("mhpmcounter16", MHPMCOUNTER16_CSR, mand, imp, 0);
  defineCsr("mhpmcounter17", MHPMCOUNTER17_CSR, mand, imp, 0);
  defineCsr("mhpmcounter18", MHPMCOUNTER18_CSR, mand, imp, 0);
  defineCsr("mhpmcounter19", MHPMCOUNTER19_CSR, mand, imp, 0);
  defineCsr("mhpmcounter20", MHPMCOUNTER20_CSR, mand, imp, 0);
  defineCsr("mhpmcounter21", MHPMCOUNTER21_CSR, mand, imp, 0);
  defineCsr("mhpmcounter22", MHPMCOUNTER22_CSR, mand, imp, 0);
  defineCsr("mhpmcounter23", MHPMCOUNTER23_CSR, mand, imp, 0);
  defineCsr("mhpmcounter24", MHPMCOUNTER24_CSR, mand, imp, 0);
  defineCsr("mhpmcounter25", MHPMCOUNTER25_CSR, mand, imp, 0);
  defineCsr("mhpmcounter26", MHPMCOUNTER26_CSR, mand, imp, 0);
  defineCsr("mhpmcounter27", MHPMCOUNTER27_CSR, mand, imp, 0);
  defineCsr("mhpmcounter28", MHPMCOUNTER28_CSR, mand, imp, 0);
  defineCsr("mhpmcounter29", MHPMCOUNTER29_CSR, mand, imp, 0);
  defineCsr("mhpmcounter30", MHPMCOUNTER30_CSR, mand, imp, 0);
  defineCsr("mhpmcounter31", MHPMCOUNTER31_CSR, mand, imp, 0);

  defineCsr("mcycleh", MCYCLEH_CSR, mand, imp, 0);
  defineCsr("minstreth", MINSTRETH_CSR, mand, imp, 0);

  defineCsr("mhpmcounter3h", MHPMCOUNTER3H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter4h", MHPMCOUNTER4H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter5h", MHPMCOUNTER5H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter6h", MHPMCOUNTER6H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter7h", MHPMCOUNTER7H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter8h", MHPMCOUNTER8H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter9h", MHPMCOUNTER9H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter10h", MHPMCOUNTER10H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter11h", MHPMCOUNTER11H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter12h", MHPMCOUNTER12H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter13h", MHPMCOUNTER13H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter14h", MHPMCOUNTER14H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter15h", MHPMCOUNTER15H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter16h", MHPMCOUNTER16H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter17h", MHPMCOUNTER17H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter18h", MHPMCOUNTER18H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter19h", MHPMCOUNTER19H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter20h", MHPMCOUNTER20H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter21h", MHPMCOUNTER21H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter22h", MHPMCOUNTER22H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter23h", MHPMCOUNTER23H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter24h", MHPMCOUNTER24H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter25h", MHPMCOUNTER25H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter26h", MHPMCOUNTER26H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter27h", MHPMCOUNTER27H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter28h", MHPMCOUNTER28H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter29h", MHPMCOUNTER29H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter30h", MHPMCOUNTER30H_CSR, mand, imp, 0);
  defineCsr("mhpmcounter31h", MHPMCOUNTER31H_CSR, mand, imp, 0);

  // Machine counter setup.
  defineCsr("mhpmevent3", MHPMEVENT3_CSR, mand, imp, 0);
  defineCsr("mhpmevent4", MHPMEVENT4_CSR, mand, imp, 0);
  defineCsr("mhpmevent5", MHPMEVENT5_CSR, mand, imp, 0);
  defineCsr("mhpmevent6", MHPMEVENT6_CSR, mand, imp, 0);
  defineCsr("mhpmevent7", MHPMEVENT7_CSR, mand, imp, 0);
  defineCsr("mhpmevent8", MHPMEVENT8_CSR, mand, imp, 0);
  defineCsr("mhpmevent9", MHPMEVENT9_CSR, mand, imp, 0);
  defineCsr("mhpmevent10", MHPMEVENT10_CSR, mand, imp, 0);
  defineCsr("mhpmevent11", MHPMEVENT11_CSR, mand, imp, 0);
  defineCsr("mhpmevent12", MHPMEVENT12_CSR, mand, imp, 0);
  defineCsr("mhpmevent13", MHPMEVENT13_CSR, mand, imp, 0);
  defineCsr("mhpmevent14", MHPMEVENT14_CSR, mand, imp, 0);
  defineCsr("mhpmevent15", MHPMEVENT15_CSR, mand, imp, 0);
  defineCsr("mhpmevent16", MHPMEVENT16_CSR, mand, imp, 0);
  defineCsr("mhpmevent17", MHPMEVENT17_CSR, mand, imp, 0);
  defineCsr("mhpmevent18", MHPMEVENT18_CSR, mand, imp, 0);
  defineCsr("mhpmevent19", MHPMEVENT19_CSR, mand, imp, 0);
  defineCsr("mhpmevent20", MHPMEVENT20_CSR, mand, imp, 0);
  defineCsr("mhpmevent21", MHPMEVENT21_CSR, mand, imp, 0);
  defineCsr("mhpmevent22", MHPMEVENT22_CSR, mand, imp, 0);
  defineCsr("mhpmevent23", MHPMEVENT23_CSR, mand, imp, 0);
  defineCsr("mhpmevent24", MHPMEVENT24_CSR, mand, imp, 0);
  defineCsr("mhpmevent25", MHPMEVENT25_CSR, mand, imp, 0);
  defineCsr("mhpmevent26", MHPMEVENT26_CSR, mand, imp, 0);
  defineCsr("mhpmevent27", MHPMEVENT27_CSR, mand, imp, 0);
  defineCsr("mhpmevent28", MHPMEVENT28_CSR, mand, imp, 0);
  defineCsr("mhpmevent29", MHPMEVENT29_CSR, mand, imp, 0);
  defineCsr("mhpmevent30", MHPMEVENT30_CSR, mand, imp, 0);
  defineCsr("mhpmevent31", MHPMEVENT31_CSR, mand, imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineSupervisorRegs()
{
  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Supervisor trap SETUP_CSR.
  defineCsr("sstatus", SSTATUS_CSR, !mand, !imp, 0);
  defineCsr("sedeleg", SEDELEG_CSR, !mand, !imp, 0);
  defineCsr("sideleg", SIDELEG_CSR, !mand, !imp, 0);
  defineCsr("sie", SIE_CSR, !mand, !imp, 0);
  defineCsr("stvec", STVEC_CSR, !mand, !imp, 0);
  defineCsr("scounteren", SCOUNTEREN_CSR, !mand, !imp, 0);

  // Supervisor Trap Handling 
  defineCsr("sscratch", SSCRATCH_CSR, !mand, !imp, 0);
  defineCsr("sepc", SEPC_CSR, !mand, !imp, 0);
  defineCsr("scause", SCAUSE_CSR, !mand, !imp, 0);
  defineCsr("stval", STVAL_CSR, !mand, !imp, 0);
  defineCsr("sip", SIP_CSR, !mand, !imp, 0);

  // Supervisor Protection and Translation 
  defineCsr("satp", SATP_CSR, !mand, !imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineUserRegs()
{
  bool mand = true;  // Mandatory.
  bool imp = true; // Implemented.

  // User trap setup.
  defineCsr("ustatus", USTATUS_CSR, !mand, !imp, 0);
  defineCsr("uie", UIE_CSR, !mand, !imp, 0);
  defineCsr("utvec", UTVEC_CSR, !mand, !imp, 0);

  // User Trap Handling
  defineCsr("uscratch", USCRATCH_CSR, !mand, !imp, 0);
  defineCsr("uepc", UEPC_CSR, !mand, !imp, 0);
  defineCsr("ucause", UCAUSE_CSR, !mand, !imp, 0);
  defineCsr("utval", UTVAL_CSR, !mand, !imp, 0);
  defineCsr("uip", UIP_CSR, !mand, !imp, 0);

  // User Floating-Point CSRs
  defineCsr("fflags", FFLAGS_CSR, !mand, !imp, 0);
  defineCsr("frm", FRM_CSR, !mand, !imp, 0);
  defineCsr("fcsr", FCSR_CSR, !mand, !imp, 0);

  // User Counter/Timers
  defineCsr("cycle", CYCLE_CSR, mand, imp, 0);
  defineCsr("time", TIME_CSR, mand, imp, 0);
  defineCsr("instret", INSTRET_CSR, mand, imp, 0);

  defineCsr("hpmcounter3", HPMCOUNTER3_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter4", HPMCOUNTER4_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter5", HPMCOUNTER5_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter6", HPMCOUNTER6_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter7", HPMCOUNTER7_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter8", HPMCOUNTER8_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter9", HPMCOUNTER9_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter10", HPMCOUNTER10_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter11", HPMCOUNTER11_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter12", HPMCOUNTER12_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter13", HPMCOUNTER13_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter14", HPMCOUNTER14_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter15", HPMCOUNTER15_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter16", HPMCOUNTER16_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter17", HPMCOUNTER17_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter18", HPMCOUNTER18_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter19", HPMCOUNTER19_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter20", HPMCOUNTER20_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter21", HPMCOUNTER21_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter22", HPMCOUNTER22_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter23", HPMCOUNTER23_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter24", HPMCOUNTER24_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter25", HPMCOUNTER25_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter26", HPMCOUNTER26_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter27", HPMCOUNTER27_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter28", HPMCOUNTER28_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter29", HPMCOUNTER29_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter30", HPMCOUNTER30_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter31", HPMCOUNTER31_CSR, !mand, !imp, 0);

  defineCsr("cycleh", CYCLEH_CSR, !mand, !imp, 0);
  defineCsr("timeh", TIMEH_CSR, !mand, !imp, 0);
  defineCsr("instreth", INSTRETH_CSR, !mand, !imp, 0);

  defineCsr("hpmcounter3h", HPMCOUNTER3H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter4h", HPMCOUNTER4H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter5h", HPMCOUNTER5H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter6h", HPMCOUNTER6H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter7h", HPMCOUNTER7H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter8h", HPMCOUNTER8H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter9h", HPMCOUNTER9H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter10h", HPMCOUNTER10H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter11h", HPMCOUNTER11H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter12h", HPMCOUNTER12H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter13h", HPMCOUNTER13H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter14h", HPMCOUNTER14H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter15h", HPMCOUNTER15H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter16h", HPMCOUNTER16H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter17h", HPMCOUNTER17H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter18h", HPMCOUNTER18H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter19h", HPMCOUNTER19H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter20h", HPMCOUNTER20H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter21h", HPMCOUNTER21H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter22h", HPMCOUNTER22H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter23h", HPMCOUNTER23H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter24h", HPMCOUNTER24H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter25h", HPMCOUNTER25H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter26h", HPMCOUNTER26H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter27h", HPMCOUNTER27H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter28h", HPMCOUNTER28H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter29h", HPMCOUNTER29H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter30h", HPMCOUNTER30H_CSR, !mand, !imp, 0);
  defineCsr("hpmcounter31h", HPMCOUNTER31H_CSR, !mand, !imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineDebugRegs()
{
  typedef Csr<URV> Reg;

  bool mand = true;  // Mandatory.
  bool imp = true; // Implemented.

  // Debug/Trace registers.
  defineCsr("tselect", TSELECT_CSR, !mand, imp, 0);
  defineCsr("tdata1", TDATA1_CSR, !mand, imp, 0);
  defineCsr("tdata2", TDATA2_CSR, !mand, imp, 0);
  defineCsr("tdata3", TDATA3_CSR, !mand, !imp, 0);

  // Define triggers.
  URV triggerCount = 4;
  triggers_ = Triggers<URV>(triggerCount);

  Data1Bits<URV> data1Mask(0), data1Val(0);

  // Set the masks of the read-write fields of data1 to all 1.
  URV allOnes = ~URV(0);
  data1Mask.mcontrol_.dmode_   = allOnes;
  data1Mask.mcontrol_.hit_     = allOnes;
  data1Mask.mcontrol_.select_  = allOnes;
  data1Mask.mcontrol_.action_  = allOnes;
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
  URV dcsrMask = ~URV(0);
  dcsrMask &= URV(7) << 28; // xdebugver
  dcsrMask &= 3;  // prv
  URV dcsrVal = (URV(4) << 28) | 3;
  Reg* dscr = defineCsr("dscr", DSCR_CSR, !mand, imp, dcsrVal, dcsrMask);
  dscr->setIsDebug(true);

  Reg* dpc = defineCsr("dpc", DPC_CSR, !mand, imp, 0);
  dpc->setIsDebug(true);

  Reg* dscratch = defineCsr("dscratch", DSCRATCH_CSR, !mand, !imp, 0);
  dscratch->setIsDebug(true);
}


template <typename URV>
void
CsRegs<URV>::defineNonStandardRegs()
{
  URV romask = 0;  // Mask for read-only regs.

  bool mand = true; // Mandatory.
  bool imp = true;  // Implemented.

  defineCsr("mrac",   MRAC_CSR, !mand, imp, 0);

  // mdseac is read-only to CSR instructions but is modifiable with
  // poke.
  Csr<URV>* mdseac = defineCsr("mdseac", MDSEAC_CSR, !mand, imp, 0, romask);
  mdseac->setPokeMask(~romask);

  URV mask = 1;  // Only least sig bit writeable.
  defineCsr("mdseal", MDSEAL_CSR, !mand, imp, 0, mask);

  // Least sig 10 bits of interrupt vector table (meivt) are read only.
  mask = (~URV(0)) << 10;
  defineCsr("meivt", MEIVT_CSR, !mand, imp, 0, mask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  defineCsr("meipt", MEIPT_CSR, !mand, imp, 0, mask);

  // The external interrupt claim-id/priority capture does not hold
  // any state. It always yield zero on read.
  defineCsr("meicpct", MEICPCT_CSR, !mand, imp, 0, romask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  defineCsr("meicidpl", MEICIDPL_CSR, !mand, imp, 0, mask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  defineCsr("meicurpl", MEICURPL_CSR, !mand, imp, 0, mask);

  // None of the bits are writeable by CSR instructions. All but least
  // sig 2 bis are modifiable.
  mask = 0;

  Csr<URV>* meihap = defineCsr("meihap", MEIHAP_CSR, !mand, imp, 0, mask);
  meihap->setPokeMask((~URV(0)) << 2);
}


template <typename URV>
uint64_t
CsRegs<URV>::getRetiredInstCount() const
{
  if (MINSTRET_CSR < 0 or MINSTRET_CSR >= regs_.size())
    return 0;

  const Csr<URV>& csr = regs_.at(MINSTRET_CSR);
  if (not csr.isImplemented())
    return 0;

  if (sizeof(URV) == 8)  // 64-bit machine
    return csr.read();

  if (sizeof(URV) == 4)
    {
      if (MINSTRETH_CSR < 0 or MINSTRETH_CSR >= regs_.size())
	return 0;

      const Csr<URV>& csrh = regs_.at(MINSTRETH_CSR);
      if (not csrh.isImplemented())
	return 0;

      uint64_t count = uint64_t(csrh.read()) << 32;
      count |= csr.read();
      return count;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return 0;
}


template <typename URV>
bool
CsRegs<URV>::setRetiredInstCount(uint64_t count)
{
  if (MINSTRET_CSR < 0 or MINSTRET_CSR >= regs_.size())
    return false;

  Csr<URV>& csr = regs_.at(MINSTRET_CSR);
  if (not csr.isImplemented())
    return false;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      csr.write(count);
      return true;
    }

  if (sizeof(URV) == 4)
    {
      if (MINSTRETH_CSR < 0 or MINSTRETH_CSR >= regs_.size())
	return false;

      Csr<URV>& csrh = regs_.at(MINSTRETH_CSR);
      if (not csrh.isImplemented())
	return false;
      csrh.write(count >> 32);
      csr.write(count);
      return true;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return false;
}


template <typename URV>
uint64_t
CsRegs<URV>::getCycleCount() const
{
  if (MCYCLE_CSR < 0 or MCYCLE_CSR >= regs_.size())
    return 0;

  const Csr<URV>& csr = regs_.at(MCYCLE_CSR);
  if (not csr.isImplemented())
    return 0;

  if (sizeof(URV) == 8)  // 64-bit machine
    return csr.read();

  if (sizeof(URV) == 4)
    {
      if (MCYCLEH_CSR < 0 or MCYCLEH_CSR >= regs_.size())
	return 0;

      const Csr<URV>& csrh = regs_.at(MCYCLEH_CSR);
      if (not csrh.isImplemented())
	return 0;

      uint64_t count = uint64_t(csrh.read()) << 32;
      count |= csr.read();
      return count;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return 0;
}


template <typename URV>
bool
CsRegs<URV>::setCycleCount(uint64_t count)
{
  if (MCYCLE_CSR < 0 or MCYCLE_CSR >= regs_.size())
    return false;

  Csr<URV>& csr = regs_.at(MCYCLE_CSR);
  if (not csr.isImplemented())
    return false;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      csr.write(count);
      return true;
    }

  if (sizeof(URV) == 4)
    {
      if (MCYCLEH_CSR < 0 or MCYCLEH_CSR >= regs_.size())
	return false;

      Csr<URV>& csrh = regs_.at(MCYCLEH_CSR);
      if (not csrh.isImplemented())
	return false;
      csrh.write(count >> 32);
      csr.write(count);
      return true;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return false;
}


template <typename URV>
bool
CsRegs<URV>::poke(CsrNumber number, PrivilegeMode mode, URV value)
{
  if (number < 0 or number >= regs_.size())
    return false;

  Csr<URV>& reg = regs_.at(number);
  if (mode < reg.privilegeMode())
    return false;

  if (not reg.isImplemented())
    return false;

  bool debugMode = true;

  if (number >= TDATA1_CSR and number <= TDATA3_CSR)
    return writeTdata(number, mode, debugMode, value);

  reg.poke(value);
  return true;
}


template <typename URV>
bool
CsRegs<URV>::readTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
		       URV& value) const
{
  // Determine currently selected trigger.
  URV trigger = 0;
  if (not read(TSELECT_CSR, mode, debugMode, trigger))
    return false;

  if (number == TDATA1_CSR)
    return triggers_.readData1(trigger, value);

  if (number == TDATA2_CSR)
    return triggers_.readData2(trigger, value);

  if (number == TDATA3_CSR)
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
  if (not read(TSELECT_CSR, mode, debugMode, trigger))
    return false;

  if (number == TDATA1_CSR)
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

  if (number == TDATA2_CSR)
    return triggers_.writeData2(trigger, value);

  if (number == TDATA3_CSR)
    return triggers_.writeData3(trigger, value);

  return false;
}



template class WdRiscv::CsRegs<uint32_t>;
template class WdRiscv::CsRegs<uint64_t>;
