//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2018 Western Digital Corporation or its affiliates.
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
// 
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//

#include <iostream>
#include <algorithm>
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
		       bool implemented, URV resetValue, URV writeMask,
		       URV pokeMask, bool isDebug, bool quiet)
{
  size_t ix = size_t(csrn);

  if (ix >= regs_.size())
    return nullptr;

  if (nameToNumber_.count(name))
    {
      if (not quiet)
	std::cerr << "Error: CSR " << name << " already defined\n";
      return nullptr;
    }

  auto& csr = regs_.at(ix);
  if (csr.isDefined())
    {
      if (not quiet)
	std::cerr << "Error: CSR number 0x" << std::hex << size_t(csrn)
		  << " is already defined as " << csr.getName() << '\n';
      return nullptr;
    }

  csr.setDefined(true);

  csr.config(name, csrn, mandatory, implemented, resetValue, writeMask,
	     pokeMask, isDebug);

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
  if (not csr)
    return false;

  if (mode < csr->privilegeMode() or csr->isReadOnly())
    return false;

  if (csr->isDebug() and not debugMode)
    return false;

  // fflags and frm are part of fcsr
  if (number == CsrNumber::FFLAGS or number == CsrNumber::FRM or number == CsrNumber::FCSR)
    {
      csr->write(value);
      recordWrite(number);
      updateFcsrGroupForWrite(number, value);
      return true;
    }

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    {
      if (not writeTdata(number, mode, debugMode, value))
	return false;
      recordWrite(number);
      return true;
    }

  if (number >= CsrNumber::MHPMEVENT3 and number <= CsrNumber::MHPMEVENT31)
    {
      if (value > maxEventId_)
	value = maxEventId_;
      unsigned counterIx = unsigned(number) - unsigned(CsrNumber::MHPMEVENT3);
      assignEventToCounter(value, counterIx);
    }

  if (number == CsrNumber::MRAC)
    {
      // A value of 0b11 (io/cacheable) for the ith region is invalid:
      // Make it 0b10 (io/non-cacheable).
      URV mask = 0b11;
      for (unsigned i = 0; i < sizeof(URV)*8; i += 2)
	{
	  if ((value & mask) == mask)
	    value = (value & ~mask) | (0b10 << i);
	  mask = mask << 2;
	}
    }

  csr->write(value);
  recordWrite(number);

  // Cache interrupt enable.
  if (number == CsrNumber::MSTATUS)
    {
      MstatusFields<URV> fields(csr->read());
      interruptEnable_ = fields.bits_.MIE;
    }

  // Writing MDEAU unlocks mdseac.
  if (number == CsrNumber::MDEAU)
    lockMdseac(false);

  // Writing of the MEIVT changes the base address in MEIHAP.
  if (number == CsrNumber::MEIVT)
    {
      value = (value >> 10) << 10;  // Clear least sig 10 bits keeping base.
      size_t meihapIx = size_t(CsrNumber::MEIHAP);
      URV meihap = regs_.at(meihapIx).read();
      meihap &= 0x3ff;  // Clear base address bits.
      meihap |= value;  // Copy base address bits from MEIVT.
      regs_.at(meihapIx).poke(meihap);
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

  triggers_.reset();

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
		       URV resetValue, URV mask, URV pokeMask, bool isDebug)
{
  auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  size_t num = size_t(iter->second);
  if (num >= regs_.size())
    return false;

  return configCsr(CsrNumber(num), implemented, resetValue, mask, pokeMask,
		   isDebug);
}


template <typename URV>
bool
CsRegs<URV>::configCsr(CsrNumber csrNum, bool implemented,
		       URV resetValue, URV mask, URV pokeMask, bool isDebug)
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

  csr.setImplemented(implemented);
  csr.setInitialValue(resetValue);
  csr.setWriteMask(mask);
  csr.setPokeMask(pokeMask);
  csr.pokeNoMask(resetValue);
  csr.setIsDebug(isDebug);

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
      bool isDebug = false;
      if (not configCsr(csrNum, true, resetValue, mask, pokeMask, isDebug))
	errors++;

      if constexpr (sizeof(URV) == 4)
         {
	   csrNum = CsrNumber(i + unsigned(CsrNumber::MHPMCOUNTER3H));
	   if (not configCsr(csrNum, true, resetValue, mask, pokeMask, isDebug))
	     errors++;
	 }

      csrNum = CsrNumber(i + unsigned(CsrNumber::MHPMEVENT3));
      if (not configCsr(csrNum, true, resetValue, mask, pokeMask, isDebug))
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
CsRegs<URV>::recordWrite(CsrNumber num)
{
  auto& lwr = lastWrittenRegs_;
  if (std::find(lwr.begin(), lwr.end(), num) == lwr.end())
    lwr.push_back(num);
}


template <typename URV>
void
CsRegs<URV>::defineMachineRegs()
{
  URV rom = 0;        // Read-only mask: no bit writeable.
  URV wam = ~URV(0);  // Write-all mask: all bits writeable.

  bool mand = true;  // Mandatory.
  bool imp = true;   // Implemented.

  using Csrn = CsrNumber;

  // Machine info.
  defineCsr("mvendorid", Csrn::MVENDORID, mand, imp, 0, rom, rom);
  defineCsr("marchid",   Csrn::MARCHID,   mand, imp, 0, rom, rom);
  defineCsr("mimpid",    Csrn::MIMPID,    mand, imp, 0, rom, rom);
  defineCsr("mhartid",   Csrn::MHARTID,   mand, imp, 0, rom, rom);

  // Machine trap setup.

  // mstatus
  //           S R        T T T M S M X  F  M  R  S M R S U M R S U
  //           D E        S W V X U P S  S  P  E  P P E P P I E I I
  //             S        R   M R M R       P  S  P I S I I E S E E
  //                                V               E   E E
  URV mask = 0b0'00000000'1'1'1'1'1'1'11'11'11'00'1'1'0'1'1'1'0'1'1;
  URV val = 0;
  if constexpr (sizeof(URV) == 8)
    mask |= (URV(0b1111) << 32);  // Mask for SXL and UXL.
  defineCsr("mstatus", Csrn::MSTATUS, mand, imp, val, mask, mask);
  defineCsr("misa", Csrn::MISA, mand,  imp, 0x40001104, rom, rom);
  defineCsr("medeleg", Csrn::MEDELEG, !mand, !imp, 0, 0, 0);
  defineCsr("mideleg", Csrn::MIDELEG, !mand, !imp, 0, 0, 0);

  // Interrupt enable: Least sig 12 bits corresponding to the 12
  // interrupt causes are writable.
  URV mieMask = 0xfff; 
  defineCsr("mie", Csrn::MIE, mand, imp, 0, mieMask, mieMask);

  // Initial value of 0: vectored interrupt. Mask of ~2 to make bit 1
  // non-writable.
  mask = ~URV(2);
  defineCsr("mtvec", Csrn::MTVEC, mand, imp, 0, mask, mask);

  defineCsr("mcounteren", Csrn::MCOUNTEREN, !mand, !imp, 0, 0, 0);

  // Machine trap handling: mscratch and mepc.
  defineCsr("mscratch", Csrn::MSCRATCH, mand, imp, 0, wam, wam);
  mask = ~URV(1);  // Bit 0 of MEPC is not writable.
  defineCsr("mepc", Csrn::MEPC, mand, imp, 0, mask, mask);

  // All bits of mcause writeable.
  defineCsr("mcause", Csrn::MCAUSE, mand, imp, 0, wam, wam);
  defineCsr("mtval", Csrn::MTVAL, mand, imp, 0, wam, wam);

  // MIP is read-only for CSR instructions but the bits corresponding
  // to defined interrupts are modifiable.
  defineCsr("mip", CsrNumber::MIP, mand, imp, 0, rom, mieMask);

  // Machine protection and translation.
  defineCsr("pmpcfg0",   Csrn::PMPCFG0,   !mand, imp, 0, wam, wam);
  defineCsr("pmpcfg1",   Csrn::PMPCFG1,   !mand, imp, 0, wam, wam);
  defineCsr("pmpcfg2",   Csrn::PMPCFG2,   !mand, imp, 0, wam, wam);
  defineCsr("pmpcfg3",   Csrn::PMPCFG3,   !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr0",  Csrn::PMPADDR0,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr1",  Csrn::PMPADDR1,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr2",  Csrn::PMPADDR2,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr3",  Csrn::PMPADDR3,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr4",  Csrn::PMPADDR4,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr5",  Csrn::PMPADDR5,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr6",  Csrn::PMPADDR6,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr7",  Csrn::PMPADDR7,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr8",  Csrn::PMPADDR8,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr9",  Csrn::PMPADDR9,  !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr10", Csrn::PMPADDR10, !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr11", Csrn::PMPADDR11, !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr12", Csrn::PMPADDR12, !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr13", Csrn::PMPADDR13, !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr14", Csrn::PMPADDR14, !mand, imp, 0, wam, wam);
  defineCsr("pmpaddr15", Csrn::PMPADDR15, !mand, imp, 0, wam, wam);

  // Machine Counter/Timers.
  defineCsr("mcycle",    Csrn::MCYCLE,    mand, imp, 0, wam, wam);
  defineCsr("minstret",  Csrn::MINSTRET,  mand, imp, 0, wam, wam);
  defineCsr("mcycleh",   Csrn::MCYCLEH,   mand, imp, 0, wam, wam);
  defineCsr("minstreth", Csrn::MINSTRETH, mand, imp, 0, wam, wam);

  // Define mhpmcounter3/mhpmcounter3h to mhpmcounter31/mhpmcounter31h
  // as write-anything/read-zero (user can change that in the config
  // file by setting the number of writeable counters). Same for
  // mhpmevent3/mhpmevent3h to mhpmevent3h/mhpmevent31h.
  for (unsigned i = 3; i <= 31; ++i)
    {
      CsrNumber csrNum = CsrNumber(unsigned(CsrNumber::MHPMCOUNTER3) + i - 3);
      std::string name = "mhpmcounter" + std::to_string(i);
      defineCsr(name, csrNum, mand, imp, 0, rom, rom);

      // High register counterpart of mhpmcounter.
      name += "h";
      csrNum = CsrNumber(unsigned(CsrNumber::MHPMCOUNTER3H) + i - 3);
      defineCsr(name, csrNum, mand, imp, 0, rom, rom);

      csrNum = CsrNumber(unsigned(CsrNumber::MHPMEVENT3) + i - 3);
      name = "mhpmevent" + std::to_string(i);
      defineCsr(name, csrNum, mand, imp, 0, rom, rom);
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

  using Csrn = CsrNumber;

  // Only bits spp, spie, upie, sie and uie of sstatus are writeable.
  URV mask = 0x233;
  defineCsr("sstatus",    Csrn::SSTATUS,    !mand, !imp, 0, mask, mask);

  defineCsr("sedeleg",    Csrn::SEDELEG,    !mand, !imp, 0, 0, 0);
  defineCsr("sideleg",    Csrn::SIDELEG,    !mand, !imp, 0, 0, 0);
  defineCsr("sie",        Csrn::SIE,        !mand, !imp, 0, 0, 0);
  defineCsr("stvec",      Csrn::STVEC,      !mand, !imp, 0, 0, 0);
  defineCsr("scounteren", Csrn::SCOUNTEREN, !mand, !imp, 0, 0, 0);

  // Supervisor Trap Handling 
  defineCsr("sscratch",   Csrn::SSCRATCH,   !mand, !imp, 0, 0, 0);
  defineCsr("sepc",       Csrn::SEPC,       !mand, !imp, 0, 0, 0);
  defineCsr("scause",     Csrn::SCAUSE,     !mand, !imp, 0, 0, 0);
  defineCsr("stval",      Csrn::STVAL,      !mand, !imp, 0, 0, 0);
  defineCsr("sip",        Csrn::SIP,        !mand, !imp, 0, 0, 0);

  // Supervisor Protection and Translation 
  defineCsr("satp",       Csrn::SATP,       !mand, !imp, 0, 0, 0);
}


template <typename URV>
void
CsRegs<URV>::defineUserRegs()
{
  bool mand = true;    // Mandatory.
  bool imp  = true;    // Implemented.
  URV  wam  = ~URV(0); // Write-all mask: all bits writeable.

  using Csrn = CsrNumber;

  // User trap setup.
  URV mask = 0x11; // Only UPIE and UIE bits are writeable.
  defineCsr("ustatus",  Csrn::USTATUS,  !mand, !imp, 0, mask, mask);
  defineCsr("uie",      Csrn::UIE,      !mand, !imp, 0, wam, wam);
  defineCsr("utvec",    Csrn::UTVEC,    !mand, !imp, 0, wam, wam);

  // User Trap Handling
  defineCsr("uscratch", Csrn::USCRATCH, !mand, !imp, 0, wam, wam);
  defineCsr("uepc",     Csrn::UEPC,     !mand, !imp, 0, wam, wam);
  defineCsr("ucause",   Csrn::UCAUSE,   !mand, !imp, 0, wam, wam);
  defineCsr("utval",    Csrn::UTVAL,    !mand, !imp, 0, wam, wam);
  defineCsr("uip",      Csrn::UIP,      !mand, !imp, 0, wam, wam);

  // User Floating-Point CSRs
  defineCsr("fflags",   Csrn::FFLAGS,   !mand, !imp, 0, wam, wam);
  defineCsr("frm",      Csrn::FRM,      !mand, !imp, 0, wam, wam);
  defineCsr("fcsr",     Csrn::FCSR,     !mand, !imp, 0, 0xff, 0xff);

  // User Counter/Timers
  defineCsr("cycle",    Csrn::CYCLE,    !mand, imp,  0, wam, wam);
  defineCsr("time",     Csrn::TIME,     !mand, imp,  0, wam, wam);
  defineCsr("instret",  Csrn::INSTRET,  !mand, imp,  0, wam, wam);
  defineCsr("cycleh",   Csrn::CYCLEH,   !mand, !imp, 0, wam, wam);
  defineCsr("timeh",    Csrn::TIMEH,    !mand, !imp, 0, wam, wam);
  defineCsr("instreth", Csrn::INSTRETH, !mand, !imp, 0, wam, wam);

  // Define hpmcounter3/hpmcounter3h to hpmcounter31/hpmcounter31h
  // as write-anything/read-zero (user can change that in the config
  // file).  Same for mhpmevent3/mhpmevent3h to mhpmevent3h/mhpmevent31h.
  for (unsigned i = 3; i <= 31; ++i)
    {
      CsrNumber csrNum = CsrNumber(unsigned(CsrNumber::HPMCOUNTER3) + i - 3);
      std::string name = "hpmcounter" + std::to_string(i);
      defineCsr(name, csrNum, !mand, !imp, 0, wam, wam);

      // High register counterpart of mhpmcounter.
      name += "h";
      csrNum = CsrNumber(unsigned(CsrNumber::HPMCOUNTER3H) + i - 3);
      defineCsr(name, csrNum, !mand, !imp, 0, wam, wam);
    }
}


template <typename URV>
void
CsRegs<URV>::defineDebugRegs()
{
  bool mand = true; // Mandatory.
  bool imp = true;  // Implemented.
  URV wam = ~URV(0);  // Write-all mask: all bits writeable.

  using Csrn = CsrNumber;

  // Debug/Trace registers.
  defineCsr("tselect", Csrn::TSELECT, !mand, imp,  0, wam, wam);
  defineCsr("tdata1",  Csrn::TDATA1,  !mand, imp,  0, wam, wam);
  defineCsr("tdata2",  Csrn::TDATA2,  !mand, imp,  0, wam, wam);
  defineCsr("tdata3",  Csrn::TDATA3,  !mand, !imp, 0, wam, wam);

  // Define triggers.
  unsigned triggerCount = 4;
  triggers_ = Triggers<URV>(triggerCount);

  Data1Bits<URV> data1Mask(0), data1Val(0);

  // Set the masks of the read-write fields of data1 to all 1.
  data1Mask.mcontrol_.dmode_   = 1;
  data1Mask.mcontrol_.hit_     = 1;
  data1Mask.mcontrol_.select_  = 1;
  data1Mask.mcontrol_.action_  = 1; // Only least sig bit writeable
  data1Mask.mcontrol_.chain_   = 1;
  data1Mask.mcontrol_.match_   = 1; // Only least sig bit of match is writeable.
  data1Mask.mcontrol_.m_       = 1;
  data1Mask.mcontrol_.execute_ = 1;
  data1Mask.mcontrol_.store_   = 1;
  data1Mask.mcontrol_.load_    = 1;

  // Set intitial values of fields of data1.
  data1Val.mcontrol_.type_ = unsigned(TriggerType::AddrData);
  data1Val.mcontrol_.maskMax_ = 8*sizeof(URV) - 1;  // 31 or 63.

  // Values, write-masks, and poke-masks of the three components of
  // the triggres.
  URV val1(data1Val.value_), val2(0), val3(0);
  URV wm1(data1Mask.value_), wm2(~URV(0)), wm3(0);
  URV pm1(wm1), pm2(wm2), pm3(wm3);

  triggers_.config(0, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);
  triggers_.config(1, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);
  triggers_.config(2, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);

  Data1Bits<URV> icountMask(0), icountVal(0);

  icountMask.icount_.dmode_  = 1;
  icountMask.icount_.count_  = (~0) & 0x3fff;
  icountMask.icount_.m_      = 1;
  icountMask.icount_.action_ = 0;
  icountMask.icount_.action_ = (~0) & 0x3f;

  icountVal.icount_.type_ = unsigned(TriggerType::InstCount);
  icountVal.icount_.count_ = 0;

  triggers_.config(3, icountVal.value_, 0, 0, icountMask.value_, 0, 0,
		   icountMask.value_, 0, 0);

  hasActiveTrigger_ = triggers_.hasActiveTrigger();
  hasActiveInstTrigger_ = triggers_.hasActiveInstTrigger();

  // Debug mode registers.
  URV dcsrVal = 0x40000003;
  URV dcsrMask = 0x00008e04;
  URV dcsrPokeMask = dcsrMask | 0x1c8; // Cause field modifiable
  bool isDebug = true;
  defineCsr("dcsr", Csrn::DCSR, !mand, imp, dcsrVal, dcsrMask,
	    dcsrPokeMask, isDebug);

  // Least sig bit of dpc is not writeable.
  URV dpcMask = ~URV(1);
  defineCsr("dpc", CsrNumber::DPC, !mand, imp, 0, dpcMask, dpcMask, isDebug);

  defineCsr("dscratch", CsrNumber::DSCRATCH, !mand, !imp, 0, wam, wam,
	    isDebug);
}


template <typename URV>
void
CsRegs<URV>::defineNonStandardRegs()
{
  URV rom = 0;        // Read-only mask: no bit writeable.
  URV wam = ~URV(0);  // Write-all mask: all bits writeable.

  bool mand = true; // Mandatory.
  bool imp = true;  // Implemented.

  using Csrn = CsrNumber;

  defineCsr("mrac",   Csrn::MRAC,     !mand, imp, 0, wam, wam);

  // mdseac is read-only to CSR insts but is modifiable with poke.
  defineCsr("mdseac", Csrn::MDSEAC,   !mand, imp, 0, rom, wam);

  // mdeau is write-only, it unlocks mdseac when written, it always
  // reads zero.
  defineCsr("mdeau",  Csrn::MDEAU,    !mand, imp, 0, rom, rom);

  // Least sig 10 bits of interrupt vector table (meivt) are read only.
  URV mask = (~URV(0)) << 10;
  defineCsr("meivt",  Csrn::MEIVT,    !mand, imp, 0, mask, mask);

  // None of the bits are writeable by CSR instructions. All but least
  // sig 2 bis are modifiable.
  defineCsr("meihap", Csrn::MEIHAP,   !mand, imp, 0, rom, ~URV(3));

  mask = 1;  // Only least sig bit writeable
  defineCsr("mgpmc",  Csrn::MGPMC,    !mand, imp, 1, mask, mask);
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

  // fflags and frm are parts of fcsr
  if (number == CsrNumber::FFLAGS or number == CsrNumber::FRM or number == CsrNumber::FCSR)
    {
      csr->poke(value);
      updateFcsrGroupForPoke(number, value);
      return true;
    }

  if (number >= CsrNumber::TDATA1 and number <= CsrNumber::TDATA3)
    return pokeTdata(number, value);

  if (number >= CsrNumber::MHPMEVENT3 and number <= CsrNumber::MHPMEVENT31)
    {
      if (value > maxEventId_)
	value = maxEventId_;
      unsigned counterIx = unsigned(number) - unsigned(CsrNumber::MHPMEVENT3);
      assignEventToCounter(value, counterIx);
    }

  if (number == CsrNumber::MRAC)
    {
      // A value of 0b11 (io/cacheable) for the ith region is invalid:
      // Make it 0b10 (io/non-cacheable).
      URV mask = 0b11;
      for (unsigned i = 0; i < sizeof(URV)*8; i += 2)
	{
	  if ((value & mask) == mask)
	    value = (value & ~mask) | (0b10 << i);
	  mask = mask << 2;
	}
    }

  csr->poke(value);

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

  // The CSR instructions never execute in debug mode.
  bool dMode = false;
  if (number == CsrNumber::TDATA1)
    {
      bool ok = triggers_.writeData1(trigger, dMode, value);
      if (ok) 
	{
	  // TDATA1 modified, update cached values
	  hasActiveTrigger_ = triggers_.hasActiveTrigger();
	  hasActiveInstTrigger_ = triggers_.hasActiveInstTrigger();
	}
      return ok;
    }

  if (number == CsrNumber::TDATA2)
    return triggers_.writeData2(trigger, dMode, value);

  if (number == CsrNumber::TDATA3)
    return triggers_.writeData3(trigger, dMode, value);

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
