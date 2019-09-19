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
#include <iosfwd>
#include <type_traits>
#include "InstId.hpp"
#include "InstEntry.hpp"
#include "IntRegs.hpp"
#include "CsRegs.hpp"
#include "FpRegs.hpp"
#include "Memory.hpp"
#include "InstProfile.hpp"
#include "DecodedInst.hpp"

namespace WdRiscv
{

  /// Thrown by the simulator when a stop (store to to-host) is seen
  /// or when the target program reaches the exit system call.
  class CoreException : public std::exception
  {
  public:

    enum Type { Stop, Exit };

    CoreException(Type type, const char* message = "", uint64_t address = 0,
		  uint64_t value = 0)
      : type_(type), msg_(message), addr_(address), val_(value)
    { }

    const char* what() const noexcept override
    { return msg_; }

    Type type() const
    { return type_; }

    uint64_t address() const
    { return addr_; }

    uint64_t value() const
    { return val_; }

  private:
    Type type_ = Stop;
    const char* msg_ = "";
    uint64_t addr_ = 0;
    uint64_t val_ = 0;
  };
    

  /// Changes made by the execution of one instruction. Useful for
  /// test pattern generation.
  struct ChangeRecord
  {
    void clear()
    { *this = ChangeRecord(); }

    uint64_t newPc = 0;        // Value of pc after instruction execution.
    bool hasException = false; // True if instruction causes an exception.

    bool hasIntReg = false;    // True if there is an integer register change.
    unsigned intRegIx = 0;     // Number of changed integer register if any.
    uint64_t intRegValue = 0;  // Value of changed integer register if any.

    bool hasFpReg = false;     // True if there is an FP register change.
    unsigned fpRegIx = 0;      // Number of changed fp register if any.
    uint64_t fpRegValue = 0;   // Value of changed fp register if any.
    
    unsigned memSize = 0;      // Size of changed memory (0 if none).
    size_t memAddr = 0;        // Address of changed memory if any.
    uint64_t memValue = 0;     // Value of changed memory if any.

    // An exception will result in changing multiple CSRs.
    std::vector<CsrNumber> csrIx;   // Numbers of changed CSRs if any.
    std::vector<uint64_t> csrValue; // Values of changed CSRs if any.
  };


  /// Model a RISCV hart with integer registers of type URV (uint32_t
  /// for 32-bit registers and uint64_t for 64-bit registers).
  template <typename URV>
  class Hart
  {
  public:
    
    /// Signed register type corresponding to URV. For example, if URV
    /// is uint32_t, then SRV will be int32_t.
    typedef typename std::make_signed_t<URV> SRV;

    /// Constructor: Define a hart with the given integer register
    /// cont, the givel local hart id (id within core) and associate
    /// it with the given memory.
    Hart(unsigned localHartId, Memory& memory, unsigned intRegCount);

    /// Destructor.
    ~Hart();

    /// Return count of integer registers.
    size_t intRegCount() const
    { return intRegs_.size(); }

    /// Return the name of the given integer register. Return an
    /// abi-name (e.g. sp) if abi names are enabled.
    std::string intRegName(unsigned regIx) const
    { return intRegs_.regName(regIx, abiNames_); }

    /// Return the name of the given floating point register. Return an
    /// abi-name (e.g. fa0) if abi names are enabled.
    std::string fpRegName(unsigned regIx) const
    { return fpRegs_.regName(regIx, abiNames_); }

    /// Return the name (e.g. x1) or the abi-name (e.g. ra) of the
    /// given integer register.
    std::string intRegName(unsigned regIx, bool abiName) const
    { return intRegs_.regName(regIx, abiName); }

    /// Return count of floating point registers. Return zero if
    /// extension f is not enabled.
    size_t fpRegCount() const
    { return isRvf()? fpRegs_.size() : 0; }

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

    /// Set val to the value of integer register reg returning true on
    /// success. Return false leaving val unmodified if reg is out of
    /// bounds. If successful, set name to the register name.
    bool peekIntReg(unsigned reg, URV& val, std::string& name) const;

    /// Return to the value of integer register reg which must not be 
    /// out of bounds (otherwise we trigger an assert).
    URV peekIntReg(unsigned reg) const;

    /// Set the given integer register, reg, to the given value
    /// returning true on success. Return false if reg is out of
    /// bound.
    bool pokeIntReg(unsigned reg, URV val);

    /// Set val to the bit-pattern of the value of the floating point
    /// register returning true on success. Return false leaving val
    /// unmodified if reg is out of bounds of if no floating point
    /// extension is enabled.
    bool peekFpReg(unsigned reg, uint64_t& val) const;

    /// Set the given FP register, reg, to the given value returning
    /// true on success. Return false if reg is out of bound.
    bool pokeFpReg(unsigned reg, uint64_t val);

    /// Set val to the value of the control and status register csr
    /// returning true on success. Return false leaving val unmodified
    /// if csr is out of bounds.
    bool peekCsr(CsrNumber csr, URV& val) const;

    /// Set val, reset, writeMask, and pokeMask respectively to the
    /// value, reset-value, write-mask and poke-mask of the control
    /// and status register csr returning true on success. Return
    /// false leaving parameters unmodified if csr is out of bounds.
    bool peekCsr(CsrNumber csr, URV& val, URV& reset, URV& writeMask,
		 URV& pokeMask) const;

    /// Set val/name to the value/name of the control and status
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

    /// Find the floating point with the given name.  Set num to the
    /// number of the corresponding register if found. Return true on
    /// success and false if no such register.
    bool findFpReg(const std::string& name, unsigned& num) const;

    /// Find the control and status register with the given name
    /// (which may represent an integer or a symbolic name). Return
    /// pointer to CSR on success and nullptr if no such register.
    Csr<URV>* findCsr(const std::string& name);

    /// Find the control and status register with the given number.
    /// Return pointer to CSR on success and nullptr if no such
    /// register.
    const Csr<URV>* findCsr(CsrNumber number)
    { return csRegs_.findCsr(number); }

    /// Configure given CSR. Return true on success and false if
    /// no such CSR.
    bool configCsr(const std::string& name, bool implemented,
		   URV resetValue, URV mask, URV pokeMask,
		   bool isDebug, bool shared);

    /// Define a new CSR (beyond the standard CSRs defined by the
    /// RISCV spec). Return true on success and false if name/number
    /// already in use.
    bool defineCsr(const std::string& name, CsrNumber number,
		   bool implemented, URV resetValue, URV mask,
		   URV pokeMask, bool isDebug);

