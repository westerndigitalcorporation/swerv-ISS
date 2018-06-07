#include <iostream>
#include <assert.h>
#include "CsRegs.hpp"


using namespace WdRiscv;


template <typename URV>
CsRegs<URV>::CsRegs() 
  : traceWrites_(false)
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

  // Setup name to number map.
  for (const auto& reg : regs_)
    if (not (reg.getName().empty()))
      nameToNumber_[reg.getName()] = reg.getNumber();
}


template <typename URV>
CsRegs<URV>::~CsRegs()
{
  regs_.clear();
  nameToNumber_.clear();
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

  if (number == MDSEAL_CSR)
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
		   URV resetValue, URV mask)
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
  return true;
}


template <typename URV>
void
CsRegs<URV>::defineMachineRegs()
{
  typedef Csr<URV> Reg;

  URV romask = 0;  // Mask for read-only regs.

  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Machine info.
  regs_.at(MVENDORID_CSR) = Reg("mvendorid", MVENDORID_CSR, mand, imp, 0);
  regs_.at(MARCHID_CSR) = Reg("marchid", MARCHID_CSR, mand, imp, 0, romask);
  regs_.at(MIMPID_CSR) = Reg("mimpid", MIMPID_CSR, mand, imp, 0, romask);
  regs_.at(MHARTID_CSR) = Reg("mhartid", MHARTID_CSR, mand, imp, 0, romask);

  // Machine trap setup.
  URV mstatusMask = (1 << 7) | (1 << 3); // Only bits mpie(7) and mie(3) writable
  URV mstatusVal = 0x1800;  // MPP bits hard wired to 11.
  regs_.at(MSTATUS_CSR) = Reg("mstatus", MSTATUS_CSR, mand, imp, mstatusVal,
			      mstatusMask);
  regs_.at(MISA_CSR) = Reg("misa", MISA_CSR, mand,  imp, 0x40001104, romask);
  regs_.at(MEDELEG_CSR) = Reg("medeleg", MEDELEG_CSR, !mand, !imp, 0);
  regs_.at(MIDELEG_CSR) = Reg("mideleg", MIDELEG_CSR, !mand, !imp, 0);

  // Interrupt enable: Only MEIP, MTIP and MSBUSIP (WD extension) are writable.
  URV mieMask = (URV(1) << MeipBit) | (URV(1) << MtipBit) | (URV(1) << MsbusipBit);
  regs_.at(MIE_CSR) = Reg("mie", MIE_CSR, mand, imp, 0, mieMask);

  // Initial value of 0: vectored interrupt. Mask of ~2 to make bit 1
  // non-writable.
  regs_.at(MTVEC_CSR) = Reg("mtvec", MTVEC_CSR, mand, imp, 0, ~URV(2));

  regs_.at(MCOUNTEREN_CSR) = Reg("mcounteren", MCOUNTEREN_CSR, !mand, !imp, 0);

  // Machine trap handling: mscratch and mepc.
  regs_.at(MSCRATCH_CSR) = Reg("mscratch", MSCRATCH_CSR, mand, imp, 0);
  URV mepcMask = ~URV(1);  // Bit 0 of MEPC is not writable.
  regs_.at(MEPC_CSR) = Reg("mepc", MEPC_CSR, mand, imp, 0, mepcMask);

  // All bits of mcause writeable.
  regs_.at(MCAUSE_CSR) = Reg("mcause", MCAUSE_CSR, mand, imp, 0);
  regs_.at(MTVAL_CSR) = Reg("mtval", MTVAL_CSR, mand, imp, 0);

  // MIP is read-only for CSR instructions but bits meip, mtip and
  // msbusip are modifiable.
  regs_.at(MIP_CSR) = Reg("mip", MIP_CSR, mand, imp, 0, romask);
  regs_.at(MIP_CSR).setPokeMask(mieMask);

  // Machine protection and translation.
  regs_.at(PMPCFG0_CSR) = Reg("pmpcfg0", PMPCFG0_CSR, mand, imp, 0);
  regs_.at(PMPCFG1_CSR) = Reg("pmpcfg1", PMPCFG1_CSR, mand, imp, 0);
  regs_.at(PMPCFG2_CSR) = Reg("pmpcfg2", PMPCFG2_CSR, mand, imp, 0);
  regs_.at(PMPCFG3_CSR) = Reg("pmpcfg3", PMPCFG3_CSR, mand, imp, 0);
  regs_.at(PMPADDR0_CSR) = Reg("pmpaddr0", PMPADDR0_CSR, mand, imp, 0);
  regs_.at(PMPADDR1_CSR) = Reg("pmpaddr1", PMPADDR1_CSR, mand, imp, 0);
  regs_.at(PMPADDR2_CSR) = Reg("pmpaddr2", PMPADDR2_CSR, mand, imp, 0);
  regs_.at(PMPADDR3_CSR) = Reg("pmpaddr3", PMPADDR3_CSR, mand, imp, 0);
  regs_.at(PMPADDR4_CSR) = Reg("pmpaddr4", PMPADDR4_CSR, mand, imp, 0);
  regs_.at(PMPADDR5_CSR) = Reg("pmpaddr5", PMPADDR5_CSR, mand, imp, 0);
  regs_.at(PMPADDR6_CSR) = Reg("pmpaddr6", PMPADDR6_CSR, mand, imp, 0);
  regs_.at(PMPADDR7_CSR) = Reg("pmpaddr7", PMPADDR7_CSR, mand, imp, 0);
  regs_.at(PMPADDR8_CSR) = Reg("pmpaddr8", PMPADDR8_CSR, mand, imp, 0);
  regs_.at(PMPADDR9_CSR) = Reg("pmpaddr9", PMPADDR9_CSR, mand, imp, 0);
  regs_.at(PMPADDR10_CSR) = Reg("pmpaddr10", PMPADDR10_CSR, mand, imp, 0);
  regs_.at(PMPADDR11_CSR) = Reg("pmpaddr11", PMPADDR11_CSR, mand, imp, 0);
  regs_.at(PMPADDR12_CSR) = Reg("pmpaddr12", PMPADDR12_CSR, mand, imp, 0);
  regs_.at(PMPADDR13_CSR) = Reg("pmpaddr13", PMPADDR13_CSR, mand, imp, 0);
  regs_.at(PMPADDR14_CSR) = Reg("pmpaddr14", PMPADDR14_CSR, mand, imp, 0);
  regs_.at(PMPADDR15_CSR) = Reg("pmpaddr15", PMPADDR15_CSR, mand, imp, 0);

  // Machine Counter/Timers
  regs_.at(MCYCLE_CSR) = Reg("mcycle", MCYCLE_CSR, mand, imp, 0);
  regs_.at(MINSTRET_CSR) = Reg("minstret", MINSTRET_CSR, mand, imp, 0);
  regs_.at(MHPMCOUNTER3_CSR) = Reg("mhpmcounter3", MHPMCOUNTER3_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER4_CSR) = Reg("mhpmcounter4", MHPMCOUNTER4_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER5_CSR) = Reg("mhpmcounter5", MHPMCOUNTER5_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER6_CSR) = Reg("mhpmcounter6", MHPMCOUNTER6_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER7_CSR) = Reg("mhpmcounter7", MHPMCOUNTER7_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER8_CSR) = Reg("mhpmcounter8", MHPMCOUNTER8_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER9_CSR) = Reg("mhpmcounter9", MHPMCOUNTER9_CSR, mand, imp,
				   0);
  regs_.at(MHPMCOUNTER10_CSR) = Reg("mhpmcounter10", MHPMCOUNTER10_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER11_CSR) = Reg("mhpmcounter11", MHPMCOUNTER11_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER12_CSR) = Reg("mhpmcounter12", MHPMCOUNTER12_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER13_CSR) = Reg("mhpmcounter13", MHPMCOUNTER13_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER14_CSR) = Reg("mhpmcounter14", MHPMCOUNTER14_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER15_CSR) = Reg("mhpmcounter15", MHPMCOUNTER15_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER16_CSR) = Reg("mhpmcounter16", MHPMCOUNTER16_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER17_CSR) = Reg("mhpmcounter17", MHPMCOUNTER17_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER18_CSR) = Reg("mhpmcounter18", MHPMCOUNTER18_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER19_CSR) = Reg("mhpmcounter19", MHPMCOUNTER19_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER20_CSR) = Reg("mhpmcounter20", MHPMCOUNTER20_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER21_CSR) = Reg("mhpmcounter21", MHPMCOUNTER21_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER22_CSR) = Reg("mhpmcounter22", MHPMCOUNTER22_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER23_CSR) = Reg("mhpmcounter23", MHPMCOUNTER23_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER24_CSR) = Reg("mhpmcounter24", MHPMCOUNTER24_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER25_CSR) = Reg("mhpmcounter25", MHPMCOUNTER25_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER26_CSR) = Reg("mhpmcounter26", MHPMCOUNTER26_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER27_CSR) = Reg("mhpmcounter27", MHPMCOUNTER27_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER28_CSR) = Reg("mhpmcounter28", MHPMCOUNTER28_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER29_CSR) = Reg("mhpmcounter29", MHPMCOUNTER29_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER30_CSR) = Reg("mhpmcounter30", MHPMCOUNTER30_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER31_CSR) = Reg("mhpmcounter31", MHPMCOUNTER31_CSR,
				    mand, imp, 0);

  regs_.at(MCYCLEH_CSR) = Reg("mcycleh", MCYCLEH_CSR, mand, imp, 0);
  regs_.at(MINSTRETH_CSR) = Reg("minstreth", MINSTRETH_CSR, mand, imp, 0);

  regs_.at(MHPMCOUNTER3H_CSR) = Reg("mhpmcounter3h", MHPMCOUNTER3H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER4H_CSR) = Reg("mhpmcounter4h", MHPMCOUNTER4H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER5H_CSR) = Reg("mhpmcounter5h", MHPMCOUNTER5H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER6H_CSR) = Reg("mhpmcounter6h", MHPMCOUNTER6H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER7H_CSR) = Reg("mhpmcounter7h", MHPMCOUNTER7H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER8H_CSR) = Reg("mhpmcounter8h", MHPMCOUNTER8H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER9H_CSR) = Reg("mhpmcounter9h", MHPMCOUNTER9H_CSR,
				    mand, imp, 0);
  regs_.at(MHPMCOUNTER10H_CSR) = Reg("mhpmcounter10h", MHPMCOUNTER10H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER11H_CSR) = Reg("mhpmcounter11h", MHPMCOUNTER11H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER12H_CSR) = Reg("mhpmcounter12h", MHPMCOUNTER12H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER13H_CSR) = Reg("mhpmcounter13h", MHPMCOUNTER13H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER14H_CSR) = Reg("mhpmcounter14h", MHPMCOUNTER14H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER15H_CSR) = Reg("mhpmcounter15h", MHPMCOUNTER15H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER16H_CSR) = Reg("mhpmcounter16h", MHPMCOUNTER16H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER17H_CSR) = Reg("mhpmcounter17h", MHPMCOUNTER17H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER18H_CSR) = Reg("mhpmcounter18h", MHPMCOUNTER18H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER19H_CSR) = Reg("mhpmcounter19h", MHPMCOUNTER19H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER20H_CSR) = Reg("mhpmcounter20h", MHPMCOUNTER20H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER21H_CSR) = Reg("mhpmcounter21h", MHPMCOUNTER21H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER22H_CSR) = Reg("mhpmcounter22h", MHPMCOUNTER22H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER23H_CSR) = Reg("mhpmcounter23h", MHPMCOUNTER23H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER24H_CSR) = Reg("mhpmcounter24h", MHPMCOUNTER24H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER25H_CSR) = Reg("mhpmcounter25h", MHPMCOUNTER25H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER26H_CSR) = Reg("mhpmcounter26h", MHPMCOUNTER26H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER27H_CSR) = Reg("mhpmcounter27h", MHPMCOUNTER27H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER28H_CSR) = Reg("mhpmcounter28h", MHPMCOUNTER28H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER29H_CSR) = Reg("mhpmcounter29h", MHPMCOUNTER29H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER30H_CSR) = Reg("mhpmcounter30h", MHPMCOUNTER30H_CSR,
				     mand, imp, 0);
  regs_.at(MHPMCOUNTER31H_CSR) = Reg("mhpmcounter31h", MHPMCOUNTER31H_CSR,
				     mand, imp, 0);

  // Machine counter setup.
  regs_.at(MHPMEVENT3_CSR) = Reg("mhpmevent3", MHPMEVENT3_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT4_CSR) = Reg("mhpmevent4", MHPMEVENT4_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT5_CSR) = Reg("mhpmevent5", MHPMEVENT5_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT6_CSR) = Reg("mhpmevent6", MHPMEVENT6_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT7_CSR) = Reg("mhpmevent7", MHPMEVENT7_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT8_CSR) = Reg("mhpmevent8", MHPMEVENT8_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT9_CSR) = Reg("mhpmevent9", MHPMEVENT9_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT10_CSR) = Reg("mhpmevent10", MHPMEVENT10_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT11_CSR) = Reg("mhpmevent11", MHPMEVENT11_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT12_CSR) = Reg("mhpmevent12", MHPMEVENT12_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT13_CSR) = Reg("mhpmevent13", MHPMEVENT13_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT14_CSR) = Reg("mhpmevent14", MHPMEVENT14_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT15_CSR) = Reg("mhpmevent15", MHPMEVENT15_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT16_CSR) = Reg("mhpmevent16", MHPMEVENT16_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT17_CSR) = Reg("mhpmevent17", MHPMEVENT17_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT18_CSR) = Reg("mhpmevent18", MHPMEVENT18_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT19_CSR) = Reg("mhpmevent19", MHPMEVENT19_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT20_CSR) = Reg("mhpmevent20", MHPMEVENT20_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT21_CSR) = Reg("mhpmevent21", MHPMEVENT21_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT22_CSR) = Reg("mhpmevent22", MHPMEVENT22_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT23_CSR) = Reg("mhpmevent23", MHPMEVENT23_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT24_CSR) = Reg("mhpmevent24", MHPMEVENT24_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT25_CSR) = Reg("mhpmevent25", MHPMEVENT25_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT26_CSR) = Reg("mhpmevent26", MHPMEVENT26_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT27_CSR) = Reg("mhpmevent27", MHPMEVENT27_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT28_CSR) = Reg("mhpmevent28", MHPMEVENT28_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT29_CSR) = Reg("mhpmevent29", MHPMEVENT29_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT30_CSR) = Reg("mhpmevent30", MHPMEVENT30_CSR, mand, imp, 0);
  regs_.at(MHPMEVENT31_CSR) = Reg("mhpmevent31", MHPMEVENT31_CSR, mand, imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineSupervisorRegs()
{
  typedef Csr<URV> Reg;

  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  // Supervisor trap SETUP_CSR.
  regs_.at(SSTATUS_CSR) = Reg("sstatus", SSTATUS_CSR, !mand, !imp, 0);
  regs_.at(SEDELEG_CSR) = Reg("sedeleg", SEDELEG_CSR, !mand, !imp, 0);
  regs_.at(SIDELEG_CSR) = Reg("sideleg", SIDELEG_CSR, !mand, !imp, 0);
  regs_.at(SIE_CSR) = Reg("sie", SIE_CSR, !mand, !imp, 0);
  regs_.at(STVEC_CSR) = Reg("stvec", STVEC_CSR, !mand, !imp, 0);
  regs_.at(SCOUNTEREN_CSR) = Reg("scounteren", SCOUNTEREN_CSR, !mand, !imp, 0);

  // Supervisor Trap Handling 
  regs_.at(SSCRATCH_CSR) = Reg("sscratch", SSCRATCH_CSR, !mand, !imp, 0);
  regs_.at(SEPC_CSR) = Reg("sepc", SEPC_CSR, !mand, !imp, 0);
  regs_.at(SCAUSE_CSR) = Reg("scause", SCAUSE_CSR, !mand, !imp, 0);
  regs_.at(STVAL_CSR) = Reg("stval", STVAL_CSR, !mand, !imp, 0);
  regs_.at(SIP_CSR) = Reg("sip", SIP_CSR, !mand, !imp, 0);

  // Supervisor Protection and Translation 
  regs_.at(SATP_CSR) = Reg("satp", SATP_CSR, !mand, !imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineUserRegs()
{
  typedef Csr<URV> Reg;

  bool mand = true;  // Mandatory.
  bool imp = true; // Implemented.

  // User trap setup.
  regs_.at(USTATUS_CSR) = Reg("ustatus", USTATUS_CSR, !mand, !imp, 0);
  regs_.at(UIE_CSR) = Reg("uie", UIE_CSR, !mand, !imp, 0);
  regs_.at(UTVEC_CSR) = Reg("utvec", UTVEC_CSR, !mand, !imp, 0);

  // User Trap Handling
  regs_.at(USCRATCH_CSR) = Reg("uscratch", USCRATCH_CSR, !mand, !imp, 0);
  regs_.at(UEPC_CSR) = Reg("uepc", UEPC_CSR, !mand, !imp, 0);
  regs_.at(UCAUSE_CSR) = Reg("ucause", UCAUSE_CSR, !mand, !imp, 0);
  regs_.at(UTVAL_CSR) = Reg("utval", UTVAL_CSR, !mand, !imp, 0);
  regs_.at(UIP_CSR) = Reg("uip", UIP_CSR, !mand, !imp, 0);

  // User Floating-Point CSRs
  regs_.at(FFLAGS_CSR) = Reg("fflags", FFLAGS_CSR, !mand, !imp, 0);
  regs_.at(FRM_CSR) = Reg("frm", FRM_CSR, !mand, !imp, 0);
  regs_.at(FCSR_CSR) = Reg("fcsr", FCSR_CSR, !mand, !imp, 0);

  // User Counter/Timers
  regs_.at(CYCLE_CSR) = Reg("cycle", CYCLE_CSR, mand, imp, 0);
  regs_.at(TIME_CSR) = Reg("time", TIME_CSR, mand, imp, 0);
  regs_.at(INSTRET_CSR) = Reg("instret", INSTRET_CSR, mand, imp, 0);
  regs_.at(HPMCOUNTER3_CSR) = Reg("hpmcounter3", HPMCOUNTER3_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER4_CSR) = Reg("hpmcounter4", HPMCOUNTER4_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER5_CSR) = Reg("hpmcounter5", HPMCOUNTER5_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER6_CSR) = Reg("hpmcounter6", HPMCOUNTER6_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER7_CSR) = Reg("hpmcounter7", HPMCOUNTER7_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER8_CSR) = Reg("hpmcounter8", HPMCOUNTER8_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER9_CSR) = Reg("hpmcounter9", HPMCOUNTER9_CSR, !mand,
				  !imp, 0);
  regs_.at(HPMCOUNTER10_CSR) = Reg("hpmcounter10", HPMCOUNTER10_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER11_CSR) = Reg("hpmcounter11", HPMCOUNTER11_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER12_CSR) = Reg("hpmcounter12", HPMCOUNTER12_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER13_CSR) = Reg("hpmcounter13", HPMCOUNTER13_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER14_CSR) = Reg("hpmcounter14", HPMCOUNTER14_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER15_CSR) = Reg("hpmcounter15", HPMCOUNTER15_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER16_CSR) = Reg("hpmcounter16", HPMCOUNTER16_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER17_CSR) = Reg("hpmcounter17", HPMCOUNTER17_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER18_CSR) = Reg("hpmcounter18", HPMCOUNTER18_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER19_CSR) = Reg("hpmcounter19", HPMCOUNTER19_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER20_CSR) = Reg("hpmcounter20", HPMCOUNTER20_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER21_CSR) = Reg("hpmcounter21", HPMCOUNTER21_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER22_CSR) = Reg("hpmcounter22", HPMCOUNTER22_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER23_CSR) = Reg("hpmcounter23", HPMCOUNTER23_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER24_CSR) = Reg("hpmcounter24", HPMCOUNTER24_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER25_CSR) = Reg("hpmcounter25", HPMCOUNTER25_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER26_CSR) = Reg("hpmcounter26", HPMCOUNTER26_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER27_CSR) = Reg("hpmcounter27", HPMCOUNTER27_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER28_CSR) = Reg("hpmcounter28", HPMCOUNTER28_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER29_CSR) = Reg("hpmcounter29", HPMCOUNTER29_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER30_CSR) = Reg("hpmcounter30", HPMCOUNTER30_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER31_CSR) = Reg("hpmcounter31", HPMCOUNTER31_CSR,
				   !mand, !imp, 0);

  regs_.at(CYCLE_CSR) = Reg("cycle", CYCLE_CSR, !mand, !imp, 0);
  regs_.at(TIME_CSR) = Reg("time", TIME_CSR, !mand, !imp, 0);
  regs_.at(INSTRET_CSR) = Reg("instret", INSTRET_CSR, !mand, !imp, 0);
  regs_.at(HPMCOUNTER3_CSR) = Reg("hpmcounter3", HPMCOUNTER3_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER4_CSR) = Reg("hpmcounter4", HPMCOUNTER4_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER5_CSR) = Reg("hpmcounter5", HPMCOUNTER5_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER6_CSR) = Reg("hpmcounter6", HPMCOUNTER6_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER7_CSR) = Reg("hpmcounter7", HPMCOUNTER7_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER8_CSR) = Reg("hpmcounter8", HPMCOUNTER8_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER9_CSR) = Reg("hpmcounter9", HPMCOUNTER9_CSR,
				  !mand, !imp, 0);
  regs_.at(HPMCOUNTER10_CSR) = Reg("hpmcounter10", HPMCOUNTER10_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER11_CSR) = Reg("hpmcounter11", HPMCOUNTER11_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER12_CSR) = Reg("hpmcounter12", HPMCOUNTER12_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER13_CSR) = Reg("hpmcounter13", HPMCOUNTER13_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER14_CSR) = Reg("hpmcounter14", HPMCOUNTER14_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER15_CSR) = Reg("hpmcounter15", HPMCOUNTER15_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER16_CSR) = Reg("hpmcounter16", HPMCOUNTER16_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER17_CSR) = Reg("hpmcounter17", HPMCOUNTER17_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER18_CSR) = Reg("hpmcounter18", HPMCOUNTER18_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER19_CSR) = Reg("hpmcounter19", HPMCOUNTER19_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER20_CSR) = Reg("hpmcounter20", HPMCOUNTER20_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER21_CSR) = Reg("hpmcounter21", HPMCOUNTER21_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER22_CSR) = Reg("hpmcounter22", HPMCOUNTER22_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER23_CSR) = Reg("hpmcounter23", HPMCOUNTER23_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER24_CSR) = Reg("hpmcounter24", HPMCOUNTER24_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER25_CSR) = Reg("hpmcounter25", HPMCOUNTER25_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER26_CSR) = Reg("hpmcounter26", HPMCOUNTER26_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER27_CSR) = Reg("hpmcounter27", HPMCOUNTER27_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER28_CSR) = Reg("hpmcounter28", HPMCOUNTER28_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER29_CSR) = Reg("hpmcounter29", HPMCOUNTER29_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER30_CSR) = Reg("hpmcounter30", HPMCOUNTER30_CSR,
				   !mand, !imp, 0);
  regs_.at(HPMCOUNTER31_CSR) = Reg("hpmcounter31", HPMCOUNTER31_CSR,
				   !mand, !imp, 0);
}


template <typename URV>
void
CsRegs<URV>::defineDebugRegs()
{
  typedef Csr<URV> Reg;

  bool mand = true;  // Mandatory.
  bool imp = true; // Implemented.

  // Debug/Trace registers.
  regs_.at(TSELECT_CSR) = Reg("tselect", TSELECT_CSR, !mand, imp, 0);
  regs_.at(TDATA1_CSR) = Reg("tdata1", TDATA1_CSR, !mand, imp, 0);
  regs_.at(TDATA2_CSR) = Reg("tdata2", TDATA2_CSR, !mand, imp, 0);
  regs_.at(TDATA3_CSR) = Reg("tdata3", TDATA3_CSR, !mand, imp, 0);

  // Debug mode registers.
  URV dcsrMask = ~URV(0);
  dcsrMask &= URV(7) << 28; // xdebugver
  dcsrMask &= 3;  // prv
  URV dcsrVal = (URV(4) << 28) | 3;
  regs_.at(DSCR_CSR) = Reg("dscr", DSCR_CSR, !mand, imp, dcsrVal, dcsrMask);
  regs_.at(DSCR_CSR).setIsDebug(true);

  regs_.at(DPC_CSR) = Reg("dpc", DPC_CSR, !mand, imp, 0);
  regs_.at(DPC_CSR).setIsDebug(true);

  regs_.at(DSCRATCH_CSR) = Reg("dscratch", DSCRATCH_CSR, !mand, !imp, 0);
  regs_.at(DSCRATCH_CSR).setIsDebug(true);
}


template <typename URV>
void
CsRegs<URV>::defineNonStandardRegs()
{
  typedef Csr<URV> Reg;

  bool mand = true; // Mandatory.
  bool imp = true;  // Implemented.

  regs_.at(MRAC_CSR)   = Reg("mrac",   MRAC_CSR, !mand, imp, 0);

  // mdseac is read-only to CSR instructions but is modifiable with
  // poke.
  regs_.at(MDSEAC_CSR) = Reg("mdseac", MDSEAC_CSR, !mand, imp, 0);
  regs_.at(MDSEAC_CSR).setPokeMask(~URV(0));

  URV mask = 1;  // Only least sig bit writeable.
  regs_.at(MDSEAL_CSR) = Reg("mdseal", MDSEAL_CSR, !mand, imp, 0, mask);

  // Least sig 10 bits of interrupt vector table (meivt) are read only.
  mask = (~URV(0)) << 10;
  regs_.at(MEIVT_CSR) = Reg("meivt", MEIVT_CSR, !mand, imp, 0, mask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  regs_.at(MEIPT_CSR) = Reg("meipt", MEIPT_CSR, !mand, imp, 0, mask);

  // The external interrupt claim-id/priority capture does not hold
  // any state. It always yield zero on read.
  regs_.at(MEICPCT_CSR) = Reg("meicpct", MEICPCT_CSR, !mand, imp, 0, 0);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  regs_.at(MEICIDPL_CSR) = Reg("meicidpl", MEICIDPL_CSR, !mand, imp, 0, mask);

  // Only least sig 4 bits writeable.
  mask = 0xf;
  regs_.at(MEICURPL_CSR) = Reg("meicurpl", MEICURPL_CSR, !mand, imp, 0, mask);

  // None of the bits are writeable by CSR instructions. All but least
  // sig 2 bis are modifiable.
  mask = 0;
  regs_.at(MEIHAP_CSR) = Reg("meihap", MEIHAP_CSR, !mand, imp, 0, mask);
  URV pokeMask = (~URV(0)) << 2;
  regs_.at(MEIHAP_CSR).setPokeMask(pokeMask);
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

  reg.poke(value);
  return true;
}


template class WdRiscv::CsRegs<uint32_t>;
template class WdRiscv::CsRegs<uint64_t>;
