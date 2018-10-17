// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <iosfwd>
#include <type_traits>
#include "InstId.hpp"
#include "InstInfo.hpp"
#include "IntRegs.hpp"
#include "CsRegs.hpp"
#include "FpRegs.hpp"
#include "Memory.hpp"
#include "InstProfile.hpp"

namespace WdRiscv
{

  class CoreException : public std::exception
  {
  public:

    enum Type { Stop, TriggerHit };

    CoreException(Type type, const char* message = "", uint64_t address = 0,
		  uint64_t value = 0, bool triggerBefore = false)
      : type_(type), msg_(message), addr_(address), val_(value),
	triggerBefore_(triggerBefore)
    { }

    const char* what() const noexcept override
    { return msg_; }

    Type type() const
    { return type_; }

    uint64_t address() const
    { return addr_; }

    uint64_t value() const
    { return val_; }

    /// Valid/meaningful for TriggerHit type.
    bool isTriggerBefore() const
    { return triggerBefore_; }

  private:
    Type type_ = Stop;
    const char* msg_ = "";
    uint64_t addr_ = 0;
    uint64_t val_ = 0;
    bool triggerBefore_ = false;
  };
    

  /// Model a RISCV core with registers of type URV (uint32_t for
  /// 32-bit registers and uint64_t for 64-bit registers).
  template <typename URV>
  class Core
  {
  public:
    
    /// Signed register type corresponding to URV. For example, if URV
    /// is uint32_t, then SRV will be int32_t.
    typedef typename std::make_signed_t<URV> SRV;

    /// Constructor: Define a core with given memory size and register
    /// count.
    Core(unsigned hartId, size_t memorySize, unsigned intRegCount);

    /// Destructor.
    ~Core();

    /// Return count of integer registers.
    size_t intRegCount() const
    { return intRegs_.size(); }

    /// Return size of memory in bytes.
    size_t memorySize() const
    { return memory_.size(); }

    /// Return the value of the program counter.
    URV peekPc() const;

    /// Set the program counter to the given address.
    void pokePc(URV address);

    /// Set val to the value of integer register reg returning true on
    /// success. Return false leaving val unmodified if reg is out of
    /// bounds.
    bool peekIntReg(unsigned reg, URV& val) const;

    /// Set the given integer register, reg, to the given value
    /// returning true on success. Return false if reg is out of
    /// bound.
    bool pokeIntReg(unsigned reg, URV val);

    /// Set val to the bit-pattern of the value of the floating point
    /// register returning true on success. Return false leaving val
    /// unmodified if reg is out of bounds of if no floating point
    /// extension is enabled.
    bool peekFpReg(unsigned reg, uint64_t& val) const;

    /// Set val to the value of the constrol and status register csr
    /// returning true on success. Return false leaving val unmodified
    /// if csr is out of bounds.
    bool peekCsr(CsrNumber csr, URV& val) const;

    /// Set val, writeMask, and pokeMask respectively to the value,
    /// write-mask and poke-mask of the constrol and status register
    /// csr returning true on success. Return false leaving paramters
    /// unmodified if csr is out of bounds.
    bool peekCsr(CsrNumber csr, URV& val, URV& writeMask, URV& pokeMask) const;

    /// Set val/name to the value/name of the constrol and status
    /// register csr returning true on success. Return false leaving
    /// val/name unmodified if csr is out of bounds.
    bool peekCsr(CsrNumber csr, URV& val, std::string& name) const;

    /// Set the given control and status register, csr, to the given
    /// value returning true on success. Return false if csr is out of
    /// bound.
    bool pokeCsr(CsrNumber csr, URV val);

    /// Find the integer register with the given name (which may
    /// represent an integer or a symbolic name). Set num to the
    /// number of the corresponding register if found. Return true on
    /// success and false if no such register.
    bool findIntReg(const std::string& name, unsigned& num) const;

    /// Find the control and status register with the given name
    /// (which may represent an integer or a symbolic name). Set num
    /// to the number of the corresponding register if found. Return
    /// true on success and false if no such register.
    bool findCsr(const std::string& name, CsrNumber& num) const;

    /// Configure given CSR. Return true on success and false if
    /// no such CSR.
    bool configCsr(const std::string& name, bool implemented,
		   URV resetValue, URV mask, URV pokeMask);

    /// Configure given trigger with given reset values, write and
    /// poke maksks. Return true on success and false on failure.
    bool configTrigger(unsigned trigger, URV val1, URV val2, URV val3,
		       URV wm1, URV wm2, URV wm3,
		       URV pm1, URV pm2, URV pm3)
    {
      return csRegs_.configTrigger(trigger, val1, val2, val3,
				   wm1, wm2, wm3, pm1, pm2, pm3);
    }

    /// Restrict chaining only to pairs of consecutive (even-numbered followed
    /// by odd) triggers.
    void configEvenOddTriggerChaining(bool flag)
    { csRegs_.configEvenOddTriggerChaining(flag); }

    /// Configure machine mode performance counters returning true on
    /// success and false on failure. N consecutive counters starting
    /// at MHPMCOUNTER3/MHPMCOUNTER3H are made read/write. The
    /// remaining counters are made read only. For each counter that
    /// is made read-write the corresponding MHPMEVENT is made
    /// read-write.
    bool configMachineModePerfCounters(unsigned n);