    /// Configure given trigger with given reset values, write and
    /// poke masks. Return true on success and false on failure.
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

    /// Fill given vector (cleared on entry) with the numbers of
    /// implemented CSRs.
    void getImplementedCsrs(std::vector<CsrNumber>& vec) const;

    /// Reset this hart. Reset all CSRs to their initial value. Reset all
    /// integer registers to zero. Reset PC to the reset-pc as
    /// defined by defineResetPc (default is zero).
    void reset(bool resetMemoryMappedRegister = false);

    /// Run fetch-decode-execute loop. If a stop address (see
    /// setStopAddress) is defined, stop when the program counter
    /// reaches that address. If a tohost address is defined (see
    /// setToHostAdress), stop when a store instruction writes into
    /// that address. If given file is non-null, then print to that
    /// file a record for each executed instruction.
    bool run(FILE* file = nullptr);

    /// Run one instruction at the current program counter. Update
    /// program counter. If file is non-null then print thereon
    /// tracing information related to the executed instruction.
    void singleStep(FILE* file = nullptr);

    /// Determine the effect of instruction fetching and discarding n
    /// bytes (where n is the instruction size of the given
    /// instruction) from memory and then executing the given
    /// instruction without actually changing the state of the hart or
    /// the memory. Return true if the instruction would execute
    /// without an exception. Return false otherwise. In either case
    /// set the record fields corresponding to the resources that
    /// would have been changed by the execution of the instruction.
    bool whatIfSingleStep(URV programCounter, uint32_t inst,
			  ChangeRecord& record);

    /// Similar to the above method but without fetching anything from
    /// from instruction memory (in other words, this variant will
    /// never cause an misaligned/instruction-access-fault
    /// exception).
    bool whatIfSingleStep(uint32_t inst, ChangeRecord& record);

    /// Similar to the above but without fetching register operands.
    /// Register operand values are obtained from the given decoded
    /// instruction object.
    bool whatIfSingStep(const DecodedInst& inst, ChangeRecord& record);

    /// Run until the program counter reaches the given address. Do
    /// execute the instruction at that address. If file is non-null
    /// then print thereon tracing information after each executed
    /// instruction. Similar to method run with respect to tohost.
    bool runUntilAddress(URV address, FILE* file = nullptr);

    /// Helper to runUntiAddress: Same as runUntilAddress but does not
    /// print run-time and instructions per second.
    bool untilAddress(URV address, FILE* file = nullptr);

    /// Define the program counter value at which the run method will
    /// stop.
    void setStopAddress(URV address)
    { stopAddr_ = address; stopAddrValid_ = true; }

    /// Undefine stop address (see setStopAddress).
    void clearStopAddress()
    { stopAddrValid_ = false; }

    /// Define the memory address corresponding to console io. Reading/writing
    /// a byte (lb/sb) from/to that address reads/writes a byte to/from
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

    /// Disassemble given instruction putting results on the given
    /// stream.
    void disassembleInst(const DecodedInst& di, std::ostream&);

    /// Disassemble given instruction putting results into the given
    /// string.
    void disassembleInst(uint32_t inst, std::string& str);

    /// Disassemble given instruction putting results into the given
    /// string.
    void disassembleInst(const DecodedInst& di, std::string& str);

    /// Decode given instruction returning a pointer to the
    /// instruction information and filling op0, op1 and op2 with the
    /// corresponding operand specifier values. For example, if inst
    /// is the instruction code for "addi r3, r4, 77", then the
    /// returned value would correspond to addi and op0, op1 and op2
    /// will be set to 3, 4, and 77 respectively. If an instruction
    /// has fewer than 3 operands then only a subset of op0, op1 and
    /// op2 will be set. If inst is not a valid instruction , then we
    /// return a reference to the illegal-instruction info.
    const InstEntry& decode(uint32_t inst, uint32_t& op0, uint32_t& op1,
			    uint32_t& op2, uint32_t& op3);

    /// Similar to the above decode method but with decoded data
    /// placed in the given DecodedInst object.
    void decode(URV address, uint32_t inst, DecodedInst& decodedInst);

    /// Load the given hex file and set memory locations accordingly.
    /// Return true on success. Return false if file does not exists,
    /// cannot be opened or contains malformed data.
    /// File format: A line either contains @address where address
    /// is a hexadecimal memory address or one or more space separated
    /// tokens each consisting of two hexadecimal digits.
    bool loadHexFile(const std::string& file);

    /// Load the given ELF file and place ints contents in memory.
    /// Return true on success. Return false if file does not exists,
    /// cannot be opened or contains malformed data. On success, set
    /// entryPoint to the program entry-point of the loaded file. If
    /// the to-host-address is not set then set it to the value
    /// corresponding to the to-host-symbol if such that symbol is
    /// found in the ELF file.
    bool loadElfFile(const std::string& file, size_t& entryPoint);

    /// Locate the given ELF symbol (symbols are collected for every
    /// loaded ELF file) returning true if symbol is found and false
    /// otherwise. Set value to the corresponding value if symbol is
    /// found.
    bool findElfSymbol(const std::string& symbol, ElfSymbol& value) const
    { return memory_.findElfSymbol(symbol, value); }

    /// Locate the ELF function cotaining the give address returning true
    /// on success and false on failure.  If successful set name to the
    /// corresponding function name and symbol to the corresponding symbol
    /// value.
    bool findElfFunction(URV addr, std::string& name, ElfSymbol& value) const
    { return memory_.findElfFunction(addr, name, value); }

    /// Print the ELF symbols on the given stream. Output format:
    /// <name> <value>
    void printElfSymbols(std::ostream& out) const
    { memory_.printElfSymbols(out); }

    /// Set val to the value of the memory byte at the given address
    /// returning true on success and false if address is out of
    /// bounds.
    bool peekMemory(size_t address, uint8_t& val) const
    { return memory_.readByte(address, val); }

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
    /// bounds, location not mapped, location not writable etc...)
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

    /// Define value of program counter after a non-maskable interrupt.
    void defineNmiPc(URV addr)
    { nmiPc_ = addr; }

    /// Clear/set pending non-maskable-interrupt.
    void setPendingNmi(NmiCause cause = NmiCause::UNKNOWN);

    /// Clear pending non-maskable-interrupt.
    void clearPendingNmi();

