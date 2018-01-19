#include <iomanip>
#include <iostream>
#include <sstream>
#include <boost/format.hpp>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "Core.hpp"
#include "Inst.hpp"

using namespace WdRiscv;


template <typename URV>
Core<URV>::Core(unsigned hartId, size_t memorySize, unsigned intRegCount)
  : hartId_(hartId), memory_(memorySize), intRegs_(intRegCount)
{
}


template <typename URV>
Core<URV>::~Core()
{
}


template <typename URV>
void
Core<URV>::initialize()
{
}


template <typename URV>
bool
Core<URV>::loadHexFile(const std::string& file)
{
  return memory_.loadHexFile(file);
}


template <typename URV>
bool
Core<URV>::loadElfFile(const std::string& file, size_t& entryPoint,
		       size_t& exitPoint, size_t& toHost, bool& hasToHost)
{
  return memory_.loadElfFile(file, entryPoint, exitPoint, toHost, hasToHost);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint8_t& val) const
{
  return memory_.readByte(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint16_t& val) const
{
  return memory_.readHalfWord(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint32_t& val) const
{
  return memory_.readWord(address, val);
}


template <typename URV>
bool
Core<URV>::pokeMemory(size_t address, uint32_t& val)
{
  return memory_.writeWord(address, val);
}


template <typename URV>
void
Core<URV>::setToHostAddress(size_t address)
{
  toHost_ = address;
  toHostValid_ = true;
}


template <typename URV>
void
Core<URV>::clearToHostAddress()
{
  toHost_ = 0;
  toHostValid_ = false;
}


template <typename URV>
bool
Core<URV>::selfTest()
{
  size_t errors = 0;

  // Writing x0 has no effect. Reading x0 yields zero.
  execOri(RegX0, RegX1, ~URV(0));           // ori x0, x1, 0xffff    
  if (intRegs_.read(RegX0) != 0)
    {
      std::cerr << "Writing to x0 erroneously effectual.\n";
      errors++;
    }
  execAndi(RegX1, RegX0, ~URV(0));         // andi x1, x0, 0xffff     x1 <- 0
  if (intRegs_.read(RegX1) != 0)
    {
      std::cerr << "Reading x0 yielded non-zero value\n";
      errors++;
    }

  // All bits of registers (except x0) toggle.
  for (uint32_t ix = 1; ix < intRegs_.size(); ++ix)
    {
      execAddi(ix, RegX0, 0);          // reg[ix] <- 0
      execXori(ix, RegX0, ~URV(0));    // reg[ix] <- ~0
      if (intRegs_.read(ix) != ~URV(0))
	{
	  std::cerr << "Failed to write all ones to register x" << ix << '\n';
	  errors++;
	}

      execXor(ix, ix, ix);
      if (intRegs_.read(ix) != 0)
	{
	  std::cerr << "Failed to write all zeros to register x" << ix <<  '\n';
	  errors++;
	}
    }
  if (errors)
    return false;

  // Simple tests of integer instruction.
  execLui(1, 0x01234000); // lui x1, 0x1234000  x1 <- 0x01234000
  execOri(2, 1, 0x567);   // ori x2, x1, 0x567  x2 <- 0x01234567)
  if (intRegs_.read(2) != 0x01234567)
    {
      std::cerr << "lui + ori failed\n";
      errors++;
    }

  execAddi(RegX1, RegX0, 0x700);
  execAddi(RegX1, RegX1, 0x700);
  execAddi(RegX1, RegX1, 0x700);
  execAddi(RegX1, RegX1, 0x700);
  if (intRegs_.read(RegX1) != 4 * 0x700)
    {
      std::cerr << "addi positive immediate failed\n";
      errors++;
    }

  execAddi(RegX1, RegX0, SRV(-1));
  execAddi(RegX1, RegX1, SRV(-1));
  execAddi(RegX1, RegX1, SRV(-1));
  execAddi(RegX1, RegX1, SRV(-1));
  if (intRegs_.read(RegX1) != URV(-4))
    {
      std::cerr << "addi positive immediate failed\n";
      errors++;
    }

  if (errors)
    return false;

  // Put a loop at location 0x100.

  RFormInst i0(0);
  i0.encodeAdd(RegX1, RegX0, RegX0);   // 100 add x1, x0, x0    x1 <- 0

  IFormInst i1(0);
  i1.encodeAddi(RegX2, RegX0, 16);     // 104 addi x2, x0, 128  x2 <- 16

  IFormInst i2(0);
  i2.encodeSlli(RegX2, RegX2, 1);      // 108 slli x2, x2, 1    x2 <- 32

  IFormInst i3(0);
  i3.encodeAddi(RegX1, RegX1, 1);      // 10c addi x1, x1, 1    x1 <- x1 + 1

  IFormInst i4(0);
  i4.encodeAndi(RegX3, RegX1, 0x03ff); // 110 andi x3, x1, 03ff  keep least 10 bits
  
  SFormInst i5(0);
  i5.encodeSb(RegX3, RegX1, 0x400);    // 114 sb x1, 1024(x3)

  IFormInst i6(0);
  i6.encodeAddi(RegX2, RegX2, -1);     // 118 addi x2, x2, -1  x2 <- x2 - 1

  BFormInst i7(0);
  i7.encodeBge(RegX2, RegX0, -16);     // 11c bge x2, x0, -16  if x2 > 0 goto 0x10c

  RFormInst i8(0);
  i8.encodeAdd(RegX0, RegX0, RegX0);   // 120 add x0, x0, x0   nop

  memory_.writeWord(0x100, i0.code);
  memory_.writeWord(0x104, i1.code);
  memory_.writeWord(0x108, i2.code);
  memory_.writeWord(0x10c, i3.code);
  memory_.writeWord(0x110, i4.code);
  memory_.writeWord(0x114, i5.code);
  memory_.writeWord(0x118, i6.code);
  memory_.writeWord(0x11c, i7.code);
  memory_.writeWord(0x120, i8.code);

  // Set program counter to entry of loop.
  pc_ = 0x100;

  // Diassemble the loop.
  std::string str;
  for (size_t addr = 0x100; addr < 0x124; addr += 4)
    {
      uint32_t code = 0;
      if (memory_.readWord(addr, code))
	{
	  disassembleInst(code, str);
	  std::cout << (boost::format("%08x") % code) << ' ' << str << '\n';
	}
    }

  // Run the loop
  runUntilAddress(0x124);

  return errors == 0;
}


template <typename URV>
bool
Core<URV>::readInst(size_t address, uint32_t& inst)
{
  inst = 0;

  uint16_t low;  // Low 2 bytes of instruction.
  if (not memory_.readHalfWord(currPc_, low))
    return false;

  inst = low;

  if ((inst & 0x3) == 3)  // Non-compressed instruction.
    {
      uint16_t high;
      if (not memory_.readHalfWord(currPc_ + 2, high))
	return false;
      inst |= (uint32_t(high) << 16);
    }

  return true;
}


template <typename URV>
void
Core<URV>::illegalInst()
{
  uint32_t currInst;
  if (not readInst(currPc_, currInst))
    assert(0 and "Failed to re-read current instruction");

  initiateException(ILLEGAL_INST, currPc_, currInst);
}


template <typename URV>
void
Core<URV>::unimplemented()
{
  illegalInst();
}


// Start an asynchronous exception.
template <typename URV>
void
Core<URV>::initiateInterrupt(InterruptCause cause, URV pc)
{
  bool interrupt = true;
  initiateTrap(interrupt, cause, pc, 0);
}


// Start a synchronous exception.
template <typename URV>
void
Core<URV>::initiateException(ExceptionCause cause, URV pc, URV info)
{
  bool interrupt = false;
  initiateTrap(interrupt, cause, pc, info);
}


template <typename URV>
void
Core<URV>::initiateTrap(bool interrupt, URV cause, URV pcToSave, URV info)
{

  // TBD: support cores with S and U privilege modes.

  PrivilegeMode prevMode = privilegeMode_;

  // Exceptions are taken in machine mode.
  privilegeMode_ = MACHINE_MODE;
  PrivilegeMode nextMode = MACHINE_MODE;

  // But they can be delegated. TBD: handle delegation to S/U modes
  // updating nextMode.

  CsrNumber epcNum = MEPC_CSR;
  CsrNumber causeNum = MCAUSE_CSR;
  CsrNumber tvalNum = MTVAL_CSR;
  CsrNumber tvecNum = MTVEC_CSR;

  if (nextMode == SUPERVISOR_MODE)
    {
      epcNum = SEPC_CSR;
      causeNum = SCAUSE_CSR;
      tvalNum = STVAL_CSR;
      tvecNum = STVEC_CSR;
    }
  else if (nextMode == USER_MODE)
    {
      epcNum = UEPC_CSR;
      causeNum = UCAUSE_CSR;
      tvalNum = UTVAL_CSR;
      tvecNum = UTVEC_CSR;
    }

  // Save addres of instruction that caused the exception or address
  // of interrupted instruction.
  if (not csRegs_.write(epcNum, privilegeMode_, pcToSave & ~(URV(1))))
    assert(0 and "Failed to write EPC register");

  // Save the exception cause.
  URV causeRegVal = cause;
  if (interrupt)
    causeRegVal |= 1 << (mxlen_ - 1);
  if (not csRegs_.write(causeNum, privilegeMode_, causeRegVal))
    assert(0 and "Failed to write CAUSE register");

  // Save synchronous exception info
  if (not interrupt)
    if (not csRegs_.write(tvalNum, privilegeMode_, info))
      assert(0 and "Failed to write TVAL register");

  // Update status register saving xIE in xPIE and prevoius privilege
  // mode in xPP by getting current value of mstatus ...
  URV status = 0;
  if (not csRegs_.read(MSTATUS_CSR, privilegeMode_, status))
    assert(0 and "Failed to read MSTATUS register");

  // ... updating its fields
  MstatusFields<URV> fields(status);

  if (nextMode == MACHINE_MODE)
    {
      fields.MPP = prevMode;
      fields.MPIE = fields.MIE;
      fields.MIE = 0;
    }
  else if (nextMode == SUPERVISOR_MODE)
    {
      fields.SPP = prevMode;
      fields.SPIE = fields.SIE;
      fields.SIE = 0;
    }
  else if (nextMode == USER_MODE)
    {
      fields.UPIE = fields.UIE;
      fields.UIE = 0;
    }

  // ... and putting it back
  if (not csRegs_.write(MSTATUS_CSR, privilegeMode_, fields.value_))
    assert(0 and "Failed to write MSTATUS register");
  
  // Set program counter to trap handler address.
  URV tvec = 0;
  if (not csRegs_.read(tvecNum, privilegeMode_, tvec))
    assert(0 and "Failed to read TVEC register");

  URV base = (tvec >> 2) << 2;  // Clear least sig 2 bits.
  unsigned tvecMode = tvec & 0x3;

  if (tvecMode == 1 and interrupt)
    base = base + 4*cause;

  pc_ = (base >> 1) << 1;  // Clear least sig bit

  // Change privilege mode.
  privilegeMode_ = nextMode;
}


template <typename URV>
bool
Core<URV>::peekIntReg(unsigned ix, URV& val) const
{ 
  if (ix < intRegs_.size())
    {
      val = intRegs_.read(ix);
      return true;
    }
  return false;
}


template <typename URV>
bool
Core<URV>::pokeIntReg(unsigned ix, URV val)
{ 
  if (ix < intRegs_.size())
    {
      intRegs_.write(ix, val);
      return true;
    }
  return false;
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val) const
{ 
  Csr<URV> csr;
  if (not csRegs_.findCsr(csrn, csr))
    return false;

  return csRegs_.read(csrn, MACHINE_MODE, val);
}


template <typename URV>
bool
Core<URV>::pokeCsr(CsrNumber csr, URV val)
{ 
  return csRegs_.write(csr, MACHINE_MODE, val);
}


template <typename URV>
URV
Core<URV>::peekPc() const
{
  return pc_;
}


template <typename URV>
void
Core<URV>::pokePc(URV address)
{
  pc_ = (address >> 1) << 1; // Clear least sig big
}


template <typename URV>
bool
Core<URV>::findIntReg(const std::string& name, unsigned& num) const
{
  return intRegs_.findReg(name, num);
}


template <typename URV>
bool
Core<URV>::findCsr(const std::string& name, CsrNumber& num) const
{
  Csr<URV> csr;
  if (not csRegs_.findCsr(name, csr))
    return false;
  num = csr.getNumber();
  return true;
}


template <typename URV>
void
Core<URV>::traceInst(uint32_t inst, uint64_t tag, std::string& tmp,
		     FILE* out)
{
  bool spikeCompatible = true;  // TBD: remove.

  // TBD: Change format when using 64-bit.
  disassembleInst(inst, tmp);

  char instBuff[128];
  if ((inst & 0x3) == 3)
    sprintf(instBuff, "%08x", inst);
  else
    sprintf(instBuff, "%04x", inst);

  bool pending = false;  // True if a printed line need to be terminated.

  // Process integer register diff.
  int reg = intRegs_.getLastWrittenReg();
  URV value = 0;
  if (reg > 0)
    {
      value = intRegs_.read(reg);
      fprintf(out, "#%d %d %08x %8s r %08x %08x  %s",
	      tag, hartId_, currPc_, instBuff, reg, value, tmp.c_str());
      pending = true;
    }

  // Process CSR diff.
  std::vector<CsrNumber> csrs;
  csRegs_.getLastWrittenRegs(csrs);
  std::sort(csrs.begin(), csrs.end());

  for (CsrNumber csr : csrs)
    {
      if (not csRegs_.read(CsrNumber(csr), MACHINE_MODE, value))
	continue;

      bool print = true;
      //if (spikeCompatible and reg > 0)
      //print = false;  // Spike does not print CSR if int reg printed.
      if (print)
	{
	  if (pending)
	    fprintf(out, "  +\n");
	  fprintf(out, "#%d %d %08x %8s c %08x %08x  %s",
		  tag, hartId_, currPc_, instBuff, csr, value, tmp.c_str());
	  pending = true;
	}
    }

  // Process memory diff.
  size_t address = 0;
  unsigned writeSize = memory_.getLastWriteInfo(address);
  if (writeSize > 0)
    {
      if (pending)
	fprintf(out, "  +\n");

      uint32_t word = 0;

      if (writeSize == 1)
	for (size_t i = 0; i < 4; i++)
	  {
	    uint8_t byte = 0;
	    memory_.readByte(address+i, byte);
	    word = word | (byte << (8*i));
	  }
      else if (writeSize == 2)
	{
	  for (size_t i = 0; i < 4; i += 2)
	    {
	      uint16_t half = 0;
	      memory_.readHalfWord(address+i, half);
	      word = word | (half << 16*i);
	    }
	}
      else if (writeSize == 4)
	memory_.readWord(address, word);
      else if (writeSize == 8)
	{
	  memory_.readWord(address, word);
	  fprintf(out, "#%d %d %08x %8s m %08x %08x", tag,
		  hartId_, currPc_, instBuff, address, word);
	  fprintf(out, "  %s  +\n", tmp.c_str());

	  address += 4;
	  memory_.readWord(address, word);
	}
      else
	std::cerr << "Houston we have a problem. Unhandeled write size "
		  << writeSize << " at instruction address "
		  << std::hex << currPc_ << std::endl;

      // Temporary: Compatibility with spike trace.
      if (spikeCompatible)
	word = lastWrittenWord_;

      fprintf(out, "#%d %d %08x %8s m %08x %08x", tag,
	      hartId_, currPc_, instBuff, address, word);
      fprintf(out, "  %s", tmp.c_str());
      pending = true;
    }

  if (pending) 
    fprintf(out, "\n");
  else
    {
      // No diffs: Generate an x0 record.
      fprintf(out, "#%d %d %08x %8s r %08x %08x  %s\n",
	      tag, hartId_, currPc_, instBuff, 0, 0, tmp.c_str());
    }
}


template <typename URV>
void
Core<URV>::runUntilAddress(URV address, FILE* traceFile)
{
  struct timeval t0;
  gettimeofday(&t0, nullptr);

  std::string instStr;
  instStr.reserve(128);

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  csRegs_.getRetiredInstCount(retiredInsts_);
  csRegs_.getCycleCount(cycleCount_);

  bool trace = traceFile != nullptr;
  csRegs_.traceWrites(trace);

  try
    {
      while (pc_ != address)
	{
	  // Reset trace data (items changed by the execution of an instr)
	  if (__builtin_expect(trace, 0))
	    {
	      intRegs_.clearLastWrittenReg();
	      csRegs_.clearLastWrittenRegs();
	      memory_.clearLastWriteInfo();
	    }

	  // Fetch instruction incrementing program counter. A two-byte
	  // value is first loaded. If its least significant bits are
	  // 00, 01, or 10 then we have a 2-byte instruction and the fetch
	  // is complete. If the least sig bits are 11 then we have a 4-byte
	  // instruction and two additional bytes are loaded.
	  currPc_ = pc_;

	  bool misaligned = (pc_ & 1);
	  if (__builtin_expect(misaligned, 0))
	    {
	      ++cycleCount_;
	      initiateException(INST_ADDR_MISALIGNED, pc_, pc_ /*info*/);
	      continue; // Next instruction in trap handler.
	    }

	  uint32_t inst;
	  bool fetchFail = not memory_.readWord(pc_, inst);
	  if (__builtin_expect(fetchFail, 0))
	    {
	      // See if a 2-byte fetch will work.
	      uint16_t half;
	      if (not memory_.readHalfWord(pc_, half))
		{
		  ++cycleCount_;
		  initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
		  continue; // Next instruction in trap handler.
		}
	      inst = half;
	      if ((inst & 3) == 3)
		{ // 4-byte instruction but 4-byte fetch fails.
		  ++cycleCount_;
		  initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
		  continue;
		}
	    }

	  // Execute instruction (possibly fetching additional 2 bytes).
	  if (__builtin_expect( (inst & 3) == 3, 1) )
	    {
	      // 4-byte instruction
	      pc_ += 4;
	      execute32(inst);
	    }
	  else
	    {
	      // Compressed (2-byte) instruction.
	      pc_ += 2;
	      inst = (inst << 16) >> 16; // Clear top 16 bits.
	      execute16(inst);
	    }

	  ++cycleCount_;
	  ++retiredInsts_;

	  if (__builtin_expect(trace, 0))
	    traceInst(inst, retiredInsts_, instStr, traceFile);
	}
    }
  catch (...)
    {  // Wrote to tohost
      if (trace)
	{
	  uint32_t inst = 0;
	  readInst(currPc_, inst);
	  traceInst(inst, retiredInsts_ + 1, instStr, traceFile);
	}
      std::cout.flush();
      std::cerr << "Stopped...\n";
    }

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);

  // Simulator stats.
  struct timeval t1;
  gettimeofday(&t1, nullptr);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)*1e-6;

  std::cout << "Retired " << retiredInsts_ << " instruction"
	    << (retiredInsts_ > 1? "s" : "") << " in "
	    << (boost::format("%.2fs") % elapsed);
  if (elapsed > 0)
    std::cout << "  " << size_t(retiredInsts_/elapsed) << " inst/s";
  std::cout << '\n';
}