    /// Set the maximum event id that can be written to the mhpmevent
    /// registers. Larger values are replaced by this max-value before
    /// being written to the mhpmevent registers. Return true on
    /// success and false on failure.
    void configMachineModeMaxPerfEvent(URV maxId)
    { csRegs_.setMaxEventId(maxId); }

    /// Get the values of the three components of the given debug
    /// trigger. Return true on success and false if trigger is out of
    /// bounds.
    bool peekTrigger(URV trigger, URV& data1, URV& data2, URV& data3) const
    { return csRegs_.peekTrigger(trigger, data1, data2, data3); }

    /// Get the values of the three components of the given debug
    /// trigger as well as the components write and poke masks. Return
    /// true on success and false if trigger is out of bounds.
    bool peekTrigger(URV trigger, URV& val1, URV& val2, URV& val3,
		     URV& wm1, URV& wm2, URV& wm3,
		     URV& pm1, URV& pm2, URV& pm3) const
    { return csRegs_.peekTrigger(trigger, val1, val2, val3, wm1, wm2, wm3,
				 pm1, pm2, pm3); }

    /// Set the values of the three components of the given debug
    /// trigger. Return true on success and false if trigger is out of
    /// bounds.
    bool pokeTrigger(URV trigger, URV data1, URV data2, URV data3)
    { return csRegs_.pokeTrigger(trigger, data1, data2, data3); }

    /// Rset core. Reset all CSRs to their initial value. Reset all
    /// integer registers to zero. Reset PC to the reset-pc as
    /// defined by defineResetPc (default is zero).
    void reset();

    /// Run fetch-decode-execute loop. If a stop address (see
    /// setStoAddress) is defined, stop when the prgram counter
    /// reaches that address. If a tohost address is defined (see
    /// setToHostAdress), stop when a store instruction writes into
    /// that address. If given file is non-null, then print to that
    /// file a record for each executed instruction.
    bool run(FILE* file = nullptr);

    /// Run one instruction at the current program counter. Update
    /// program counter. If file is non-null then print thereon
    /// tracing information related to the executed instruction.
    void singleStep(FILE* file = nullptr);

    /// Run until the program counter reaches the given address. Do
    /// execute the instruction at that address. If file is non-null
    /// then print thereon tracing information after each executed
    /// instruction. Similar to method run with respect to tohost.
    bool runUntilAddress(URV address, FILE* file = nullptr);

    /// Define the program counter value at which the run method will
    /// stop.
    void setStopAddress(URV address)
    { stopAddr_ = address; stopAddrValid_ = true; }

    /// Undefine stop address (see setStopAddress).
    void clearStopAddress()
    { stopAddrValid_ = false; }

    /// Define the memory address corresponding to console io. Reading/writing
    /// a byte (lb/sb) from/to that haddress reads/writes a byte to/from
    /// the console.
    void setConsoleIo(URV address)
    { conIo_ = address; conIoValid_ = true; }

    /// Undefine console io address (see setConsoleIo).
    void clearConsoleIo()
    { conIoValid_ = false; }

    /// Console output gets directed to given file.
    void setConsoleOutput(FILE* out)
    { consoleOut_ = out; }

    /// If a console io memory mapped location is defined then put its
    /// address in address and return true; otherwise, return false
    /// leaving address unmodified.
    bool getConsoleIo(URV& address) const
    { if (conIoValid_) address = conIo_; return conIoValid_; }

    /// Disassemble given instruction putting results on the given
    /// stream.
    void disassembleInst(uint32_t inst, std::ostream&);

    /// Disassemble given instruction putting results into the given
    /// string.
    void disassembleInst(uint32_t inst, std::string& str);

    /// Helper to disassembleInst. Disassemble a 32-bit instruction.
    void disassembleInst32(uint32_t inst, std::ostream&);

    /// Helper to disassembleInst. Disassemble a compressed (16-bit)
    /// instruction.
    void disassembleInst16(uint16_t inst, std::ostream&);

    /// Helper to disassembleInst. Disassemble a 32-bit instruction.
    void disassembleInst32(uint32_t inst, std::string& str);

    /// Helper to disassembleInst. Disassemble a compressed (16-bit)
    /// instruction.
    void disassembleInst16(uint16_t inst, std::string& str);

    /// Expand given 16-bit co to the equivalent 32-bit instruction
    /// code returning true on sucess and false if given 16-bit code
    /// does not correspond to a valid instruction.
    bool expandInst(uint16_t code16, uint32_t& code32) const;

    /// Decode given instruction returning a pointer to the
    /// instruction information and filling op0, op1 and op2 with the
    /// corresponding operand specifier values. For example, if inst
    /// is the instruction code for "addi r3, r4, 77", then the
    /// returned value would correspond to addi and op0, op1 and op2
    /// will be set to 3, 4, and 77 respectively. If an instruction
    /// has fewer than 3 operands then only a subset of op0, op1 and
    /// op2 will be set. If inst is not a valid instruction , then we
    /// return a reference to the illegal-instruction info.
    const InstInfo& decode(uint32_t inst, uint32_t& op0, uint32_t& op1,
			   int32_t& op2);

    /// Load the given hex file and set memory locations accordingly.
    /// Return true on success. Return false if file does not exists,
    /// cannot be opened or contains malformed data.
    /// File format: A line either contains @address where address
    /// is a hexadecimal memory address or one or more space separated
    /// tokens each consisting of two hexadecimal digits.
    bool loadHexFile(const std::string& file);

