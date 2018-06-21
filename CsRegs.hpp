// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include "Triggers.hpp"

namespace WdRiscv
{

  enum InterruptCause
    {
      U_SOFTWARE  = 0,  // User mode software interrupt
      S_SOFTWARE  = 1,  // Supervisor mode software interrupt
      M_SOFTWARE  = 3,  // Machine mode software interrupt
      U_TIMER     = 4,  // User mode timer interrupt
      S_TIMER     = 5,  // Supervisor
      M_TIMER     = 7,  // Machine
      U_EXTERNAL  = 8,  // User mode external interrupt
      S_EXTERNAL  = 9,  // Supervisor
      M_EXTERNAL  = 11, // Machine
      M_STORE_BUS = 31  // Store-bus error (WD extension).
    };

  enum ExceptionCause 
    {
      INST_ADDR_MISALIGNED  = 0,
      INST_ACCESS_FAULT     = 1,
      ILLEGAL_INST          = 2,
      BREAKPOINT            = 3,
      LOAD_ADDR_MISALIGNED  = 4,
      LOAD_ACCESS_FAULT     = 5,
      STORE_ADDR_MISALIGNED = 6,
      STORE_ACCESS_FAULT    = 7,
      U_ENV_CALL            = 8,  // Environment call from user mode
      S_ENV_CALL            = 9,  // Supervisor
      M_ENV_CALL            = 11, // Machine
      INST_PAGE_FAULT       = 12,
      LOAD_PAGE_FAULT       = 13,
      STORE_PAGE_FAULT      = 15
    };

  /// Privilige mode.
  enum PrivilegeMode { USER_MODE = 0, RESERVED_MODE = 2, SUPERVISOR_MODE = 1,
		       MACHINE_MODE = 3 };

  /// Costrol and status register number.
  enum CsrNumber
    {
      // Machine mode registers.

      // Machine info.
      MVENDORID_CSR = 0xF11,
      MARCHID_CSR = 0xF12,
      MIMPID_CSR = 0xF13,
      MHARTID_CSR = 0xF14,

      // Machine trap setup.
      MSTATUS_CSR = 0x300,
      MISA_CSR = 0x301,
      MEDELEG_CSR = 0x302,
      MIDELEG_CSR = 0x303,
      MIE_CSR = 0x304,
      MTVEC_CSR = 0x305,
      MCOUNTEREN_CSR = 0x306,

      // Machine trap handling
      MSCRATCH_CSR = 0x340,
      MEPC_CSR = 0x341,
      MCAUSE_CSR = 0x342,
      MTVAL_CSR = 0x343,
      MIP_CSR = 0x344,

      // Machine protection and translation.
      PMPCFG0_CSR = 0x3a0,
      PMPCFG1_CSR = 0x3a1,
      PMPCFG2_CSR = 0x3a2,
      PMPCFG3_CSR = 0x3a3,
      PMPADDR0_CSR = 0x3b0,
      PMPADDR1_CSR = 0x3b1,
      PMPADDR2_CSR = 0x3b2,
      PMPADDR3_CSR = 0x3b3,
      PMPADDR4_CSR = 0x3b4,
      PMPADDR5_CSR = 0x3b5,
      PMPADDR6_CSR = 0x3b6,
      PMPADDR7_CSR = 0x3b7,
      PMPADDR8_CSR = 0x3b8,
      PMPADDR9_CSR = 0x3b9,
      PMPADDR10_CSR = 0x3ba,
      PMPADDR11_CSR = 0x3bb,
      PMPADDR12_CSR = 0x3bc,
      PMPADDR13_CSR = 0x3bd,
      PMPADDR14_CSR = 0x3be,
      PMPADDR15_CSR = 0x3bf,