/// Run indefinitely.  If the tohost address is defined, then run till
/// a write is attempted to that address.
template <typename URV>
void
Core<URV>::run(FILE* file)
{
  if (stopAddrValid_)
    {
      runUntilAddress(stopAddr_, file);
      return;
    }

  if (file)
    {
      URV addres = ~URV(0);  // Invalid stop PC.
      runUntilAddress(~URV(0), file);
      return;
    }

  csRegs_.traceWrites(false);

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  csRegs_.getRetiredInstCount(retiredInsts_);
  csRegs_.getCycleCount(cycleCount_);

  try
    {
      while (true) 
	{
	  // Fetch instruction incrementing program counter. A two-byte
	  // value is first loaded. If its least significant bits are
	  // 00, 01, or 10 then we have a 2-byte instruction and the fetch
	  // is complete. If the least sig bits are 11 then we have a 4-byte
	  // instruction and two additional bytes are loaded.
	  currPc_ = pc_;

	  bool misaligned = (pc_ & 1);
	  if (__builtin_expect(misaligned, 0))
	    {
	      ++cycleCount_;
	      initiateException(INST_ADDR_MISALIGNED, pc_, pc_ /*info*/);
	      continue; // Next instruction in trap handler.
	    }

	  uint32_t inst;
	  bool fetchFail = not memory_.readWord(pc_, inst);
	  if (__builtin_expect(fetchFail, 0))
	    {
	      // See if a 2-byte fetch will work.
	      uint16_t half;
	      if (not memory_.readHalfWord(pc_, half))
		{
		  ++cycleCount_;
		  initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
		  continue; // Next instruction in trap handler.
		}
	      inst = half;
	      if ((inst & 3) == 3)
		{ // 4-byte instruction but 4-byte fetch fails.
		  ++cycleCount_;
		  initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
		  continue;
		}
	    }

	  // Execute instruction (possibly fetching additional 2 bytes).
	  if (__builtin_expect( (inst & 3) == 3, 1) )
	    {
	      // 4-byte instruction
	      pc_ += 4;
	      execute32(inst);
	    }
	  else
	    {
	      // Compressed (2-byte) instruction.
	      pc_ += 2;
	      inst = (inst << 16) >> 16; // Clear top 16 bits.
	      execute16(inst);
	    }

	  ++cycleCount_;
	  ++retiredInsts_;
	}
    }
  catch (...)
    {
      std::cout.flush();
      std::cerr << "stopped...\n";
    }

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);
}