    /// Load the given ELF file and set memory locations accordingly.
    /// Return true on success. Return false if file does not exists,
    /// cannot be opened or contains malformed data. If successful,
    /// set entryPoint to the entry point of the loaded file.
    bool loadElfFile(const std::string& file, size_t& entryPoint,
		     size_t& exitPoint,
		     std::unordered_map<std::string, size_t>& symbols);

    /// Set val to the value of the memory byte at the given address
    /// returning true on success and false if address is out of
    /// bounds.
    bool peekMemory(size_t address, uint8_t& val) const;

    /// Set val to the value of the half-word at the given address
    /// returning true on success and false if address is out of
    /// bounds. Memory is little endian.
    bool peekMemory(size_t address, uint16_t& val) const;

    /// Set val to the value of the word at the given address
    /// returning true on success and false if address is out of
    /// bounds. Memory is little endian.
    bool peekMemory(size_t address, uint32_t& val) const;

    /// Set val to the value of the word at the given address
    /// returning true on success and false if address is out of
    /// bounds. Memory is little endian.
    bool peekMemory(size_t address, uint64_t& val) const;

    /// Set the memory byte at the given address to the given value.
    /// Return true on success and false on failure (address out of
    /// bounds, location not mapped, location notwritebale etc...)
    bool pokeMemory(size_t address, uint8_t val);

    /// Halt word version of the above.
    bool pokeMemory(size_t address, uint16_t val);

    /// Word version of the above.
    bool pokeMemory(size_t address, uint32_t val);

    /// Double word version of the above.
    bool pokeMemory(size_t address, uint64_t val);

    /// Define value of program counter after a reset.
    void defineResetPc(URV addr)
    { resetPc_ = addr; }

    /// Define valye of program counter after a non-maskable interrupt.
    void defineNmiPc(URV addr)
    { nmiPc_ = addr; }

    /// Clear/set pending non-maskable-interrupt.
    void setPendingNmi(URV cause = 0);

    /// Clear pending non-maskable-interrupt.
    void clearPendingNmi();

    /// Define address to which a write will stop the simulator. An
    /// sb, sh, or sw instruction will stop the simulator if the write
    /// address of he instruction is identical to the given address.
    void setToHostAddress(size_t address);

    /// Undefine address to which a write will stop the simulator
    void clearToHostAddress();

    /// Set address to the special addreess writing to which stops the
    /// simulation. Return true on success and false on failure (no
    /// such address defined).
    bool getToHostAddress(size_t& address) const
    { if (toHostValid_) address = toHost_; return toHostValid_; }

    /// Support for tracing: Return the pc of the last executed
    /// instruction.
    URV lastPc() const;

    /// Support for tracing: Return the index of the integer register
    /// written by the last executed instruction. Return -1 it no
    /// integer register was written.
    int lastIntReg() const;

    /// Support for tracing: Fill the csrs vector with the
    /// register-numbers of the CSRs written by the execution of the
    /// last instruction. CSRs modified as a side effect (e.g. mcycle
    /// and minstret) are not included. Fill the triggers vector with
    /// the number of the debug-trigger registers writen by the
    /// execution of the last instruction.
    void lastCsr(std::vector<CsrNumber>& csrs,
		 std::vector<unsigned>& triggers) const;

    /// Support for tracing: Fill the addresses and words vectors with
    /// the addresses of the memory words modified by the last
    /// executed instruction and their corresponding values.
    void lastMemory(std::vector<size_t>& addresses,
		    std::vector<uint32_t>& words) const;

    /// Read instruction at given address. Return true on sucess and
    /// false if address is out of memory bounds.
    bool readInst(size_t address, uint32_t& instr);

    /// Set instruction count limit: When running with tracing the
    /// run and the runUntil methods will stop if the retired instruction
    /// count (true count and not value of minstret) reaches or exceeds
    /// the limit.
    void setInstructionCountLimit(uint64_t limit)
    { instCountLim_ = limit; }

    /// Reset executed instruction count.
    void setInstructionCount(uint64_t count)
    { counter_ = count; }

    /// Get executed instruction count.
    uint64_t getInstructionCount() const 
    { return counter_; }

    /// Define instruction closed coupled memory (in core instruction memory).
    bool defineIccm(size_t region, size_t offset, size_t size);

    /// Define data closed coupled memory (in core data memory).
    bool defineDccm(size_t region, size_t offset, size_t size);

    /// Define a region for memory mapped registers.
    bool defineMemoryMappedRegisterRegion(size_t region, size_t offset,
					  size_t size);

    /// Define a memory mapped register. Region (as defined by region
    /// and offset) must be already defined using
    /// defineMemoryMappedRegisterRegion. The register address must not
    /// fall outside the region
    bool defineMemoryMappedRegisterWriteMask(size_t region,
					     size_t regionOffset,
					     size_t registerBlockOffset,
					     size_t registerIx,
					     uint32_t mask);

    /// Called after memory is configured to refine memory access to
    /// sections of regions containing ICCM, DCCM or PIC-registers.
    void finishMemoryConfig()
    { memory_.finishMemoryConfig();}

    /// Direct the core to take an instruction access fault exception
    /// within the next singleStep invocation.
    void postInstAccessFault()
    { forceFetchFail_ = true; }

    /// Direct the core to take a data access fault exception within
    /// the subsequent singleStep invocation executing a load/store
    /// instruction.
    void postDataAccessFault()
    { forceAccessFail_ = true; }