    /// Define address to which a write will stop the simulator. An
    /// sb, sh, or sw instruction will stop the simulator if the write
    /// address of he instruction is identical to the given address.
    void setToHostAddress(size_t address);

    /// Special target program symbol writing to which stops the
    /// simulated program.
    void setTohostSymbol(const std::string& sym)
    { toHostSym_ = sym; }

    /// Undefine address to which a write will stop the simulator
    void clearToHostAddress();

    /// Set address to the special address writing to which stops the
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

    /// Support for tracing: Return the index of the floating point
    /// register written by the last executed instruction. Return -1
    /// it no FP register was written.
    int lastFpReg() const;

    /// Support for tracing: Fill the csrs vector with the
    /// register-numbers of the CSRs written by the execution of the
    /// last instruction. CSRs modified as a side effect (e.g. mcycle
    /// and minstret) are not included. Fill the triggers vector with
    /// the number of the debug-trigger registers written by the
    /// execution of the last instruction.
    void lastCsr(std::vector<CsrNumber>& csrs,
		 std::vector<unsigned>& triggers) const;

    /// Support for tracing: Fill the addresses and words vectors with
    /// the addresses of the memory words modified by the last
    /// executed instruction and their corresponding values.
    void lastMemory(std::vector<size_t>& addresses,
		    std::vector<uint32_t>& words) const;

    /// Return data address of last executed load instruction.
    URV lastLoadAddress() const
    { return loadAddr_; }

    /// Read instruction at given address. Return true on success and
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
    { instCounter_ = count; }

    /// Get executed instruction count.
    uint64_t getInstructionCount() const 
    { return instCounter_; }

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
    void finishCcmConfig()
    { memory_.finishCcmConfig(); }

    /// Turn off all fetch access (except in ICCM regions) then turn
    /// it on only in the pages overlapping the given address windows.
    /// Return true on success and false on failure (invalid window
    /// entry).  Do nothing returning true if the windows vector is
    /// empty.
    bool configMemoryFetch(const std::vector< std::pair<URV,URV> >& windows);

    /// Turn off all data access (except in DCCM/PIC regions) then
    /// turn it on only in the pages overlapping the given address
    /// windows. Return true on success and false on failure (invalid
    /// window entry). Do nothing returning true if the windows vector
    /// is empty.
    bool configMemoryDataAccess(const std::vector< std::pair<URV,URV> >& windows);

    /// Direct this hart to take an instruction access fault exception
    /// within the next singleStep invocation.
    void postInstAccessFault(URV offset)
    { forceFetchFail_ = true; forceFetchFailOffset_ = offset; }

    /// Direct this hart to take a data access fault exception within
    /// the subsequent singleStep invocation executing a load/store
    /// instruction or take an NMI (double-bit-ecc) within the
    /// subsequent interrupt if fast-interrupt is enabled.
    void postDataAccessFault(URV offset);

    /// Enable printing of load-instruction data address in
    /// instruction trace mode.
    void setTraceLoad(bool flag)
    { traceLoad_ = flag; }

    /// Return count of traps (exceptions or interrupts) seen by this
    /// hart.
    uint64_t getTrapCount() const
    { return exceptionCount_ + interruptCount_; }

    /// Return count of exceptions seen by this hart.
    uint64_t getExceptionCount() const
    { return exceptionCount_; }

    /// Return count of interrupts seen by this hart.
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
    bool applyLoadException(URV address, unsigned tag, unsigned& matchCount);

    /// This supports the test-bench. Mark load-queue entry matching
    /// given address as completed and remove it from the queue. Set
    /// match count to 1 if matching entry is found and zero
    /// otherwise. Return true if matching entry found. The testbench
    /// will invoke this only for loads where the destination register
    /// is updated.
    bool applyLoadFinished(URV address, unsigned tag, unsigned& matchCount);

    /// Enable processing of imprecise load exceptions.
    void enableLoadExceptions(bool flag)
    { loadQueueEnabled_ = flag; }

    /// Set load queue size (used when load exceptions are enabled).
    void setLoadQueueSize(unsigned size)
    { maxLoadQueueSize_ = size; }

    /// Enable collection of instruction frequencies.
    void enableInstructionFrequency(bool b);

    /// Enable expedited dispatch of external interrupt handler: Instead of
    /// setting pc to the external interrupt handler, we set it to the
    /// specific entry associated with the external interrupt id.
    void enableFastInterrupts(bool b)
    { fastInterrupts_ = b; }

    /// Enable/disable the zbb (bit manipulation base) extension. When
    /// disbaled all the instructions in zbb extension result in an
    /// illegal instruction exception.
    void enableRvzbb(bool flag)
    { rvzbb_ = flag; }

    /// Enable/disable the zbs (bit manipulation single)
    /// extension. When disbaled all the instructions in zbs extension
    /// result in an illegal instruction exception.
    void enableRvzbs(bool flag)
    { rvzbs_ = flag; }

    /// Put this hart in debug mode setting the DCSR cause field to
    /// the given cause.
    void enterDebugMode(DebugModeCause cause, URV pc);

    /// Put this hart in debug mode setting the DCSR cause field to
    /// either DEBUGGER or SETP depending on the step bit of DCSR.
    /// given cause.
    void enterDebugMode(URV pc);

    /// True if in debug mode.
    bool inDebugMode() const
    { return debugMode_; }

    /// True if in debug-step mode.
    bool inDebugStepMode() const
    { return debugStepMode_; }

    /// Take this hart out of debug mode.
    void exitDebugMode();

    /// Enable/disable imprecise store error rollback. This is useful
    /// in test-bench server mode.
    void enableStoreErrorRollback(bool flag)
    { storeErrorRollback_ = flag; }

    /// Enable/disable imprecise load error rollback. This is useful
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

    /// Enable use of ABI register names (e.g. sp instead of x2) in
    /// instruction disassembly.
    void enableAbiNames(bool flag)
    { abiNames_ = flag; }

    /// Return true if ABI register names are enabled.
    bool abiNames() const
    { return abiNames_; }

    /// Enable emulation of newlib system calls.
    void enableNewlib(bool flag)
    { newlib_ = flag; }

    /// Enable emulation of Linux system calls.
    void enableLinux(bool flag)
    { linux_ = flag; }

    /// For Linux emulation: Set initial target program break to the
    /// RISCV page address larger than or equal to the given address.
    void setTargetProgramBreak(URV addr);

