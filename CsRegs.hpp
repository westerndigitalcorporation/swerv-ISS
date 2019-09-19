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

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include "Triggers.hpp"
#include "PerfRegs.hpp"


namespace WdRiscv
{

  /// RISCV interrupt cause.
  enum class InterruptCause : uint32_t
    {
      U_SOFTWARE   = 0,  // User mode software interrupt
      S_SOFTWARE   = 1,  // Supervisor mode software interrupt
      M_SOFTWARE   = 3,  // Machine mode software interrupt
      U_TIMER      = 4,  // User mode timer interrupt
      S_TIMER      = 5,  // Supervisor
      M_TIMER      = 7,  // Machine
      U_EXTERNAL   = 8,  // User mode external interrupt
      S_EXTERNAL   = 9,  // Supervisor
      M_EXTERNAL   = 11, // Machine
      M_INT_TIMER1 = 28, // Internal timer 1 (WD extension) bit position.
      M_INT_TIMER0 = 29, // Internal timer 0 (WD extension) bit position.
      M_LOCAL      = 30  // Correctable error local interrupt (WD extension)
    };

  /// RISCV exception cause.
  enum class ExceptionCause 
    {
      INST_ADDR_MISAL   = 0,  // Instruction address misaligned
      INST_ACC_FAULT    = 1,  // Instruction access fault
      ILLEGAL_INST      = 2,  // Illegal instruction
      BREAKP            = 3,  // Breakpoint
      LOAD_ADDR_MISAL   = 4,  // Load address misaligned
      LOAD_ACC_FAULT    = 5,  // Load access fault
      STORE_ADDR_MISAL  = 6,  // Store address misaligned
      STORE_ACC_FAULT   = 7,  // Store access fault.
      U_ENV_CALL        = 8,  // Environment call from user mode
      S_ENV_CALL        = 9,  // Environment call from supervisor mode
      M_ENV_CALL        = 11, // Environment call from machine mode
      INST_PAGE_FAULT   = 12, // Instruction page fault
      LOAD_PAGE_FAULT   = 13, // Load page fault
      STORE_PAGE_FAULT  = 15, // Store page fault
      NONE
    };

  /// Non-maskable interrupt cause.
  enum class NmiCause : uint32_t
    {
      UNKNOWN               = 0,
      STORE_EXCEPTION       = 0xf0000000,
      LOAD_EXCEPTION        = 0xf0000001,
      DOUBLE_BIT_ECC        = 0xf0001000,
      DCCM_ACCESS_ERROR     = 0xf0001001,
      NON_DCCM_ACCESS_ERROR = 0xf0001002
    };

  /// Secondary exception cause values (WD special).
  enum class SecondaryCause : uint32_t
    {
      NONE = 0,

      // Cause = INST_ACC_FAULT
      INST_BUS_ERROR = 0,
      INST_DOUBLE_ECC = 1,
      INST_LOCAL_UNMAPPED = 2,
      INST_MEM_PROTECTION = 3,

      // Cause = BREAKP
      TRIGGER_HIT = 1,

      // Cause = LOAD_ADDR_MISAL
      LOAD_MISAL_REGION_CROSS = 0,
      LOAD_MISAL_IO = 1,

      // Cause = LOAD_ACC_FAULT
      LOAD_ACC_LOCAL_UNMAPPED = 0,
      LOAD_ACC_DOUBLE_ECC = 1,
      LOAD_ACC_STACK_CHECK = 2,
      LOAD_ACC_MEM_PROTECTION = 3,
      LOAD_ACC_64BIT = 4,
      LOAD_ACC_REGION_PREDICTION = 5,
      LOAD_ACC_PIC = 6,
      LOAD_ACC_AMO = 7,

      // Cause = STORE_ADDR_MISAL
      STORE_MISAL_REGION_CROSS = 0,
      STORE_MISAL_IO = 1,

      // Cause = STORE_ACC_FAULT
      STORE_ACC_LOCAL_UNMAPPED = 0,
      STORE_ACC_DOUBLE_ECC = 1,
      STORE_ACC_STACK_CHECK = 2,
      STORE_ACC_MEM_PROTECTION = 3,
      STORE_ACC_64BIT = 4,
      STORE_ACC_REGION_PREDICTION = 5,
      STORE_ACC_PIC = 6,
      STORE_ACC_AMO = 7
    };


  /// Reason for entering debug mode (value stored in cause field
  /// of dcsr)
  enum class DebugModeCause
    {
      EBREAK = 1, TRIGGER = 2, DEBUGGER = 3, STEP = 4
    };

  /// Privilige mode.
  enum class PrivilegeMode { User = 0, Reserved = 2, Supervisor = 1,
			     Machine = 3 };

  /// Control and status register number.
  enum class CsrNumber
    {
      // Machine mode registers.

      // Machine info.
      MVENDORID = 0xF11,
      MARCHID = 0xF12,
      MIMPID = 0xF13,
      MHARTID = 0xF14,