      // Machine Counter/Timers
      MCYCLE_CSR = 0xb00,
      MINSTRET_CSR = 0xb02,
      MHPMCOUNTER3_CSR = 0xb03,
      MHPMCOUNTER4_CSR = 0xb04,
      MHPMCOUNTER5_CSR = 0xb05,
      MHPMCOUNTER6_CSR = 0xb06,
      MHPMCOUNTER7_CSR = 0xb07,
      MHPMCOUNTER8_CSR = 0xb08,
      MHPMCOUNTER9_CSR = 0xb09,
      MHPMCOUNTER10_CSR = 0xb0a,
      MHPMCOUNTER11_CSR = 0xb0b,
      MHPMCOUNTER12_CSR = 0xb0c,
      MHPMCOUNTER13_CSR = 0xb0d,
      MHPMCOUNTER14_CSR = 0xb0e,
      MHPMCOUNTER15_CSR = 0xb0f,
      MHPMCOUNTER16_CSR = 0xb10,
      MHPMCOUNTER17_CSR = 0xb11,
      MHPMCOUNTER18_CSR = 0xb12,
      MHPMCOUNTER19_CSR = 0xb13,
      MHPMCOUNTER20_CSR = 0xb14,
      MHPMCOUNTER21_CSR = 0xb15,
      MHPMCOUNTER22_CSR = 0xb16,
      MHPMCOUNTER23_CSR = 0xb17,
      MHPMCOUNTER24_CSR = 0xb18,
      MHPMCOUNTER25_CSR = 0xb19,
      MHPMCOUNTER26_CSR = 0xb1a,
      MHPMCOUNTER27_CSR = 0xb1b,
      MHPMCOUNTER28_CSR = 0xb1c,
      MHPMCOUNTER29_CSR = 0xb1d,
      MHPMCOUNTER30_CSR = 0xb1e,
      MHPMCOUNTER31_CSR = 0xb1f,

      MCYCLEH_CSR = 0xb80,
      MINSTRETH_CSR = 0xb82,
      MHPMCOUNTER3H_CSR = 0xb83,
      MHPMCOUNTER4H_CSR = 0xb84,
      MHPMCOUNTER5H_CSR = 0xb85,
      MHPMCOUNTER6H_CSR = 0xb86,
      MHPMCOUNTER7H_CSR = 0xb87,
      MHPMCOUNTER8H_CSR = 0xb88,
      MHPMCOUNTER9H_CSR = 0xb89,
      MHPMCOUNTER10H_CSR = 0xb8a,
      MHPMCOUNTER11H_CSR = 0xb8b,
      MHPMCOUNTER12H_CSR = 0xb8c,
      MHPMCOUNTER13H_CSR = 0xb8d,
      MHPMCOUNTER14H_CSR = 0xb8e,
      MHPMCOUNTER15H_CSR = 0xb8f,
      MHPMCOUNTER16H_CSR = 0xb90,
      MHPMCOUNTER17H_CSR = 0xb91,
      MHPMCOUNTER18H_CSR = 0xb92,
      MHPMCOUNTER19H_CSR = 0xb93,
      MHPMCOUNTER20H_CSR = 0xb94,
      MHPMCOUNTER21H_CSR = 0xb95,
      MHPMCOUNTER22H_CSR = 0xb96,
      MHPMCOUNTER23H_CSR = 0xb97,
      MHPMCOUNTER24H_CSR = 0xb98,
      MHPMCOUNTER25H_CSR = 0xb99,
      MHPMCOUNTER26H_CSR = 0xb9a,
      MHPMCOUNTER27H_CSR = 0xb9b,
      MHPMCOUNTER28H_CSR = 0xb9c,
      MHPMCOUNTER29H_CSR = 0xb9d,
      MHPMCOUNTER30H_CSR = 0xb9e,
      MHPMCOUNTER31H_CSR = 0xb9f,

      // Machine counter setup.
      MHPMEVENT3_CSR = 0x323,
      MHPMEVENT4_CSR = 0x324,
      MHPMEVENT5_CSR = 0x325,
      MHPMEVENT6_CSR = 0x326,
      MHPMEVENT7_CSR = 0x327,
      MHPMEVENT8_CSR = 0x328,
      MHPMEVENT9_CSR = 0x329,
      MHPMEVENT10_CSR = 0x32a,
      MHPMEVENT11_CSR = 0x32b,
      MHPMEVENT12_CSR = 0x32c,
      MHPMEVENT13_CSR = 0x32d,
      MHPMEVENT14_CSR = 0x32e,
      MHPMEVENT15_CSR = 0x32f,
      MHPMEVENT16_CSR = 0x330,
      MHPMEVENT17_CSR = 0x331,
      MHPMEVENT18_CSR = 0x332,
      MHPMEVENT19_CSR = 0x333,
      MHPMEVENT20_CSR = 0x334,
      MHPMEVENT21_CSR = 0x335,
      MHPMEVENT22_CSR = 0x336,
      MHPMEVENT23_CSR = 0x337,
      MHPMEVENT24_CSR = 0x338,
      MHPMEVENT25_CSR = 0x339,
      MHPMEVENT26_CSR = 0x33a,
      MHPMEVENT27_CSR = 0x33b,
      MHPMEVENT28_CSR = 0x33c,
      MHPMEVENT29_CSR = 0x33d,
      MHPMEVENT30_CSR = 0x33e,
      MHPMEVENT31_CSR = 0x33f,