    /// Enable printing of load-instruction data address in
    /// instruction trace mode.
    void setTraceLoad(bool flag)
    { traceLoad_ = flag; }

    /// Return count of traps (exceptions or interupts) seen by this
    /// core.
    uint64_t getTrapCount() const
    { return exceptionCount_ + interruptCount_; }

    /// Return count of exceptions seen by this core.
    uint64_t getExceptionCount() const
    { return exceptionCount_; }

    /// Return count of interrupts seen by this core.
    uint64_t getInterruptCount() const
    { return interruptCount_; }

    /// Set pre and post to the count of "before"/"after" triggers
    /// that tripped by the last executed instruction.
    void countTrippedTriggers(unsigned& pre, unsigned& post) const
    { csRegs_.countTrippedTriggers(pre, post); }

    /// Apply an imprecise store exception at given address. Return
    /// true if address is found exactly once in the store
    /// queue. Return false otherwise. Save the given address in
    /// mdseac. Set matchCount to the number of entries in the store
    /// queue that match the given address.
    bool applyStoreException(URV address, unsigned& matchCount);

    /// Apply an imprecise load exception at given address. Return
    /// true if address is found exactly once in the pending load
    /// queue. Return false otherwise. Save the given address in
    /// mdseac. Set matchCount to the number of entries in the store
    /// queue that match the given address.
    bool applyLoadException(URV address, unsigned& matchCount);

    /// Enable processing of imprecise store exceptions.
    void enableStoreExceptions(bool flag)
    { maxStoreQueueSize_ = flag? 4 : 0; }

    /// Enable processing of imprecise load exceptions.
    void enableLoadExceptions(bool flag)
    { maxLoadQueueSize_ = flag? 4 : 0; }

    /// Enable collection of instruction frequencies.
    void enableInstructionFrequency(bool b);

    /// Put the core in debug mode setting the DCSR cause field to the
    /// given cause.
    void enterDebugMode(DebugModeCause cause, URV pc);

    /// Put the core in debug mode setting the DCSR cause field to either
    /// DEBUGGER or SETP depending on the step bit of DCSR.
    /// given cause.
    void enterDebugMode(URV pc);

    bool inDebugMode() const
    { return debugMode_; }

    /// Take the core out of debug mode.
    void exitDebugMode();

    /// Enable/disable imrepcise store error rollback. This is useful
    /// in test-bench server mode.
    void enableStoreErrorRollback(bool flag)
    { storeErrorRollback_ = flag; }

    /// Enable/disbale imrepcise load error rollback. This is useful
    /// in test-bench server mode.
    void enableLoadErrorRollback(bool flag)
    { loadErrorRollback_ = flag; }

    /// Print collected instruction frequency to the given file.
    void reportInstructionFrequency(FILE* file) const;

    /// Reset trace data (items changed by the execution of an
    /// instruction.)
    void clearTraceData();

    /// Enable debug-triggers. Without this, triggers will not trip
    /// and will not cause exceptions.
    void enableTriggers(bool flag)
    { enableTriggers_ = flag;  }

    /// Enable performance counters (count up for some enabled
    /// performance counters when their events do occur).
    void enablePerformanceCounters(bool flag)
    { enableCounters_ = flag;  }

    /// Enable gdb-mode.
    void enableGdb(bool flag)
    { enableGdb_ = flag; }

    /// Return true if rv32f (single precision floating point)
    /// extension is enabled in this core.
    bool isRvf() const
    { return rvf_; }

    /// Return true if rv64d (double precision floating point)
    /// extension is enabled in this core.
    bool isRvd() const
    { return rvd_; }

    /// Return true if rv64 (64-bit option) extension is enabled in
    /// this core.
    bool isRv64() const
    { return rv64_; }

    /// Return true if rvm (multiply/divide) extension is enabled in
    /// this core.
    bool isRvm() const
    { return rvm_; }

    /// Return true if rvc (compression) extension is enabled in this
    /// core.
    bool isRvc() const
    { return rvc_; }

    /// Return true if rva (atomic) extension is enabled in this core.
    bool isRva() const
    { return rva_; }

    /// Return true if rvu (user-mode) extension is enabled in this
    /// core.
    bool isRvs() const
    { return rvs_; }

    /// Return true if rvu (user-mode) extension is enabled in this
    /// core.
    bool isRvu() const
    { return rvu_; }

  protected:

    /// Helper to decode. Used for compressed instructions.
    const InstInfo& decode16(uint32_t inst, uint32_t& op0, uint32_t& op1,
			     int32_t& op2);

    /// Helper to disassemble methods. Print an rv32f floating point
    /// instruction with 4 operands.
    void printFp32f(std::ostream&, const std::string& inst,
		    unsigned rd, unsigned rs1, unsigned rs2,
		    unsigned rs3, RoundingMode mode);

    /// Helper to disassemble methods. Print an rv32d floating point
    /// instruction with 4 operands.
    void printFp32d(std::ostream&, const std::string& inst,
		    unsigned rd, unsigned rs1, unsigned rs2,
		    unsigned rs3, RoundingMode mode);

    /// Helper to disassemble methods. Print an rv32f floating point
    /// instruction with 3 operands.
    void printFp32f(std::ostream&, const std::string& inst,
		    unsigned rd, unsigned rs1, unsigned rs2,
		    RoundingMode mode);