template <typename URV>
void
Core<URV>::execute32(uint32_t inst)
{
  // Decode and execute.
  if ((inst & 0x3) == 0x3) 
    {
      unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

      if (opcode < 13)
	{
	  if (opcode == 0)  // 00000   I-form
	    {
	      IFormInst iform(inst);
	      unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	      SRV imm = iform.immed<SRV>();
	      switch (iform.fields.funct3)
		{
		case 0: execLb(rd, rs1, imm);  break;
		case 1: execLh(rd, rs1, imm);  break;
		case 2: execLw(rd, rs1, imm);  break;
		case 3: execLd(rd, rs1, imm);  break;
		case 4: execLbu(rd, rs1, imm); break;
		case 5: execLhu(rd, rs1, imm); break;
		case 6: execLwu(rd, rs1, imm); break;
		default: illegalInst();        break;
		}
	    }
	  else if (opcode == 3)  // 00011  I-form
	    {
	      IFormInst iform(inst);
	      unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	      unsigned funct3 = iform.fields.funct3;
	      if (rd != 0 or rs1 != 0)
		illegalInst();
	      else if (funct3 == 0)
		{
		  if (iform.top4() != 0)
		    illegalInst();
		  else
		    execFence(iform.pred(), iform.succ());
		}
	      else if (funct3 == 1)
		{
		  if (iform.uimmed() != 0)
		    illegalInst();
		  else
		    execFencei();
		}
	      else
		illegalInst();
	    }
	  else if (opcode == 4)  // 00100  I-form
	    {
	      IFormInst iform(inst);
	      unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	      SRV imm = iform.immed<SRV>();
	      unsigned funct3 = iform.fields.funct3;

	      if      (funct3 == 0)  execAddi(rd, rs1, imm);
	      else if (funct3 == 1)
		{
		  if (iform.fields2.top7 == 0)
		    execSlli(rd, rs1, iform.fields2.shamt);
		  else
		    illegalInst();
		}
	      else if (funct3 == 2)  execSlti(rd, rs1, imm);
	      else if (funct3 == 3)  execSltiu(rd, rs1, imm);
	      else if (funct3 == 4)  execXori(rd, rs1, imm);
	      else if (funct3 == 5)
		{
		  if (iform.fields2.top7 == 0)
		    execSrli(rd, rs1, iform.fields2.shamt);
		  else if (iform.fields2.top7 == 0x20)
		    execSrai(rd, rs1, iform.fields2.shamt);
		  else
		    illegalInst();
		}
	      else if (funct3 == 6)  execOri(rd, rs1, imm);
	      else if (funct3 == 7)  execAndi(rd, rs1, imm);
	      else                   illegalInst();
	    }
	  else if (opcode == 5)  // 00101   U-form
	    {
	      UFormInst uform(inst);
	      execAuipc(uform.rd, uform.immed<SRV>());
	    }
	  else if (opcode == 8)  // 01000  S-form
	    {
	      SFormInst sform(inst);
	      unsigned rs1 = sform.rs1, rs2 = sform.rs2, funct3 = sform.funct3;
	      SRV imm = sform.immed<SRV>();
	      if      (funct3 == 0)  execSb(rs1, rs2, imm);
	      else if (funct3 == 1)  execSh(rs1, rs2, imm);
	      else if (funct3 == 2)  execSw(rs1, rs2, imm);
	      else                   illegalInst();
	    }
	  else if (opcode == 12)  // 01100  R-form
	    {
	      RFormInst rform(inst);
	      unsigned rd = rform.rd, rs1 = rform.rs1, rs2 = rform.rs2;
	      unsigned funct7 = rform.funct7, funct3 = rform.funct3;
	      if (funct7 == 0)
		{
		  if      (funct3 == 0) execAdd(rd, rs1, rs2);
		  else if (funct3 == 1) execSll(rd, rs1, rs2);
		  else if (funct3 == 2) execSlt(rd, rs1, rs2);
		  else if (funct3 == 3) execSltu(rd, rs1, rs2);
		  else if (funct3 == 4) execXor(rd, rs1, rs2);
		  else if (funct3 == 5) execSrl(rd, rs1, rs2);
		  else if (funct3 == 6) execOr(rd, rs1, rs2);
		  else if (funct3 == 7) execAnd(rd, rs1, rs2);
		}
	      else if (funct7 == 1)
		{
		  if      (funct3 == 0) execMul(rd, rs1, rs2);
		  else if (funct3 == 1) execMulh(rd, rs1, rs2);
		  else if (funct3 == 2) execMulhsu(rd, rs1, rs2);
		  else if (funct3 == 3) execMulhu(rd, rs1, rs2);
		  else if (funct3 == 4) execDiv(rd, rs1, rs2);
		  else if (funct3 == 5) execDivu(rd, rs1, rs2);
		  else if (funct3 == 6) execRem(rd, rs1, rs2);
		  else if (funct3 == 7) execRemu(rd, rs1, rs2);
		}
	      else if (funct7 == 0x20)
		{
		  if      (funct3 == 0) execSub(rd, rs1, rs2);
		  else if (funct3 == 5) execSra(rd, rs1, rs2);
		  else                  illegalInst();
		}
	      else
		illegalInst();
	    }
	  else if (opcode == 11)  // 01011  R-form atomics
	    {
	      RFormInst rf(inst);
	      uint32_t top5 = rf.top5(), f3 = rf.funct3;
	      uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	      bool r1 = rf.r1(), aq = rf.aq();
	      if (f3 == 2)
		{
		  if (top5 == 0)          unimplemented();  // amoadd.w 
		  else if (top5 == 1)     unimplemented();  // amoswap.w
		  else if (top5 == 2)     unimplemented();  // lr.w     
		  else if (top5 == 3)     unimplemented();  // sc.w     
		  else if (top5 == 4)     unimplemented();  // amoxor.w 
		  else if (top5 == 8)     unimplemented();  // amoor.w  
		  else if (top5 == 0x10)  unimplemented();  // amomin.w 
		  else if (top5 == 0x14)  unimplemented();  // amomax.w 
		  else if (top5 == 0x18)  unimplemented();  // maominu.w
		  else if (top5 == 0x1c)  unimplemented();  // maomaxu.w
		}
	      else if (f3 == 3)
		{
		  if (top5 == 0)          unimplemented();  // amoadd.d
		  else if (top5 == 1)     unimplemented();  // amoswap.d
		  else if (top5 == 2)     unimplemented();  // lr.d
		  else if (top5 == 3)     unimplemented();  // sc.d
		  else if (top5 == 4)     unimplemented();  // amoxor.d
		  else if (top5 == 8)     unimplemented();  // amoor.d
		  else if (top5 == 0x10)  unimplemented();  // amomin.d
		  else if (top5 == 0x14)  unimplemented();  // amomax.d
		  else if (top5 == 0x18)  unimplemented();  // maominu.d
		  else if (top5 == 0x1c)  unimplemented();  // maomaxu.d
		}
	      else illegalInst();
	    }
	}
      else
	{
	  if (opcode ==  13)  // 01101  U-form
	    {
	      UFormInst uform(inst);
	      execLui(uform.rd, uform.immed<SRV>());
	    }
	  else if (opcode ==  24) // 11000   B-form
	    {
	      BFormInst bform(inst);
	      unsigned rs1 = bform.rs1, rs2 = bform.rs2, funct3 = bform.funct3;
	      SRV imm = bform.immed<SRV>();
	      if      (funct3 == 0)  execBeq(rs1, rs2, imm);
	      else if (funct3 == 1)  execBne(rs1, rs2, imm);
	      else if (funct3 == 4)  execBlt(rs1, rs2, imm);
	      else if (funct3 == 5)  execBge(rs1, rs2, imm);
	      else if (funct3 == 6)  execBltu(rs1, rs2, imm);
	      else if (funct3 == 7)  execBgeu(rs1, rs2, imm);
	      else                   illegalInst();
	    }
	  else if (opcode == 25)  // 11001  I-form
	    {
	      IFormInst iform(inst);
	      if (iform.fields.funct3 == 0)
		execJalr(iform.fields.rd, iform.fields.rs1, iform.immed<SRV>());
	      else
		illegalInst();
	    }
	  else if (opcode == 27)  // 11011  J-form
	    {
	      JFormInst jform(inst);
	      execJal(jform.rd, jform.immed<SRV>());
	    }
	  else if (opcode == 28)  // 11100  I-form
	    {
	      IFormInst iform(inst);
	      unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	      uint32_t csr = iform.uimmed();
	      switch (iform.fields.funct3)
		{
		case 0:
		  {
		    uint32_t funct7 = csr >> 5;
		    if (funct7 == 0) // ecall ebreak uret
		      {
			if (rs1 != 0 or rd != 0) illegalInst();
			else if (csr == 0)     execEcall();
			else if (csr == 1)     execEbreak();
			else if (csr == 2)     execUret();
			else                   illegalInst();
		      }
		    else if (funct7 == 9)
		      {
			if (rd != 0) illegalInst();
			else         unimplemented();  // sfence.vma
		      }
		    else if (csr == 0x102) execSret();
		    else if (csr == 0x302) execMret();
		    else if (csr == 0x105) execWfi();
		    else                   illegalInst();
		  }
		  break;
		case 1: execCsrrw(rd, csr, rs1); break;
		case 2: execCsrrs(rd, csr, rs1); break;
		case 3: execCsrrc(rd, csr, rs1); break;
		case 5: execCsrrwi(rd, csr, rs1); break;
		case 6: execCsrrsi(rd, csr, rs1); break;
		case 7: execCsrrci(rd, csr, rs1); break;
		default: illegalInst(); break;
		}
	    }
	  else
	    illegalInst();
	}
    }
  else
    illegalInst();
}