      // Machine trap setup.
      MSTATUS = 0x300,
      MISA = 0x301,
      MEDELEG = 0x302,
      MIDELEG = 0x303,
      MIE = 0x304,
      MTVEC = 0x305,
      MCOUNTEREN = 0x306,
      MCOUNTINHIBIT = 0x320,

      // Machine trap handling
      MSCRATCH = 0x340,
      MEPC = 0x341,
      MCAUSE = 0x342,
      MTVAL = 0x343,
      MIP = 0x344,

      // Machine protection and translation.
      PMPCFG0 = 0x3a0,
      PMPCFG1 = 0x3a1,
      PMPCFG2 = 0x3a2,
      PMPCFG3 = 0x3a3,
      PMPADDR0 = 0x3b0,
      PMPADDR1 = 0x3b1,
      PMPADDR2 = 0x3b2,
      PMPADDR3 = 0x3b3,
      PMPADDR4 = 0x3b4,
      PMPADDR5 = 0x3b5,
      PMPADDR6 = 0x3b6,
      PMPADDR7 = 0x3b7,
      PMPADDR8 = 0x3b8,
      PMPADDR9 = 0x3b9,
      PMPADDR10 = 0x3ba,
      PMPADDR11 = 0x3bb,
      PMPADDR12 = 0x3bc,
      PMPADDR13 = 0x3bd,
      PMPADDR14 = 0x3be,
      PMPADDR15 = 0x3bf,

      // Machine Counter/Timers
      MCYCLE = 0xb00,
      MINSTRET = 0xb02,
      MHPMCOUNTER3 = 0xb03,
      MHPMCOUNTER4 = 0xb04,
      MHPMCOUNTER5 = 0xb05,
      MHPMCOUNTER6 = 0xb06,
      MHPMCOUNTER7 = 0xb07,
      MHPMCOUNTER8 = 0xb08,
      MHPMCOUNTER9 = 0xb09,
      MHPMCOUNTER10 = 0xb0a,
      MHPMCOUNTER11 = 0xb0b,
      MHPMCOUNTER12 = 0xb0c,
      MHPMCOUNTER13 = 0xb0d,
      MHPMCOUNTER14 = 0xb0e,
      MHPMCOUNTER15 = 0xb0f,
      MHPMCOUNTER16 = 0xb10,
      MHPMCOUNTER17 = 0xb11,
      MHPMCOUNTER18 = 0xb12,
      MHPMCOUNTER19 = 0xb13,
      MHPMCOUNTER20 = 0xb14,
      MHPMCOUNTER21 = 0xb15,
      MHPMCOUNTER22 = 0xb16,
      MHPMCOUNTER23 = 0xb17,
      MHPMCOUNTER24 = 0xb18,
      MHPMCOUNTER25 = 0xb19,
      MHPMCOUNTER26 = 0xb1a,
      MHPMCOUNTER27 = 0xb1b,
      MHPMCOUNTER28 = 0xb1c,
      MHPMCOUNTER29 = 0xb1d,
      MHPMCOUNTER30 = 0xb1e,
      MHPMCOUNTER31 = 0xb1f,

      MCYCLEH = 0xb80,
      MINSTRETH = 0xb82,
      MHPMCOUNTER3H = 0xb83,
      MHPMCOUNTER4H = 0xb84,
      MHPMCOUNTER5H = 0xb85,
      MHPMCOUNTER6H = 0xb86,
      MHPMCOUNTER7H = 0xb87,
      MHPMCOUNTER8H = 0xb88,
      MHPMCOUNTER9H = 0xb89,
      MHPMCOUNTER10H = 0xb8a,
      MHPMCOUNTER11H = 0xb8b,
      MHPMCOUNTER12H = 0xb8c,
      MHPMCOUNTER13H = 0xb8d,
      MHPMCOUNTER14H = 0xb8e,
      MHPMCOUNTER15H = 0xb8f,
      MHPMCOUNTER16H = 0xb90,
      MHPMCOUNTER17H = 0xb91,
      MHPMCOUNTER18H = 0xb92,
      MHPMCOUNTER19H = 0xb93,
      MHPMCOUNTER20H = 0xb94,
      MHPMCOUNTER21H = 0xb95,
      MHPMCOUNTER22H = 0xb96,
      MHPMCOUNTER23H = 0xb97,
      MHPMCOUNTER24H = 0xb98,
      MHPMCOUNTER25H = 0xb99,
      MHPMCOUNTER26H = 0xb9a,
      MHPMCOUNTER27H = 0xb9b,
      MHPMCOUNTER28H = 0xb9c,
      MHPMCOUNTER29H = 0xb9d,
      MHPMCOUNTER30H = 0xb9e,
      MHPMCOUNTER31H = 0xb9f,