    /// For Linux emulation: Put the program arguments on the stack
    /// suitable for calling the target program main from _start.
    /// Return true on success and false on failure (not all stack
    /// area required is writable).
    bool setTargetProgramArgs(const std::vector<std::string>& args);

    /// Return true if given address is in the data closed coupled
    /// memory of this hart.
    bool isAddressInDccm(size_t addr) const
    { return memory_.isAddrInDccm(addr); }

    /// Return true if given data (ld/st) address is external to the hart.
    bool isDataAddressExternal(size_t addr) const
    { return memory_.isDataAddrExternal(addr); }

    /// Return true if rv32f (single precision floating point)
    /// extension is enabled in this hart.
    bool isRvf() const
    { return rvf_; }

    /// Return true if rv64d (double precision floating point)
    /// extension is enabled in this hart.
    bool isRvd() const
    { return rvd_; }

    /// Return true if rv64 (64-bit option) extension is enabled in
    /// this hart.
    bool isRv64() const
    { return rv64_; }

    /// Return true if rvm (multiply/divide) extension is enabled in
    /// this hart.
    bool isRvm() const
    { return rvm_; }

    /// Return true if rvc (compression) extension is enabled in this
    /// hart.
    bool isRvc() const
    { return rvc_; }

    /// Return true if rva (atomic) extension is enabled in this hart.
    bool isRva() const
    { return rva_; }

    /// Return true if rvu (user-mode) extension is enabled in this
    /// hart.
    bool isRvs() const
    { return rvs_; }

    /// Return true if rvu (user-mode) extension is enabled in this
    /// hart.
    bool isRvu() const
    { return rvu_; }

    /// Return true if zbb extension is enabled in this hart.
    bool isRvzbb() const
    { return rvzbb_; }

    /// Return true if zbs extension is enabled in this hart.
    bool isRvzbs() const
    { return rvzbs_; }

    /// Return true if current program is considered finihsed (either
    /// reached stop address or executed exit limit).
    bool hasTargetProgramFinished() const
    { return targetProgFinished_; }

    /// Mark target program as finished/non-finished based on flag.
    void setTargetProgramFinished(bool flag)
    { targetProgFinished_ = flag; }

    /// Make atomic memory operations illegal/legal outside of the DCCM
    /// region based on the value of flag (true/false).
    void setAmoIllegalOutsideDccm(bool flag)
    { amoIllegalOutsideDccm_ = flag; }

    /// Make load/store instructions take an exception if the base
    /// address (value in rs1) and the effective address refer to
    /// regions of different types.
    void setEaCompatibleWithBase(bool flag)
    { eaCompatWithBase_ = flag; }

    size_t getMemorySize() const
    { return memory_.size(); }

    /// Copy memory region configuration from other processor.
    void copyMemRegionConfig(const Hart<URV>& other);

    /// Return true if hart was put in run state after reset. Hart 0
    /// is automatically in run state after reset. If mhartstart CSR
    /// exists, then each remaining hart must be explicitly started by
    /// hart 0 by writing to the corresponding bit in that CSR. This
    /// is special for WD.
    bool isStarted() const
    { return hartStarted_; }

    /// Mark this hart as started.
    void setStarted(bool flag)
    { hartStarted_ = flag; }

    /// Return the local (within a core) hart-id of this hart.  Local
    /// hart ids are dense and start at zero.
    unsigned localHartId()
    { return localHartId_; }

    /// Tie the shared CSRs in this hart to the corresponding CSRs in
    /// the target hart making them share the same location for their
    /// value.
    void tieSharedCsrsTo(Hart<URV>& target)
    { return csRegs_.tieSharedCsrsTo(target.csRegs_); }

    /// Return true if non-maskable interrupts (NMIs) should be delivered
    /// to this hart.
    bool isNmiEnabled() const
    { return nmiEnabled_; }

    /// Enable delivery of NMIs to this hart.
    bool enableNmi(bool flag)
    { return nmiEnabled_ = flag; }

    /// Record given CSR number for later reporting of CSRs modified by
    /// an instruction.
    void recordCsrWrite(CsrNumber csr)
    { csRegs_.recordWrite(csr); }

  protected:

    /// Helper to run method: Run until toHost is written or until
    /// exit is called.
    bool simpleRun();

    /// Helper to decode. Used for compressed instructions.
    const InstEntry& decode16(uint16_t inst, uint32_t& op0, uint32_t& op1,
			      uint32_t& op2);

    /// Helper to whatIfSingleStep.
    void collectAndUndoWhatIfChanges(URV prevPc, ChangeRecord& record);

    /// Return the effective rounding mode for the currently executing
    /// floating point instruction.
    RoundingMode effectiveRoundingMode(RoundingMode instMode);

    /// Update the accrued floating point bits in the FCSR register.
    void updateAccruedFpBits();

    /// Undo the effect of the last executed instruction given that
    /// that a trigger has tripped.
    void undoForTrigger();

    /// Return true if the mie bit of the mstatus register is on.
    bool isInterruptEnabled() const
    { return csRegs_.isInterruptEnabled(); }

    /// Based on current trigger configurations, either take an
    /// exception returning false or enter debug mode returning true.
    bool takeTriggerAction(FILE* traceFile, URV epc, URV info,
			   uint64_t& counter, bool beforeTiming);

    /// Helper to load/store.
    bool misalignedAccessCausesException(URV addr, unsigned accessSize,
					 SecondaryCause& secCause) const;

    /// Helper to load methods: Initiate an exception with the given
    /// cause and data address.
    void initiateLoadException(ExceptionCause cause, URV addr,
			       SecondaryCause secCause);

    /// Helper to store methods: Initiate an exception with the given
    /// cause and data address.
    void initiateStoreException(ExceptionCause cause, URV addr,
				SecondaryCause secCause);

    /// Helper to load methods: Return true if base and effective
    /// address fall in regions of different types (with respect to io
    /// and cacheability).
    bool effectiveAndBaseAddrMismatch(URV base, URV addr);

    /// Helper to lb, lh, lw and ld. Load type should be int_8, int16_t
    /// etc... for signed byte, halfword etc... and uint8_t, uint16_t
    /// etc... for lbu, lhu, etc...
    /// Return true if the load is successful. Return false if an exception
    /// or a trigger is encoutered.
    template<typename LOAD_TYPE>
    bool load(uint32_t rd, uint32_t rs1, int32_t imm);