template <typename URV>
void
Core<URV>::execute16(uint16_t inst)
{
  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  switch (quadrant)
    {
    case 0:    // quadrant 0
      switch (funct3) 
	{
	case 0:   // illegal, c.addi4spn
	  {
	    if (inst == 0)
	      illegalInst();
	    else
	      {
		CiwFormInst ciwf(inst);
		unsigned immed = ciwf.immed();
		if (immed == 0)
		  illegalInst();  // As of v2.3 of User-Level ISA (Dec 2107).
		else
		  execAddi((8+ciwf.rdp), RegSp, immed);  // c.addi4spn
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  illegalInst();
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    execLw((8+clf.rdp), (8+clf.rs1p), clf.lwImmed());
	  }
	  break;
	case 3:  // c.flw, c.ld
	  if (rv64_)
	    {
	      ClFormInst clf(inst);
	      execLd((8+clf.rdp), (8+clf.rs1p), clf.lwImmed());
	    }
	  else
	    illegalInst();  // c.flw
	  break;
	case 4:  // reserved
	  illegalInst();
	  break;
	case 5:  // c.fsd, c.sq
	  illegalInst();
	  break;
	case 6:  // c.sw
	  {
	    CswFormInst csw(inst);
	    execSw(8+csw.rs1p, 8+csw.rs2p, csw.immed());
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  if (rv64_)
	    {
	      CswFormInst csw(inst);
	      execSd(8+csw.rs1p, 8+csw.rs2p, csw.immed());
	    }
	  else
	    illegalInst(); // c.fsw
	  break;
	}
      break;

    case 1:    // quadrant 1
      switch (funct3)
	{
	case 0:  // c.nop, c.addi
	  {
	    CiFormInst cif(inst);
	    execAddi(cif.rd, cif.rd, cif.addiImmed());
	  }
	  break;
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 this is c.addiw
	  {
	    CjFormInst cjf(inst);
	    execJal(RegRa, cjf.immed());
	  }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    execAddi(cif.rd, RegX0, cif.addiImmed());
	  }
	  break;

	case 3:  // c.addi16sp, c.luio
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      illegalInst();
	    else if (cif.rd == RegSp)
	      execAddi(cif.rd, cif.rd, cif.addi16spImmed());
	    else
	      execLui(cif.rd, cif.luiImmed());
	  }
	  break;

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
	case 4:
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    unsigned rd = 8 + caf.rdp;
	    switch (caf.funct2)
	      {
	      case 0:
		execSrli(rd, rd, caf.shiftImmed());
		break;
	      case 1:
		execSrai(rd, rd, caf.shiftImmed());
		break;
	      case 2:
		execAndi(rd, rd, immed);
		break;
	      case 3:
		{
		  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
		  unsigned rs2 = 8 + rs2p;
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0: execSub(rd, rd, rs2); break;
			case 1: execXor(rd, rd, rs2); break;
			case 2: execOr(rd, rd, rs2); break;
			case 3: execAnd(rd, rd, rs2); break;
			}
		    }
		  else
		    {
		      switch ((immed >> 3) & 3)
			{
			case 0: illegalInst(); break; // subw
			case 1: illegalInst(); break; // addw
			case 3: illegalInst(); break; // reserved
			case 4: illegalInst(); break; // reserved
			}
		    }
		}
		break;
	      }
	  }
	  break;

	case 5:  // c.j
	  {
	    CjFormInst cjf(inst);
	    execJal(RegX0, cjf.immed());
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    execBeq(8+cbf.rs1p, RegX0, cbf.immed());
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    execBne(8+cbf.rs1p, RegX0, cbf.immed());
	  }
	  break;
	}
      break;

    case 2:    // quadrant 2
      switch (funct3)
	{
	case 0:  // c.slli, c.slli64
	  {
	    CiFormInst cif(inst);
	    unsigned immed = unsigned(cif.slliImmed());
	    execSlli(cif.rd, cif.rd, immed);
	  }
	  break;

	case 1:  // c.fldsp, c.lqsp
	  illegalInst();
	  break;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.rd;
	    // rd == 0 is legal per Andrew Watterman
	    execLw(rd, RegSp, cif.lwspImmed());
	  }
	break;

	case 3:  // c.flwsp c.ldsp
	  illegalInst();
	  break;

	case 4:   // c.jr c.mv c.ebreak c.jalr c.add
	  {
	    CiFormInst cif(inst);
	    unsigned immed = cif.addiImmed();
	    unsigned rd = cif.rd;
	    unsigned rs2 = immed & 0x1f;
	    if ((immed & 0x20) == 0)  // c.jr or c.mv
	      {
		if (rs2 == RegX0)
		  {
		    if (rd == RegX0)
		      illegalInst();
		    else
		      execJalr(RegX0, rd, 0);
		  }
		else
		  execAdd(rd, RegX0, rs2);
	      }
	    else  // c.ebreak, c.jalr or c.add 
	      {
		if (rs2 == RegX0)
		  {
		    if (rd == RegX0)
		      execEbreak();
		    else
		      execJalr(RegRa, rd, 0);
		  }
		else
		  execAdd(rd, rd, rs2);
	      }
	  }
	  break;

	case 5:
	  illegalInst();
	  break;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    execSw(RegSp, csw.rs2, csw.immed());  // imm(sp) <- rs2
	  }
	  break;

	case 7:
	  illegalInst();
	  break;
	}
      break;

    case 3:  // quadrant 3
      illegalInst();
      break;

    default:
      illegalInst();
      break;
    }
}