      // Machine counter setup.
      MHPMEVENT3 = 0x323,
      MHPMEVENT4 = 0x324,
      MHPMEVENT5 = 0x325,
      MHPMEVENT6 = 0x326,
      MHPMEVENT7 = 0x327,
      MHPMEVENT8 = 0x328,
      MHPMEVENT9 = 0x329,
      MHPMEVENT10 = 0x32a,
      MHPMEVENT11 = 0x32b,
      MHPMEVENT12 = 0x32c,
      MHPMEVENT13 = 0x32d,
      MHPMEVENT14 = 0x32e,
      MHPMEVENT15 = 0x32f,
      MHPMEVENT16 = 0x330,
      MHPMEVENT17 = 0x331,
      MHPMEVENT18 = 0x332,
      MHPMEVENT19 = 0x333,
      MHPMEVENT20 = 0x334,
      MHPMEVENT21 = 0x335,
      MHPMEVENT22 = 0x336,
      MHPMEVENT23 = 0x337,
      MHPMEVENT24 = 0x338,
      MHPMEVENT25 = 0x339,
      MHPMEVENT26 = 0x33a,
      MHPMEVENT27 = 0x33b,
      MHPMEVENT28 = 0x33c,
      MHPMEVENT29 = 0x33d,
      MHPMEVENT30 = 0x33e,
      MHPMEVENT31 = 0x33f,

      // Supervisor mode registers.

      // Supervisor trap setup.
      SSTATUS = 0x100,
      SEDELEG = 0x102,
      SIDELEG = 0x103,
      SIE = 0x104,
      STVEC = 0x105,
      SCOUNTEREN = 0x106,
      // Supervisor Trap Handling 
      SSCRATCH = 0x140,
      SEPC = 0x141,
      SCAUSE = 0x142,
      STVAL = 0x143,
      SIP = 0x144,
      // Supervisor Protection and Translation 
      SATP = 0x180,

      // User mode registers.

      // User trap setup.
      USTATUS = 0x000,
      UIE = 0x004,
      UTVEC = 0x005,

      // User Trap Handling
      USCRATCH = 0x040,
      UEPC = 0x041,
      UCAUSE = 0x042,
      UTVAL = 0x043,
      UIP = 0x044,

      // User Floating-Point CSRs
      FFLAGS = 0x001,
      FRM = 0x002,
      FCSR = 0x003,

      // User Counter/Timers
      CYCLE = 0xc00,
      TIME = 0xc01,
      INSTRET = 0xc02,
      HPMCOUNTER3 = 0xc03,
      HPMCOUNTER4 = 0xc04,
      HPMCOUNTER5 = 0xc05,
      HPMCOUNTER6 = 0xc06,
      HPMCOUNTER7 = 0xc07,
      HPMCOUNTER8 = 0xc08,
      HPMCOUNTER9 = 0xc09,
      HPMCOUNTER10 = 0xc0a,
      HPMCOUNTER11 = 0xc0b,
      HPMCOUNTER12 = 0xc0c,
      HPMCOUNTER13 = 0xc0d,
      HPMCOUNTER14 = 0xc0e,
      HPMCOUNTER15 = 0xc0f,
      HPMCOUNTER16 = 0xc10,
      HPMCOUNTER17 = 0xc11,
      HPMCOUNTER18 = 0xc12,
      HPMCOUNTER19 = 0xc13,
      HPMCOUNTER20 = 0xc14,
      HPMCOUNTER21 = 0xc15,
      HPMCOUNTER22 = 0xc16,
      HPMCOUNTER23 = 0xc17,
      HPMCOUNTER24 = 0xc18,
      HPMCOUNTER25 = 0xc19,
      HPMCOUNTER26 = 0xc1a,
      HPMCOUNTER27 = 0xc1b,
      HPMCOUNTER28 = 0xc1c,
      HPMCOUNTER29 = 0xc1d,
      HPMCOUNTER30 = 0xc1e,
      HPMCOUNTER31 = 0xc1f,

      CYCLEH = 0xc80,
      TIMEH = 0xc81,
      INSTRETH = 0xc82,
      HPMCOUNTER3H = 0xc83,
      HPMCOUNTER4H = 0xc84,
      HPMCOUNTER5H = 0xc85,
      HPMCOUNTER6H = 0xc86,
      HPMCOUNTER7H = 0xc87,
      HPMCOUNTER8H = 0xc88,
      HPMCOUNTER9H = 0xc89,
      HPMCOUNTER10H = 0xc8a,
      HPMCOUNTER11H = 0xc8b,
      HPMCOUNTER12H = 0xc8c,
      HPMCOUNTER13H = 0xc8d,
      HPMCOUNTER14H = 0xc8e,
      HPMCOUNTER15H = 0xc8f,
      HPMCOUNTER16H = 0xc90,
      HPMCOUNTER17H = 0xc91,
      HPMCOUNTER18H = 0xc92,
      HPMCOUNTER19H = 0xc93,
      HPMCOUNTER20H = 0xc94,
      HPMCOUNTER21H = 0xc95,
      HPMCOUNTER22H = 0xc96,
      HPMCOUNTER23H = 0xc97,
      HPMCOUNTER24H = 0xc98,
      HPMCOUNTER25H = 0xc99,
      HPMCOUNTER26H = 0xc9a,
      HPMCOUNTER27H = 0xc9b,
      HPMCOUNTER28H = 0xc9c,
      HPMCOUNTER29H = 0xc9d,
      HPMCOUNTER30H = 0xc9e,
      HPMCOUNTER31H = 0xc9f,

