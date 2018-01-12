#include <assert.h>
#include "CsRegs.hpp"


using namespace WdRiscv;


template <typename URV>
CsRegs<URV>::CsRegs()
  : lastWrittenReg_(-1)
{
  // Allocate CSR vector.  All entries are invalid.
  regs_.clear();
  regs_.resize(MAX_CSR_ + 1);

  // Define CSR entries.
  defineMachineRegs();
  defineSupervisorRegs();
  defineUserRegs();
  defineDebugRegs();

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
CsRegs<URV>::read(CsrNumber number, PrivilegeMode mode, URV& value) const
{
  if (number < 0 or number >= regs_.size())
    return false;

  const Csr<URV>& reg = regs_.at(number);
  if (mode < reg.privilegeMode())
    return false;

  if (not reg.isImplemented())
    return false;

  value = reg.getValue();
  return true;
}
  

template <typename URV>
bool
CsRegs<URV>::write(CsrNumber number, PrivilegeMode mode, URV value)
{
  if (number < 0 or number >= regs_.size())
    return false;

  Csr<URV>& reg = regs_.at(number);
  if (mode < reg.privilegeMode())
    return false;

  if (reg.isReadOnly() or not reg.isImplemented())
    return false;

  reg.setValue(value);

  lastWrittenReg_ = number;

  return true;
}


template <typename URV>
void
CsRegs<URV>::defineMachineRegs()
{
  typedef Csr<URV> Reg;

  URV romask = 0;  // Mask for read-only regs.

  // Machine info.
  regs_.at(MVENDORID_CSR) = Reg("mvendorid", MVENDORID_CSR, true, 0);
  regs_.at(MARCHID_CSR) = Reg("marchid", MARCHID_CSR, true, 0, romask);
  regs_.at(MIMPID_CSR) = Reg("mimpid", MIMPID_CSR, true, 0, romask);
  regs_.at(MHARTID_CSR) = Reg("mhartid", MHARTID_CSR, true, 0, romask);

  // Machine trap setup.
  regs_.at(MSTATUS_CSR) = Reg("mstatus", MSTATUS_CSR, true, 0);
  regs_.at(MISA_CSR) = Reg("misa", MISA_CSR, true, 0x40001104, romask);
  regs_.at(MEDELEG_CSR) = Reg("medeleg", MEDELEG_CSR, false, 0);
  regs_.at(MIDELEG_CSR) = Reg("mideleg", MIDELEG_CSR, false, 0);

  regs_.at(MIE_CSR) = Reg("mie", MIE_CSR, true, 0, romask);

  // Initial value of 0: vectored interrupt. Mask of ~2 to make bit 1
  // non-writable.
  regs_.at(MTVEC_CSR) = Reg("mtvec", MTVEC_CSR, true, 0xee000000, ~URV(2));

  regs_.at(MCOUNTEREN_CSR) = Reg("mcounteren", MCOUNTEREN_CSR, true, 0);

  // Machine trap handling
  regs_.at(MSCRATCH_CSR) = Reg("mscratch", MSCRATCH_CSR, true, 0);
  regs_.at(MEPC_CSR) = Reg("mepc", MEPC_CSR, true, 0);

  // Since values above 15 are reserved in mcause, writable bits are
  // the most significant bit and bits 3-0.
  URV mask = (URV(1) << (regWidth() - 1)) | 0xf;
  regs_.at(MCAUSE_CSR) = Reg("mcause", MCAUSE_CSR, true, 0, mask);
  regs_.at(MTVAL_CSR) = Reg("mtval", MTVAL_CSR, true, 0);
  regs_.at(MIP_CSR) = Reg("mip", MIP_CSR, true, 0);

  // Machine protection and translation.
  regs_.at(PMPCFG0_CSR) = Reg("pmpcfg0", PMPCFG0_CSR, true, 0);
  regs_.at(PMPCFG1_CSR) = Reg("pmpcfg1", PMPCFG1_CSR, true, 0);
  regs_.at(PMPCFG2_CSR) = Reg("pmpcfg2", PMPCFG2_CSR, true, 0);
  regs_.at(PMPCFG3_CSR) = Reg("pmpcfg3", PMPCFG3_CSR, true, 0);
  regs_.at(PMADDR0_CSR) = Reg("pmaddr0", PMADDR0_CSR, true, 0);
  regs_.at(PMADDR1_CSR) = Reg("pmaddr1", PMADDR1_CSR, true, 0);
  regs_.at(PMADDR2_CSR) = Reg("pmaddr2", PMADDR2_CSR, true, 0);
  regs_.at(PMADDR3_CSR) = Reg("pmaddr3", PMADDR3_CSR, true, 0);
  regs_.at(PMADDR4_CSR) = Reg("pmaddr4", PMADDR4_CSR, true, 0);
  regs_.at(PMADDR5_CSR) = Reg("pmaddr5", PMADDR5_CSR, true, 0);
  regs_.at(PMADDR6_CSR) = Reg("pmaddr6", PMADDR6_CSR, true, 0);
  regs_.at(PMADDR7_CSR) = Reg("pmaddr7", PMADDR7_CSR, true, 0);
  regs_.at(PMADDR8_CSR) = Reg("pmaddr8", PMADDR8_CSR, true, 0);
  regs_.at(PMADDR9_CSR) = Reg("pmaddr9", PMADDR9_CSR, true, 0);
  regs_.at(PMADDR10_CSR) = Reg("pmaddr10", PMADDR10_CSR, true, 0);
  regs_.at(PMADDR11_CSR) = Reg("pmaddr11", PMADDR11_CSR, true, 0);
  regs_.at(PMADDR12_CSR) = Reg("pmaddr12", PMADDR12_CSR, true, 0);
  regs_.at(PMADDR13_CSR) = Reg("pmaddr13", PMADDR13_CSR, true, 0);
  regs_.at(PMADDR14_CSR) = Reg("pmaddr14", PMADDR14_CSR, true, 0);
  regs_.at(PMADDR15_CSR) = Reg("pmaddr15", PMADDR15_CSR, true, 0);

  // Machine Counter/Timers
  regs_.at(MCYCLE_CSR) = Reg("mcycle", MCYCLE_CSR, true, 0);
  regs_.at(MINSTRET_CSR) = Reg("minstret", MINSTRET_CSR, true, 0);
  regs_.at(MHPMCOUNTER3_CSR) = Reg("mhpmcounter3", MHPMCOUNTER3_CSR, true, 0);
  regs_.at(MHPMCOUNTER4_CSR) = Reg("mhpmcounter4", MHPMCOUNTER4_CSR, true, 0);
  regs_.at(MHPMCOUNTER5_CSR) = Reg("mhpmcounter5", MHPMCOUNTER5_CSR, true, 0);
  regs_.at(MHPMCOUNTER6_CSR) = Reg("mhpmcounter6", MHPMCOUNTER6_CSR, true, 0);
  regs_.at(MHPMCOUNTER7_CSR) = Reg("mhpmcounter7", MHPMCOUNTER7_CSR, true, 0);
  regs_.at(MHPMCOUNTER8_CSR) = Reg("mhpmcounter8", MHPMCOUNTER8_CSR, true, 0);
  regs_.at(MHPMCOUNTER9_CSR) = Reg("mhpmcounter9", MHPMCOUNTER9_CSR, true, 0);
  regs_.at(MHPMCOUNTER10_CSR) = Reg("mhpmcounter10", MHPMCOUNTER10_CSR, true, 0);
  regs_.at(MHPMCOUNTER11_CSR) = Reg("mhpmcounter11", MHPMCOUNTER11_CSR, true, 0);
  regs_.at(MHPMCOUNTER12_CSR) = Reg("mhpmcounter12", MHPMCOUNTER12_CSR, true, 0);
  regs_.at(MHPMCOUNTER13_CSR) = Reg("mhpmcounter13", MHPMCOUNTER13_CSR, true, 0);
  regs_.at(MHPMCOUNTER14_CSR) = Reg("mhpmcounter14", MHPMCOUNTER14_CSR, true, 0);
  regs_.at(MHPMCOUNTER15_CSR) = Reg("mhpmcounter15", MHPMCOUNTER15_CSR, true, 0);
  regs_.at(MHPMCOUNTER16_CSR) = Reg("mhpmcounter16", MHPMCOUNTER16_CSR, true, 0);
  regs_.at(MHPMCOUNTER17_CSR) = Reg("mhpmcounter17", MHPMCOUNTER17_CSR, true, 0);
  regs_.at(MHPMCOUNTER18_CSR) = Reg("mhpmcounter18", MHPMCOUNTER18_CSR, true, 0);
  regs_.at(MHPMCOUNTER19_CSR) = Reg("mhpmcounter19", MHPMCOUNTER19_CSR, true, 0);
  regs_.at(MHPMCOUNTER20_CSR) = Reg("mhpmcounter20", MHPMCOUNTER20_CSR, true, 0);
  regs_.at(MHPMCOUNTER21_CSR) = Reg("mhpmcounter21", MHPMCOUNTER21_CSR, true, 0);
  regs_.at(MHPMCOUNTER22_CSR) = Reg("mhpmcounter22", MHPMCOUNTER22_CSR, true, 0);
  regs_.at(MHPMCOUNTER23_CSR) = Reg("mhpmcounter23", MHPMCOUNTER23_CSR, true, 0);
  regs_.at(MHPMCOUNTER24_CSR) = Reg("mhpmcounter24", MHPMCOUNTER24_CSR, true, 0);
  regs_.at(MHPMCOUNTER25_CSR) = Reg("mhpmcounter25", MHPMCOUNTER25_CSR, true, 0);
  regs_.at(MHPMCOUNTER26_CSR) = Reg("mhpmcounter26", MHPMCOUNTER26_CSR, true, 0);
  regs_.at(MHPMCOUNTER27_CSR) = Reg("mhpmcounter27", MHPMCOUNTER27_CSR, true, 0);
  regs_.at(MHPMCOUNTER28_CSR) = Reg("mhpmcounter28", MHPMCOUNTER28_CSR, true, 0);
  regs_.at(MHPMCOUNTER29_CSR) = Reg("mhpmcounter29", MHPMCOUNTER29_CSR, true, 0);
  regs_.at(MHPMCOUNTER30_CSR) = Reg("mhpmcounter30", MHPMCOUNTER30_CSR, true, 0);
  regs_.at(MHPMCOUNTER31_CSR) = Reg("mhpmcounter31", MHPMCOUNTER31_CSR, true, 0);

  regs_.at(MCYCLEH_CSR) = Reg("mcycleh", MCYCLEH_CSR, true, 0);
  regs_.at(MINSTRETH_CSR) = Reg("minstreth", MINSTRETH_CSR, true, 0);
  regs_.at(MHPMCOUNTER3H_CSR) = Reg("mhpmcounter3h", MHPMCOUNTER3H_CSR, true, 0);
  regs_.at(MHPMCOUNTER4H_CSR) = Reg("mhpmcounter4h", MHPMCOUNTER4H_CSR, true, 0);
  regs_.at(MHPMCOUNTER5H_CSR) = Reg("mhpmcounter5h", MHPMCOUNTER5H_CSR, true, 0);
  regs_.at(MHPMCOUNTER6H_CSR) = Reg("mhpmcounter6h", MHPMCOUNTER6H_CSR, true, 0);
  regs_.at(MHPMCOUNTER7H_CSR) = Reg("mhpmcounter7h", MHPMCOUNTER7H_CSR, true, 0);
  regs_.at(MHPMCOUNTER8H_CSR) = Reg("mhpmcounter8h", MHPMCOUNTER8H_CSR, true, 0);
  regs_.at(MHPMCOUNTER9H_CSR) = Reg("mhpmcounter9h", MHPMCOUNTER9H_CSR, true, 0);
  regs_.at(MHPMCOUNTER10H_CSR) = Reg("mhpmcounter10h", MHPMCOUNTER10H_CSR, true, 0);
  regs_.at(MHPMCOUNTER11H_CSR) = Reg("mhpmcounter11h", MHPMCOUNTER11H_CSR, true, 0);
  regs_.at(MHPMCOUNTER12H_CSR) = Reg("mhpmcounter12h", MHPMCOUNTER12H_CSR, true, 0);
  regs_.at(MHPMCOUNTER13H_CSR) = Reg("mhpmcounter13h", MHPMCOUNTER13H_CSR, true, 0);
  regs_.at(MHPMCOUNTER14H_CSR) = Reg("mhpmcounter14h", MHPMCOUNTER14H_CSR, true, 0);
  regs_.at(MHPMCOUNTER15H_CSR) = Reg("mhpmcounter15h", MHPMCOUNTER15H_CSR, true, 0);
  regs_.at(MHPMCOUNTER16H_CSR) = Reg("mhpmcounter16h", MHPMCOUNTER16H_CSR, true, 0);
  regs_.at(MHPMCOUNTER17H_CSR) = Reg("mhpmcounter17h", MHPMCOUNTER17H_CSR, true, 0);
  regs_.at(MHPMCOUNTER18H_CSR) = Reg("mhpmcounter18h", MHPMCOUNTER18H_CSR, true, 0);
  regs_.at(MHPMCOUNTER19H_CSR) = Reg("mhpmcounter19h", MHPMCOUNTER19H_CSR, true, 0);
  regs_.at(MHPMCOUNTER20H_CSR) = Reg("mhpmcounter20h", MHPMCOUNTER20H_CSR, true, 0);
  regs_.at(MHPMCOUNTER21H_CSR) = Reg("mhpmcounter21h", MHPMCOUNTER21H_CSR, true, 0);
  regs_.at(MHPMCOUNTER22H_CSR) = Reg("mhpmcounter22h", MHPMCOUNTER22H_CSR, true, 0);
  regs_.at(MHPMCOUNTER23H_CSR) = Reg("mhpmcounter23h", MHPMCOUNTER23H_CSR, true, 0);
  regs_.at(MHPMCOUNTER24H_CSR) = Reg("mhpmcounter24h", MHPMCOUNTER24H_CSR, true, 0);
  regs_.at(MHPMCOUNTER25H_CSR) = Reg("mhpmcounter25h", MHPMCOUNTER25H_CSR, true, 0);
  regs_.at(MHPMCOUNTER26H_CSR) = Reg("mhpmcounter26h", MHPMCOUNTER26H_CSR, true, 0);
  regs_.at(MHPMCOUNTER27H_CSR) = Reg("mhpmcounter27h", MHPMCOUNTER27H_CSR, true, 0);
  regs_.at(MHPMCOUNTER28H_CSR) = Reg("mhpmcounter28h", MHPMCOUNTER28H_CSR, true, 0);
  regs_.at(MHPMCOUNTER29H_CSR) = Reg("mhpmcounter29h", MHPMCOUNTER29H_CSR, true, 0);
  regs_.at(MHPMCOUNTER30H_CSR) = Reg("mhpmcounter30h", MHPMCOUNTER30H_CSR, true, 0);
  regs_.at(MHPMCOUNTER31H_CSR) = Reg("mhpmcounter31h", MHPMCOUNTER31H_CSR, true, 0);

  // Machine counter setup.
  regs_.at(MHPMEVENT3_CSR) = Reg("mhpmevent3", MHPMEVENT3_CSR, true, 0);
  regs_.at(MHPMEVENT4_CSR) = Reg("mhpmevent4", MHPMEVENT4_CSR, true, 0);
  regs_.at(MHPMEVENT5_CSR) = Reg("mhpmevent5", MHPMEVENT5_CSR, true, 0);
  regs_.at(MHPMEVENT6_CSR) = Reg("mhpmevent6", MHPMEVENT6_CSR, true, 0);
  regs_.at(MHPMEVENT7_CSR) = Reg("mhpmevent7", MHPMEVENT7_CSR, true, 0);
  regs_.at(MHPMEVENT8_CSR) = Reg("mhpmevent8", MHPMEVENT8_CSR, true, 0);
  regs_.at(MHPMEVENT9_CSR) = Reg("mhpmevent9", MHPMEVENT9_CSR, true, 0);
  regs_.at(MHPMEVENT10_CSR) = Reg("mhpmevent10", MHPMEVENT10_CSR, true, 0);
  regs_.at(MHPMEVENT11_CSR) = Reg("mhpmevent11", MHPMEVENT11_CSR, true, 0);
  regs_.at(MHPMEVENT12_CSR) = Reg("mhpmevent12", MHPMEVENT12_CSR, true, 0);
  regs_.at(MHPMEVENT13_CSR) = Reg("mhpmevent13", MHPMEVENT13_CSR, true, 0);
  regs_.at(MHPMEVENT14_CSR) = Reg("mhpmevent14", MHPMEVENT14_CSR, true, 0);
  regs_.at(MHPMEVENT15_CSR) = Reg("mhpmevent15", MHPMEVENT15_CSR, true, 0);
  regs_.at(MHPMEVENT16_CSR) = Reg("mhpmevent16", MHPMEVENT16_CSR, true, 0);
  regs_.at(MHPMEVENT17_CSR) = Reg("mhpmevent17", MHPMEVENT17_CSR, true, 0);
  regs_.at(MHPMEVENT18_CSR) = Reg("mhpmevent18", MHPMEVENT18_CSR, true, 0);
  regs_.at(MHPMEVENT19_CSR) = Reg("mhpmevent19", MHPMEVENT19_CSR, true, 0);
  regs_.at(MHPMEVENT20_CSR) = Reg("mhpmevent20", MHPMEVENT20_CSR, true, 0);
  regs_.at(MHPMEVENT21_CSR) = Reg("mhpmevent21", MHPMEVENT21_CSR, true, 0);
  regs_.at(MHPMEVENT22_CSR) = Reg("mhpmevent22", MHPMEVENT22_CSR, true, 0);
  regs_.at(MHPMEVENT23_CSR) = Reg("mhpmevent23", MHPMEVENT23_CSR, true, 0);
  regs_.at(MHPMEVENT24_CSR) = Reg("mhpmevent24", MHPMEVENT24_CSR, true, 0);
  regs_.at(MHPMEVENT25_CSR) = Reg("mhpmevent25", MHPMEVENT25_CSR, true, 0);
  regs_.at(MHPMEVENT26_CSR) = Reg("mhpmevent26", MHPMEVENT26_CSR, true, 0);
  regs_.at(MHPMEVENT27_CSR) = Reg("mhpmevent27", MHPMEVENT27_CSR, true, 0);
  regs_.at(MHPMEVENT28_CSR) = Reg("mhpmevent28", MHPMEVENT28_CSR, true, 0);
  regs_.at(MHPMEVENT29_CSR) = Reg("mhpmevent29", MHPMEVENT29_CSR, true, 0);
  regs_.at(MHPMEVENT30_CSR) = Reg("mhpmevent30", MHPMEVENT30_CSR, true, 0);
  regs_.at(MHPMEVENT31_CSR) = Reg("mhpmevent31", MHPMEVENT31_CSR, true, 0);

}


template <typename URV>
void
CsRegs<URV>::defineSupervisorRegs()
{
  typedef Csr<URV> Reg;

  // Supervisor trap SETUP_CSR.
  regs_.at(SSTATUS_CSR) = Reg("sstatus", SSTATUS_CSR, true, 0);
  regs_.at(SEDELEG_CSR) = Reg("sedeleg", SEDELEG_CSR, true, 0);
  regs_.at(SIDELEG_CSR) = Reg("sideleg", SIDELEG_CSR, true, 0);
  regs_.at(SIE_CSR) = Reg("sie", SIE_CSR, true, 0);
  regs_.at(STVEC_CSR) = Reg("stvec", STVEC_CSR, true, 0);
  regs_.at(SCOUNTEREN_CSR) = Reg("scounteren", SCOUNTEREN_CSR, true, 0);

  // Supervisor Trap Handling 
  regs_.at(SSCRATCH_CSR) = Reg("sscratch", SSCRATCH_CSR, true, 0);
  regs_.at(SEPC_CSR) = Reg("sepc", SEPC_CSR, true, 0);
  regs_.at(SCAUSE_CSR) = Reg("scause", SCAUSE_CSR, true, 0);
  regs_.at(STVAL_CSR) = Reg("stval", STVAL_CSR, true, 0);
  regs_.at(SIP_CSR) = Reg("sip", SIP_CSR, true, 0);

  // Supervisor Protection and Translation 
  regs_.at(SATP_CSR) = Reg("satp", SATP_CSR, true, 0);

}


template <typename URV>
void
CsRegs<URV>::defineUserRegs()
{
  typedef Csr<URV> Reg;

  // User trap setup.
  regs_.at(USTATUS_CSR) = Reg("ustatus", USTATUS_CSR, true, 0);
  regs_.at(UIE_CSR) = Reg("uie", UIE_CSR, true, 0);
  regs_.at(UTVEC_CSR) = Reg("utvec", UTVEC_CSR, true, 0);

  // User Trap Handling
  regs_.at(USCRATCH_CSR) = Reg("uscratch", USCRATCH_CSR, true, 0);
  regs_.at(UEPC_CSR) = Reg("uepc", UEPC_CSR, true, 0);
  regs_.at(UCAUSE_CSR) = Reg("ucause", UCAUSE_CSR, true, 0);
  regs_.at(UTVAL_CSR) = Reg("utval", UTVAL_CSR, true, 0);
  regs_.at(UIP_CSR) = Reg("uip", UIP_CSR, true, 0);

  // User Floating-Point CSRs
  regs_.at(FFLAGS_CSR) = Reg("fflags", FFLAGS_CSR, true, 0);
  regs_.at(FRM_CSR) = Reg("frm", FRM_CSR, true, 0);
  regs_.at(FCSR_CSR) = Reg("fcsr", FCSR_CSR, true, 0);

  // User Counter/Timers
  regs_.at(CYCLE_CSR) = Reg("cycle", CYCLE_CSR, true, 0);
  regs_.at(TIME_CSR) = Reg("time", TIME_CSR, true, 0);
  regs_.at(INSTRET_CSR) = Reg("instret", INSTRET_CSR, true, 0);
  regs_.at(HPMCOUNTER3_CSR) = Reg("hpmcounter3", HPMCOUNTER3_CSR, true, 0);
  regs_.at(HPMCOUNTER4_CSR) = Reg("hpmcounter4", HPMCOUNTER4_CSR, true, 0);
  regs_.at(HPMCOUNTER5_CSR) = Reg("hpmcounter5", HPMCOUNTER5_CSR, true, 0);
  regs_.at(HPMCOUNTER6_CSR) = Reg("hpmcounter6", HPMCOUNTER6_CSR, true, 0);
  regs_.at(HPMCOUNTER7_CSR) = Reg("hpmcounter7", HPMCOUNTER7_CSR, true, 0);
  regs_.at(HPMCOUNTER8_CSR) = Reg("hpmcounter8", HPMCOUNTER8_CSR, true, 0);
  regs_.at(HPMCOUNTER9_CSR) = Reg("hpmcounter9", HPMCOUNTER9_CSR, true, 0);
  regs_.at(HPMCOUNTER10_CSR) = Reg("hpmcounter10", HPMCOUNTER10_CSR, true, 0);
  regs_.at(HPMCOUNTER11_CSR) = Reg("hpmcounter11", HPMCOUNTER11_CSR, true, 0);
  regs_.at(HPMCOUNTER12_CSR) = Reg("hpmcounter12", HPMCOUNTER12_CSR, true, 0);
  regs_.at(HPMCOUNTER13_CSR) = Reg("hpmcounter13", HPMCOUNTER13_CSR, true, 0);
  regs_.at(HPMCOUNTER14_CSR) = Reg("hpmcounter14", HPMCOUNTER14_CSR, true, 0);
  regs_.at(HPMCOUNTER15_CSR) = Reg("hpmcounter15", HPMCOUNTER15_CSR, true, 0);
  regs_.at(HPMCOUNTER16_CSR) = Reg("hpmcounter16", HPMCOUNTER16_CSR, true, 0);
  regs_.at(HPMCOUNTER17_CSR) = Reg("hpmcounter17", HPMCOUNTER17_CSR, true, 0);
  regs_.at(HPMCOUNTER18_CSR) = Reg("hpmcounter18", HPMCOUNTER18_CSR, true, 0);
  regs_.at(HPMCOUNTER19_CSR) = Reg("hpmcounter19", HPMCOUNTER19_CSR, true, 0);
  regs_.at(HPMCOUNTER20_CSR) = Reg("hpmcounter20", HPMCOUNTER20_CSR, true, 0);
  regs_.at(HPMCOUNTER21_CSR) = Reg("hpmcounter21", HPMCOUNTER21_CSR, true, 0);
  regs_.at(HPMCOUNTER22_CSR) = Reg("hpmcounter22", HPMCOUNTER22_CSR, true, 0);
  regs_.at(HPMCOUNTER23_CSR) = Reg("hpmcounter23", HPMCOUNTER23_CSR, true, 0);
  regs_.at(HPMCOUNTER24_CSR) = Reg("hpmcounter24", HPMCOUNTER24_CSR, true, 0);
  regs_.at(HPMCOUNTER25_CSR) = Reg("hpmcounter25", HPMCOUNTER25_CSR, true, 0);
  regs_.at(HPMCOUNTER26_CSR) = Reg("hpmcounter26", HPMCOUNTER26_CSR, true, 0);
  regs_.at(HPMCOUNTER27_CSR) = Reg("hpmcounter27", HPMCOUNTER27_CSR, true, 0);
  regs_.at(HPMCOUNTER28_CSR) = Reg("hpmcounter28", HPMCOUNTER28_CSR, true, 0);
  regs_.at(HPMCOUNTER29_CSR) = Reg("hpmcounter29", HPMCOUNTER29_CSR, true, 0);
  regs_.at(HPMCOUNTER30_CSR) = Reg("hpmcounter30", HPMCOUNTER30_CSR, true, 0);
  regs_.at(HPMCOUNTER31_CSR) = Reg("hpmcounter31", HPMCOUNTER31_CSR, true, 0);

  regs_.at(CYCLE_CSR) = Reg("cycle", CYCLE_CSR, true, 0);
  regs_.at(TIME_CSR) = Reg("time", TIME_CSR, true, 0);
  regs_.at(INSTRET_CSR) = Reg("instret", INSTRET_CSR, true, 0);
  regs_.at(HPMCOUNTER3_CSR) = Reg("hpmcounter3", HPMCOUNTER3_CSR, true, 0);
  regs_.at(HPMCOUNTER4_CSR) = Reg("hpmcounter4", HPMCOUNTER4_CSR, true, 0);
  regs_.at(HPMCOUNTER5_CSR) = Reg("hpmcounter5", HPMCOUNTER5_CSR, true, 0);
  regs_.at(HPMCOUNTER6_CSR) = Reg("hpmcounter6", HPMCOUNTER6_CSR, true, 0);
  regs_.at(HPMCOUNTER7_CSR) = Reg("hpmcounter7", HPMCOUNTER7_CSR, true, 0);
  regs_.at(HPMCOUNTER8_CSR) = Reg("hpmcounter8", HPMCOUNTER8_CSR, true, 0);
  regs_.at(HPMCOUNTER9_CSR) = Reg("hpmcounter9", HPMCOUNTER9_CSR, true, 0);
  regs_.at(HPMCOUNTER10_CSR) = Reg("hpmcounter10", HPMCOUNTER10_CSR, true, 0);
  regs_.at(HPMCOUNTER11_CSR) = Reg("hpmcounter11", HPMCOUNTER11_CSR, true, 0);
  regs_.at(HPMCOUNTER12_CSR) = Reg("hpmcounter12", HPMCOUNTER12_CSR, true, 0);
  regs_.at(HPMCOUNTER13_CSR) = Reg("hpmcounter13", HPMCOUNTER13_CSR, true, 0);
  regs_.at(HPMCOUNTER14_CSR) = Reg("hpmcounter14", HPMCOUNTER14_CSR, true, 0);
  regs_.at(HPMCOUNTER15_CSR) = Reg("hpmcounter15", HPMCOUNTER15_CSR, true, 0);
  regs_.at(HPMCOUNTER16_CSR) = Reg("hpmcounter16", HPMCOUNTER16_CSR, true, 0);
  regs_.at(HPMCOUNTER17_CSR) = Reg("hpmcounter17", HPMCOUNTER17_CSR, true, 0);
  regs_.at(HPMCOUNTER18_CSR) = Reg("hpmcounter18", HPMCOUNTER18_CSR, true, 0);
  regs_.at(HPMCOUNTER19_CSR) = Reg("hpmcounter19", HPMCOUNTER19_CSR, true, 0);
  regs_.at(HPMCOUNTER20_CSR) = Reg("hpmcounter20", HPMCOUNTER20_CSR, true, 0);
  regs_.at(HPMCOUNTER21_CSR) = Reg("hpmcounter21", HPMCOUNTER21_CSR, true, 0);
  regs_.at(HPMCOUNTER22_CSR) = Reg("hpmcounter22", HPMCOUNTER22_CSR, true, 0);
  regs_.at(HPMCOUNTER23_CSR) = Reg("hpmcounter23", HPMCOUNTER23_CSR, true, 0);
  regs_.at(HPMCOUNTER24_CSR) = Reg("hpmcounter24", HPMCOUNTER24_CSR, true, 0);
  regs_.at(HPMCOUNTER25_CSR) = Reg("hpmcounter25", HPMCOUNTER25_CSR, true, 0);
  regs_.at(HPMCOUNTER26_CSR) = Reg("hpmcounter26", HPMCOUNTER26_CSR, true, 0);
  regs_.at(HPMCOUNTER27_CSR) = Reg("hpmcounter27", HPMCOUNTER27_CSR, true, 0);
  regs_.at(HPMCOUNTER28_CSR) = Reg("hpmcounter28", HPMCOUNTER28_CSR, true, 0);
  regs_.at(HPMCOUNTER29_CSR) = Reg("hpmcounter29", HPMCOUNTER29_CSR, true, 0);
  regs_.at(HPMCOUNTER30_CSR) = Reg("hpmcounter30", HPMCOUNTER30_CSR, true, 0);
  regs_.at(HPMCOUNTER31_CSR) = Reg("hpmcounter31", HPMCOUNTER31_CSR, true, 0);
}


template <typename URV>
void
CsRegs<URV>::defineDebugRegs()
{
  typedef Csr<URV> Reg;

  // Debug/Trace registers.
  regs_.at(TSELECT_CSR) = Reg("tselect", TSELECT_CSR, true, 0);
  regs_.at(TDATA1_CSR) = Reg("tdata1", TDATA1_CSR, true, 0);
  regs_.at(TDATA2_CSR) = Reg("tdata2", TDATA2_CSR, true, 0);
  regs_.at(TDATA3_CSR) = Reg("tdata3", TDATA3_CSR, true, 0);

  // Debug mode registers.
  regs_.at(DSCR_CSR) = Reg("dscr", DSCR_CSR, true, 0);
  regs_.at(DPC_CSR) = Reg("dpc", DPC_CSR, true, 0);
  regs_.at(DSCRATCH_CSR) = Reg("dscratch", DSCRATCH_CSR, true, 0);
}


template <typename URV>
bool
CsRegs<URV>::getRetiredInstCount(uint64_t& count) const
{
  if (MINSTRET_CSR < 0 or MINSTRET_CSR >= regs_.size())
    return false;

  const Csr<URV>& csr = regs_.at(MINSTRET_CSR);
  if (not csr.isImplemented())
    return false;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      count = csr.getValue();
      return true;
    }

  if (sizeof(URV) == 4)
    {
      if (MINSTRETH_CSR < 0 or MINSTRETH_CSR >= regs_.size())
	return false;

      const Csr<URV>& csrh = regs_.at(MINSTRETH_CSR);
      if (not csrh.isImplemented())
	return false;

      count = uint64_t(csrh.getValue()) << 32;
      count |= csr.getValue();
      return true;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return false;
}


template <typename URV>
bool
CsRegs<URV>::setRetiredInstCount(uint64_t count)
{
  if (MINSTRET_CSR < 0 or MINSTRET_CSR >= regs_.size())
    return false;

  Csr<URV>& csr = regs_.at(MCYCLE_CSR);
  if (not csr.isImplemented())
    return false;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      csr.setValue(count);
      return true;
    }

  if (sizeof(URV) == 4)
    {
      if (MINSTRETH_CSR < 0 or MINSTRETH_CSR >= regs_.size())
	return false;

      Csr<URV>& csrh = regs_.at(MINSTRETH_CSR);
      if (not csrh.isImplemented())
	return false;
      csrh.setValue(count >> 32);
      csr.setValue(count);
      return true;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return false;
}


template <typename URV>
bool
CsRegs<URV>::getCycleCount(uint64_t& count) const
{
  if (MCYCLE_CSR < 0 or MCYCLE_CSR >= regs_.size())
    return false;

  const Csr<URV>& csr = regs_.at(MCYCLE_CSR);
  if (not csr.isImplemented())
    return false;

  if (sizeof(URV) == 8)  // 64-bit machine
    {
      count = csr.getValue();
      return true;
    }

  if (sizeof(URV) == 4)
    {
      if (MCYCLEH_CSR < 0 or MCYCLEH_CSR >= regs_.size())
	return false;

      const Csr<URV>& csrh = regs_.at(MCYCLEH_CSR);
      if (not csrh.isImplemented())
	return false;

      count = uint64_t(csrh.getValue()) << 32;
      count |= csr.getValue();
      return true;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return false;
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
      csr.setValue(count);
      return true;
    }

  if (sizeof(URV) == 4)
    {
      if (MCYCLEH_CSR < 0 or MCYCLEH_CSR >= regs_.size())
	return false;

      Csr<URV>& csrh = regs_.at(MCYCLEH_CSR);
      if (not csrh.isImplemented())
	return false;
      csrh.setValue(count >> 32);
      csr.setValue(count);
      return true;
    }

  assert(0 and "Only 32 and 64-bit CSRs are currently implemented");
  return false;
}


template <typename URV>
void
CsRegs<URV>::setMeip(bool bit)
{
  URV val = regs_.at(MIP_CSR).getValue();

  if (bit)
    val |= (URV(1) << 11);
  else
    val &= ~(URV(1) << 11);

  regs_.at(MIP_CSR).setValueNoMask(val);
}


template <typename URV>
void
CsRegs<URV>::setMtip(bool bit)
{
  URV val = regs_.at(MIP_CSR).getValue();

  if (bit)
    val |= (URV(1) << 7);
  else
    val &= ~(URV(1) << 7);

  regs_.at(MIP_CSR).setValueNoMask(val);
}


template <typename URV>
void
CsRegs<URV>::setMsip(bool bit)
{
  URV val = regs_.at(MIP_CSR).getValue();

  if (bit)
    val |= (URV(1) << 3);
  else
    val &= ~(URV(1) << 3);

  regs_.at(MIP_CSR).setValueNoMask(val);
}


template class CsRegs<uint32_t>;
template class CsRegs<uint64_t>;