template <typename URV>
void
Core<URV>::disassembleInst(uint32_t inst, std::ostream& stream)
{
  // Decode and disassemble
  if ((inst & 0x3) == 0x3) 
    disassembleInst32(inst, stream);
  else
    disassembleInst16(inst, stream);
}


template <typename URV>
void
Core<URV>::disassembleInst(uint32_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst(inst, oss);
  str = oss.str();
}


template <typename URV>
bool
Core<URV>::expandInst(uint16_t inst, uint32_t& code32) const
{
  code32 = 0; // Start with an illegal instruction.

  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  switch (quadrant)
    {
    case 0:    // quadrant 0
      switch (funct3) 
	{
	case 0:   // illegal, c.addi4spn
	  {
	    if (inst == 0)
	      return false;
	    CiwFormInst ciwf(inst);
	    unsigned immed = ciwf.immed();
	    if (immed == 0)
	      return false;
	    return IFormInst::encodeAddi(8+ciwf.rdp, RegSp, immed, code32);
	  }
	case 1: // c_fld, c_lq  
	  return false;
	case 2: // c.lw
	  {
	    ClFormInst c(inst);
	    return IFormInst::encodeLw(8+c.rdp, 8+c.rs1p, c.lwImmed(), code32);
	  }
	case 3:  // c.flw, c.ld
	  if (rv64_)
	    {
	      ClFormInst c(inst);
	      return IFormInst::encodeLd(8+c.rdp, 8+c.rs1p, c.lwImmed(), code32);
	    }
	  return false;  // c.flw
	case 4:  // reserved
	  return false;
	case 5:  // c.fsd, c.sq
	  return false;
	case 6:  // c.sw
	  {
	    CswFormInst csw(inst);
	    return SFormInst::encodeSw(8+csw.rs1p, 8+csw.rs2p, csw.immed(),
				       code32);
	  }
	case 7:  // c.fsw, c.sd
	  if (rv64_)
	    {
	      CswFormInst csw(inst);
	      return SFormInst::encodeSd(8+csw.rs1p, 8+csw.rs2p, csw.immed(),
					 code32);
	    }
	  return false;  // c.fsw
	}
      break;

    case 1:    // quadrant 1
      switch (funct3)
	{
	case 0:  // c.nop, c.addi
	  {
	    CiFormInst c(inst);
	    return IFormInst::encodeAddi(c.rd, c.rd, c.addiImmed(), code32);
	  }
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 tis is c.addiw
	  {
	    // jal(1, cjf.immed());
	    CjFormInst cjf(inst);
	    return JFormInst::encodeJal(RegRa, cjf.immed(), code32);
	  }

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    return IFormInst::encodeAddi(cif.rd, RegX0, cif.addiImmed(),
					 code32);
	  }

	case 3:  // ci.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed = cif.addi16spImmed();
	    if (immed == 0)
	      return false;
	    if (cif.rd == RegSp)  // c.addi16sp
	      return IFormInst::encodeAddi(cif.rd, cif.rd, cif.addi16spImmed(),
					   code32);
	    return UFormInst::encodeLui(cif.rd, cif.luiImmed(), code32);
	  }

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
	case 4:
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    unsigned rd = 8 + caf.rdp;
	    switch (caf.funct2)
	      {
	      case 0: // srli64, srli
		if (caf.ic5 != 0 and not rv64_)
		  return false;  // // As of v2.3 of User-Level ISA (Dec 2107).
		return IFormInst::encodeSrli(rd, rd, caf.shiftImmed(), code32);
	      case 1:  // srai64, srai
		if (caf.ic5 != 0 and not rv64_)
		  return false; // As of v2.3 of User-Level ISA (Dec 2107).
		return IFormInst::encodeSrai(rd, rd, caf.shiftImmed(), code32);
	      case 2:  // c.andi
		return IFormInst::encodeAndi(rd, rd, immed, code32);
	      case 3:  // c.sub c.xor c.or c.subw c.addw
		{
		  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
		  unsigned rs2 = 8 + rs2p;
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0: 
			  return RFormInst::encodeSub(rd, rd, rs2, code32);
			case 1:
			  return RFormInst::encodeXor(rd, rd, rs2, code32);
			case 2:
			  return RFormInst::encodeOr(rd, rd, rs2, code32);
			case 3: 
			  return RFormInst::encodeAnd(rd, rd, rs2,  code32);
			}
		    }
		  else
		    {
		      switch ((immed >> 3) & 3)
			{
			case 0: return false; // subw
			case 1: return false; // addw
			case 3: return false; // reserved
			case 4: return false; // reserved
			}
		    }
		}
		break;
	      }
	  }
	  break;

	case 5:  // c.j
	  {
	    // jal(0, cjf.immed());
	    CjFormInst cjf(inst);
	    return JFormInst:: encodeJal(RegX0, cjf.immed(), code32);
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    return BFormInst::encodeBeq(8+cbf.rs1p, RegX0, cbf.immed(), code32);
	  }

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    return BFormInst::encodeBne(8+cbf.rs1p, RegX0, cbf.immed(), code32);
	  }
	}
      break;

    case 2:    // quadrant 2
      switch (funct3)
	{
	case 0:  // c.slli, c.slli64
	  {
	    CiFormInst cif(inst);
	    unsigned immed = unsigned(cif.slliImmed());
	    if (cif.ic5 != 0 and not rv64_)
	      return false;
	    return IFormInst::encodeSlli(cif.rd, cif.rd, immed, code32);
	  }

	case 1:  // c.fldsp, c.lqsp
	  return false;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.rd;
	    // rd == 0 is legal per Andrew Watterman
	    return IFormInst::encodeLw(rd, RegSp, cif.lwspImmed(), code32);
	  }

	case 3:  // c.flwsp c.ldsp
	  return false;

	case 4:   // c.jr c.mv c.ebreak c.jalr c.add
	  {
	    CiFormInst cif(inst);
	    unsigned immed = cif.addiImmed();
	    unsigned rd = cif.rd;
	    unsigned rs2 = immed & 0x1f;
	    if ((immed & 0x20) == 0)
	      {
		if (rs2 == RegX0)
		  {
		    if (rd == RegX0)
		      return false;
		    return IFormInst::encodeJalr(RegX0, rd, 0, code32);
		  }
		return RFormInst::encodeAdd(rd, RegX0, rs2, code32);
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      return IFormInst::encodeEbreak(code32);
		    return IFormInst::encodeJalr(RegRa, rd, 0, code32);
		  }
		return RFormInst::encodeAdd(rd, rd, rs2,  code32);
	      }
	  }

	case 5:
	  return false;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    return SFormInst::encodeSw(RegSp, csw.rs2, csw.immed(), code32);
	  }

	case 7:
	  return false;
	}
      break;

    case 3:  // quadrant 3
      return false;

    default:
      return false;
    }

  return false;
}