    /// Helper to disassemble methods. Print an rv32d floating point
    /// instruction with 3 operands.
    void printFp32d(std::ostream&, const std::string& inst,
		    unsigned rd, unsigned rs1, unsigned rs2,
		    RoundingMode mode);

    /// Return the effective rounding mode for the currently executing
    /// floating point instruction. This assumes that execute32 or
    /// execute16 has already set the instruction rounging mode.
    RoundingMode effectiveRoundingMode();

    /// Update the accrued floating point bits in the FCSR register.
    void updateAccruedFpBits();

    /// Undo the effect of the last executed instruction given that
    /// that a trigger has tripped.
    void undoForTrigger();

    /// Return true if the mie bit of the mstatus register is on.
    bool isInterruptEnabled() const
    { return csRegs_.isInterruptEnabled(); }

    /// Baded on current trigger configurations, either take an
    /// exception reutrning false or enter debug mode returning true.
    bool takeTriggerAction(FILE* traceFile, URV epc, URV info,
			   uint64_t& counter, bool beforeTiming);

    /// Record given CSR number for later reporting of CSRs modifed by
    /// an instruction.
    void recordCsrWrite(CsrNumber csr)
    { csRegs_.recordWrite(csr); }

    /// Helper to load/store.
    bool misalignedAccessCausesException(URV addr, unsigned accessSize) const;

    /// Helper to lb, lh, lw and ld. Load type should be int_8, int16_t
    /// etc... for signed byte, halfword etc... and uint8_t, uint16_t
    /// etc... for lbu, lhu, etc...
    template<typename LOAD_TYPE>
    void load(uint32_t rd, uint32_t rs1, int32_t imm);

    /// Helper to sb, sh, sw ... Sore type should be uint8_t, uint16_t
    /// etc... for sb, sh, etc...
    template<typename STORE_TYPE>
    void store(URV addr, STORE_TYPE value);

    /// Helper to execLr. Load type should be int32_t, or int64_t.
    template<typename LOAD_TYPE>
    void loadReserve(uint32_t rd, uint32_t rs1);

    /// Helper to execSc. Store type should be uint32_t, or uint64_t.
    /// Return true if store is successful. Return false otherwise
    /// (exception or trigger or condition failed).
    template<typename STORE_TYPE>
    bool storeConditional(URV addr, STORE_TYPE value);

    /// Helper to CSR instructions. Keep minstret and mcycle up to date.
    void preCsrInstruction(CsrNumber csr);

    /// Helper to CSR instructions: Write csr and integer register.
    /// Write next value to csr.
    void commitCsrWrite(CsrNumber csr, URV csrVal, unsigned intReg,
			URV intRegVal);

    /// Return true if one or more load-address/store-address trigger
    /// has a hit on the given address and given timing
    /// (before/after). Set the hit bit of all the triggers that trip.
    bool ldStAddrTriggerHit(URV addr, TriggerTiming t, bool isLoad, bool ie)
    { return csRegs_.ldStAddrTriggerHit(addr, t, isLoad, ie); }

    /// Return true if one or more load-address/store-address trigger
    /// has a hit on the given data value and given timing
    /// (before/after). Set the hit bit of all the triggers that trip.
    bool ldStDataTriggerHit(URV value, TriggerTiming t, bool isLoad, bool ie)
    { return csRegs_.ldStDataTriggerHit(value, t, isLoad, ie); }

    /// Return true if one or more execution trigger has a hit on the
    /// given address and given timing (before/after). Set the hit bit
    /// of all the triggers that trip.
    bool instAddrTriggerHit(URV addr, TriggerTiming t, bool ie)
    { return csRegs_.instAddrTriggerHit(addr, t, ie); }

    /// Return true if one or more execution trigger has a hit on the
    /// given opcode value and given timing (before/after). Set the
    /// hit bit of all the triggers that trip.
    bool instOpcodeTriggerHit(URV opcode, TriggerTiming t, bool ie)
    { return csRegs_.instOpcodeTriggerHit(opcode, t, ie); }

    /// Make all active icount triggers count down, return true if
    /// any of them counts down to zero.
    bool icountTriggerHit()
    { return csRegs_.icountTriggerHit(isInterruptEnabled()); }

    /// Return true if hart has one or more active debug triggers.
    bool hasActiveTrigger() const
    { return (enableTriggers_ and csRegs_.hasActiveTrigger()); }

    /// Return true if hart has one or more active debug instruction
    /// (execute) triggers.
    bool hasActiveInstTrigger() const
    { return (enableTriggers_ and csRegs_.hasActiveInstTrigger()); }

    /// Collect instruction stats (for instruction profile and/or
    /// peformance monitors).
    void accumulateInstructionStats(uint32_t inst);

    /// Fetch an instruction. Return true on success. Return false on
    /// fail (in which case an exception is initiated). May fetch a
    /// compresssed instruction (16-bits) in which case the upper 16
    /// bits are not defined (may contain arbitrary values).
    bool fetchInst(size_t address, uint32_t& instr);

    /// Fetch an instruction given that a trigger has tripped. Return
    /// true on successs. Return false on a a fail in which case
    /// either a trigger exception is initiated (as opposed to an
    /// instruction-fail exception).
    bool fetchInstPostTrigger(size_t address, uint32_t& inst, FILE* trace);