      // Supervisor mode registers.

      // Supervisor trap setup.
      SSTATUS_CSR = 0x100,
      SEDELEG_CSR = 0x102,
      SIDELEG_CSR = 0x103,
      SIE_CSR = 0x104,
      STVEC_CSR = 0x105,
      SCOUNTEREN_CSR = 0x106,
      // Supervisor Trap Handling 
      SSCRATCH_CSR = 0x140,
      SEPC_CSR = 0x141,
      SCAUSE_CSR = 0x142,
      STVAL_CSR = 0x143,
      SIP_CSR = 0x144,
      // Supervisor Protection and Translation 
      SATP_CSR = 0x180,

      // User mode registers.

      // User trap setup.
      USTATUS_CSR = 0x000,
      UIE_CSR = 0x004,
      UTVEC_CSR = 0x005,

      // User Trap Handling
      USCRATCH_CSR = 0x040,
      UEPC_CSR = 0x041,
      UCAUSE_CSR = 0x042,
      UTVAL_CSR = 0x043,
      UIP_CSR = 0x044,

      // User Floating-Point CSRs
      FFLAGS_CSR = 0x001,
      FRM_CSR = 0x002,
      FCSR_CSR = 0x003,

      // User Counter/Timers
      CYCLE_CSR = 0xc00,
      TIME_CSR = 0xc01,
      INSTRET_CSR = 0xc02,
      HPMCOUNTER3_CSR = 0xc03,
      HPMCOUNTER4_CSR = 0xc04,
      HPMCOUNTER5_CSR = 0xc05,
      HPMCOUNTER6_CSR = 0xc06,
      HPMCOUNTER7_CSR = 0xc07,
      HPMCOUNTER8_CSR = 0xc08,
      HPMCOUNTER9_CSR = 0xc09,
      HPMCOUNTER10_CSR = 0xc0a,
      HPMCOUNTER11_CSR = 0xc0b,
      HPMCOUNTER12_CSR = 0xc0c,
      HPMCOUNTER13_CSR = 0xc0d,
      HPMCOUNTER14_CSR = 0xc0e,
      HPMCOUNTER15_CSR = 0xc0f,
      HPMCOUNTER16_CSR = 0xc10,
      HPMCOUNTER17_CSR = 0xc11,
      HPMCOUNTER18_CSR = 0xc12,
      HPMCOUNTER19_CSR = 0xc13,
      HPMCOUNTER20_CSR = 0xc14,
      HPMCOUNTER21_CSR = 0xc15,
      HPMCOUNTER22_CSR = 0xc16,
      HPMCOUNTER23_CSR = 0xc17,
      HPMCOUNTER24_CSR = 0xc18,
      HPMCOUNTER25_CSR = 0xc19,
      HPMCOUNTER26_CSR = 0xc1a,
      HPMCOUNTER27_CSR = 0xc1b,
      HPMCOUNTER28_CSR = 0xc1c,
      HPMCOUNTER29_CSR = 0xc1d,
      HPMCOUNTER30_CSR = 0xc1e,
      HPMCOUNTER31_CSR = 0xc1f,