      // Debug/Trace registers.
      TSELECT = 0x7a0,
      TDATA1  = 0x7a1,
      TDATA2  = 0x7a2,
      TDATA3  = 0x7a3,

      // Debug mode registers.
      DCSR     = 0x7b0,
      DPC      = 0x7b1,
      DSCRATCH = 0x7b2,

      // Non-standard registers.
      MRAC     = 0x7c0,
      MDSEAC   = 0xfc0,
      MDEAU    = 0xbc0,
      MGPMC    = 0x7d0, // group performance monitor control

      MEIVT    = 0xbc8, // Ext int vector table reg 
      MEIHAP   = 0xfc8, // Ext int handler address pointer reg

      MSPCBA   = 0x7f4, // Stack pointer checker base address
      MSPCTA   = 0x7f5, // Stack pointer checker top address
      MSPCC    = 0x7f6, // Stack pointer checker control

      MDBAC   = 0xbc1,  // D-Bus 64-bit access control
      MDBHD   = 0xbc7,  // D-Bus 64-bit high data

      MSCAUSE  = 0x7ff, // Secondary exception cause

      MAX_CSR_ = 0xfff,
      MIN_CSR_ = 0      // csr with smallest number
    };


  template <typename URV>
  class CsRegs;

  template <typename URV>
  class Core;

  /// Model a control and status register. The template type URV
  /// (unsigned register value) is the type of the register value. It
  /// should be uint32_t for 32-bit implementations and uint64_t for
  /// 64-bit.
  template <typename URV>
  class Csr
  {
  public:

    /// Default constructor.
    Csr()
    { valuePtr_ = &value_; }

    /// Constructor. The mask indicates which bits are writable: A zero bit
    /// in the mask corresponds to a non-writable (preserved) bit in the
    /// register value. To make the whole register writable, set mask to
    /// all ones.
    Csr(const std::string& name, CsrNumber number, bool mandatory,
	bool implemented, URV value, URV writeMask = ~URV(0))
      : name_(name), number_(unsigned(number)), mandatory_(mandatory),
	implemented_(implemented), initialValue_(value), value_(value),
	writeMask_(writeMask), pokeMask_(writeMask)
    { valuePtr_ = &value_; }

    /// Copy constructor.
    Csr(const Csr<URV>& other)
      : name_(other.name_), number_(other.number_),
	mandatory_(other.mandatory_),
	implemented_(other.implemented_), debug_(other.debug_),
	initialValue_(other.initialValue_), value_(other.value_),
	valuePtr_(nullptr),
	writeMask_(other.writeMask_), pokeMask_(other.pokeMask_)
    {
      valuePtr_ = &value_;
      *valuePtr_ = other.valuePtr_? *other.valuePtr_ : other.value_;
    }

    /// Return lowest privilege mode that can access the register.
    /// Bits 9 and 8 of the register number encode the privilege mode.
    PrivilegeMode privilegeMode() const
    { return PrivilegeMode((number_ & 0x300) >> 8); }

    /// Return true if register is read-only. Bits ten and eleven of
    /// the register number denote read-only when both one and read-write
    /// otherwise.
    bool isReadOnly() const
    { return (number_ & 0xc00) == 0xc00; }

    /// Return true if register is implemented.
    bool isImplemented() const
    { return implemented_; }

    /// Return true if register is mandatory (not optional).
    bool isMandatory() const
    { return mandatory_; }

    /// Return true if this register has been marked as a debug-mode
    /// register.
    bool isDebug() const
    { return debug_; }

    /// Return true if this register is shared between harts.
    bool isShared() const
    { return shared_; }

    /// Return the current value of this register.
    URV read() const
    { return *valuePtr_; }

    /// Return the write-mask associated with this register. A
    /// register value bit is writable by the write method if and only
    /// if the corresponding bit in the mask is 1; otherwise, the bit
    /// is preserved.
    URV getWriteMask() const
    { return writeMask_; }

    /// Return the mask associated with this register. A register
    /// value bit is modifiable if and only if the corresponding bit
    /// in the mask is 1; otherwise, the bit is preserved. The write
    /// mask is used by the CSR write instructions. The poke mask
    /// allows the caller to change bits that are read only for CSR
    /// instructions but are modifiable by the hardware.
    URV getPokeMask() const
    { return pokeMask_; }

    /// Return the reset value of this CSR.
    URV getResetValue() const
    { return initialValue_; }

    /// Return the number of this register.
    CsrNumber getNumber() const
    { return CsrNumber(number_); }

    /// Return the name of this register.
    const std::string& getName() const
    { return name_; }

    /// Register a pre-poke call back which will get invoked with CSR and
    /// poked value.
    void registerPrePoke(std::function<void(Csr<URV>&, URV&)> func)
    { prePoke_.push_back(func); }

    /// Register a pre-write call back which will get invoked with
    /// CSR and written value.
    void registerPreWrite(std::function<void(Csr<URV>&, URV&)> func)
    { preWrite_.push_back(func); }