    /// Helper to load method: Return possible load exception (wihtout
    /// taking any exception).
    ExceptionCause determineLoadException(unsigned rs1, URV base, URV addr,
					  unsigned ldSize,
					  SecondaryCause& secCause);

    /// Helper to sb, sh, sw ... Sore type should be uint8_t, uint16_t
    /// etc... for sb, sh, etc...
    /// Return true if store is successful. Return false if an exception
    /// or a trigger is encoutered.
    template<typename STORE_TYPE>
    bool store(unsigned rs1, URV base, URV addr, STORE_TYPE value);

    /// Helper to store method: Return possible exception (wihtout
    /// taking any exception). Update stored value by doing memory
    /// mapped register masking.
    template<typename STORE_TYPE>
    ExceptionCause determineStoreException(unsigned rs1, URV base, URV addr,
					   STORE_TYPE& storeVal,
					   SecondaryCause& secCause);

    /// Helper to execLr. Load type should be int32_t, or int64_t.
    /// Return true if instruction is successful. Return false if an
    /// exception occurs or a trigger is tripped.
    template<typename LOAD_TYPE>
    bool loadReserve(uint32_t rd, uint32_t rs1);

    /// Helper to execSc. Store type should be uint32_t, or uint64_t.
    /// Return true if store is successful. Return false otherwise
    /// (exception or trigger or condition failed).
    template<typename STORE_TYPE>
    bool storeConditional(unsigned rs1, URV addr, STORE_TYPE value);

    /// Do a 64-bit wide load in one transaction. This is swerv
    /// specfic.
    bool wideLoad(unsigned rd, URV addr, unsigned ldSize);

    /// Do a 64-bit wide store in one transaction. This is swerv
    /// specfic.
    bool wideStore(URV addr, URV storeVal, unsigned storeSize);

    /// Helper to load methods. Check loads performed with stack
    /// pointer.  Return true if referenced bytes are all between the
    /// stack bottom and the stack pointer value excluding the stack
    /// pointer value.  Initiate an exception and return false
    /// otherwise.
    bool checkStackLoad(URV addr, unsigned loadSize);

    /// Helper to store methods. Check stores performed with stack
    /// pointer. Return true if referenced bytes are all between the
    /// stack bottom and the stack top excluding the stack top and
    /// false otherwise.
    bool checkStackStore(URV addr, unsigned storeSize);

    /// Helper to CSR instructions. Keep minstret and mcycle up to date.
    void preCsrInstruction(CsrNumber csr);

    /// Helper to CSR instructions: Write csr and integer register if csr
    /// is writeable.
    void doCsrWrite(CsrNumber csr, URV csrVal, unsigned intReg,
		    URV intRegVal);

    /// Helper to CSR instructions: Read csr register returning true
    /// on success and false on failure (csr does not exist or is not
    /// accessible).  is writeable.
    bool doCsrRead(CsrNumber csr, URV& csrVal);

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

    /// Return true if this hart has one or more active debug
    /// triggers.
    bool hasActiveTrigger() const
    { return (enableTriggers_ and csRegs_.hasActiveTrigger()); }

    /// Return true if this hart has one or more active debug instruction
    /// (execute) triggers.
    bool hasActiveInstTrigger() const
    { return (enableTriggers_ and csRegs_.hasActiveInstTrigger()); }

    /// Collect instruction stats (for instruction profile and/or
    /// performance monitors).
    void accumulateInstructionStats(const DecodedInst&);

    /// Update performance counters: Enabled counters tick up
    /// according to the events associated with the most recent
    /// retired instruction.
    void updatePerformanceCounters(uint32_t inst, const InstEntry&,
				   uint32_t op0, uint32_t op1);

    /// Fetch an instruction. Return true on success. Return false on
    /// fail (in which case an exception is initiated). May fetch a
    /// compressed instruction (16-bits) in which case the upper 16
    /// bits are not defined (may contain arbitrary values).
    bool fetchInst(URV address, uint32_t& instr);

    /// Fetch an instruction given that a trigger has tripped. Return
    /// true on success. Return false on a a fail in which case either
    /// a trigger exception is initiated (as opposed to an
    /// instruction-fail exception).
    bool fetchInstPostTrigger(URV address, uint32_t& inst, FILE* trace);

    /// Write trace information about the given instruction to the
    /// given file. This is assumed to be called after instruction
    /// execution. Tag is the record tag (the retired instruction
    /// count after instruction is executed). Tmp is a temporary
    /// string (for performance).
    void printInstTrace(const DecodedInst& di, uint64_t tag, std::string& tmp,
			FILE* out, bool interrupt = false);

    /// Variant of the above for cases where the trace is printed
    /// before decode. If the instruction is not available then a zero
    /// (illegal) should be passed.
    void printInstTrace(uint32_t instruction, uint64_t tag, std::string& tmp,
			FILE* out, bool interrupt = false);

    /// Start a synchronous exceptions.
    void initiateException(ExceptionCause cause, URV pc, URV info,
			   SecondaryCause secCause = SecondaryCause::NONE);

    /// Start an asynchronous exception (interrupt).
    void initiateInterrupt(InterruptCause cause, URV pc);

    /// Start an asynchronous exception (interrupt) directly from the
    /// interrupt handler associated with the interrupt id. Return
    /// true on success. Return false if there is an error while
    /// accessing the table of interrupt handler addresses.
    void initiateFastInterrupt(InterruptCause cause, URV pc);

    /// Start a non-maskable interrupt.
    void initiateNmi(URV cause, URV pc);

    /// Code common to fast-interrupt and non-maskable-interrupt. Do
    /// interrupts without considering the delegation registers.
    void undelegatedInterrupt(URV cause, URV pcToSave, URV nextPc);

    /// If a non-maskable-interrupt is pending take it. If an external
    /// interrupt is pending and interrupts are enabled, then take
    /// it. Return true if an nmi or an interrupt is taken and false
    /// otherwise.
    bool processExternalInterrupt(FILE* traceFile, std::string& insStr);

    /// Helper to FP execution: Set the invalid bit in FCSR.
    void setInvalidInFcsr();

    /// Execute decoded instruction. Branch/jump instructions will
    /// modify pc_.
    void execute(const DecodedInst* di);

    /// Helper to decode: Decode instructions associated with opcode
    /// 1010011.
    const InstEntry& decodeFp(uint32_t inst, uint32_t& op0, uint32_t& op1,
			      uint32_t& op2, uint32_t& op3);