      CYCLEH_CSR = 0xc80,
      TIMEH_CSR = 0xc81,
      INSTRETH_CSR = 0xc82,
      HPMCOUNTER3H_CSR = 0xc83,
      HPMCOUNTER4H_CSR = 0xc84,
      HPMCOUNTER5H_CSR = 0xc85,
      HPMCOUNTER6H_CSR = 0xc86,
      HPMCOUNTER7H_CSR = 0xc87,
      HPMCOUNTER8H_CSR = 0xc88,
      HPMCOUNTER9H_CSR = 0xc89,
      HPMCOUNTER10H_CSR = 0xc8a,
      HPMCOUNTER11H_CSR = 0xc8b,
      HPMCOUNTER12H_CSR = 0xc8c,
      HPMCOUNTER13H_CSR = 0xc8d,
      HPMCOUNTER14H_CSR = 0xc8e,
      HPMCOUNTER15H_CSR = 0xc8f,
      HPMCOUNTER16H_CSR = 0xc90,
      HPMCOUNTER17H_CSR = 0xc91,
      HPMCOUNTER18H_CSR = 0xc92,
      HPMCOUNTER19H_CSR = 0xc93,
      HPMCOUNTER20H_CSR = 0xc94,
      HPMCOUNTER21H_CSR = 0xc95,
      HPMCOUNTER22H_CSR = 0xc96,
      HPMCOUNTER23H_CSR = 0xc97,
      HPMCOUNTER24H_CSR = 0xc98,
      HPMCOUNTER25H_CSR = 0xc99,
      HPMCOUNTER26H_CSR = 0xc9a,
      HPMCOUNTER27H_CSR = 0xc9b,
      HPMCOUNTER28H_CSR = 0xc9c,
      HPMCOUNTER29H_CSR = 0xc9d,
      HPMCOUNTER30H_CSR = 0xc9e,
      HPMCOUNTER31H_CSR = 0xc9f,

      // Debug/Trace registers.
      TSELECT_CSR = 0x7a0,
      TDATA1_CSR = 0x7a1,
      TDATA2_CSR = 0x7a2,
      TDATA3_CSR = 0x7a3,

      // Debug mode registers.
      DSCR_CSR = 0x7b0,
      DPC_CSR = 0x7b1,
      DSCRATCH_CSR = 0x7b2,

      // Non-standard registers.
      MRAC_CSR = 0x7c0,
      MDSEAC_CSR = 0xfc0,
      MDSEAL_CSR = 0xbc0,

      MEIVT_CSR    = 0xbc8, // Ext int vector table reg 
      MEIPT_CSR    = 0xbc9, // Ext int priority threshold reg
      MEICPCT_CSR  = 0xbca, // Ext int claim ID/priority capture trigger reg
      MEICIDPL_CSR = 0xbcb, // Ext int claim ID’s priority level reg
      MEICURPL_CSR = 0xbcc, // Ext int current priority level reg
      MEIHAP_CSR   = 0xfc8, // Ext int handler address pointer reg
      MAX_CSR_     = MEIHAP_CSR,
      MIN_CSR_     = USTATUS_CSR  // csr with smallest number
    };


  template <typename URV>
  class CsRegs;


  /// Model a control and status register. The template type URV
  /// (unsigned register value) is the type of the register value. It
  /// should be uint32_t for 32-bit implementattions and uint64_t for
  /// 64-bit.
  template <typename URV>
  class Csr
  {
  public:
    /// Default constructor.
    Csr()
    { }

    /// Constructor. The mask indicates which bits are writeable: A zero bit
    /// in the mask corresponds to a non-writeable (preserved) bit in the
    /// register value. To make the whole register writable, set mask to
    /// all ones.
    Csr(const std::string& name, CsrNumber number, bool mandatory, bool valid,
	URV value, URV writeMask = ~URV(0))
      : name_(name), number_(number), mandatory_(mandatory), valid_(valid),
	initialValue_(value), value_(value), writeMask_(writeMask),
	pokeMask_(writeMask)
    { }

    /// Return lowest privilige mode that can access the register.
    /// Bits 9 and 8 of the register number encode the privilge mode.
    PrivilegeMode privilegeMode() const
    { return PrivilegeMode((number_ & 0x300) >> 8); }

    /// Return true if register is read-only. Bits ten and eleven of
    /// the register number denote read-only when both one and read-write
    /// otherwise.
    bool isReadOnly() const
    { return (number_ & 0xc00) == 0xc00; }

    /// Return true if register is implemented.
    bool isImplemented() const
    { return valid_; }

    /// Return true if register is mandatory (not optional).
    bool isMandatory() const
    { return mandatory_; }

    /// Set the value of this register to the given value x honoring
    /// the write mask (defined at construction): Set the ith bit of
    /// this register to the ith bit of the given value x if the ith
    /// bit of the write mask is 1; otherwise, leave the ith bit
    /// unomdified. This is the interface used by the CSR
    /// instructions.
    void write(URV x)
    { value_ = (x & writeMask_) | (value_ & ~writeMask_); }