    /// Register a post-poke call back which will get invoked with CSR and
    /// poked value.
    void registerPostPoke(std::function<void(Csr<URV>&, URV)> func)
    { postPoke_.push_back(func); }

    /// Register a post-write call back which will get invoked with
    /// CSR and written value.
    void registerPostWrite(std::function<void(Csr<URV>&, URV)> func)
    { postWrite_.push_back(func); }

    /// Register a post-reset call back.
    void registerPostReset(std::function<void(Csr<URV>&)> func)
    { postReset_.push_back(func); }

  protected:

    friend class CsRegs<URV>;
    friend class Hart<URV>;

    void operator=(const Csr<URV>& other) = delete;

    /// Associate given location with the value of this CSR. The
    /// previous value of the CSR is lost. If given location is null
    /// then the default location defined in this object is restored.
    void tie(URV* location)
    { valuePtr_ = location ? location : &value_; }

    /// Reset to initial (power-on) value.
    void reset()
    {
      *valuePtr_ = initialValue_;
      for (auto func : postReset_)
        func(*this);
    }

    /// Configure.
    void config(const std::string& name, CsrNumber num, bool mandatory,
		bool implemented, URV value, URV writeMask, URV pokeMask,
		bool isDebug)
    { name_ = name; number_ = unsigned(num); mandatory_ = mandatory;
      implemented_ = implemented; initialValue_ = value;
      writeMask_ = writeMask; pokeMask_ = pokeMask;
      debug_ = isDebug; *valuePtr_ = value; }

    /// Define the mask used by the poke method to write this
    /// register. The mask defined the register bits that are
    /// modifiable (even though such bits may not be writable using a
    /// CSR instruction). For example, the meip bit (of the mip CSR)
    /// is not writable using a CSR instruction but is modifiable.
    void setPokeMask(URV mask)
    { pokeMask_ = mask; }

    /// Mark register as a debug-mode register. Assessing a debug-mode
    /// register when the processor is not in debug mode will trigger an
    /// illegal instruction exception.
    void setIsDebug(bool flag)
    { debug_ = flag; }

    /// Mark regiser as shared between harts.
    void setIsShared(bool flag)
    { shared_ = flag; }

    void setImplemented(bool flag)
    { implemented_ = flag; }

    void setInitialValue(URV v)
    { initialValue_ = v; }

    void setDefined(bool flag)
    { defined_ = flag; }

    bool isDefined() const
    { return defined_; }

    void pokeNoMask(URV v)
    { *valuePtr_ = v; }

    void setWriteMask(URV mask)
    { writeMask_ = mask; }

    /// Set the value of this register to the given value x honoring
    /// the write mask (defined at construction): Set the ith bit of
    /// this register to the ith bit of the given value x if the ith
    /// bit of the write mask is 1; otherwise, leave the ith bit
    /// unmodified. This is the interface used by the CSR
    /// instructions.
    void write(URV x)
    {
      if (not hasPrev_)
	{
	  prev_ = *valuePtr_;
	  hasPrev_ = true;
	}
      for (auto func : preWrite_)
        func(*this, x);

      URV newVal = (x & writeMask_) | (*valuePtr_ & ~writeMask_);
      *valuePtr_ = newVal;

      for (auto func : postWrite_)
        func(*this, newVal);
    }

    /// Similar to the write method but using the poke mask instead of
    /// the write mask. This is the interface used by non-csr
    /// instructions to change modifiable (but not writable through
    /// CSR instructions) bits of this register.
    void poke(URV x)
    {
      for (auto func : prePoke_)
        func(*this, x);

      URV newVal = (x & pokeMask_) | (*valuePtr_ & ~pokeMask_);
      *valuePtr_ = newVal;

      for (auto func : postPoke_)
        func(*this, newVal);
    }

    /// Return the value of this register before last sequence of
    /// writes. Return current value if no writes since
    /// clearLastWritten.
    URV prevValue() const
    { return hasPrev_? prev_ : read(); }

    /// Clear previous value recorded by first write since
    /// clearLastWritten.
    void clearLastWritten()
    { hasPrev_ = false; }

  private:

    std::string name_;
    unsigned number_ = 0;
    bool mandatory_ = false;   // True if mandated by architecture.
    bool implemented_ = false; // True if register is implemented.
    bool defined_ = false;
    bool debug_ = false;       // True if this is a debug-mode register.
    bool shared_ = false;      // True if this is shared between harts.
    URV initialValue_ = 0;
    URV value_ = 0;
    URV prev_ = 0;
    bool hasPrev_ = false;

    // This will point to value_ except when shadowing the value of
    // some other register.
    URV* valuePtr_ = nullptr;

    URV writeMask_ = ~URV(0);
    URV pokeMask_ = ~URV(0);

    std::vector<std::function<void(Csr<URV>&, URV)>> postPoke_;
    std::vector<std::function<void(Csr<URV>&, URV)>> postWrite_;

    std::vector<std::function<void(Csr<URV>&, URV&)>> prePoke_;
    std::vector<std::function<void(Csr<URV>&, URV&)>> preWrite_;