    /// Helper to disassembleInst32: Disassemble instructions
    /// associated with opcode 1010011.
    void disassembleFp(uint32_t inst, std::ostream& stream);

    /// Change machine state and program counter in reaction to an
    /// exception or an interrupt. Given pc is the program counter to
    /// save (address of instruction causing the asynchronous
    /// exception or the instruction to resume after asynchronous
    /// exception is handled). The info value holds additional
    /// information about an exception.
    void initiateTrap(bool interrupt, URV cause, URV pcToSave, URV info,
		      URV secCause);

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

    /// Implement some newlib/Linux system calls in the simulator.
    URV emulateSyscall();

    /// Check address associated with an atomic memory operation (AMO)
    /// instruction. Return true if AMO accsess is allowed. Return false
    /// trigerring an exception if address is misaligned or if it is out
    /// of DCCM range in DCCM-only mode.
    bool validateAmoAddr(uint32_t rs1, URV addr, unsigned accessSize);

    /// Do the load value part of a word-sized AMO instruction. Return
    /// true on success putting the loaded value in val. Return false
    /// if a trigger tripped or an exception took place in which case
    /// val is not modified.
    bool amoLoad32(uint32_t rs1, URV& val);

    /// Do the load value part of a double-word-sized AMO
    /// instruction. Return true on success putting the loaded value
    /// in val. Return false if a trigger tripped or an exception took
    /// place in which case val is not modified.
    bool amoLoad64(uint32_t rs1, URV& val);

    /// Invalidate cache entries overlapping the bytes written by a
    /// store.
    void invalidateDecodeCache(URV addr, unsigned storeSize);

    /// Update stack checker paramters after a write/poke to a CSR.
    void updateStackChecker();

    /// Enable disable wide load/store mode (64-bit on 32-bit machine).
    void enableWideLdStMode(bool flag)
    { wideLdSt_ = flag; }

    /// Helper to shift/bit execute insrtuction with immediate
    /// operands: Signal an illegal instruction if immediate value is
    /// greater than XLEN-1 returning false; otherwise return true.
    bool checkShiftImmediate(URV imm);

    /// Helper to the run mehtods: Log (on the standard error) the
    /// cause of a stop signaled with an exception. Return true if
    /// program finished successfully, return false otherwise.  If
    /// traceFile is non-null, then trace the instruction that caused
    /// the stop.
    bool logStop(const CoreException& ce, uint64_t instCount, FILE* traceFile);

    // rs1: index of source register (value range: 0 to 31)
    // rs2: index of source register (value range: 0 to 31)
    // rd: index of destination register (value range: 0 to 31)
    // offset: singed integer.
    // imm: signed integer.
    // All immediate and offset values are assumed to be already unpacked
    // and sign extended if necessary.

    // The program counter is adjusted (size of current instruction
    // added) before any of the following exec methods are called. To
    // get the address before adjustment, use currPc_.

    void execBeq(const DecodedInst*);
    void execBne(const DecodedInst*);
    void execBlt(const DecodedInst*);
    void execBltu(const DecodedInst*);
    void execBge(const DecodedInst*);
    void execBgeu(const DecodedInst*);
    void execJalr(const DecodedInst*);
    void execJal(const DecodedInst*);
    void execLui(const DecodedInst*);
    void execAuipc(const DecodedInst*);
    void execAddi(const DecodedInst*);
    void execSlli(const DecodedInst*);
    void execSlti(const DecodedInst*);
    void execSltiu(const DecodedInst*);
    void execXori(const DecodedInst*);
    void execSrli(const DecodedInst*);
    void execSrai(const DecodedInst*);
    void execOri(const DecodedInst*);
    void execAndi(const DecodedInst*);
    void execAdd(const DecodedInst*);
    void execSub(const DecodedInst*);
    void execSll(const DecodedInst*);
    void execSlt(const DecodedInst*);
    void execSltu(const DecodedInst*);
    void execXor(const DecodedInst*);
    void execSrl(const DecodedInst*);
    void execSra(const DecodedInst*);
    void execOr(const DecodedInst*);
    void execAnd(const DecodedInst*);

    void execFence(const DecodedInst*);
    void execFencei(const DecodedInst*);

    void execEcall(const DecodedInst*);
    void execEbreak(const DecodedInst*);
    void execMret(const DecodedInst*);
    void execUret(const DecodedInst*);
    void execSret(const DecodedInst*);

    void execWfi(const DecodedInst*);

    void execCsrrw(const DecodedInst*);
    void execCsrrs(const DecodedInst*);
    void execCsrrc(const DecodedInst*);
    void execCsrrwi(const DecodedInst*);
    void execCsrrsi(const DecodedInst*);
    void execCsrrci(const DecodedInst*);

    void execLb(const DecodedInst*);
    void execLh(const DecodedInst*);
    void execLw(const DecodedInst*);
    void execLbu(const DecodedInst*);
    void execLhu(const DecodedInst*);

    void execSb(const DecodedInst*);
    void execSh(const DecodedInst*);
    void execSw(const DecodedInst*);

    void execMul(const DecodedInst*);
    void execMulh(const DecodedInst*);
    void execMulhsu(const DecodedInst*);
    void execMulhu(const DecodedInst*);
    void execDiv(const DecodedInst*);
    void execDivu(const DecodedInst*);
    void execRem(const DecodedInst*);
    void execRemu(const DecodedInst*);

    // rv64i
    void execLwu(const DecodedInst*);
    void execLd(const DecodedInst*);
    void execSd(const DecodedInst*);
    void execSlliw(const DecodedInst*);
    void execSrliw(const DecodedInst*);
    void execSraiw(const DecodedInst*);
    void execAddiw(const DecodedInst*);
    void execAddw(const DecodedInst*);
    void execSubw(const DecodedInst*);
    void execSllw(const DecodedInst*);
    void execSrlw(const DecodedInst*);
    void execSraw(const DecodedInst*);

    // rv64m
    void execMulw(const DecodedInst*);
    void execDivw(const DecodedInst*);
    void execDivuw(const DecodedInst*);
    void execRemw(const DecodedInst*);
    void execRemuw(const DecodedInst*);