    /// Return the current value of this register.
    URV read() const
    { return value_; }

    /// Return the write-mask associated with this register. A
    /// register value bit is writable by the write method if and only
    /// if the corresponding bit in the mask is 1; othwrwise, the bit
    /// is preserved.
    URV getWriteMask() const
    { return writeMask_; }

    /// Return the mask associated with this register. A register
    /// value bit is modifiable if and only if the corresponding bit
    /// in the mask is 1; othwrwise, the bit is preserved. The write
    /// mask is used by the CSR write instructions. The poke mask
    /// allows the caller to change bits that are read only for CSR
    /// instructions but are modifiable by the hardware.
    URV getPokeMask() const
    { return pokeMask_; }

    /// Return the number of this register.
    CsrNumber getNumber() const
    { return number_; }

    /// Return the name of this register.
    const std::string& getName() const
    { return name_; }

  protected:

    friend class CsRegs<URV>;

    /// Reset to intial (power-on) value.
    void reset()
    { value_ = initialValue_; }

    /// Define the mask used by the poke method to write this
    /// register. The mask defined the register bits that are
    /// modifiable (even though such bits may not be writeable using a
    /// CSR instruction). For example, the meip bit (of the mip CSR)
    /// is not writebale using a CSR instruction but is modifiable.
    void setPokeMask(URV mask)
    { pokeMask_ = mask; }

    /// Mark register as a debug-mode register. Acessesing a debug-mode
    /// register when the processor is not in debug mode will trigger an
    /// illegal instruction exception.
    void setIsDebug(bool flag)
    { debug_ = flag; }

    /// Return true if this regiser has been marked as a debug-mode
    /// register.
    bool isDebug() const
    { return debug_; }

    /// Similar to the write method but using the poke mask instead of
    /// the write mask. This is the interface used by non-csr
    /// instructions to change modifiable (but not writeable through
    /// CSR instructions) bits of this register.
    void poke(URV x)
    { value_ = (x & pokeMask_) | (value_ & ~pokeMask_); }

  private:
    std::string name_;
    CsrNumber number_ = CsrNumber(0);
    bool mandatory_ = false;   // True if mandated by architercture.
    bool valid_ = false;       // True if register is implemented.
    bool debug_ = false;       // True if this is a debug-mode reigster.
    URV initialValue_ = 0;
    URV value_ = 0;
    URV writeMask_ = ~URV(0);
    URV pokeMask_ = ~URV(0);
  };


  template <typename URV>
  class Core;


  /// Model the control and status register set.
  template <typename URV>
  class CsRegs
  {
  public:

    friend class Core<uint32_t>;
    friend class Core<uint64_t>;

    CsRegs();
    
    ~CsRegs();

    /// Set reg to a copy of the control-and-status description
    /// corresponding to the given name returning true on success. If
    /// no such name, return true leaving reg unmodified.
    bool findCsr(const std::string& name, Csr<URV>& reg) const;

    /// Set reg to a copy of the control-and-status description
    /// corresponding to the given name returning true on success. If
    /// no such name, return true leaving reg unmodified.
    bool findCsr(CsrNumber number, Csr<URV>& reg) const;

    /// Set value fo the value of the scr having the given number
    /// returning true on success.  Return false leaving value
    /// unmodified if there is no csr with the given number or if the
    /// csr is not valid or if the the given mode has no access to the
    /// register.
    bool read(CsrNumber number, PrivilegeMode mode, bool debugMode,
	      URV& value) const;

    /// Set the the csr having the given number to the given value
    /// returning true on success. Return false writing nothing if
    /// there is no csr with the given number or if the csr is not
    /// valid or if the given mode has no access to the register.
    bool write(CsrNumber number, PrivilegeMode mode, bool debugMode,
	       URV value);

    /// Return true if given register is writable in the given mode.
    bool isWriteable(CsrNumber number, PrivilegeMode mode) const;

    /// Return the number of bits in a register in this register file.
    static constexpr uint32_t regWidth()
    { return sizeof(URV)*8; }

  protected:

    /// Define csr with given name and numebr. Return pointer to csr
    /// on succes or nullptr if given name is already in use or if the
    /// csr number is out of bounds.
    Csr<URV>* defineCsr(const std::string& name, CsrNumber number,
			bool mandatory, bool valid, URV value,
			URV writeMask = ~URV(0));

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

    bool ldStAddrTriggerHit(URV address, TriggerTiming timing, bool isLoad)
    {
      bool hit = triggers_.ldStAddrTriggerHit(address, timing, isLoad);
      if (hit)
	recordWrite(TDATA1_CSR);  // Hit bit in TDATA1 changed.
      return hit;
    }

    bool ldStDataTriggerHit(URV address, TriggerTiming timing, bool isLoad)
    {
      bool hit = triggers_.ldStDataTriggerHit(address, timing, isLoad);
      if (hit)
	recordWrite(TDATA1_CSR);  // Hit bit in TDATA1 changed.
      return hit;
    }

    bool instAddrTriggerHit(URV address, TriggerTiming timing)
    {
      bool hit = triggers_.instAddrTriggerHit(address, timing);
      if (hit)
	recordWrite(TDATA1_CSR);  // Hit bit in TDATA1 changed.
      return hit;
    }

    bool instOpcodeTriggerHit(URV opcode, TriggerTiming timing)
    {
      bool hit = triggers_.instOpcodeTriggerHit(opcode, timing);
      if (hit)
	recordWrite(TDATA1_CSR);  // Hit bit in TDATA1 changed.
      return hit;
    }

    bool icountTriggerHit()
    {
      bool hit = triggers_.icountTriggerHit();
      if (hit)
	recordWrite(TDATA1_CSR);  // Hit bit in TDTA1 changed.
      return hit;
    }

    /// Set register to the given value masked by the poke mask. A
    /// read-only register can be changed this way as long as its poke
    /// mask is non-zero. Return true on sucess and false if number is
    /// out of bounds.
    bool poke(CsrNumber number, PrivilegeMode mode, URV value);

    /// Reset all CSRs to their intial (power-on) values.
    void reset()
    {
      for (auto& csr : regs_)
	if (csr.isImplemented())
	  csr.reset();
    }

    /// Configure CSR.
    bool configCsr(const std::string& name, bool implemented,
		   URV resetValue, URV mask, URV pokeMask);

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

    /// Return the count of retired instructions. Return zero if
    /// retired-instruction register is not impelemented.
    uint64_t getRetiredInstCount() const;

    /// Set the value(s) of the retired instruction register(s) to the
    /// given count returning true on success and false if the retired
    /// instruction register(s) is(are) not implemented.
    bool setRetiredInstCount(uint64_t count);

    /// Return the cycle-count saved in this register file. Return 0
    /// if the cycle count register is not implemented.
    uint64_t getCycleCount() const;

    /// Set the value(s) of the cycle count register(s) to the given
    /// count returning true on success and false if the retired
    /// instruction register(s) is(are) not implemented.
    bool setCycleCount(uint64_t count);

    /// Set the store error address capture register. Return true on
    /// success and false if register is not implemented.
    bool setStoreErrorAddrCapture(URV value);

    bool readTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
		   URV& value) const;
    
    bool writeTdata(CsrNumber number, PrivilegeMode mode, bool debugMode,
		    URV value);

  protected:

    /// Trace writes if flag is true.
    void traceWrites(bool flag)
    { traceWrites_ = flag; }

    /// Record a write if writes are being traced.
    void recordWrite(CsrNumber num)
    { if (traceWrites_) lastWrittenRegs_.push_back(num); }

    /// Clear the remembered indices of the CSR register(s) written by
    /// the last instruction.
    void clearLastWrittenRegs()
    {
      lastWrittenRegs_.clear();
      triggers_.clearLastWrittenTriggers();
    }

    /// Configure given trigger with given reset values, write and
    /// poke maksks. Return true on success and false on failure.
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

  private:

    std::vector< Csr<URV> > regs_;
    std::unordered_map<std::string, CsrNumber> nameToNumber_;

    Triggers<URV> triggers_;

    // Register written since most recent clearLastWrittenRegs
    std::vector<CsrNumber> lastWrittenRegs_;

    bool traceWrites_;

    // These can be obtained from Triggers. Speed up access by caching
    // them in here.
    bool hasActiveTrigger_ = false;
    bool hasActiveInstTrigger_ = false;
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