template <typename URV>
void
Core<URV>::disassembleInst32(uint32_t inst, std::ostream& stream)
{
  if ((inst & 3) != 3)  // Must be in quadrant 3.
    {
      stream << "invalid";
      return;
    }

  unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

  switch (opcode)
    {
    case 0:  // 00000   I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	SRV imm = iform.immed<SRV>();
	switch (iform.fields.funct3)
	  {
	  case 0:
	    stream << "lb     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 1:
	    stream << "lh     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 2:
	    stream << "lw     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 3:
	    stream << "ld     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 4:
	    stream << "lbu    x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 5:
	    stream << "lhu    x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 6:
	    stream << "lwu    x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  default: stream << "invalid";
	    break;
	  }
      }
      break;

    case 3:  // 00011  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	unsigned funct3 = iform.fields.funct3;
	if (rd != 0 or rs1 != 0)
	  stream << "invalid";
	else if (funct3 == 0)
	  {
	    if (iform.top4() != 0)
	      stream << "invalid";
	    else
	      stream << "fence  " << iform.pred() << ", " << iform.succ();
	  }
	else if (funct3 == 1)
	  {
	    if (iform.uimmed() != 0)
	      stream << "invalid";
	    else
	      stream << "fence.i ";
	  }
	else
	  stream << "invalid";
      }
      break;

    case 4:  // 00100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	SRV imm = iform.immed<SRV>();
	switch (iform.fields.funct3)
	  {
	  case 0: 
	    stream << "addi   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 1: 
	    if (iform.top7() == 0)
	      stream << "slli   x" << rd << ", x" << rs1 << ", "
		     << iform.fields2.shamt;
	    else
	      stream << "invalid";
	    break;
	  case 2:
	    stream << "slti   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 3:
	    stream << "sltiu  x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 4:
	    stream << "xori   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 5:
	    if (iform.top7() == 0)
	      stream << "srli   x" << rd << ", x" << rs1 << ", "
		     << iform.fields2.shamt;
	    else if (iform.top7() == 0x20)
	      stream << "srai   x" << rd << ", x" << rs1 << ", "
		     << iform.fields2.shamt;
	    else
	      stream << "invalid";
	    break;
	  case 6:
	    stream << "ori    x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 7:
	    stream << "andi   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  default:
	    stream << "invalid";
	    break;
	  }
      }
      break;

    case 5:  // 00101   U-form
      {
	UFormInst uform(inst);
	stream << "auipc x" << uform.rd << ", 0x"
	       << std::hex << ((uform.immed<SRV>() >> 12) & 0xfffff);
      }
      break;

    case 8:  // 01000  S-form
      {
	SFormInst sform(inst);
	unsigned rs1 = sform.rs1, rs2 = sform.rs2;
	SRV imm = sform.immed<SRV>();
	switch (sform.funct3)
	  {
	  case 0:
	    stream << "sb     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 1:
	    stream << "sh     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 2:
	    stream << "sw     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  default:
	    stream << "invalid";
	    break;
	  }
      }
      break;

    case 11:  // 01011  R-form  atomics
      {
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.funct3;
	uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	bool r1 = rf.r1(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if (top5 == 0)          stream << "invalid";  // amoadd.w
	    else if (top5 == 1)     stream << "invalid";  // amoswap.w
	    else if (top5 == 2)     stream << "invalid";  // lr.w
	    else if (top5 == 3)     stream << "invalid";  // sc.w
	    else if (top5 == 4)     stream << "invalid";  // amoxor.w
	    else if (top5 == 8)     stream << "invalid";  // amoor.w
	    else if (top5 == 0x10)  stream << "invalid";  // amomin.w
	    else if (top5 == 0x14)  stream << "invalid";  // amomax.w
	    else if (top5 == 0x18)  stream << "invalid";  // maominu.w
	    else if (top5 == 0x1c)  stream << "invalid";  // maomaxu.w
	  }
	else if (f3 == 3)
	  {
	    if (top5 == 0)          stream << "invalid";  // amoadd.d
	    else if (top5 == 1)     stream << "invalid";  // amoswap.d
	    else if (top5 == 2)     stream << "invalid";  // lr.d
	    else if (top5 == 3)     stream << "invalid";  // sc.d
	    else if (top5 == 4)     stream << "invalid";  // amoxor.d
	    else if (top5 == 8)     stream << "invalid";  // amoor.d
	    else if (top5 == 0x10)  stream << "invalid";  // amomin.d
	    else if (top5 == 0x14)  stream << "invalid";  // amomax.d
	    else if (top5 == 0x18)  stream << "invalid";  // maominu.d
	    else if (top5 == 0x1c)  stream << "invalid";  // maomaxu.d
	  }
	else stream << "invalid";
      }
      break;

    case 12:  // 01100  R-form
      {
	RFormInst rform(inst);
	unsigned rd = rform.rd, rs1 = rform.rs1, rs2 = rform.rs2;
	unsigned funct7 = rform.funct7, funct3 = rform.funct3;
	if (funct7 == 0)
	  {
	    if (funct3 == 0)
	      stream << "add    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 1)
	      stream << "sll    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 2)
	      stream << "slt    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 3)
	      stream << "sltu   x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 4)
	      stream << "xor    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 5)
	      stream << "srl    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 6)
	      stream << "or     x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 7)
	      stream << "and    x" << rd << ", x" << rs1 << ", x" << rs2;
	  }
	else if (funct7 == 1)
	  {
	    if (funct3 == 0)
	      stream << "mul    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 1)
	      stream << "mulh   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 2)
	      stream << "mulhsu x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 3)
	      stream << "mulhu  x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 4)
	      stream << "div    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "divu   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 6)
	      stream << "rem    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 7)
	      stream << "remu   x" << rd << ", x" << rs1 << ", x" << rs2;
	  }
	else if (funct7 == 0x20)
	  {
	    if (funct3 == 0)
	      stream << "sub    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "sra    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "invalid";
	  }
	else
	  stream << "invalid";
      }
      break;

    case 13:  // 01101  U-form
      {
	UFormInst uform(inst);
	stream << "lui    x" << uform.rd << ", " << uform.immed<SRV>();
      }
      break;

    case 24:  // 11000   B-form
      {
	BFormInst bform(inst);
	unsigned rs1 = bform.rs1, rs2 = bform.rs2;
	SRV imm = bform.immed<SRV>();
	switch (bform.funct3)
	  {
	  case 0:
	    stream << "beq    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 1:
	    stream << "bne    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 4:
	    stream << "blt    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 5:
	    stream << "bge    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 6:
	    stream << "bltu   x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 7:
	    stream << "bgeu   x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  default:
	    stream << "invalid";
	    break;
	  }
      }
      break;

    case 25:  // 11001  I-form
      {
	IFormInst iform(inst);
	if (iform.fields.funct3 == 0)
	  stream << "jalr   x" << iform.fields.rd << ", x" << iform.fields.rs1
		 << ", " << iform.immed<SRV>();
	else
	  stream << "invalid";
      }
      break;

    case 27:  // 11011  J-form
      {
	JFormInst jform(inst);
	stream << "jal    x" << jform.rd << ", " << jform.immed<SRV>();
      }
      break;

    case 28:  // 11100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	CsrNumber csrNum = CsrNumber(iform.uimmed());
	std::string csrName;
	Csr<URV> csr;
	if (csRegs_.findCsr(csrNum, csr))
	  csrName = csr.getName();
	else
	  csrName = "invalid";
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      uint32_t func7 = iform.top7();
	      if (func7 == 0)
		{
		  if (rs1 != 0 or rd != 0)  stream << "invalid";
		  else if (csrNum == 0)     stream << "ecall";
		  else if (csrNum == 1)     stream << "ebreak";
		  else if (csrNum == 2)     stream << "uret";
		  else                      stream << "invalid";
		}
	      else if (func7 == 9)
		{
		  uint32_t rs2 = iform.rs2();
		  if (rd != 0) stream << "invalid";
		  else         stream << "SFENCE.VMA " << rs1 << ", " << rs2;
		}
	      else if (csrNum == 0x102) stream << "sret";
	      else if (csrNum == 0x302) stream << "mret";
	      else if (csrNum == 0x105) stream << "wfi";
	      else                      stream << "invalid";
	    }
	    break;
	  case 1:
	    stream << "csrrw  x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 2:
	    stream << "csrrs  x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 3:
	    stream << "csrrc  x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 5:
	    stream << "csrrwi x" << rd << ", " << csrName << ", " << rs1;
	    break;
	  case 6:
	    stream << "csrrsi x" << rd << ", " << csrName << ", " << rs1;
	    break;
	  case 7:
	    stream << "csrrci x" << rd << ", " << csrName << ", " << rs1;
	    break;
	  default: 
	    stream << "invalid";
	    break;
	  }
      }
      break;

    default:
      stream << "invlaid";
      break;
    }
}


