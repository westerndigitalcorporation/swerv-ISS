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
#include "Memory.hpp"

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
	M_STORE_BUS = 12  // Store-bus error (WD extension).
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


  class CoreException : public std::exception
  {
  public:

    enum Type { Stop };

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

    /// Set val to the value of the constrol and status register csr
    /// returning true on success. Return false leaving val unmodified
    /// if csr is out of bounds.
    bool peekCsr(CsrNumber csr, URV& val) const;

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
		   URV resetValue, URV mask);

    /// Rset core. Reset all CSRs to their initial value. Reset all
    /// integer registers to zero. Reset PC to the reset-pc as
    /// defined by defineResetPc (default is zero).
    void reset();

    /// Run fetch-decode-execute loop. If a stop address is defined,
    /// stop when the prgram counter reaches that address. If a tohost
    /// address is defined, stop when a store instruction writes into
    /// that address. Trigger an external interrupt if a SIGUSR2
    /// signal is received. Stop is a SIGTERM is received.
    bool run(FILE* file = nullptr);

    /// Run one instruction at the current program counter. Update
    /// program counter. If file is non-null then print thereon
    /// tracing information related to the executed instruction.
    void singleStep(FILE* file = nullptr);

    /// Run until the program counter reaches the given address. Do
    /// execute the instruction at that address. If file is non-null
    /// then print thereon tracing information after each executed
    /// instruction.
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
    /// op2 will be set. If inst is not valid instruction , then we
    /// return a reference to the illegal-instruction info.
    const InstInfo& decode(uint32_t inst, uint32_t& op0, uint32_t& op1,
			   int32_t& op2) const;

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

    bool pokeMemory(size_t address, uint8_t val);
    bool pokeMemory(size_t address, uint16_t val);
    bool pokeMemory(size_t address, uint32_t val);
    bool pokeMemory(size_t address, uint64_t val);

    /// Define value of program counter after a reset.
    void defineResetPc(URV addr)
    { resetPc_ = addr; }

    /// Define address to which a write will stop the simulator. An
    /// sb, sh, or sw instruction will stop the simulator if the write
    /// address of he instruction is identical to the given address.
    void setToHostAddress(size_t address);

    /// Undefine address to which a write will stop the simulator
    void clearToHostAddress();

    bool getToHostAddress(size_t& address) const
    { if (toHostValid_) address = toHost_; return toHostValid_; }

    /// Support for tracing: Return the pc of the last executed
    /// instruction.
    URV lastPc() const;

    /// Support for tracing: Return the index of the integer register
    /// written by the last executed instruction. Return -1 it no
    /// integer register was written.
    int lastIntReg() const;

    /// Support for tracing: Fill the csrs register with the
    /// register-numbers of the CSRs written by the execution of the
    /// last instruction.  CSRs modified as a side effect (e.g. mcycle
    /// and minstret) are not included.
    void lastCsr(std::vector<CsrNumber>& csrs) const;

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
    bool defineMemoryMappedRegisterRegion(size_t region, size_t size,
					  size_t regionOffset);

    /// Define a memory mapped register. Region (as defined by region
    /// and offset) must be already defined using
    /// defineMemoryMappedRegisterRegion. The register address must not
    /// fall outside the region
    bool defineMemoryMappedRegisterWriteMask(size_t region,
					     size_t regionOffset,
					     size_t registerBlockOffset,
					     size_t registerIx,
					     uint32_t mask);

    /// Direct the core to take an instruction access fault exception
    /// within the next singleStep invocation.
    void postInstAccessFault()
    { forceFetchFail_ = true; }

    /// Direct the core to take a data access fault exception within
    /// the subsequent singleStep invocation executing a load/store
    /// instruction.
    void postDataAccessFault()
    { forceAccessFail_ = true; }

    /// Run self test. Return true on success and false on failure.
    /// Processor state is not preserved. Neither is memory state.
    bool selfTest();

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

    /// Apply an imprecise store exception at given address. Return
    /// true if address is found exactly once in the store
    /// queue. Return false otherwise. If the mdseal CSR contains 0,
    /// then save the given address in mdseac setting mdseal to 1.
    /// Set matchCount to the number of entries in the store queue
    /// that match the given address.
    bool applyStoreException(URV address, unsigned& matchCount);

    /// Enable processing of imprecise store exceptions.
    void enableStoreExceptions(bool flag)
    { maxStoreQueueSize_ = flag? 4 : 0; }

    /// Enable collection of instruction frequencies.
    void enableInstructionFrequency(bool flag)
    { instFreq_ = flag; if (flag) instFreqVec_.resize(size_t(InstId::maxId) + 1); }

    /// Put the core in debug mode.
    void enterDebugMode()
    { debugMode_ = true; }

    /// Take the core out of debug mode.
    void exitDebugMode()
    { debugMode_ = false; }

    /// Print collected instruction frequency to the given file.
    void reportInstructionFrequency(FILE* file) const;

  protected:

    void accumulateInstructionFrequency(uint32_t inst);

    /// Reset trace data (items changed by the execution of an instr)
    void clearTraceData();

    /// Fetch an instruction. Return true on success. Return false on
    /// fail (in which case an exception is initiated). May fetch a
    /// compresssed instruction (16-bits) in which case the upper 16
    /// bits are not defined (may contain arbitrary values).
    bool fetchInst(size_t address, uint32_t& instr);

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

  private:

    // We model store buffer in order to undo store effects after an
    // imprecise store exception.
    struct StoreInfo
    {
      StoreInfo(unsigned size, size_t addr, uint64_t data)
	: size_(size), addr_(addr), data_(data)
      { }

      unsigned size_ = 0;  // 0: invalid object.
      size_t addr_ = 0;
      uint64_t data_ = 0;
    };

    void putInStoreQueue(unsigned size, size_t addr, uint64_t data);

  private:

    unsigned hartId_ = 0;        // Hardware thread id.
    Memory memory_;
    IntRegs<URV> intRegs_;       // Integer register file.
    CsRegs<URV> csRegs_;         // Control and status registers.
    bool rv64_ = sizeof(URV)==8; // True if 64-bit extension enabled.
    URV pc_ = 0;                 // Program counter. Incremented by instr fetch.
    URV currPc_ = 0;             // Pc of instr being executed (pc_ before fetch).
    URV resetPc_ = 0;            // Pc to use on reset.
    URV stopAddr_ = 0;           // Pc at which to stop the simulator.
    bool stopAddrValid_ = false; // True if stopAddr_ is valid.
    URV toHost_ = 0;             // Writing to this stops the simulator.
    bool toHostValid_ = false;   // True if toHost_ is valid.
    URV conIo_ = 0;              // Writing a byte to this writes to console.
    bool conIoValid_ = false;    // True if conIo_ is valid.

    uint64_t retiredInsts_ = 0;  // Proxy for minstret CSR.
    uint64_t cycleCount_ = 0;    // Proxy for mcylcel CSR.
    uint64_t counter_ = 0;       // Retired instruction count.
    uint64_t instCountLim_ = ~uint64_t(0);
    uint64_t exceptionCount_ = 0;
    uint64_t interruptCount_ = 0;
    bool forceAccessFail_ = false;  // Force load/store access fault.
    bool forceFetchFail_ = false;   // Forece fetch access fault.
    bool instFreq_ = false;         // Enable collection of instruction frequency.

    bool traceLoad_ = false;        // Trace addr of load inst if true.
    URV loadAddr_ = 0;              // Address of data of most recent load inst.
    bool loadAddrValid_ = false;    // True if loadAddr_ valid.

    bool lastTrapValid_ = 0;        // True if last trap info available.
    bool lastTrapInterrupt_ = 0;    // True if alst trap is an interrupt.
    uint32_t lastTrapCause_ = 0;    // Last trap cause.

    // We keep track of the last committed 4 stores so that we can
    // revert it in the case of an imprecise store exception.
    std::vector<StoreInfo> storeQueue_;
    unsigned maxStoreQueueSize_ = 4;

    PrivilegeMode privilegeMode_ = MACHINE_MODE;     // Privilige mode.
    bool debugMode_ = false;                         // True in debug mode.
    unsigned mxlen_ = 8*sizeof(URV);

    InstInfoTable instTable_;
    std::vector<uint32_t> instFreqVec_; // Instruction frequency
  };
}