    /// Write trace information about the given instruction to the
    /// given file. This is assumed to be called after instruction
    /// execution. Tag is the record tag (the retired instruction
    /// count after instruction is executed). Tmp is a temporary
    /// string (for performance).
    void traceInst(uint32_t inst, uint64_t tag, std::string& tmp, FILE* out,
		   bool interrupt = false);

    /// Start a synchronous exceptions.
    void initiateException(ExceptionCause cause, URV pc, URV info);

    /// Start an asynchronous exception (interrupt).
    void initiateInterrupt(InterruptCause cause, URV pc);

    /// Start a non-maskable interrupt.
    void initiateNmi(URV cause, URV pc);

    /// If a non-maskable-interrupt is pending take it. If an external
    /// interrupt is pending and interrupts are enabled, then take
    /// it. Return true if an nmi or an interrupt is taken and false
    /// otherwise.
    bool processExternalInterrupt(FILE* traceFile, std::string& insStr);

    /// Execute given 32-bit instruction. Assumes currPc_ is set to
    /// the address of the instruction in simulated memory. Assumes
    /// pc_ is set to currPc_ plus 4. Neither pc_ or currPc_ is used
    /// to reference simulated memory. A branch instruction or an
    /// exception will end up modifying pc_.
    void execute32(uint32_t inst);

    /// Execute given 16-bit instruction. Assumes currPc_ is set to
    /// the address of the instruction in simulated memory. Assumes
    /// pc_ is set to currPc_ plus 2. Neither pc_ or currPc_ is used
    /// to reference simulated memory. A branch instruction or an
    /// exception will end up modifying pc_.
    void execute16(uint16_t inst);

    /// Helper to decode: Deocde instructions associated with opcode
    /// 1010011.
    const InstInfo& decodeFp(uint32_t inst, uint32_t& op0, uint32_t& op1,
			     int32_t& op2);

    /// Helper to disassembleInst32: Disassemble instructions
    /// associated with opcode 1010011.
    void disassembleFp(uint32_t inst, std::ostream& stream);

    /// Decode and execute floating point instructions associated with
    /// opcode 1010011. This is a heleper to execute32.
    void executeFp(uint32_t inst);

    /// Change machine state and program counter in reaction to an
    /// exception or an interrupt. Given pc is the program counter to
    /// save (address of instruction causing the asynchronous
    /// exception or the instruction to resume after asynchronous
    /// exception is handeled). The info value holds additional
    /// information about an exception.
    void initiateTrap(bool interrupt, URV cause, URV pcToSave, URV info);

    /// Illegal instruction. One of the following:
    ///   - Invalid opcode.
    ///   - Machine mode instruction executed when not in machine mode.
    ///   - Invalid CSR.
    ///   - Write to a read-only CSR.
    void illegalInst();

    /// Place holder for not-yet implemented instructions. Calls
    /// illegal instruction.
    void unimplemented();

    /// Return true if an external interrupts are enabled and an external
    /// interrupt is pending and is enabled. Set cause to the type of
    /// interrupt.
    bool isInterruptPossible(InterruptCause& cause);

    /// Return true if 256mb region of address is idempotent.
    bool isIdempotentRegion(size_t addr) const;

    // rs1: index of source register (value range: 0 to 31)
    // rs2: index of source register (value range: 0 to 31)
    // rd: index of destination register (value range: 0 to 31)
    // offset: singed integer.
    // imm: signed integer.
    // All immediate and offset values are assumed to be already unpacked
    // and sign extended if necessary.

    // The program counter is adjusted (size of current instruction added)
    // before any of the following methods are called. To get the address
    // before adjustment, use currPc_.
    void execBeq(uint32_t rs1, uint32_t rs2, int32_t offset);
    void execBne(uint32_t rs1, uint32_t rs2, int32_t offset);
    void execBlt(uint32_t rs1, uint32_t rs2, int32_t offset);
    void execBltu(uint32_t rs1, uint32_t rs2, int32_t offset);
    void execBge(uint32_t rs1, uint32_t rs2, int32_t offset);
    void execBgeu(uint32_t rs1, uint32_t rs2, int32_t offset);

    void execJalr(uint32_t rd, uint32_t rs1, int32_t offset);
    void execJal(uint32_t rd, uint32_t offset, int32_t = 0);

    void execLui(uint32_t rd, uint32_t imm, int32_t = 0);
    void execAuipc(uint32_t rd, uint32_t imm, int32_t = 0);