template <typename URV>
void
Core<URV>::disassembleInst16(uint16_t inst, std::ostream& stream)
{
  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  switch (quadrant)
    {
    case 0:    // quadrant 0
      switch (funct3) 
	{
	case 0:   // illegal, c.addi4spn
	  {
	    if (inst == 0)
	      stream << "invalid";
	    else
	      {
		CiwFormInst ciwf(inst);
		unsigned immed = ciwf.immed();
		if (immed == 0)
		  stream << "invalid";
		else
		  stream << "c.addi4spn x" << ciwf.rdp << ", " << (immed >> 2);
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  stream << "invalid";
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    stream << "c.lw   x" << clf.rdp << ", " << clf.lwImmed() << "(x"
		   << clf.rs1p << ")";
	  }
	  break;
	case 3:  // c.flw, c.ld
	  stream << "invalid";
	  break;
	case 4:  // reserver
	  stream << "invalid";
	  break;
	case 5:  // c.fsd, c.sq
	  stream << "invalid";
	  break;
	case 6:  // c.sw
	  {
	    CswFormInst csw(inst);
	    stream << "c.sw   x" << csw.rs2p << ", " << csw.immed() << "(x"
		   << csw.rs1p << ")";
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  stream << "invalid";
	  break;
	}
      break;

    case 1:    // quadrant 1
      switch (funct3)
	{
	case 0:  // c.nop, c.addi
	  {
	    CiFormInst cif(inst);
	    if (cif.rd == 0)
	      stream << "c.nop";
	    else
	      stream << "c.addi x" << cif.rd << ", " << cif.addiImmed();
	  }
	  break;
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 tis is c.addiw
	  {
	    CjFormInst cjf(inst);
	    stream << "c.jal   " << cjf.immed();
	  }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    stream << "c.li   x" << cif.rd << ", " << cif.addiImmed();
	  }
	  break;

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      stream << "invalid";
	    else if (cif.rd == RegSp)
	      stream << "c.addi16sp" << ' ' << (immed16 >> 4);
	    else
	      stream << "c.lui  x" << cif.rd << ", " << cif.luiImmed();
	  }
	  break;

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
	case 4:
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    unsigned rd = 8 + caf.rdp;
	    switch (caf.funct2)
	      {
	      case 0:
		if (caf.ic5 != 0 and not rv64_)
		  stream << "invalid";
		else
		  stream << "c.srli x" << caf.rdp << ", " << caf.shiftImmed();
		break;
	      case 1:
		if (caf.ic5 != 0 and not rv64_)
		  stream << "invalid";
		else
		  stream << "c.srai x" << caf.rdp << ", " << caf.shiftImmed();
		break;
	      case 2:
		stream << "c.andi x" << caf.rdp << ", " << immed;
		break;
	      case 3:
		{
		  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0:
			  stream << "c.sub  x" << caf.rdp << ", x" << rs2p; break;
			case 1:
			  stream << "c.xor  x" << caf.rdp << ", x" << rs2p; break;
			case 2:
			  stream << "c.or   x"  << caf.rdp << ", x" << rs2p; break;
			case 3:
			  stream << "c.and  x" << caf.rdp << ", x" << rs2p; break;
			}
		    }
		  else
		    {
		      switch ((immed >> 3) & 3)
			{
			case 0: stream << "invalid"; break; // subw
			case 1: stream << "invalid"; break; // addw
			case 3: stream << "invalid"; break; // reserved
			case 4: stream << "invalid"; break; // reserved
			}
		    }
		}
		break;
	      }
	  }
	  break;

	case 5:  // c.j
	  {
	    CjFormInst cjf(inst);
	    stream << "c.j " << cjf.immed();
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    stream << "c.beqz x" << cbf.rs1p << ", " << cbf.immed();
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    stream << "c.bnez x" << cbf.rs1p << ", " << cbf.immed();
	  }
	  break;
	}
      break;

    case 2:    // quadrant 2
      switch (funct3)
	{
	case 0:  // c.slli c.slli64
	  {
	    CiFormInst cif(inst);
	    unsigned immed = unsigned(cif.slliImmed());
	    if (cif.ic5 != 0 and not rv64_)
	      stream << "invalid";  // TBD: ok for RV64
	    else
	      stream << "c.slli x" << cif.rd << ", " << immed;
	  }
	  break;

	case 1:   // c.fldsp, c.lqsp
	  stream << "invalid";
	  break;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.rd;
	    // rd == 0 is legal per Andrew Watterman
	    stream << "c.lwsp x" << rd << ", " << (cif.lwspImmed() >> 2);
	  }
	break;

	case 3:  // c.flwsp c.ldsp
	  stream << "invalid";
	  break;

	case 4:  // c.jr c.mv c.ebreak c.jalr c.add
	  {
	    CiFormInst cif(inst);
	    unsigned immed = cif.addiImmed();
	    unsigned rd = cif.rd;
	    unsigned rs2 = immed & 0x1f;
	    if ((immed & 0x20) == 0)
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "invalid";
		    else
		      stream << "c.jr   x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "invalid";
		    else
		      stream << "c.mv   x" << rd << ", x" << rs2;
		  }
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "c.ebreak";
		    else
		      stream << "c.jalr x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "invalid";
		    else
		      stream << "c.add  x" << rd << ", x" << rs2;
		  }
	      }
	  }
	  break;

	case 5:  // c.fsdsp  c.sqsp
	  stream << "invalid";
	  break;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    stream << "c.swsp x" << csw.rs2 << ", " << (csw.immed() >> 2);
	  }
	  break;

	case 7:  // c.fswsp c.sdsp
	  stream << "invalid";
	  break;
	}
      break;

    case 3:  // quadrant 3
      stream << "invalid";
      break;

    default:
      stream << "invalid";
      break;
    }
}


template <typename URV>
void
Core<URV>::disassembleInst32(uint32_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst32(inst, oss);
  str = oss.str();
}


template <typename URV>
void
Core<URV>::disassembleInst16(uint16_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst16(inst, oss);
  str = oss.str();
}