    // rv32f
    void execFlw(const DecodedInst*);
    void execFsw(const DecodedInst*);
    void execFmadd_s(const DecodedInst*);
    void execFmsub_s(const DecodedInst*);
    void execFnmsub_s(const DecodedInst*);
    void execFnmadd_s(const DecodedInst*);
    void execFadd_s(const DecodedInst*);
    void execFsub_s(const DecodedInst*);
    void execFmul_s(const DecodedInst*);
    void execFdiv_s(const DecodedInst*);
    void execFsqrt_s(const DecodedInst*);
    void execFsgnj_s(const DecodedInst*);
    void execFsgnjn_s(const DecodedInst*);
    void execFsgnjx_s(const DecodedInst*);
    void execFmin_s(const DecodedInst*);
    void execFmax_s(const DecodedInst*);
    void execFcvt_w_s(const DecodedInst*);
    void execFcvt_wu_s(const DecodedInst*);
    void execFmv_x_w(const DecodedInst*);
    void execFeq_s(const DecodedInst*);
    void execFlt_s(const DecodedInst*);
    void execFle_s(const DecodedInst*);
    void execFclass_s(const DecodedInst*);
    void execFcvt_s_w(const DecodedInst*);
    void execFcvt_s_wu(const DecodedInst*);
    void execFmv_w_x(const DecodedInst*);

    // rv32f + rv64
    void execFcvt_l_s(const DecodedInst*);
    void execFcvt_lu_s(const DecodedInst*);
    void execFcvt_s_l(const DecodedInst*);
    void execFcvt_s_lu(const DecodedInst*);

    // rv32d
    void execFld(const DecodedInst*);
    void execFsd(const DecodedInst*);
    void execFmadd_d(const DecodedInst*);
    void execFmsub_d(const DecodedInst*);
    void execFnmsub_d(const DecodedInst*);
    void execFnmadd_d(const DecodedInst*);
    void execFadd_d(const DecodedInst*);
    void execFsub_d(const DecodedInst*);
    void execFmul_d(const DecodedInst*);
    void execFdiv_d(const DecodedInst*);
    void execFsgnj_d(const DecodedInst*);
    void execFsgnjn_d(const DecodedInst*);
    void execFsgnjx_d(const DecodedInst*);
    void execFmin_d(const DecodedInst*);
    void execFmax_d(const DecodedInst*);
    void execFcvt_d_s(const DecodedInst*);
    void execFcvt_s_d(const DecodedInst*);
    void execFsqrt_d(const DecodedInst*);
    void execFle_d(const DecodedInst*);
    void execFlt_d(const DecodedInst*);
    void execFeq_d(const DecodedInst*);
    void execFcvt_w_d(const DecodedInst*);
    void execFcvt_wu_d(const DecodedInst*);
    void execFcvt_d_w(const DecodedInst*);
    void execFcvt_d_wu(const DecodedInst*);
    void execFclass_d(const DecodedInst*);

    // rv32d + rv64
    void execFcvt_l_d(const DecodedInst*);
    void execFcvt_lu_d(const DecodedInst*);
    void execFcvt_d_l(const DecodedInst*);
    void execFcvt_d_lu(const DecodedInst*);
    void execFmv_d_x(const DecodedInst*);
    void execFmv_x_d(const DecodedInst*);

    // atomic
    void execAmoadd_w(const DecodedInst*);
    void execAmoswap_w(const DecodedInst*);
    void execLr_w(const DecodedInst*);
    void execSc_w(const DecodedInst*);
    void execAmoxor_w(const DecodedInst*);
    void execAmoor_w(const DecodedInst*);
    void execAmoand_w(const DecodedInst*);
    void execAmomin_w(const DecodedInst*);
    void execAmomax_w(const DecodedInst*);
    void execAmominu_w(const DecodedInst*);
    void execAmomaxu_w(const DecodedInst*);

    // atmomic + rv64
    void execAmoadd_d(const DecodedInst*);
    void execAmoswap_d(const DecodedInst*);
    void execLr_d(const DecodedInst*);
    void execSc_d(const DecodedInst*);
    void execAmoxor_d(const DecodedInst*);
    void execAmoor_d(const DecodedInst*);
    void execAmoand_d(const DecodedInst*);
    void execAmomin_d(const DecodedInst*);
    void execAmomax_d(const DecodedInst*);
    void execAmominu_d(const DecodedInst*);
    void execAmomaxu_d(const DecodedInst*);

    // Bit manipulation: zbb
    void execClz(const DecodedInst*);
    void execCtz(const DecodedInst*);
    void execPcnt(const DecodedInst*);
    void execAndn(const DecodedInst*);
    void execOrn(const DecodedInst*);
    void execXnor(const DecodedInst*);
    void execSlo(const DecodedInst*);
    void execSro(const DecodedInst*);
    void execSloi(const DecodedInst*);
    void execSroi(const DecodedInst*);
    void execMin(const DecodedInst*);
    void execMax(const DecodedInst*);
    void execMinu(const DecodedInst*);
    void execMaxu(const DecodedInst*);
    void execRol(const DecodedInst*);
    void execRor(const DecodedInst*);
    void execRori(const DecodedInst*);
    void execRev8(const DecodedInst*);
    void execRev(const DecodedInst*);
    void execPack(const DecodedInst*);

    // Bit manipulation: zbs
    void execSbset(const DecodedInst*);
    void execSbclr(const DecodedInst*);
    void execSbinv(const DecodedInst*);
    void execSbext(const DecodedInst*);

    void execSbseti(const DecodedInst*);
    void execSbclri(const DecodedInst*);
    void execSbinvi(const DecodedInst*);
    void execSbexti(const DecodedInst*);

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
      LoadInfo()
	: size_(0), addr_(0), regIx_(0), prevData_(0), valid_(false),
	  wide_(false)
      { }

      LoadInfo(unsigned size, size_t addr, unsigned regIx, uint64_t prev,
	       bool isWide, unsigned tag)
	: size_(size), addr_(addr), regIx_(regIx), tag_(tag), prevData_(prev),
	  valid_(true), wide_(isWide)
      { }

      bool isValid() const  { return valid_; }
      void makeInvalid() { valid_ = false; }

      unsigned size_ = 0;
      size_t addr_ = 0;
      unsigned regIx_ = 0;
      unsigned tag_ = 0;
      uint64_t prevData_ = 0;
      bool valid_ = false;
      bool wide_ = false;
    };

    void putInLoadQueue(unsigned size, size_t addr, unsigned regIx,
			uint64_t prevData, bool isWide = false);

    void removeFromLoadQueue(unsigned regIx);

    void invalidateInLoadQueue(unsigned regIx);

  private:

    unsigned localHartId_ = 0;   // Hardware thread id witin core.
    bool hartStarted_ = true;    // True if hart is running. WD special.
    Memory& memory_;
    IntRegs<URV> intRegs_;       // Integer register file.
    CsRegs<URV> csRegs_;         // Control and status registers.
    FpRegs<double> fpRegs_;      // Floating point registers.
    bool rv64_ = sizeof(URV)==8; // True if 64-bit base (RV64I).
    bool rva_ = false;           // True if extension A (atomic) enabled.
    bool rvc_ = true;            // True if extension C (compressed) enabled.
    bool rvd_ = false;           // True if extension D (double fp) enabled.
    bool rvf_ = false;           // True if extension F (single fp) enabled.
    bool rvm_ = true;            // True if extension M (mul/div) enabled.
    bool rvs_ = false;           // True if extension S (supervisor-mode) enabled.
    bool rvu_ = false;           // True if extension U (user-mode) enabled.
    bool rvzbb_ = false;         // True if extension zbb enabled.
    bool rvzbs_ = false;         // True if extension zbs enabled.
    URV pc_ = 0;                 // Program counter. Incremented by instr fetch.
    URV currPc_ = 0;             // Addr instr being executed (pc_ before fetch).
    URV resetPc_ = 0;            // Pc to use on reset.
    URV stopAddr_ = 0;           // Pc at which to stop the simulator.
    bool stopAddrValid_ = false; // True if stopAddr_ is valid.

    URV toHost_ = 0;             // Writing to this stops the simulator.
    bool toHostValid_ = false;   // True if toHost_ is valid.
    std::string toHostSym_ = "tohost";   // ELF symbol to use as "tohost" addr.

    URV conIo_ = 0;              // Writing a byte to this writes to console.
    bool conIoValid_ = false;    // True if conIo_ is valid.
    URV progBreak_ = 0;          // For brk Linux emulation.

    URV nmiPc_ = 0;              // Non-maskable interrupt handler address.
    bool nmiPending_ = false;
    NmiCause nmiCause_ = NmiCause::UNKNOWN;
    bool nmiEnabled_ = true;

    // These should be cleared before each instruction when triggers enabled.
    bool hasException_ = 0;      // True if current inst has an exception.
    bool csrException_ = 0;      // True if there is a CSR related exception.
    bool triggerTripped_ = 0;    // True if a trigger trips.

    bool lastBranchTaken_ = false; // Useful for performance counters
    bool misalignedLdSt_ = false;  // Useful for performance counters

    bool misalAtomicCauseAccessFault_ = true;

    // True if effective and base addresses must be in regions of the
    // same type.
    bool eaCompatWithBase_ = false;

    uint64_t retiredInsts_ = 0;  // Proxy for minstret CSR.
    uint64_t cycleCount_ = 0;    // Proxy for mcycle CSR.
    uint64_t instCounter_ = 0;   // Absolute retired instruction count.
    uint64_t instCountLim_ = ~uint64_t(0);
    uint64_t exceptionCount_ = 0;
    uint64_t interruptCount_ = 0;
    uint64_t consecutiveIllegalCount_ = 0;
    uint64_t counterAtLastIllegal_ = 0;
    bool forceAccessFail_ = false;  // Force load/store access fault.
    bool forceFetchFail_ = false;   // Force fetch access fault.
    bool fastInterrupts_ = false;
    URV forceAccessFailOffset_ = 0;
    URV forceFetchFailOffset_ = 0;
    uint64_t forceAccessFailMark_ = 0; // Instruction at which forced fail is seen.

    bool instFreq_ = false;         // Collection instruction frequencies.
    bool enableCounters_ = false;   // Enable performance monitors.
    bool prevCountersCsrOn_ = true;
    bool countersCsrOn_ = true;     // True when counters CSR is set to 1.
    bool enableTriggers_ = false;   // Enable debug triggers.
    bool enableGdb_ = false;        // Enable gdb mode.
    bool abiNames_ = false;         // Use ABI register names when true.
    bool newlib_ = false;           // Enable newlib system calls.
    bool linux_ = false;            // ENable linux system calls.
    bool amoIllegalOutsideDccm_ = false;

    bool traceLoad_ = false;        // Trace addr of load inst if true.
    URV loadAddr_ = 0;              // Address of data of most recent load inst.
    bool loadAddrValid_ = false;    // True if loadAddr_ valid.

    // We keep track of the last committed 8 loads so that we can
    // revert in the case of an imprecise load exception.
    std::vector<LoadInfo> loadQueue_;
    unsigned maxLoadQueueSize_ = 16;
    bool loadQueueEnabled_ = false;

    PrivilegeMode privMode_ = PrivilegeMode::Machine; // Privilege mode.
    bool debugMode_ = false;         // True on debug mode.
    bool debugStepMode_ = false;     // True in debug step mode.
    bool dcsrStepIe_ = false;        // True if stepie bit set in dcsr.
    bool dcsrStep_ = false;          // True if step bit set in dcsr.
    bool ebreakInstDebug_ = false;   // True if debug mode entered from ebreak.
    bool storeErrorRollback_ = false;
    bool loadErrorRollback_ = false;
    bool targetProgFinished_ = false;
    bool useElfSymbols_ = true;
    unsigned mxlen_ = 8*sizeof(URV);
    FILE* consoleOut_ = nullptr;

    // Stack access control.
    bool checkStackAccess_ = false;
    URV stackMax_ = ~URV(0);
    URV stackMin_ = 0;

    bool wideLdSt_ = false;

    // AMO instructions have additional operands: rl and aq.
    bool amoAq_ = false;
    bool amoRl_ = false;

    InstTable instTable_;
    std::vector<InstProfile> instProfileVec_; // Instruction frequency

    // Ith entry is true if ith region has iccm/dccm/pic.
    std::vector<bool> regionHasLocalMem_;

    // Ith entry is true if ith region has dccm/pic.
    std::vector<bool> regionHasLocalDataMem_;

    // Ith entry is true if ith region has dccm/pic.
    std::vector<bool> regionHasLocalInstMem_;

    // Ith entry is true if ith region has dccm.
    std::vector<bool> regionHasDccm_;

    // Ith entry is true if ith region has pic
    std::vector<bool> regionHasMemMappedRegs_;

    // Decoded instruction cache.
    std::vector<DecodedInst> decodeCache_;
    uint32_t decodeCacheSize_ = 0;
    uint32_t decodeCacheMask_ = 0;  // Derived from decodeCacheSize_
  };
}