    std::vector<std::function<void(Csr<URV>&)>> postReset_;
  };


  /// Model the control and status register set.
  template <typename URV>
  class CsRegs
  {
  public:

    friend class Hart<uint32_t>;
    friend class Hart<uint64_t>;

    CsRegs();
    
    ~CsRegs();

    /// Return pointer to the control-and-status register
    /// corresponding to the given name or nullptr if no such
    /// register.
    Csr<URV>* findCsr(const std::string& name);

    /// Return pointer to the control-and-status register
    /// corresponding to the given number or nullptr if no such
    /// register.
    Csr<URV>* findCsr(CsrNumber number);

    /// Set value to the value of the scr having the given number
    /// returning true on success.  Return false leaving value
    /// unmodified if there is no csr with the given number or if the
    /// csr is not implemented or if the the given mode has no access
    /// to the register.
    bool read(CsrNumber number, PrivilegeMode mode, bool debugMode,
	      URV& value) const;

    /// Set the the csr having the given number to the given value
    /// returning true on success. Return false writing nothing if
    /// there is no csr with the given number or if the csr is not
    /// implemented or if the given mode has no access to the
    /// register.
    bool write(CsrNumber number, PrivilegeMode mode, bool debugMode,
	       URV value);

    /// Return true if given register is writable in the given mode.
    bool isWriteable(CsrNumber number, PrivilegeMode mode, bool
		     debugMode) const;

    /// Return the number of bits in a register in this register file.
    static constexpr uint32_t regWidth()
    { return sizeof(URV)*8; }

  protected:

    /// Define csr with given name and number. Return pointer to csr
    /// on success or nullptr if given name is already in use or if the
    /// csr number is out of bounds or if it is associated with an
    /// already defined CSR.
    Csr<URV>* defineCsr(const std::string& name, CsrNumber number,
			bool mandatory, bool implemented, URV value,
			URV writeMask, URV pokeMask, bool isDebug = false,
			bool quiet = false);

    /// Return pointer to CSR with given number. Return nullptr if
    /// number is out of bounds or if corresponding CSR is not
    /// implemented.
    Csr<URV>* getImplementedCsr(CsrNumber num)
    {
      size_t ix = size_t(num);
      if (ix >= regs_.size()) return nullptr;
      Csr<URV>* csr = &regs_.at(ix);
      return csr->isImplemented() ? csr : nullptr;
    }

    /// Return pointer to CSR with given number. Return nullptr if
    /// number is out of bounds or if corresponding CSR is not
    /// implemented.
    const Csr<URV>* getImplementedCsr(CsrNumber num) const
    {
      size_t ix = size_t(num);
      if (ix >= regs_.size()) return nullptr;
      const Csr<URV>* csr = &regs_.at(ix);
      return csr->isImplemented() ? csr : nullptr;
    }

    /// Restrict chaining only to pairs of consecutive (even-numbered followed
    /// by odd) triggers.
    void configEvenOddTriggerChaining(bool flag)
    { triggers_.setEvenOddChaining(flag); }

    /// Return true if one more debug triggers are enabled.
    bool hasActiveTrigger() const
    { return hasActiveTrigger_; }

    /// Return true if one more instruction (execution) debug triggers
    /// are enabled.
    bool hasActiveInstTrigger() const
    { return hasActiveInstTrigger_; }

    /// Get the values of the three components of the given debug
    /// trigger. Return true on success and false if trigger is out of
    /// bounds.
    bool peekTrigger(URV trigger, URV& data1, URV& data2, URV& data3) const
    { return triggers_.peek(trigger, data1, data2, data3); }

    /// Get the values of the three components of the given debug
    /// trigger as well as the components write and poke masks. Return
    /// true on success and false if trigger is out of bounds.
    bool peekTrigger(URV trigger, URV& data1, URV& data2, URV& data3,
		     URV& wm1, URV& wm2, URV& wm3,
		     URV& pm1, URV& pm2, URV& pm3) const
    { return triggers_.peek(trigger, data1, data2, data3, wm1, wm2, wm3,
			    pm1, pm2, pm3); }

    /// Set the values of the three components of the given debug
    /// trigger. Return true on success and false if trigger is out of
    /// bounds.
    bool pokeTrigger(URV trigger, URV data1, URV data2, URV data3)
    { return triggers_.poke(trigger, data1, data2, data3); }

    /// Return true if any of the load (store if isLoad is true)
    /// triggers trips. A load/store trigger trips if it matches the
    /// given address and timing and if all the remaining triggers in
    /// its chain have tripped. Set the local-hit bit of any
    /// load/store trigger that matches. If a matching load/store
    /// trigger causes its chain to trip, then set the hit bit of all
    /// the triggers in that chain.
    bool ldStAddrTriggerHit(URV addr, TriggerTiming t, bool isLoad, bool ie)
    {
      bool hit = triggers_.ldStAddrTriggerHit(addr, t, isLoad, ie);
      if (hit)
	recordWrite(CsrNumber::TDATA1);  // Hit bit in TDATA1 changed.
      return hit;
    }