template <typename URV>
void
Core<URV>::execBeq(uint32_t rs1, uint32_t rs2, Core<URV>::SRV offset)
{
  if (intRegs_.read(rs1) == intRegs_.read(rs2))
    {
      pc_ = currPc_ + offset;
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBne(uint32_t rs1, uint32_t rs2, Core<URV>::SRV offset)
{
  if (intRegs_.read(rs1) != intRegs_.read(rs2))
    {
      pc_ = currPc_ + offset;
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBlt(uint32_t rs1, uint32_t rs2, SRV offset)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  if (v1 < v2)
    {
      pc_ = currPc_ + offset;
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBltu(uint32_t rs1, uint32_t rs2, SRV offset)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  if (v1 < v2)
    {
      pc_ = currPc_ + offset;
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBge(uint32_t rs1, uint32_t rs2, SRV offset)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    {
      pc_ = currPc_ + offset;
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBgeu(uint32_t rs1, uint32_t rs2, SRV offset)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    {
      pc_ = currPc_ + offset;
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execJalr(uint32_t rd, uint32_t rs1, SRV offset)
{
  URV temp = pc_;  // pc has the address of the instruction adter jalr
  pc_ = (intRegs_.read(rs1) + offset);
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  intRegs_.write(rd, temp);
}


template <typename URV>
void
Core<URV>::execJal(uint32_t rd, SRV offset)
{
  intRegs_.write(rd, pc_);
  pc_ = currPc_ + offset;
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
}


template <typename URV>
void
Core<URV>::execLui(uint32_t rd, SRV imm)
{
  intRegs_.write(rd, imm);
}


template <typename URV>
void
Core<URV>::execAuipc(uint32_t rd, SRV imm)
{
  intRegs_.write(rd, currPc_ + imm);
}


template <typename URV>
void
Core<URV>::execAddi(uint32_t rd, uint32_t rs1, SRV imm)
{
  SRV v = intRegs_.read(rs1);
  v += imm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSlli(uint32_t rd, uint32_t rs1, uint32_t amount)
{
  if ((amount & 0x20) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = intRegs_.read(rs1) << amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSlti(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV v = SRV(intRegs_.read(rs1)) < imm ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSltiu(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) < uimm ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execXori(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) ^ uimm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrli(uint32_t rd, uint32_t rs1, uint32_t amount)
{
  if ((amount & 0x20) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = intRegs_.read(rs1) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrai(uint32_t rd, uint32_t rs1, uint32_t amount)
{
  if ((amount & 0x20) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = SRV(intRegs_.read(rs1)) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execOri(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) | uimm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execAndi(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) & uimm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execAdd(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) + intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSub(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) - intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSll(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) << (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSlt(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSltu(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execXor(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) ^ intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrl(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSra(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = SRV(intRegs_.read(rs1)) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execOr(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) | intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execAnd(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) & intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execFence(uint32_t pred, uint32_t succ)
{
  return;  // Currently a no-op.
}


template <typename URV>
void
Core<URV>::execFencei()
{
  return;  // Currently a no-op.
}


template <typename URV>
void
Core<URV>::execEcall()
{
  if (privilegeMode_ == MACHINE_MODE)
    initiateException(M_ENV_CALL, currPc_, 0);
  else if (privilegeMode_ == SUPERVISOR_MODE)
    initiateException(S_ENV_CALL, currPc_, 0);
  else if (privilegeMode_ == USER_MODE)
    initiateException(U_ENV_CALL, currPc_, 0);
  else
    assert(0 and "Invalid privilege mode in execEcall");
}


template <typename URV>
void
Core<URV>::execEbreak()
{
  URV savedPc = currPc_;  // Goes into MEPC.

  // Goes into MTVAL: Sec 3.1.21 of RISCV privileged arch (version 1.11).
  URV trapInfo = currPc_;

  initiateException(BREAKPOINT, currPc_, currPc_);
}


template <typename URV>
void
Core<URV>::execMret()
{
  if (privilegeMode_ < MACHINE_MODE)
    illegalInst();
  else
    {
      // Restore privilege mode and interrupt enable by getting
      // current value of MSTATUS ...
      URV value = 0;
      if (not csRegs_.read(MSTATUS_CSR, privilegeMode_, value))
	assert(0 and "Failed to write MSTATUS register\n");

      // ... updating/unpacking its fields
      MstatusFields<URV> fields(value);
      PrivilegeMode savedMode = PrivilegeMode(fields.MPP);
      fields.MIE = fields.MPIE;
      fields.MPP = 0;
      fields.MPIE = 1;

      // ... and putting it back
      if (not csRegs_.write(MSTATUS_CSR, privilegeMode_, fields.value_))
	assert(0 and "Failed to write MSTATUS register\n");

      // TBD: Handle MPV.

      // Restore program counter from MEPC.
      URV epc;
      if (not csRegs_.read(MEPC_CSR, privilegeMode_, epc))
	illegalInst();
      pc_ = (epc >> 1) << 1;  // Restore pc clearing least sig bit.
      
      // Update privilege mode.
      privilegeMode_ = savedMode;
    }
}


template <typename URV>
void
Core<URV>::execSret()
{
  unimplemented();  // Not yet implemented.
}


template <typename URV>
void
Core<URV>::execUret()
{
  illegalInst();  // Not yet implemented.
}


template <typename URV>
void
Core<URV>::execWfi()
{
  return;   // Currently implemented as a no-op.
}


// Set control and status register csr to value of register rs1 and
// save its original value in register rd.
template <typename URV>
void
Core<URV>::execCsrrw(uint32_t rd, uint32_t csr, uint32_t rs1)
{
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    csRegs_.setRetiredInstCount(retiredInsts_);

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    csRegs_.setCycleCount(cycleCount_);

  URV prev;
  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  if (not csRegs_.write(CsrNumber(csr), privilegeMode_, intRegs_.read(rs1)))
    {
      illegalInst();
      return;
    }

  intRegs_.write(rd, prev);

  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    if (csRegs_.getRetiredInstCount(retiredInsts_))
      retiredInsts_--;

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    if (csRegs_.getCycleCount(cycleCount_))
      cycleCount_--;
}


template <typename URV>
void
Core<URV>::execCsrrs(uint32_t rd, uint32_t csr, uint32_t rs1)
{
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    csRegs_.setRetiredInstCount(retiredInsts_);

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    csRegs_.setCycleCount(cycleCount_);

  URV prev;
  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev | intRegs_.read(rs1);

  if (rs1 != 0)
    if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
      {
	illegalInst();
	return;
      }

  intRegs_.write(rd, prev);

  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    if (csRegs_.getRetiredInstCount(retiredInsts_))
      retiredInsts_--;

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    if (csRegs_.getCycleCount(cycleCount_))
      cycleCount_--;
}


template <typename URV>
void
Core<URV>::execCsrrc(uint32_t rd, uint32_t csr, uint32_t rs1)
{
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    csRegs_.setRetiredInstCount(retiredInsts_);

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    csRegs_.setCycleCount(cycleCount_);

  URV prev;

  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev & (~ intRegs_.read(rs1));

  if (rs1 != 0)
    if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
      {
	illegalInst();
	return;
      }

  intRegs_.write(rd, prev);

  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    if (csRegs_.getRetiredInstCount(retiredInsts_))
      retiredInsts_--;

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    if (csRegs_.getCycleCount(cycleCount_))
      cycleCount_--;
}


template <typename URV>
void
Core<URV>::execCsrrwi(uint32_t rd, uint32_t csr, URV imm)
{
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    csRegs_.setRetiredInstCount(retiredInsts_);

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    csRegs_.setCycleCount(cycleCount_);

  URV prev;
  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  if (not csRegs_.write(CsrNumber(csr), privilegeMode_, imm))
    {
      illegalInst();
      return;
    }

  intRegs_.write(rd, prev);

  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    if (csRegs_.getRetiredInstCount(retiredInsts_))
      retiredInsts_--;

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    if (csRegs_.getCycleCount(cycleCount_))
      cycleCount_--;
}


template <typename URV>
void
Core<URV>::execCsrrsi(uint32_t rd, uint32_t csr, URV imm)
{
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    csRegs_.setRetiredInstCount(retiredInsts_);

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    csRegs_.setCycleCount(cycleCount_);

  URV prev;
  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev | imm;

  if (imm != 0)
    if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
      {
	illegalInst();
	return;
      }

  intRegs_.write(rd, prev);

  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    if (csRegs_.getRetiredInstCount(retiredInsts_))
      retiredInsts_--;

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    if (csRegs_.getCycleCount(cycleCount_))
      cycleCount_--;
}


template <typename URV>
void
Core<URV>::execCsrrci(uint32_t rd, uint32_t csr, URV imm)
{
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    csRegs_.setRetiredInstCount(retiredInsts_);

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    csRegs_.setCycleCount(cycleCount_);

  URV prev;

  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev & (~ imm);

  if (imm != 0)
    if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
      {
	illegalInst();
	return;
      }

  intRegs_.write(rd, prev);

  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    if (csRegs_.getRetiredInstCount(retiredInsts_))
      retiredInsts_--;

  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    if (csRegs_.getCycleCount(cycleCount_))
      cycleCount_--;
}


template <typename URV>
void
Core<URV>::execLb(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint8_t byte;  // Use a signed type.
  if (memory_.readByte(address, byte))
    {
      SRV value = int8_t(byte); // Sign extend.
      intRegs_.write(rd, value);
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execLh(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint16_t half;  // Use a signed type.
  if (memory_.readHalfWord(address, half))
    {
      SRV value = int16_t(half); // Sign extend.
      intRegs_.write(rd, value);
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execLw(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint32_t word;  // Use a signed type.
  if (memory_.readWord(address, word))
    {
      SRV value = int32_t(word); // Sign extend.
      intRegs_.write(rd, value);
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execLbu(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint8_t byte;  // Use an unsigned type.
  if (memory_.readByte(address, byte))
    {
      intRegs_.write(rd, byte); // Zero extend into register.
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execLhu(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint16_t half;  // Use an unsigned type.
  if (memory_.readHalfWord(address, half))
    {
      intRegs_.write(rd, half); // Zero extend into register.
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execSb(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  URV regVal = intRegs_.read(rs2);
  uint8_t byte = regVal;

  // If we write to special location, end the simulation.
  if (toHostValid_ and address == toHost_)
    {
      if (memory_.writeByte(address, byte))
	lastWrittenWord_ = regVal;   // Compat with spike tracer
      throw std::exception();
    }

  if (not memory_.writeByte(address, byte))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
  else
    lastWrittenWord_ = regVal;  // Compat with spike tracer
}


template <typename URV>
void
Core<URV>::execSh(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  URV regVal = intRegs_.read(rs2);
  uint16_t half = regVal;

  // If we write to special location, end the simulation.
  if (toHostValid_ and address == toHost_)
    {
      if (memory_.writeHalfWord(address, half))
	lastWrittenWord_ = regVal;   // Compat with spike tracer
      throw std::exception();
    }

  if (not memory_.writeHalfWord(address, half))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
  else
    lastWrittenWord_ = regVal;   // Compat with spike tracer
}


template <typename URV>
void
Core<URV>::execSw(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint32_t word = intRegs_.read(rs2);

  // If we write to special location, end the simulation.
  if (toHostValid_ and address == toHost_)
    {
      if (memory_.writeWord(address, word))
	lastWrittenWord_ = word;  // Compat with spike tracer
      throw std::exception();
    }

  if (not memory_.writeWord(address, word))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
  else
    lastWrittenWord_ = word;   // Compat with spike tracer
}


namespace WdRiscv
{

  template<>
  void
  Core<uint32_t>::execMul(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int32_t a = intRegs_.read(rs1);
    int32_t b = intRegs_.read(rs2);

    int32_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint32_t>::execMulh(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend.
    int64_t b = int32_t(intRegs_.read(rs2));
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhsu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));
    uint64_t b = intRegs_.read(rs2);
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    uint64_t a = intRegs_.read(rs1);
    uint64_t b = intRegs_.read(rs2);
    uint64_t c = a * b;
    uint32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template<>
  void
  Core<uint64_t>::execMul(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend to 64-bit
    __int128_t b = int64_t(intRegs_.read(rs2));

    int64_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint64_t>::execMulh(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend.
    __int128_t b = int64_t(intRegs_.read(rs2));
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhsu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));
    __uint128_t b = intRegs_.read(rs2);
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __uint128_t a = intRegs_.read(rs1);
    __uint128_t b = intRegs_.read(rs2);
    __uint128_t c = a * b;
    uint64_t high = c >> 64;

    intRegs_.write(rd, high);
  }

}


template <typename URV>
void
Core<URV>::execDiv(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = -1;   // Divide by zero result
  if (b != 0)
    c = a / b;
  intRegs_.write(rd, c);
}


template <typename URV>
void
Core<URV>::execDivu(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV a = intRegs_.read(rs1);
  URV b = intRegs_.read(rs2);
  URV c = ~ URV(0);  // Divide by zero result.
  if (b != 0)
    c = a / b;
  intRegs_.write(rd, c);
}


// Remainder instruction.
template <typename URV>
void
Core<URV>::execRem(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = a;  // Divide by zero remainder.
  if (b != 0)
    c = a % b;
  intRegs_.write(rd, c);
}


// Unsigned remainder instruction.
template <typename URV>
void
Core<URV>::execRemu(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV a = intRegs_.read(rs1);
  URV b = intRegs_.read(rs2);
  URV c = a;  // Divide by zero remainder.
  if (b != 0)
    c = a % b;
  intRegs_.write(rd, c);
}


template <typename URV>
void
Core<URV>::execLwu(uint32_t rd, uint32_t rs1, SRV imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + imm;
  uint32_t word;  // Use an unsigned type.
  if (memory_.readWord(address, word))
    {
      intRegs_.write(rd, word); // Zero extend into register.
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execLd(uint32_t rd, uint32_t rs1, SRV imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + imm;
  uint64_t value;
  if (memory_.readDoubleWord(address, value))
    intRegs_.write(rd, value);
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execSd(uint32_t rs1, uint32_t rs2, SRV imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + imm;
  uint64_t value = intRegs_.read(rs2);

  // If we write to special location, end the simulation.
  if (toHostValid_ and address == toHost_)
    {
      if (memory_.writeDoubleWord(address, value))
	lastWrittenWord_ = value;  // Compat with spike tracer
      throw std::exception();
    }

  if (not memory_.writeDoubleWord(address, value))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
  else
    lastWrittenWord_ = value;  // Compat with spike tracer
}


template class Core<uint32_t>;
template class Core<uint64_t>;