    void execAddi(uint32_t rd, uint32_t rs1, int32_t imm);
    void execSlli(uint32_t rd, uint32_t rs1, int32_t amount);
    void execSlti(uint32_t rd, uint32_t rs1, int32_t imm);
    void execSltiu(uint32_t rd, uint32_t rs1, int32_t imm);
    void execXori(uint32_t rd, uint32_t rs1, int32_t imm);
    void execSrli(uint32_t rd, uint32_t rs1, int32_t amount);
    void execSrai(uint32_t rd, uint32_t rs1, int32_t amount);
    void execOri(uint32_t rd, uint32_t rs1, int32_t imm);
    void execAndi(uint32_t rd, uint32_t rs1, int32_t imm);
    void execAdd(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSub(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSll(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSlt(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSltu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execXor(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSrl(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSra(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execOr(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAnd(uint32_t rd, uint32_t rs1, int32_t rs2);

    void execFence(uint32_t pred, uint32_t succ, int32_t = 0);
    void execFencei(uint32_t = 0, uint32_t = 0, int32_t = 0);

    void execEcall(uint32_t = 0, uint32_t = 0, int32_t = 0);
    void execEbreak(uint32_t = 0, uint32_t = 0, int32_t = 0);
    void execMret(uint32_t = 0, uint32_t = 0, int32_t = 0);
    void execUret(uint32_t = 0, uint32_t = 0, int32_t = 0);
    void execSret(uint32_t = 0, uint32_t = 0, int32_t = 0);

    void execWfi(uint32_t = 0, uint32_t = 0, int32_t = 0);

    void execCsrrw(uint32_t rd, uint32_t rs1, int32_t csr);
    void execCsrrs(uint32_t rd, uint32_t rs1, int32_t csr);
    void execCsrrc(uint32_t rd, uint32_t rs1, int32_t csr);
    void execCsrrwi(uint32_t rd, uint32_t imm, int32_t csr);
    void execCsrrsi(uint32_t rd, uint32_t imm, int32_t csr);
    void execCsrrci(uint32_t rd, uint32_t imm, int32_t csr);

    void execLb(uint32_t rd, uint32_t rs1, int32_t imm);
    void execLh(uint32_t rd, uint32_t rs1, int32_t imm);
    void execLw(uint32_t rd, uint32_t rs1, int32_t imm);
    void execLbu(uint32_t rd, uint32_t rs1, int32_t imm);
    void execLhu(uint32_t rd, uint32_t rs1, int32_t imm);

    void execSb(uint32_t rs1, uint32_t rs2 /*byte*/, int32_t imm);
    void execSh(uint32_t rs1, uint32_t rs2 /*half*/, int32_t imm);
    void execSw(uint32_t rs1, uint32_t rs2 /*word*/, int32_t imm);

    void execMul(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execMulh(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execMulhsu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execMulhu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execDiv(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execDivu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execRem(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execRemu(uint32_t rd, uint32_t rs1, int32_t rs2);

    // rv64i
    void execLwu(uint32_t rd, uint32_t rs1, int32_t imm);
    void execLd(uint32_t rd, uint32_t rs1, int32_t imm);
    void execSd(uint32_t rd, uint32_t rs1, int32_t imm);
    void execSlliw(uint32_t rd, uint32_t rs1, int32_t amount);
    void execSrliw(uint32_t rd, uint32_t rs1, int32_t amount);
    void execSraiw(uint32_t rd, uint32_t rs1, int32_t amount);
    void execAddiw(uint32_t rd, uint32_t rs1, int32_t imm);
    void execAddw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSubw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSllw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSrlw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSraw(uint32_t rd, uint32_t rs1, int32_t rs2);

    // rv64m
    void execMulw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execDivw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execDivuw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execRemw(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execRemuw(uint32_t rd, uint32_t rs1, int32_t rs2);

    // rv32f
    void execFlw(uint32_t rd, uint32_t rs1, int32_t imm);
    void execFsw(uint32_t rd, uint32_t rs1, int32_t imm);
    void execFmadd_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmsub_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFnmsub_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFnmadd_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFadd_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsub_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmul_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFdiv_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsqrt_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsgnj_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsgnjn_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsgnjx_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmin_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmax_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_w_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_wu_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmv_x_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFeq_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFlt_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFle_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFclass_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_s_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_s_wu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmv_w_x(uint32_t rd, uint32_t rs1, int32_t rs2);

    // rv32f + rv64
    void execFcvt_l_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_lu_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_s_l(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_s_lu(uint32_t rd, uint32_t rs1, int32_t rs2);

    // rv32d
    void execFld(uint32_t rd, uint32_t rs1, int32_t imm);
    void execFsd(uint32_t rd, uint32_t rs1, int32_t imm);
    void execFmadd_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmsub_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFnmsub_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFnmadd_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFadd_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsub_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmul_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFdiv_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsgnj_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsgnjn_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsgnjx_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmin_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmax_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_d_s(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_s_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFsqrt_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFle_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFlt_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFeq_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_w_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_wu_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_d_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_d_wu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFclass_d(uint32_t rd, uint32_t rs1, int32_t rs2);

    // rv32d + rv64
    void execFcvt_l_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_lu_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_d_l(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFcvt_d_lu(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmv_d_x(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execFmv_x_d(uint32_t rd, uint32_t rs1, int32_t rs2);

    // atomic
    void execAmoadd_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmoswap_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execLr_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSc_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmoxor_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmoor_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmomin_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmomax_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmominu_w(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmomaxu_w(uint32_t rd, uint32_t rs1, int32_t rs2);

    // atmomic + rv64
    void execAmoadd_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmoswap_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execLr_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execSc_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmoxor_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmoor_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmomin_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmomax_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmominu_d(uint32_t rd, uint32_t rs1, int32_t rs2);
    void execAmomaxu_d(uint32_t rd, uint32_t rs1, int32_t rs2);


  private:

    // We model store buffer in order to undo store effects after an
    // imprecise store exception.
    struct StoreInfo
    {
      StoreInfo(unsigned size = 0, size_t addr = 0, uint64_t data = 0,
		uint64_t prevData = 0)
	: size_(size), addr_(addr), newData_(data), prevData_(prevData)
      { }

      unsigned size_ = 0;  // 0: invalid object.
      size_t addr_ = 0;
      uint64_t newData_ = 0;
      uint64_t prevData_ = 0;
    };

    // We model non-blocking load buffer in order to undo load
    // effects after an imprecise load exception.
    struct LoadInfo
    {
      LoadInfo(unsigned size = 0, size_t addr = 0, unsigned regIx = 0,
	       uint64_t prevData = 0)
	: size_(size), addr_(addr), regIx_(regIx), prevData_(prevData)
      { }

      unsigned size_ = 0;  // 0: invalid object.
      size_t addr_ = 0;
      unsigned regIx_ = 0;
      uint64_t prevData_ = 0;
    };

    void putInLoadQueue(unsigned size,size_t addr, unsigned regIx,
			uint64_t prevData);

    void putInStoreQueue(unsigned size, size_t addr, uint64_t newData,
			 uint64_t prevData);

  private:

    unsigned hartId_ = 0;        // Hardware thread id.
    Memory memory_;
    IntRegs<URV> intRegs_;       // Integer register file.
    CsRegs<URV> csRegs_;         // Control and status registers.
    FpRegs<double> fpRegs_;      // Floating point registers.
    bool rv64_ = sizeof(URV)==8; // True if 64-bit base (RV64I).
    bool rva_ = false;           // True if extension A (atomic) enabled.
    bool rvc_ = true;            // True if extension C (compressed) enabled.
    bool rvd_ = false;           // True if extension D (double fp) enabled.
    bool rvf_ = false;           // True if extension F (single fp) enabled.
    bool rvm_ = true;            // True if extension M (mul/div) enabled.
    bool rvs_ = false;           // True if extension S (supervison-mode) enabled.
    bool rvu_ = false;           // True if extension U (user-mode) enabled.
    URV pc_ = 0;                 // Program counter. Incremented by instr fetch.
    URV currPc_ = 0;             // Addr instr being executed (pc_ before fetch).
    URV resetPc_ = 0;            // Pc to use on reset.
    URV stopAddr_ = 0;           // Pc at which to stop the simulator.
    bool stopAddrValid_ = false; // True if stopAddr_ is valid.
    URV toHost_ = 0;             // Writing to this stops the simulator.
    bool toHostValid_ = false;   // True if toHost_ is valid.
    URV conIo_ = 0;              // Writing a byte to this writes to console.
    bool conIoValid_ = false;    // True if conIo_ is valid.

    URV nmiPc_ = 0;              // Non-maskable interrupt handler address.
    bool nmiPending_ = false;
    bool mdseacChanged_ = false; // mdseac can be changed once between resets.
    URV nmiCause_ = 0;

    // These should be cleared before each instruction when triggers enabled.
    bool ldStException_ = 0;     // True if there is a load/store exception.
    bool csrException_ = 0;      // True if there is a CSR related exception.
    bool triggerTripped_ = 0;    // True if a trigger trips.

    bool hasLr_ = false;         // True if there is a load reservation.
    URV lrAddr_ = 0;             // Address of load reservation.

    bool lastBranchTaken_ = false; // Useful for performance counters
    bool misalignedLdSt_ = false;  // Usefule for peformance counters

    uint64_t retiredInsts_ = 0;  // Proxy for minstret CSR.
    uint64_t cycleCount_ = 0;    // Proxy for mcycle CSR.
    uint64_t counter_ = 0;       // Retired instruction count.
    uint64_t instCountLim_ = ~uint64_t(0);
    uint64_t exceptionCount_ = 0;
    uint64_t interruptCount_ = 0;
    bool forceAccessFail_ = false;  // Force load/store access fault.
    bool forceFetchFail_ = false;   // Forece fetch access fault.
    bool instFreq_ = false;         // Collection instruction frequencies.
    bool enableCounters_ = false;   // Enable performance monitors.
    bool prevCountersCsrOn_ = true;
    bool countersCsrOn_ = true;     // True when counters CSR is set to 1.
    bool enableTriggers_ = false;   // Enable debug triggers.
    bool enableGdb_ = false;        // Enable gdb mode.

    bool traceLoad_ = false;        // Trace addr of load inst if true.
    URV loadAddr_ = 0;              // Address of data of most recent load inst.
    bool loadAddrValid_ = false;    // True if loadAddr_ valid.

    // We keep track of the last committed 4 stores so that we can
    // revert in the case of an imprecise store exception.
    std::vector<StoreInfo> storeQueue_;
    unsigned maxStoreQueueSize_ = 4;

    // We keep track of the last committed 4 loads so that we can
    // revert in the case of an imprecise load exception.
    std::vector<LoadInfo> loadQueue_;
    unsigned maxLoadQueueSize_ = 4;

    PrivilegeMode privMode_ = PrivilegeMode::Machine; // Privilige mode.
    bool debugMode_ = false;                          // True on debug mode.
    bool debugStep_ = false;                          // True if doing a debug step.
    bool debugStepIe_ = false;                        // Debug step interrupt enable.
    bool ebreakInst_ = false;                         // True if ebreak was executed.
    bool storeErrorRollback_ = false;
    bool loadErrorRollback_ = false;
    unsigned mxlen_ = 8*sizeof(URV);
    FILE* consoleOut_ = nullptr;

    // FP instructions have additional operands besides rd, rs1, rs2 and imm.
    // We pass them in here.
    RoundingMode instRoundingMode_ = RoundingMode::NearestEven;
    unsigned instRs3_ = 0;

    // AMO instructions have additional operands: rl and aq.
    bool amoAq_ = false;
    bool amoRl_ = false;

    InstInfoTable instTable_;
    std::vector<InstProfile> instProfileVec_; // Instruction frequency
  };
}