    /// Similar to ldStAddrTriggerHit but for data match.
    bool ldStDataTriggerHit(URV addr, TriggerTiming t, bool isLoad, bool ie)
    {
      bool hit = triggers_.ldStDataTriggerHit(addr, t, isLoad, ie);
      if (hit)
	recordWrite(CsrNumber::TDATA1);  // Hit bit in TDATA1 changed.
      return hit;
    }

    /// Similar to ldStAddrTriggerHit but for instruction address.
    bool instAddrTriggerHit(URV addr, TriggerTiming t, bool ie)
    {
      bool hit = triggers_.instAddrTriggerHit(addr, t, ie);
      if (hit)
	recordWrite(CsrNumber::TDATA1);  // Hit bit in TDATA1 changed.
      return hit;
    }

    /// Similar to instAddrTriggerHit but for instruction opcode.
    bool instOpcodeTriggerHit(URV opcode, TriggerTiming t, bool ie)
    {
      bool hit = triggers_.instOpcodeTriggerHit(opcode, t, ie);
      if (hit)
	recordWrite(CsrNumber::TDATA1);  // Hit bit in TDATA1 changed.
      return hit;
    }

    /// Make every active icount trigger count down unless it was
    /// written by the current instruction. Set the hit bit of a
    /// counted-down register if its value becomes zero. Return true
    /// if any counted-down register reaches zero; otherwise, return
    /// false.
    bool icountTriggerHit(bool ie)
    {
      bool hit = triggers_.icountTriggerHit(ie);
      if (hit)
	recordWrite(CsrNumber::TDATA1);  // Hit bit in TDTA1 changed.
      return hit;
    }

    /// Set pre and post to the count of "before"/"after" triggers
    /// that tripped by the last executed instruction.
    void countTrippedTriggers(unsigned& pre, unsigned& post) const
    { triggers_.countTrippedTriggers(pre, post); }

    /// Associate given event number with given counter.
    /// Subsequent calls to updatePerofrmanceCounters(en) will cause
    /// given counter to count up by 1. Return true on success. Return
    /// false if counter number is out of bounds.
    bool assignEventToCounter(URV event, unsigned counter)
    {
      return mPerfRegs_.assignEventToCounter(EventNumber(event), counter);
    }

    /// Return true if there is one or more tripped trigger action set
    /// to "enter debug mode".
    bool hasEnterDebugModeTripped() const
    { return triggers_.hasEnterDebugModeTripped(); }

    /// Set value to the value of the given register returning true on
    /// success and false if number is out of bound.
    bool peek(CsrNumber number, URV& value) const;

    /// Set register to the given value masked by the poke mask. A
    /// read-only register can be changed this way as long as its poke
    /// mask is non-zero. Return true on success and false if number is
    /// out of bounds.
    bool poke(CsrNumber number, URV value);

    /// Reset all CSRs to their initial (power-on) values.
    void reset();

    /// Configure CSR. Return true on success and false on failure.
    bool configCsr(const std::string& name, bool implemented, URV resetValue,
                   URV mask, URV pokeMask, bool debug, bool shared);

    /// Configure CSR. Return true on success and false on failure.
    bool configCsr(CsrNumber csr, bool implemented, URV resetValue,
                   URV mask, URV pokeMask, bool debug, bool shared);

    /// Configure machine mode performance counters returning true on
    /// success and false on failure. N consecutive counters starting
    /// at MHPMCOUNTER3/MHPMCOUNTER3H are made read/write. The
    /// remaining counters are made read only. For each counter that
    /// is made read-write the corresponding MHPMEVENT is made
    /// read-write.
    bool configMachineModePerfCounters(unsigned numCounters);

    /// Helper to write method. Update frm/fflags after fscr is written.
    /// Update fcsr after frm/fflags is written.
    void updateFcsrGroupForWrite(CsrNumber number, URV value);

    /// Helper to poke method. Update frm/fflags after fscr is poked.
    /// Update fcsr after frm/fflags is poked.
    void updateFcsrGroupForPoke(CsrNumber number, URV value);

    /// Helper to construtor. Define machine-mode CSRs
    void defineMachineRegs();

    /// Helper to construtor. Define supervisor-mode CSRs
    void defineSupervisorRegs();

    /// Helper to construtor. Define user-mode CSRs
    void defineUserRegs();

    /// Helper to construtor. Define debug-mode CSRs
    void defineDebugRegs();

    /// Helper to construtor. Define non-standard CSRs
    void defineNonStandardRegs();

    /// Set the store error address capture register. Return true on
    /// success and false if register is not implemented.
    bool setStoreErrorAddrCapture(URV value);

    bool readTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
		   URV& value) const;
    
    bool writeTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
		    URV value);

    bool pokeTdata(CsrNumber number, URV value);

  protected:

    /// Record given CSR number as a being written by the current
    /// instruction. Recorded numbers can be later retrieved by the
    /// getLastWrittenRegs method.
    void recordWrite(CsrNumber num);

    /// Clear the remembered indices of the CSR register(s) written by
    /// the last instruction.
    void clearLastWrittenRegs()
    {
      for (auto& csrNum : lastWrittenRegs_)
	regs_.at(size_t(csrNum)).clearLastWritten();
      lastWrittenRegs_.clear();
      triggers_.clearLastWrittenTriggers();
    }

    /// Configure given trigger with given reset values, write and
    /// poke masks. Return true on success and false on failure.
    bool configTrigger(unsigned trigger, URV val1, URV val2, URV val3,
		       URV wm1, URV wm2, URV wm3,
		       URV pm1, URV pm2, URV pm3)
    {
      return triggers_.config(trigger, val1, val2, val3,
			      wm1, wm2, wm3, pm1, pm2, pm3);
    }

    /// Fill the nums vector with the numbers of the CSRs written by
    /// the last instruction.
    void getLastWrittenRegs(std::vector<CsrNumber>& csrNums,
			    std::vector<unsigned>& triggerNums) const
    {
      csrNums = lastWrittenRegs_;
      triggers_.getLastWrittenTriggers(triggerNums);
    }

    bool isInterruptEnabled() const
    { return interruptEnable_; }

    /// Tie the shared CSRs in this file to the corresponding CSRs in
    /// the target CSR file making them share the same location for
    /// their value.
    void tieSharedCsrsTo(CsRegs<URV>& target);

    /// Tie CSR values of machine mode performance counters to the
    /// elements of the given vector so that when a counter in the
    /// vector is changed the corresponding CSR value changes and
    /// vice-versa. This is done to avoid the overhead of CSR checking
    /// when incrementing performance counters.
    void tieMachinePerfCounters(std::vector<uint64_t>& counters);

    /// Set the maximum performance counter event id. Ids larger than
    /// the max value are replaced by that max.
    void setMaxEventId(URV maxId)
    { maxEventId_ = maxId; }

    /// Lock/unlock mdseac. This supports imprecise load/store exceptions.
    void lockMdseac(bool flag)
    { mdseacLocked_ = flag; }

    /// Return true if MDSEAC register is locked (it is unlocked on reset
    /// and after a write to MDEAU).
    bool mdseacLocked() const
    { return mdseacLocked_; }

  private:

    std::vector< Csr<URV> > regs_;
    std::unordered_map<std::string, CsrNumber> nameToNumber_;

    Triggers<URV> triggers_;

    // Register written since most recent clearLastWrittenRegs
    std::vector<CsrNumber> lastWrittenRegs_;

    // Counters implementing machine performance counters.
    PerfRegs mPerfRegs_;

    bool interruptEnable_ = false;  // Cached MSTATUS MIE bit.

    // These can be obtained from Triggers. Speed up access by caching
    // them in here.
    bool hasActiveTrigger_ = false;
    bool hasActiveInstTrigger_ = false;

    bool mdseacLocked_ = false; // Once written, MDSEAC persists until
                                // MDEAU is written.
    URV maxEventId_ = ~URV(0);
  };


  /// Structure used to unpack/pack the fields of the machine status
  /// register.
  template <typename URV>
  union MstatusFields;

  /// 32-bit version.
  template <>
  union MstatusFields<uint32_t>
  {
    MstatusFields(uint32_t value = 0)
      : value_(value)
    { }

    uint32_t value_;   // Machine status register value.
    struct
    {
      unsigned UIE      : 1;
      unsigned SIE      : 1;
      unsigned res3     : 1;
      unsigned MIE      : 1;
      unsigned UPIE     : 1;
      unsigned SPIE     : 1;
      unsigned res2     : 1;
      unsigned MPIE     : 1;
      unsigned SPP      : 1;
      unsigned res1     : 2;
      unsigned MPP      : 2;
      unsigned FS       : 2;
      unsigned XS       : 2;
      unsigned MPRV     : 1;
      unsigned SUM      : 1;
      unsigned MXR      : 1;
      unsigned TVM      : 1;
      unsigned TW       : 1;
      unsigned TSR      : 1;
      unsigned res0     : 8;  // Reserved
      unsigned SD       : 1;
    } bits_;
  };

  /// 64-bit version.
  template <>
  union MstatusFields<uint64_t>
  {
    MstatusFields(uint64_t value = 0)
      : value_(value)
    { }

    uint64_t value_;   // Machine status register value.
    struct
    {
      unsigned UIE      : 1;
      unsigned SIE      : 1;
      unsigned res3     : 1;
      unsigned MIE      : 1;
      unsigned UPIE     : 1;
      unsigned SPIE     : 1;
      unsigned res2     : 1;
      unsigned MPIE     : 1;
      unsigned SPP      : 1;
      unsigned res1     : 2;
      unsigned MPP      : 2;
      unsigned FS       : 2;
      unsigned XS       : 2;
      unsigned MPRV     : 1;
      unsigned SUM      : 1;
      unsigned MXR      : 1;
      unsigned TVM      : 1;
      unsigned TW       : 1;
      unsigned TSR      : 1;
      unsigned res0     : 9;
      unsigned UXL      : 2;
      unsigned SXL      : 2;
      unsigned res      : 27;  // Reserved
      unsigned SD       : 1;
    } bits_;
  };
}
