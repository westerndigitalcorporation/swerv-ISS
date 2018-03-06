#include <iomanip>
#include <iostream>
#include <sstream>
#include <boost/format.hpp>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "Core.hpp"
#include "instforms.hpp"

using namespace WdRiscv;


template <typename TYPE>
static
bool
parseNumber(const std::string& numberStr, TYPE& number)
{
  bool good = not numberStr.empty();

  if (good)
    {
      char* end = nullptr;
      if (sizeof(TYPE) == 4)
	number = strtoul(numberStr.c_str(), &end, 0);
      else if (sizeof(TYPE) == 8)
	number = strtoull(numberStr.c_str(), &end, 0);
      else
	{
	  std::cerr << "Only 32 and 64-bit numbers supported in "
		    << "parseCmdLineNumber\n";
	  return false;
	}
      if (end and *end)
	good = false;  // Part of the string are non parseable.
    }
  return good;
}


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
Core<URV>::pokeMemory(size_t address, uint32_t val)
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
inline
void
Core<URV>::execBne(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  if (intRegs_.read(rs1) == intRegs_.read(rs2))
    return;
  pc_ = currPc_ + SRV(offset);
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
}


template <typename URV>
inline
void
Core<URV>::execAddi(uint32_t rd, uint32_t rs1, int32_t imm)
{
  SRV v = intRegs_.read(rs1) + SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
inline
void
Core<URV>::execAdd(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) + intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execLw(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  uint32_t word;
  if (memory_.readWord(address, word))
    {
      SRV value = int32_t(word); // Sign extend.
      intRegs_.write(rd, value);
    }
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
bool
Core<URV>::selfTest()
{
  size_t errors = 0;

  // Writing x0 has no effect. Reading x0 yields zero.
  execOri(RegX0, RegX1, ~0);           // ori x0, x1, 0xffff    
  if (intRegs_.read(RegX0) != 0)
    {
      std::cerr << "Writing to x0 erroneously effectual.\n";
      errors++;
    }
  execAndi(RegX1, RegX0, ~0);         // andi x1, x0, 0xffff     x1 <- 0
  if (intRegs_.read(RegX1) != 0)
    {
      std::cerr << "Reading x0 yielded non-zero value\n";
      errors++;
    }

  // All bits of registers (except x0) toggle.
  for (uint32_t ix = 1; ix < intRegs_.size(); ++ix)
    {
      execAddi(ix, RegX0, 0);          // reg[ix] <- 0
      execXori(ix, RegX0, ~0);    // reg[ix] <- ~0
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
  if (not memory_.readHalfWord(address, low))
    return false;

  inst = low;

  if ((inst & 0x3) == 3)  // Non-compressed instruction.
    {
      uint16_t high;
      if (not memory_.readHalfWord(address + 2, high))
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
  URV info = 0;  // This goes into mtval.
  initiateTrap(interrupt, cause, pc, info);
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
  trapCount_++;

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

  // Clear mtval on interrupts. Save synchronous exception info.
  if (not csRegs_.write(tvalNum, privilegeMode_, info))
    assert(0 and "Failed to write TVAL register");

  // Update status register saving xIE in xPIE and prevoius privilege
  // mode in xPP by getting current value of mstatus ...
  URV status = 0;
  if (not csRegs_.read(MSTATUS_CSR, privilegeMode_, status))
    assert(0 and "Failed to read MSTATUS register");

  // ... updating its fields
  MstatusFields<URV> msf(status);

  if (nextMode == MACHINE_MODE)
    {
      msf.bits_.MPP = prevMode;
      msf.bits_.MPIE = msf.bits_.MIE;
      msf.bits_.MIE = 0;
    }
  else if (nextMode == SUPERVISOR_MODE)
    {
      msf.bits_.SPP = prevMode;
      msf.bits_.SPIE = msf.bits_.SIE;
      msf.bits_.SIE = 0;
    }
  else if (nextMode == USER_MODE)
    {
      msf.bits_.UPIE = msf.bits_.UIE;
      msf.bits_.UIE = 0;
    }

  // ... and putting it back
  if (not csRegs_.write(MSTATUS_CSR, privilegeMode_, msf.value_))
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

  if (not csr.isImplemented())
    return false;

  return csRegs_.read(csrn, MACHINE_MODE, val);
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val, std::string& name) const
{ 
  Csr<URV> csr;
  if (not csRegs_.findCsr(csrn, csr))
    return false;

  if (not csr.isImplemented())
    return false;

  if (csRegs_.read(csrn, MACHINE_MODE, val))
    {
      name = csr.getName();
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::pokeCsr(CsrNumber csr, URV val)
{ 
  bool ok = csRegs_.write(csr, MACHINE_MODE, val);
  if (ok and csr == MIP_CSR)
    {
      // The MIP mask prevents the the direct writing of the meip and
      // mtip bits. Set those bits indirectly.
      bool meip = (val & (1 << MeipBit)) != 0;
      csRegs_.setMeip(meip);
      bool mtip = (val & (1 << MtipBit)) != 0;
      csRegs_.setMtip(mtip);
    }

  return ok;
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
  if (intRegs_.findReg(name, num))
    return true;

  unsigned n = 0;
  if (parseNumber<unsigned>(name, n) and n < intRegs_.size())
    {
      num = n;
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::findCsr(const std::string& name, CsrNumber& num) const
{
  Csr<URV> csr;
  if (csRegs_.findCsr(name, csr))
    {
      num = csr.getNumber();
      return true;
    }

  unsigned n = 0;
  if (parseNumber<unsigned>(name, n))
    {
      CsrNumber csrn = CsrNumber(n);
      if (csRegs_.findCsr(csrn, csr))
	{
	  num = csr.getNumber();
	  return true;
	}
    }

  return false;
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
    {
      // 2-byte instruction: Clear top 16 bits
      uint16_t low = inst;
      inst = low;
      sprintf(instBuff, "%04x", inst);
    }

  bool pending = false;  // True if a printed line need to be terminated.

  // Process integer register diff.
  int reg = intRegs_.getLastWrittenReg();
  URV value = 0;
  if (reg > 0)
    {
      value = intRegs_.read(reg);
      if (sizeof(URV) == 4)
	fprintf(out, "#%ld %d %08x %8s r %08x %08x  %s",
		tag, hartId_, uint32_t(currPc_), instBuff, reg, uint32_t(value),
		tmp.c_str());
      else
	fprintf(out, "#%ld %d %016lx %8s r %08x %016lx  %s",
		tag, hartId_, uint64_t(currPc_), instBuff, reg, uint64_t(value),
		tmp.c_str());
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
	  if (sizeof(URV) == 4)
	    fprintf(out, "#%ld %d %08x %8s c %08x %08x  %s",
		    tag, hartId_, uint32_t(currPc_), instBuff, csr,
		    uint32_t(value), tmp.c_str());
	  else
	    fprintf(out, "#%ld %d %016lx %8s c %08x %016lx  %s",
		    tag, hartId_, uint64_t(currPc_), instBuff, csr,
		    uint64_t(value), tmp.c_str());

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
	  if (sizeof(URV) == 4)
	    fprintf(out, "#%ld %d %08x %8s m %08x %08x", tag,
		    hartId_, uint32_t(currPc_), instBuff, uint32_t(address),
		    word);
	  else
	    fprintf(out, "#%ld %d %016lx %8s m %016lx %08x", tag,
		    hartId_, uint64_t(currPc_), instBuff, uint64_t(address),
		    word);
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

      if (sizeof(URV) == 4)
	fprintf(out, "#%ld %d %08x %8s m %08x %08x", tag,
		hartId_, uint32_t(currPc_), instBuff, uint32_t(address), word);
      else
	fprintf(out, "#%ld %d %016lx %8s m %016lx %08x", tag,
		hartId_, uint64_t(currPc_), instBuff, address, word);
      fprintf(out, "  %s", tmp.c_str());
      pending = true;
    }

  if (pending) 
    fprintf(out, "\n");
  else
    {
      // No diffs: Generate an x0 record.
      if (sizeof(URV) == 4)
	fprintf(out, "#%ld %d %08x %8s r %08x %08x  %s\n",
		tag, hartId_, uint32_t(currPc_), instBuff, 0, 0, tmp.c_str());
      else
	fprintf(out, "#%ld %d %016lx %8s r %08x %08x  %s\n",
		tag, hartId_, uint64_t(currPc_), instBuff, 0, 0, tmp.c_str());
    }
}


template <typename URV>
URV
Core<URV>::lastPc() const
{
  return currPc_;
}


template <typename URV>
int
Core<URV>::lastIntReg() const
{
  return intRegs_.getLastWrittenReg();
}


template <typename URV>
void
Core<URV>::lastCsr(std::vector<CsrNumber>& csrs) const
{
  csRegs_.getLastWrittenRegs(csrs);
}


template <typename URV>
void
Core<URV>::lastMemory(std::vector<size_t>& addresses,
		      std::vector<uint32_t>& words) const
{
  addresses.clear();
  words.clear();

  size_t address = 0;
  unsigned writeSize = memory_.getLastWriteInfo(address);
  uint32_t word = 0;

  if (writeSize > 0)
    {
      // Temporary: Compatibility with spike trace.
      bool spikeCompat = true;
      if (spikeCompat)
	{
	  addresses.clear();
	  words.clear();
	  addresses.push_back(address);
	  words.push_back(lastWrittenWord_);
	  return;
	}

      if (writeSize <= 2)
	{
	  for (size_t i = 0; i < 4; i++)
	    {
	      uint8_t byte = 0;
	      memory_.readByte(address + i, byte);
	      word = word | (uint32_t(byte) << (8*i));
	    }

	  addresses.push_back(address);
	  words.push_back(word);
	}
      else if (writeSize == 4)
	{
	  memory_.readWord(address, word);
	  addresses.push_back(address);
	  words.push_back(word);
	}
      else if (writeSize == 8)
	{
	  memory_.readWord(address, word);
	  addresses.push_back(address);
	  words.push_back(word);

	  memory_.readWord(address+4, word);
	  addresses.push_back(address+4);
	  words.push_back(word);

	}
      else
	std::cerr << "Houston we have a problem. Unhandeled write size "
		  << writeSize << " at instruction address "
		  << std::hex << currPc_ << std::endl;
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
  retiredInsts_ = csRegs_.getRetiredInstCount();
  cycleCount_ = csRegs_.getCycleCount();

  bool trace = traceFile != nullptr;
  csRegs_.traceWrites(trace);

  uint64_t counter = counter_;
  uint64_t limit = instCountLim_;

  try
    {
      while (pc_ != address and counter < limit)
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

	  // Execute instruction
	  if ((inst & 3) == 3)
	    {
	      // 4-byte instruction
	      pc_ += 4;
	      execute32(inst);
	    }
	  else
	    {
	      // Compressed (2-byte) instruction.
	      pc_ += 2;
	      execute16(inst);
	    }

	  ++cycleCount_;
	  ++retiredInsts_;
	  ++counter;

	  if (__builtin_expect(trace, 0))
	    traceInst(inst, counter, instStr, traceFile);
	}
    }
  catch (...)
    {  // Wrote to tohost
      if (trace)
	{
	  uint32_t inst = 0;
	  readInst(currPc_, inst);
	  ++counter;
	  traceInst(inst, counter, instStr, traceFile);
	}
      std::cout.flush();
      std::cerr << "Stopped...\n";
    }

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);
  counter_ = counter;

  // Simulator stats.
  struct timeval t1;
  gettimeofday(&t1, nullptr);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)*1e-6;

  std::cout << "Retired " << counter << " instruction"
	    << (counter > 1? "s" : "") << " in "
	    << (boost::format("%.2fs") % elapsed);
  if (elapsed > 0)
    std::cout << "  " << size_t(counter/elapsed) << " inst/s";
  std::cout << '\n';
}


/// Run indefinitely.  If the tohost address is defined, then run till
/// a write is attempted to that address.
template <typename URV>
void
Core<URV>::run(FILE* file)
{
  if (stopAddrValid_ and not toHostValid_)
    {
      runUntilAddress(stopAddr_, file);
      return;
    }

  if (file)
    {
      URV address = ~URV(0);  // Invalid stop PC.
      runUntilAddress(address, file);
      return;
    }

  struct timeval t0;
  gettimeofday(&t0, nullptr);

  csRegs_.traceWrites(false);

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  retiredInsts_ = csRegs_.getRetiredInstCount();
  cycleCount_ = csRegs_.getCycleCount();

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

	  // Execute instruction
	  if ((inst & 3) == 3)
	    {
	      // 4-byte instruction
	      pc_ += 4;
	      execute32(inst);
	    }
	  else
	    {
	      // Compressed (2-byte) instruction.
	      pc_ += 2;
	      execute16(inst);
	    }

	  ++cycleCount_;
	  ++retiredInsts_;
	}
    }
  catch (...)
    {
      std::cout.flush();
      std::cerr << "stopped..\n";
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


template <typename URV>
bool
Core<URV>::isInterruptPossible(InterruptCause& cause)
{
  URV mstatus;
  if (not csRegs_.read(MSTATUS_CSR, MACHINE_MODE, mstatus))
    return false;

  MstatusFields<URV> fields(mstatus);
  if (not fields.bits_.MIE)
    return false;

  URV mip, mie;
  if (csRegs_.read(MIP_CSR, MACHINE_MODE, mip) and
      csRegs_.read(MIE_CSR, MACHINE_MODE, mie))
    {
      // Order of priority: machine, supervisor, user and then
      //  external, software, timer
      if (mie & (1 << MeipBit) & mip)
	{
	  cause = M_EXTERNAL;
	  return true;
	}
      if (mie & (1 << MsipBit) & mip)
	{
	  cause = M_SOFTWARE;
	  return true;
	}
      if (mie & (1 << MtipBit) & mip)
	{
	  cause = M_TIMER;
	  return true;
	}
    }

  return false;
}


template <typename URV>
void
Core<URV>::singleStep(FILE* traceFile)
{
  std::string instStr;

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  retiredInsts_ = csRegs_.getRetiredInstCount();
  cycleCount_ = csRegs_.getCycleCount();

  bool trace = traceFile != nullptr;
  csRegs_.traceWrites(trace);

  try
    {
      // Reset trace data (items changed by the execution of an instr)
      if (__builtin_expect(trace, 0))
	{
	  intRegs_.clearLastWrittenReg();
	  csRegs_.clearLastWrittenRegs();
	  memory_.clearLastWriteInfo();
	}

      // Check if there is a pending interrupt and interrupts are enabled.
      // If so, take interrupt.
      InterruptCause cause;
      if (isInterruptPossible(cause))
	initiateInterrupt(cause, pc_);

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
	  if (traceFile)
	    fprintf(traceFile, "exception\n");
	  return; // Next instruction in trap handler.
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
	      if (traceFile)
		fprintf(traceFile, "exception\n");
	      return; // Next instruction in trap handler.
	    }
	  inst = half;
	  if ((inst & 3) == 3)
	    { // 4-byte instruction but 4-byte fetch fails.
	      ++cycleCount_;
	      if (traceFile)
		fprintf(traceFile, "exception\n");
	      return; // Next instruction in trap handler.
	    }
	}

      // Execute instruction
      if ((inst & 3) == 3)
	{
	  // 4-byte instruction
	  pc_ += 4;
	  execute32(inst);
	}
      else
	{
	  // Compressed (2-byte) instruction.
	  pc_ += 2;
	  execute16(inst);
	}

      ++cycleCount_;
      ++retiredInsts_;
      ++counter_;

      if (__builtin_expect(trace, 0))
	traceInst(inst, counter_, instStr, traceFile);
    }
  catch (...)
    {  // Wrote to tohost
      if (trace)
	{
	  uint32_t inst = 0;
	  readInst(currPc_, inst);
	  ++counter_;
	  traceInst(inst, counter_, instStr, traceFile);
	}
      std::cout.flush();
      std::cerr << "Stopped...\n";
    }

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);
}


template <typename URV>
void
Core<URV>::execute32(uint32_t inst)
{
  static void *opcodeLabels[] = { &&l0, &&l1, &&l2, &&l3, &&l4, &&l5,
				  &&l6, &&l7, &&l8, &&l9, &&l10, &&l11,
				  &&l12, &&l13, &&l14, &&l15, &&l16, &&l17,
				  &&l18, &&l19, &&l20, &&l21, &&l22, &&l23,
				  &&l24, &&l25, &&l26, &&l27, &&l28, &&l29,
				  &&l30, &&l31 };

  // Decode and execute.
  bool quad3 = (inst & 0x3) == 0x3;
  if (__builtin_expect(quad3, 1))
    {
      unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

      goto *opcodeLabels[opcode];


    l0:  // 00000   I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
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
      return;

    l1:
    l2:
    l7:
    l9:
    l10:
    l15:
    l16:
    l17:
    l18:
    l19:
    l20:
    l21:
    l22:
    l23:
    l26:
    l29:
    l30:
    l31:
      illegalInst();
      return;

    l3: // 00011  I-form
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
      return;

    l4:  // 00100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
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
      return;

    l5:  // 00101   U-form
      {
	UFormInst uform(inst);
	execAuipc(uform.rd, uform.immed());
      }
      return;

    l6:  // 00110  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	unsigned funct3 = iform.fields.funct3;
	if (funct3 == 0)
	  execAddiw(rd, rs1, imm);
	else if (funct3 == 1)
	  {
	    if (iform.top7() != 0)
	      illegalInst();
	    else
	      execSlliw(rd, rs1, iform.fields2.shamt);
	  }
	else if (funct3 == 5)
	  {
	    if (iform.top7() == 0)
	      execSrliw(rd, rs1, iform.fields2.shamt);
	    else if (iform.top7() == 0x20)
	      execSraiw(rd, rs1, iform.fields2.shamt);
	    else
	      illegalInst();
	  }
      }
      return;

    l8:  // 01000  S-form
      {
	SFormInst sform(inst);
	unsigned rs1 = sform.rs1, rs2 = sform.rs2, funct3 = sform.funct3;
	int32_t imm = sform.immed();
	if      (funct3 == 0)  execSb(rs1, rs2, imm);
	else if (funct3 == 1)  execSh(rs1, rs2, imm);
	else if (funct3 == 2)  execSw(rs1, rs2, imm);
	else                   illegalInst();
      }
      return;

    l11:  // 01011  R-form atomics
      {
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.funct3;
	// uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	// bool r1 = rf.r1(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if      (top5 == 0)     unimplemented();  // amoadd.w 
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
	    if      (top5 == 0)     unimplemented();  // amoadd.d
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
      return;

    l12:  // 01100  R-form
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
      return;

    l13:  // 01101  U-form
      {
	UFormInst uform(inst);
	execLui(uform.rd, uform.immed());
      }
      return;

    l14: // 01110  R-Form
      {
	const RFormInst rform(inst);
	unsigned rd = rform.rd, rs1 = rform.rs1, rs2 = rform.rs2;
	unsigned funct7 = rform.funct7, funct3 = rform.funct3;
	if (funct7 == 0)
	  {
	    if      (funct3 == 0)  execAddw(rd, rs1, rs2);
	    else if (funct3 == 1)  execSllw(rd, rs1, rs2);
	    else if (funct3 == 5)  execSrlw(rd, rs1, rs2);
	    else                   illegalInst();
	  }
	else if (funct7 == 1)
	  {
	    if      (funct3 == 0)  execMulw(rd, rs1, rs2);
	    else if (funct3 == 4)  execDivw(rd, rs1, rs2);
	    else if (funct3 == 5)  execDivuw(rd, rs1, rs2);
	    else if (funct3 == 6)  execRemw(rd, rs1, rs2);
	    else if (funct3 == 7)  execRemuw(rd, rs1, rs2);
	    else                   illegalInst();
	  }
	else if (funct7 == 0x20)
	  {
	    if      (funct3 == 0)  execSubw(rd, rs1, rs2);
	    else if (funct3 == 5)  execSraw(rd, rs1, rs2);
	    else                   illegalInst();
	  }
      }
      return;

    l24: // 11000   B-form
      {
	BFormInst bform(inst);
	unsigned rs1 = bform.rs1, rs2 = bform.rs2, funct3 = bform.funct3;
	int32_t imm = bform.immed();
	if      (funct3 == 0)  execBeq(rs1, rs2, imm);
	else if (funct3 == 1)  execBne(rs1, rs2, imm);
	else if (funct3 == 4)  execBlt(rs1, rs2, imm);
	else if (funct3 == 5)  execBge(rs1, rs2, imm);
	else if (funct3 == 6)  execBltu(rs1, rs2, imm);
	else if (funct3 == 7)  execBgeu(rs1, rs2, imm);
	else                   illegalInst();
      }
      return;

    l25:  // 11001  I-form
      {
	IFormInst iform(inst);
	if (iform.fields.funct3 == 0)
	  execJalr(iform.fields.rd, iform.fields.rs1, iform.immed());
	else
	  illegalInst();
      }
      return;

    l27:  // 11011  J-form
      {
	JFormInst jform(inst);
	execJal(jform.rd, jform.immed());
      }
      return;

    l28:  // 11100  I-form
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
	  case 1: execCsrrw(rd, rs1, csr); break;
	  case 2: execCsrrs(rd, rs1, csr); break;
	  case 3: execCsrrc(rd, rs1, csr); break;
	  case 5: execCsrrwi(rd, rs1, csr); break;
	  case 6: execCsrrsi(rd, rs1, csr); break;
	  case 7: execCsrrci(rd, rs1, csr); break;
	  default: illegalInst(); break;
	  }
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

  if (quadrant == 0)
    {
      if (funct3 == 0)   // illegal, c.addi4spn
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
		execAddi(8+ciwf.rdp, RegSp, immed);  // c.addi4spn
	    }
	}

      else if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  execLw(8+clf.rdp, 8+clf.rs1p, clf.lwImmed());
	}

      else if (funct3 == 3)  // c.flw, c.ld
	{
	  if (not rv64_)
	    illegalInst();  // c.flw
	  else
	    {
	      ClFormInst clf(inst);
	      execLd(8+clf.rdp, 8+clf.rs1p, clf.lwImmed());
	    }
	}

      else if (funct3 == 6)  // c.sw
	{
	  CsFormInst cs(inst);
	  execSw(8+cs.rs1p, 8+cs.rs2p, cs.swImmed());
	}

      else if (funct3 == 7) // c.fsw, c.sd
	{
	  if (not rv64_)
	    illegalInst(); // c.fsw
	  else
	    {
	      CsFormInst cs(inst);
	      execSd(8+cs.rs1p, 8+cs.rs2p, cs.sdImmed());
	    }
	}

      else // funct3 is 1 (c_fld c_lq), or 4 (reserverd) or 5 (c.fsd c.sq).
	illegalInst();
      return;
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  execAddi(cif.rd, cif.rd, cif.addiImmed());
	}
	  
      else if (funct3 == 1)  // c.jal   TBD: in rv64 and rv128 this is c.addiw
	{
	  CjFormInst cjf(inst);
	  execJal(RegRa, cjf.immed());
	}

      else if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  execAddi(cif.rd, RegX0, cif.addiImmed());
	}

      else if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    illegalInst();
	  else if (cif.rd == RegSp)  // c.addi16sp
	    execAddi(cif.rd, cif.rd, immed16);
	  else
	    execLui(cif.rd, cif.luiImmed());
	}

      // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
      // c.subw c.addw
      else if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.rdp;
	  unsigned f2 = caf.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.ic5 != 0 and not rv64_)
		illegalInst(); // As of v2.3 of User-Level ISA (Dec 2107).
	      else
		execSrli(rd, rd, caf.shiftImmed());
	    }
	  else if (f2 == 1) // srai64, srai
	    {
	      if (caf.ic5 != 0 and not rv64_)
		illegalInst(); // As of v2.3 of User-Level ISA (Dec 2107).
	      else
		execSrai(rd, rd, caf.shiftImmed());
	    }
	  else if (f2 == 2)  // c.andi
	    execAndi(rd, rd, immed);
	  else  // f2 == 3: c.sub c.xor c.or c.subw c.addw
	    {
	      unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
	      unsigned rs2 = 8 + rs2p;
	      unsigned imm34 = (immed >> 3) & 3; // Bits 3 and 4 of immed
	      if ((immed & 0x20) == 0)  // Bit 5 of immed
		{
		  if      (imm34 == 0) execSub(rd, rd, rs2);
		  else if (imm34 == 1) execXor(rd, rd, rs2);
		  else if (imm34 == 2) execOr(rd, rd, rs2);
		  else                 execAnd(rd, rd, rs2);
		}
	      else
		{
		  if      (imm34 == 0) execSubw(rd, rd, rs2);
		  else if (imm34 == 1) execAddw(rd, rd, rs2);
		  else if (imm34 == 2) illegalInst(); // reserved
		  else                 illegalInst(); // reserved
		}
	    }
	}

      else if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  execJal(RegX0, cjf.immed());
	}
	  
      else if (funct3 == 6)  // c.beqz
	{
	  CbFormInst cbf(inst);
	  execBeq(8+cbf.rs1p, RegX0, cbf.immed());
	}

      else // (funct3 == 7)  // c.bnez
	{
	  CbFormInst cbf(inst);
	  execBne(8+cbf.rs1p, RegX0, cbf.immed());
	}

      return;
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.ic5 != 0 and not rv64_)
	    illegalInst();
	  else
	    execSlli(cif.rd, cif.rd, immed);
	}

      else if (funct3 == 2)  // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.rd;
	  // rd == 0 is legal per Andrew Watterman
	  execLw(rd, RegSp, cif.lwspImmed());
	}

      else if (funct3 == 4)   // c.jr c.mv c.ebreak c.jalr c.add
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

      else if (funct3 == 6)  // c.swsp
	{
	  CswspFormInst csw(inst);
	  execSw(RegSp, csw.rs2, csw.immed());  // imm(sp) <- rs2
	}

      else
	{
	  // funct3 is 1 (c.fldsp c.lqsp), or 3 (c.flwsp c.ldsp),
	  // or 5 (c.fsfsp c.sqsp) or 7 (c.fswsp, c.sdsp)
	  illegalInst();
	}

      return;
    }

  // quadrant 3
  illegalInst();
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

  if (quadrant == 0)
    {
      if (funct3 == 0)    // illegal, c.addi4spn
	{
	  if (inst == 0)
	    return false;
	  CiwFormInst ciwf(inst);
	  unsigned immed = ciwf.immed();
	  if (immed == 0)
	    return false;
	  return encodeAddi(8+ciwf.rdp, RegSp, immed, code32);
	}

      if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  return encodeLw(8+clf.rdp, 8+clf.rs1p, clf.lwImmed(), code32);
	}

      if (funct3 == 3) // c.flw, c.ld
	{
	  if (not rv64_)
	    return false;  // c.flw
	  ClFormInst clf(inst);
	  return encodeLd(8+clf.rdp, 8+clf.rs1p, clf.lwImmed(), code32);
	}

      if (funct3 == 6)  // c.sw
	  {
	    CsFormInst cs(inst);
	    return encodeSw(8+cs.rs1p, 8+cs.rs2p, cs.swImmed(), code32);
	  }

      if (funct3 == 7) // c.fsw, c.sd
	{
	  if (not rv64_)
	    return false;
	  CsFormInst cs(inst);
	  return encodeSd(8+cs.rs1p, 8+cs.rs2p, cs.sdImmed(), code32);
	}

      // funct3 is 1 (c.fld c.lq), or 4 (reserved), or 5 (c.fsd c.sq)
      return false;
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  return encodeAddi(cif.rd, cif.rd, cif.addiImmed(), code32);
	}
	  
      if (funct3 == 1)  // c.jal   TBD: in rv64 and rv128 this is c.addiw
	{
	  CjFormInst cjf(inst);
	  return encodeJal(RegRa, cjf.immed(), 0, code32);
	}

      if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  return encodeAddi(cif.rd, RegX0, cif.addiImmed(), code32);
	}

      if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    return false;
	  if (cif.rd == RegSp)  // c.addi16sp
	    return encodeAddi(cif.rd, cif.rd, immed16, code32);
	  return encodeLui(cif.rd, cif.luiImmed(), 0, code32);
	}

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
      if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.rdp;
	  unsigned f2 = caf.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.ic5 != 0 and not rv64_)
		return false;  // As of v2.3 of User-Level ISA (Dec 2107).
	      return encodeSrli(rd, rd, caf.shiftImmed(), code32);
	    }
	  if (f2 == 1)  // srai64, srai
	    {
	      if (caf.ic5 != 0 and not rv64_)
		return false; // As of v2.3 of User-Level ISA (Dec 2107).
	      return encodeSrai(rd, rd, caf.shiftImmed(), code32);
	    }
	  if (f2 == 2)  // c.andi
	    return encodeAndi(rd, rd, immed, code32);

	  // f2 == 3: c.sub c.xor c.or c.subw c.addw
	  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
	  unsigned rs2 = 8 + rs2p;
	  unsigned imm34 = (immed >> 3) & 3; // Bits 3 and 4 of immed
	  if ((immed & 0x20) == 0)  // Bit 5 of immed
	    {
	      if (imm34 == 0) return encodeSub(rd, rd, rs2, code32);
	      if (imm34 == 1) return encodeXor(rd, rd, rs2, code32);
	      if (imm34 == 2) return encodeOr(rd, rd, rs2, code32);
	      return encodeAnd(rd, rd, rs2,  code32);
	    }
	  // Bit 5 of immed is 1
	  if (not rv64_)
	    return false;
	  if (imm34 == 0) return encodeSubw(rd, rd, rs2, code32);
	  if (imm34 == 1) return encodeAddw(rd, rd, rs2, code32);
	  if (imm34 == 2) return false; // reserved
	  return false; // reserved
	}

      if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  return encodeJal(RegX0, cjf.immed(), 0, code32);
	}
	  
      if (funct3 == 6) // c.beqz
	{
	  CbFormInst cbf(inst);
	  return encodeBeq(8+cbf.rs1p, RegX0, cbf.immed(), code32);
	}

      // funct3 == 7: c.bnez
      CbFormInst cbf(inst);
      return encodeBne(8+cbf.rs1p, RegX0, cbf.immed(), code32);
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.ic5 != 0 and not rv64_)
	    return false;
	  return encodeSlli(cif.rd, cif.rd, immed, code32);
	}

      if (funct3 == 2) // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.rd;
	  // rd == 0 is legal per Andrew Watterman
	  return encodeLw(rd, RegSp, cif.lwspImmed(), code32);
	}

      if (funct3 == 4) // c.jr c.mv c.ebreak c.jalr c.add
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
		    return false;
		  return encodeJalr(RegX0, rd, 0, code32);
		}
	      return encodeAdd(rd, RegX0, rs2, code32);
	    }
	  else  // c.ebreak, c.jalr or c.add 
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return encodeEbreak(0, 0, 0, code32);
		  return encodeJalr(RegRa, rd, 0, code32);
		}
	      return encodeAdd(rd, rd, rs2, code32);
	    }
	}

      if (funct3 == 6) // c.swsp
	{
	  CswspFormInst csw(inst);
	  return encodeSw(RegSp, csw.rs2, csw.immed(), code32);
	}

      // funct3 is 1 (c.fldsp c.lqsp), or 3 (c.flwsp c.ldsp),
      // or 5 (c.fsfsp c.sqsp) or 7 (c.fswsp, c.sdsp)
      return false;
    }

  return false; // quadrant 3
}


template <typename URV>
const InstInfo&
Core<URV>::decode(uint32_t inst, uint32_t& op0, uint32_t& op1,
		  int32_t& op2) const
{
  static void *opcodeLabels[] = { &&l0, &&l1, &&l2, &&l3, &&l4, &&l5,
				  &&l6, &&l7, &&l8, &&l9, &&l10, &&l11,
				  &&l12, &&l13, &&l14, &&l15, &&l16, &&l17,
				  &&l18, &&l19, &&l20, &&l21, &&l22, &&l23,
				  &&l24, &&l25, &&l26, &&l27, &&l28, &&l29,
				  &&l30, &&l31 };

  // Expand 16-bit instructions to 32.
  if ((inst & 3) != 3)
    if (not expandInst(inst, inst))
      inst = ~0; // All ones: illegal 32-bit instruction.

  op0 = 0; op1 = 0; op2 = 0;

  bool quad3 = (inst & 0x3) == 0x3;
  if (quad3)
    {
      unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

      goto *opcodeLabels[opcode];


    l0:  // 00000   I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed(); 
	switch (iform.fields.funct3)
	  {
	  case 0:  return instTable_.getInstInfo(InstId::lb);
	  case 1:  return instTable_.getInstInfo(InstId::lh);
	  case 2:  return instTable_.getInstInfo(InstId::lw);
	  case 3:  return instTable_.getInstInfo(InstId::ld);
	  case 4:  return instTable_.getInstInfo(InstId::lbu);
	  case 5:  return instTable_.getInstInfo(InstId::lhu);
	  case 6:  return instTable_.getInstInfo(InstId::lwu);
	  default: return instTable_.getInstInfo(InstId::illegal);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l1:
    l2:
    l7:
    l9:
    l10:
    l15:
    l16:
    l17:
    l18:
    l19:
    l20:
    l21:
    l22:
    l23:
    l26:
    l29:
    l30:
    l31:
      return instTable_.getInstInfo(InstId::illegal);

    l3: // 00011  I-form
      {
	IFormInst iform(inst);
	unsigned funct3 = iform.fields.funct3;
	if (iform.fields.rd == 0 and iform.fields.rs1 == 0)
	  {
	    if (funct3 == 0)
	      {
		if (iform.top4() == 0)
		  {
		    op0 = iform.pred();
		    op1 = iform.succ();
		    return instTable_.getInstInfo(InstId::fence);
		  }
	      }
	    else if (funct3 == 1)
	      {
		if (iform.uimmed() == 0)
		  return instTable_.getInstInfo(InstId::fencei);
	      }
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l4:  // 00100  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	unsigned funct3 = iform.fields.funct3;

	if      (funct3 == 0)  return instTable_.getInstInfo(InstId::addi);
	else if (funct3 == 1)
	  {
	    if (iform.fields2.top7 == 0)
	      {
		op2 = iform.fields2.shamt;
		return instTable_.getInstInfo(InstId::slli);
	      }
	  }
	else if (funct3 == 2)  return instTable_.getInstInfo(InstId::slti);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::sltiu);
	else if (funct3 == 4)  return instTable_.getInstInfo(InstId::xori);
	else if (funct3 == 5)
	  {
	    op2 = iform.fields2.shamt;
	    if (iform.fields2.top7 == 0)
	      return instTable_.getInstInfo(InstId::srli);
	    else if (iform.fields2.top7 == 0x20)
	      return instTable_.getInstInfo(InstId::srai);
	  }
	else if (funct3 == 6)  return instTable_.getInstInfo(InstId::ori);
	else if (funct3 == 7)  return instTable_.getInstInfo(InstId::andi);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l5:  // 00101   U-form
      {
	UFormInst uform(inst);
	op0 = uform.rd;
	op1 = uform.immed();
	return instTable_.getInstInfo(InstId::auipc);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l6:  // 00110  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	unsigned funct3 = iform.fields.funct3;
	if (funct3 == 0)
	  return instTable_.getInstInfo(InstId::addiw);
	else if (funct3 == 1)
	  {
	    if (iform.top7() == 0)
	      {
		op2 = iform.fields2.shamt;
		return instTable_.getInstInfo(InstId::slliw);
	      }
	  }
	else if (funct3 == 5)
	  {
	    op2 = iform.fields2.shamt;
	    if (iform.top7() == 0)
	      return instTable_.getInstInfo(InstId::srliw);
	    else if (iform.top7() == 0x20)
	      return instTable_.getInstInfo(InstId::sraiw);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l8:  // 01000  S-form
      {
	SFormInst sform(inst);
	op0 = sform.rs1;
	op1 = sform.rs2;
	op2 = sform.immed();
	uint32_t funct3 = sform.funct3;
	if      (funct3 == 0)  return instTable_.getInstInfo(InstId::sb);
	else if (funct3 == 1)  return instTable_.getInstInfo(InstId::sh);
	else if (funct3 == 2)  return instTable_.getInstInfo(InstId::sw);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l11:  // 01011  R-form atomics
      if (false)  // Not implemented
      {
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.funct3;
	// uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	// bool r1 = rf.r1(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if (top5 == 0)    return instTable_.getInstInfo(InstId::amoadd_w);
	    if (top5 == 1)    return instTable_.getInstInfo(InstId::amoswap_w);
	    if (top5 == 2)    return instTable_.getInstInfo(InstId::lr_w);
	    if (top5 == 3)    return instTable_.getInstInfo(InstId::sc_w);
	    if (top5 == 4)    return instTable_.getInstInfo(InstId::amoxor_w);
	    if (top5 == 8)    return instTable_.getInstInfo(InstId::amoor_w);
	    if (top5 == 0x0c) return instTable_.getInstInfo(InstId::amoand_w);
	    if (top5 == 0x10) return instTable_.getInstInfo(InstId::amomin_w);
	    if (top5 == 0x14) return instTable_.getInstInfo(InstId::amomax_w);
	    if (top5 == 0x18) return instTable_.getInstInfo(InstId::amominu_w);
	    if (top5 == 0x1c) return instTable_.getInstInfo(InstId::amomaxu_w);
	  }
	else if (f3 == 3)
	  {
	    if (top5 == 0)    return instTable_.getInstInfo(InstId::amoadd_d);
	    if (top5 == 1)    return instTable_.getInstInfo(InstId::amoswap_d);
	    if (top5 == 2)    return instTable_.getInstInfo(InstId::lr_d);
	    if (top5 == 3)    return instTable_.getInstInfo(InstId::sc_d);
	    if (top5 == 4)    return instTable_.getInstInfo(InstId::amoxor_d);
	    if (top5 == 8)    return instTable_.getInstInfo(InstId::amoor_d);
	    if (top5 == 0xc)  return instTable_.getInstInfo(InstId::amoand_d);
	    if (top5 == 0x10) return instTable_.getInstInfo(InstId::amomin_d);
	    if (top5 == 0x14) return instTable_.getInstInfo(InstId::amomax_d);
	    if (top5 == 0x18) return instTable_.getInstInfo(InstId::amominu_d);
	    if (top5 == 0x1c) return instTable_.getInstInfo(InstId::amomaxu_d);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l12:  // 01100  R-form
      {
	RFormInst rform(inst);
	op0 = rform.rd;
	op1 = rform.rs1;
	op2 = rform.rs2;
	unsigned funct7 = rform.funct7, funct3 = rform.funct3;
	if (funct7 == 0)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::add);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::sll);
	    else if (funct3 == 2) return instTable_.getInstInfo(InstId::slt);
	    else if (funct3 == 3) return instTable_.getInstInfo(InstId::sltu);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::xor_);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::srl);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::or_);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::and_);
	  }
	else if (funct7 == 1)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::mul);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::mulh);
	    else if (funct3 == 2) return instTable_.getInstInfo(InstId::mulhsu);
	    else if (funct3 == 3) return instTable_.getInstInfo(InstId::mulhu);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::div);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::divu);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::rem);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::remu);
	  }
	else if (funct7 == 0x20)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::sub);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::sra);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l13:  // 01101  U-form
      {
	UFormInst uform(inst);
	op0 = uform.rd;
	op1 = uform.immed();
	return instTable_.getInstInfo(InstId::lui);
      }

    l14: // 01110  R-Form
      {
	const RFormInst rform(inst);
	op0 = rform.rd;
	op1 = rform.rs1;
	op2 = rform.rs2;
	unsigned funct7 = rform.funct7, funct3 = rform.funct3;
	if (funct7 == 0)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::addw);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::sllw);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::srlw);
	  }
	else if (funct7 == 1)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::mulw);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::divw);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::divuw);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::remw);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::remuw);
	  }
	else if (funct7 == 0x20)
	  {
	    if      (funct3 == 0)  return instTable_.getInstInfo(InstId::subw);
	    else if (funct3 == 5)  return instTable_.getInstInfo(InstId::sraw);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l24: // 11000   B-form
      {
	BFormInst bform(inst);
	op0 = bform.rs1;
	op1 = bform.rs2;
	op2 = bform.immed();
	uint32_t funct3 = bform.funct3;
	if      (funct3 == 0)  return instTable_.getInstInfo(InstId::beq);
	else if (funct3 == 1)  return instTable_.getInstInfo(InstId::bne);
	else if (funct3 == 4)  return instTable_.getInstInfo(InstId::blt);
	else if (funct3 == 5)  return instTable_.getInstInfo(InstId::bge);
	else if (funct3 == 6)  return instTable_.getInstInfo(InstId::bltu);
	else if (funct3 == 7)  return instTable_.getInstInfo(InstId::bgeu);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l25:  // 11001  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	if (iform.fields.funct3 == 0)
	  return instTable_.getInstInfo(InstId::jalr);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l27:  // 11011  J-form
      {
	JFormInst jform(inst);
	op0 = jform.rd;
	op1 = jform.immed();
	return instTable_.getInstInfo(InstId::jal);
      }

    l28:  // 11100  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.uimmed(); // csr
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      uint32_t funct7 = op2 >> 5;
	      if (funct7 == 0) // ecall ebreak uret
		{
		  if (op1 != 0 or op0 != 0)
		    return instTable_.getInstInfo(InstId::illegal);
		  else if (op2 == 0)
		    return instTable_.getInstInfo(InstId::ecall);
		  else if (op2 == 1)
		    return instTable_.getInstInfo(InstId::ebreak);
		  else if (op2 == 2)
		    return instTable_.getInstInfo(InstId::uret);
		}
	      else if (funct7 == 9)
		{
		  if (op0 != 0)
		    return instTable_.getInstInfo(InstId::illegal);
		  else // sfence.vma
		    return instTable_.getInstInfo(InstId::illegal);
		}
	      else if (op2 == 0x102)
		return instTable_.getInstInfo(InstId::sret);
	      else if (op2 == 0x302)
		return instTable_.getInstInfo(InstId::mret);
	      else if (op2 == 0x105)
		return instTable_.getInstInfo(InstId::wfi);
	    }
	    break;
	  case 1:  return instTable_.getInstInfo(InstId::csrrw);
	  case 2:  return instTable_.getInstInfo(InstId::csrrs);
	  case 3:  return instTable_.getInstInfo(InstId::csrrc);
	  case 5:  return instTable_.getInstInfo(InstId::csrrwi);
	  case 6:  return instTable_.getInstInfo(InstId::csrrsi);
	  case 7:  return instTable_.getInstInfo(InstId::csrrci);
	  default: return instTable_.getInstInfo(InstId::illegal);
	  }
	return instTable_.getInstInfo(InstId::illegal);
      }
    }
  else
    return instTable_.getInstInfo(InstId::illegal);
}


template <typename URV>
void
Core<URV>::disassembleInst32(uint32_t inst, std::ostream& stream)
{
  if ((inst & 3) != 3)  // Must be in quadrant 3.
    {
      stream << "illegal";
      return;
    }

  unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

  switch (opcode)
    {
    case 0:  // 00000   I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
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
	  default: stream << "illegal";
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
	  stream << "illegal";
	else if (funct3 == 0)
	  {
	    if (iform.top4() != 0)
	      stream << "illegal";
	    else
	      stream << "fence  " << iform.pred() << ", " << iform.succ();
	  }
	else if (funct3 == 1)
	  {
	    if (iform.uimmed() != 0)
	      stream << "illegal";
	    else
	      stream << "fence.i ";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 4:  // 00100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
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
	      stream << "illegal";
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
	      stream << "illegal";
	    break;
	  case 6:
	    stream << "ori    x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 7:
	    stream << "andi   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  default:
	    stream << "illegal";
	    break;
	  }
      }
      break;

    case 5:  // 00101   U-form
      {
	UFormInst uform(inst);
	stream << "auipc x" << uform.rd << ", 0x"
	       << std::hex << ((uform.immed() >> 12) & 0xfffff);
      }
      break;

    case 8:  // 01000  S-form
      {
	SFormInst sform(inst);
	unsigned rs1 = sform.rs1, rs2 = sform.rs2;
	int32_t imm = sform.immed();
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
	    stream << "illegal";
	    break;
	  }
      }
      break;

    case 11:  // 01011  R-form  atomics
      {
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.funct3;
	// uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	// bool r1 = rf.r1(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if      (top5 == 0)     stream << "illegal";  // amoadd.w
	    else if (top5 == 1)     stream << "illegal";  // amoswap.w
	    else if (top5 == 2)     stream << "illegal";  // lr.w
	    else if (top5 == 3)     stream << "illegal";  // sc.w
	    else if (top5 == 4)     stream << "illegal";  // amoxor.w
	    else if (top5 == 8)     stream << "illegal";  // amoor.w
	    else if (top5 == 0x0c)  stream << "illegal";  // amoand.w
	    else if (top5 == 0x10)  stream << "illegal";  // amomin.w
	    else if (top5 == 0x14)  stream << "illegal";  // amomax.w
	    else if (top5 == 0x18)  stream << "illegal";  // maominu.w
	    else if (top5 == 0x1c)  stream << "illegal";  // maomaxu.w
	  }
	else if (f3 == 3)
	  {
	    if      (top5 == 0)     stream << "illegal";  // amoadd.d
	    else if (top5 == 1)     stream << "illegal";  // amoswap.d
	    else if (top5 == 2)     stream << "illegal";  // lr.d
	    else if (top5 == 3)     stream << "illegal";  // sc.d
	    else if (top5 == 4)     stream << "illegal";  // amoxor.d
	    else if (top5 == 8)     stream << "illegal";  // amoor.d
	    else if (top5 == 0x0c)  stream << "illegal";  // amoand.d
	    else if (top5 == 0x10)  stream << "illegal";  // amomin.d
	    else if (top5 == 0x14)  stream << "illegal";  // amomax.d
	    else if (top5 == 0x18)  stream << "illegal";  // maominu.d
	    else if (top5 == 0x1c)  stream << "illegal";  // maomaxu.d
	  }
	else stream << "illegal";
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
	      stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 13:  // 01101  U-form
      {
	UFormInst uform(inst);
	stream << "lui    x" << uform.rd << ", " << uform.immed();
      }
      break;

    case 14:  // 01110  R-Form
      {
	const RFormInst rform(inst);
	unsigned rd = rform.rd, rs1 = rform.rs1, rs2 = rform.rs2;
	unsigned funct7 = rform.funct7, funct3 = rform.funct3;
	if (funct7 == 0)
	  {
	    if (funct3 == 0)
	      stream << "addw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 1)
	      stream << "sllw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "srlw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
	else if (funct7 == 1)
	  {
	    if (funct3 == 0)
	      stream << "mulw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 4)
	      stream << "divw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "divuw   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 6)
	      stream << "remw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 7)
	      stream << "remuw   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
	else if (funct7 == 0x20)
	  {
	    if (funct3 == 0)
	      stream << "subw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "sraw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
      }
      break;

    case 24:  // 11000   B-form
      {
	BFormInst bform(inst);
	unsigned rs1 = bform.rs1, rs2 = bform.rs2;
	int32_t imm = bform.immed();
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
	    stream << "illegal";
	    break;
	  }
      }
      break;

    case 25:  // 11001  I-form
      {
	IFormInst iform(inst);
	if (iform.fields.funct3 == 0)
	  stream << "jalr   x" << iform.fields.rd << ", x" << iform.fields.rs1
		 << ", " << iform.immed();
	else
	  stream << "illegal";
      }
      break;

    case 27:  // 11011  J-form
      {
	JFormInst jform(inst);
	stream << "jal    x" << jform.rd << ", " << jform.immed();
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
	  csrName = "illegal";
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      uint32_t func7 = iform.top7();
	      if (func7 == 0)
		{
		  if (rs1 != 0 or rd != 0)  stream << "illegal";
		  else if (csrNum == 0)     stream << "ecall";
		  else if (csrNum == 1)     stream << "ebreak";
		  else if (csrNum == 2)     stream << "uret";
		  else                      stream << "illegal";
		}
	      else if (func7 == 9)
		{
		  uint32_t rs2 = iform.rs2();
		  if (rd != 0) stream << "illegal";
		  else         stream << "SFENCE.VMA " << rs1 << ", " << rs2;
		}
	      else if (csrNum == 0x102) stream << "sret";
	      else if (csrNum == 0x302) stream << "mret";
	      else if (csrNum == 0x105) stream << "wfi";
	      else                      stream << "illegal";
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
	    stream << "illegal";
	    break;
	  }
      }
      break;

    default:
      stream << "illegal";
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
	      stream << "illegal";
	    else
	      {
		CiwFormInst ciwf(inst);
		unsigned immed = ciwf.immed();
		if (immed == 0)
		  stream << "illegal";
		else
		  stream << "c.addi4spn x" << (8+ciwf.rdp) << ", "
			 << (immed >> 2);
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  stream << "illegal";
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    stream << "c.lw   x" << (8+clf.rdp) << ", " << clf.lwImmed()
		   << "(x" << (8+clf.rs1p) << ")";
	  }
	  break;
	case 3:  // c.flw, c.ld
	  {
	    ClFormInst clf(inst);
	    if (rv64_)
	      stream << "c.ld   x" << (8+clf.rdp) << ", " << clf.ldImmed()
		     << "(x" << (8+clf.rs1p) << ")";
	    else
	      stream << "illegal"; // c.flw
	  }
	  break;
	case 4:  // reserved
	  stream << "illegal";
	  break;
	case 5:  // c.fsd, c.sq
	  stream << "illegal";
	  break;
	case 6:  // c.sw
	  {
	    CsFormInst cs(inst);
	    stream << "c.sw   x" << (8+cs.rs2p) << ", " << cs.swImmed()
		   << "(x" << (8+cs.rs1p) << ")";
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  {
	    CsFormInst cs(inst);
	    if (rv64_)
	      stream << "c.sd  x" << (8+cs.rs2p) << ", " << cs.sdImmed()
		     << "(x" << (8+cs.rs1p) << ")";
	    else
	      stream << "illegal"; // c.fsw
	  }
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
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 this is c.addiw
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
	      stream << "illegal";
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
	    switch (caf.funct2)
	      {
	      case 0:
		if (caf.ic5 != 0 and not rv64_)
		  stream << "illegal";
		else
		  stream << "c.srli x" << (8+caf.rdp) << ", "
			 << caf.shiftImmed();
		break;
	      case 1:
		if (caf.ic5 != 0 and not rv64_)
		  stream << "illegal";
		else
		  stream << "c.srai x" << (8+caf.rdp) << ", "
			 << caf.shiftImmed();
		break;
	      case 2:
		stream << "c.andi x" << (8+caf.rdp) << ", " << immed;
		break;
	      case 3:
		{
		  unsigned rs2 = 8+(immed & 0x7); // Lowest 3 bits of immed
		  unsigned rd = 8+caf.rdp;
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0:
			  stream << "c.sub  x" << rd << ", x" << rs2; break;
			case 1:
			  stream << "c.xor  x" << rd << ", x" << rs2; break;
			case 2:
			  stream << "c.or   x" << rd << ", x" << rs2; break;
			case 3:
			  stream << "c.and  x" << rd << ", x" << rs2; break;
			}
		    }
		  else
		    {
		      if (not rv64_)
			stream << "illegal";
		      else
			switch ((immed >> 3) & 3)
			  {
			  case 0: stream << "c.subw x" << rd << ", x" << rs2;
			    break;
			  case 1: stream << "c.addw x" << rd << ", x" << rs2;
			    break;
			  case 3: stream << "illegal";
			    break; // reserved
			  case 4: stream << "illegal";
			    break; // reserved
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
	    stream << "c.beqz x" << (8+cbf.rs1p) << ", " << cbf.immed();
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    stream << "c.bnez x" << (8+cbf.rs1p) << ", " << cbf.immed();
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
	      stream << "illegal";  // TBD: ok for RV64
	    else
	      stream << "c.slli x" << cif.rd << ", " << immed;
	  }
	  break;

	case 1:   // c.fldsp, c.lqsp
	  stream << "illegal";
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
	  stream << "illegal";
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
		      stream << "illegal";
		    else
		      stream << "c.jr   x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
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
		      stream << "illegal";
		    else
		      stream << "c.add  x" << rd << ", x" << rs2;
		  }
	      }
	  }
	  break;

	case 5:  // c.fsdsp  c.sqsp
	  stream << "illegal";
	  break;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    stream << "c.swsp x" << csw.rs2 << ", " << (csw.immed() >> 2);
	  }
	  break;

	case 7:  // c.fswsp c.sdsp
	  stream << "illegal";
	  break;
	}
      break;

    case 3:  // quadrant 3
      stream << "illegal";
      break;

    default:
      stream << "illegal";
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
Core<URV>::execBeq(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  if (intRegs_.read(rs1) != intRegs_.read(rs2))
    return;
  pc_ = currPc_ + SRV(offset);
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
}


template <typename URV>
void
Core<URV>::execBlt(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  SRV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 < v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBltu(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  URV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 < v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBge(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  SRV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execBgeu(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  URV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
    }
}


template <typename URV>
void
Core<URV>::execJalr(uint32_t rd, uint32_t rs1, int32_t offset)
{
  URV temp = pc_;  // pc has the address of the instruction adter jalr
  pc_ = (intRegs_.read(rs1) + SRV(offset));
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  intRegs_.write(rd, temp);
}


template <typename URV>
void
Core<URV>::execJal(uint32_t rd, uint32_t offset, int32_t)
{
  intRegs_.write(rd, pc_);
  pc_ = currPc_ + SRV(int32_t(offset));
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
}


template <typename URV>
void
Core<URV>::execLui(uint32_t rd, uint32_t imm, int32_t)
{
  intRegs_.write(rd, SRV(int32_t(imm)));
}


template <typename URV>
void
Core<URV>::execAuipc(uint32_t rd, uint32_t imm, int32_t)
{
  intRegs_.write(rd, currPc_ + SRV(int32_t(imm)));
}


template <typename URV>
void
Core<URV>::execSlli(uint32_t rd, uint32_t rs1, int32_t amount)
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
Core<URV>::execSlti(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = SRV(intRegs_.read(rs1)) < imm ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSltiu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) < URV(SRV(imm)) ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execXori(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) ^ SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrli(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (amount < 0)
    {
      illegalInst();
      return;
    }
  if ((amount > 31) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = intRegs_.read(rs1) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrai(uint32_t rd, uint32_t rs1, int32_t amount)
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
Core<URV>::execOri(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) | SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execAndi(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) & SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSub(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) - intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSll(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) << (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSlt(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSltu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execXor(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) ^ intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrl(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSra(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = SRV(intRegs_.read(rs1)) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execOr(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) | intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execAnd(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) & intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execFence(uint32_t pred, uint32_t succ, int32_t)
{
  return;  // Currently a no-op.
}


template <typename URV>
void
Core<URV>::execFencei(uint32_t, uint32_t, int32_t)
{
  return;  // Currently a no-op.
}


template <typename URV>
void
Core<URV>::execEcall(uint32_t, uint32_t, int32_t)
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
Core<URV>::execEbreak(uint32_t, uint32_t, int32_t)
{
  URV savedPc = currPc_;  // Goes into MEPC.

  // Goes into MTVAL: Sec 3.1.21 of RISCV privileged arch (version 1.11).
  URV trapInfo = currPc_;

  initiateException(BREAKPOINT, savedPc, trapInfo);
}


template <typename URV>
void
Core<URV>::execMret(uint32_t, uint32_t, int32_t)
{
  if (privilegeMode_ < MACHINE_MODE)
    illegalInst();
  else
    {
      // Restore privilege mode and interrupt enable by getting
      // current value of MSTATUS, ...
      URV value = 0;
      if (not csRegs_.read(MSTATUS_CSR, privilegeMode_, value))
	assert(0 and "Failed to write MSTATUS register\n");

      // ... updating/unpacking its fields,
      MstatusFields<URV> fields(value);
      PrivilegeMode savedMode = PrivilegeMode(fields.bits_.MPP);
      fields.bits_.MIE = fields.bits_.MPIE;
      fields.bits_.MPP = 0;
      fields.bits_.MPIE = 1;

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
Core<URV>::execSret(uint32_t, uint32_t, int32_t)
{
  unimplemented();  // Not yet implemented.
}


template <typename URV>
void
Core<URV>::execUret(uint32_t, uint32_t, int32_t)
{
  illegalInst();  // Not yet implemented.
}


template <typename URV>
void
Core<URV>::execWfi(uint32_t, uint32_t, int32_t)
{
  return;   // Currently implemented as a no-op.
}


// Set control and status register csr to value of register rs1 and
// save its original value in register rd.
template <typename URV>
void
Core<URV>::execCsrrw(uint32_t rd, uint32_t rs1, int32_t csr)
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

  // Csr was written. If it iwas minstret, supress auto-increment.
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // Csr was written. If it iwas mcycle, supress auto-increment.
  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


template <typename URV>
void
Core<URV>::execCsrrs(uint32_t rd, uint32_t rs1, int32_t csr)
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

  bool csrWritten = false;

  if (rs1 != 0)
    {
      csrWritten = csRegs_.write(CsrNumber(csr), privilegeMode_, next);
      if (not csrWritten)
	{
	  illegalInst();
	  return;
	}
    }

  intRegs_.write(rd, prev);

  // If minstret was written, then suppress auto-increment.
  if (csrWritten and (csr == MINSTRET_CSR or csr == MINSTRETH_CSR))
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // If mcycle was written, then suppress auto-increment.
  if (csrWritten and (csr == MCYCLE_CSR or csr == MCYCLEH_CSR))
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


template <typename URV>
void
Core<URV>::execCsrrc(uint32_t rd, uint32_t rs1, int32_t csr)
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

  bool csrWritten = false;

  if (rs1 != 0)
    {
      csrWritten = csRegs_.write(CsrNumber(csr), privilegeMode_, next);
      if (not csrWritten)
	{
	  illegalInst();
	  return;
	}
    }

  intRegs_.write(rd, prev);

  // If minstret was written, then suppress auto increment.
  // increment) take place.
  if (csrWritten and (csr == MINSTRET_CSR or csr == MINSTRETH_CSR))
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // If mcycle was written, then suppress auto increment.
  // increment) take place.
  if (csrWritten and (csr == MCYCLE_CSR or csr == MCYCLEH_CSR))
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


template <typename URV>
void
Core<URV>::execCsrrwi(uint32_t rd, uint32_t imm, int32_t csr)
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

  // Csr written: If it was minstret then suppress auto increment.
  if (csr == MINSTRET_CSR or csr == MINSTRETH_CSR)
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // Csr written: If it was mcycle then suppress auto increment.
  if (csr == MCYCLE_CSR or csr == MCYCLEH_CSR)
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


template <typename URV>
void
Core<URV>::execCsrrsi(uint32_t rd, uint32_t imm, int32_t csr)
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

  bool csrWritten = false;

  if (imm != 0)
    {
      csrWritten = csRegs_.write(CsrNumber(csr), privilegeMode_, next);
      if (not csrWritten)
	{
	  illegalInst();
	  return;
	}
    }

  intRegs_.write(rd, prev);

  // If minstret was written, then suppress auto-increment.
  if (csrWritten and (csr == MINSTRET_CSR or csr == MINSTRETH_CSR))
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // If mcycle was written, then suppress auto-increment.
  if (csrWritten and (csr == MCYCLE_CSR or csr == MCYCLEH_CSR))
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


template <typename URV>
void
Core<URV>::execCsrrci(uint32_t rd, uint32_t imm, int32_t csr)
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

  bool csrWritten = false;

  if (imm != 0)
    {
      csrWritten = csRegs_.write(CsrNumber(csr), privilegeMode_, next);
      if (not csrWritten)
      {
	illegalInst();
	return;
      }
    }

  intRegs_.write(rd, prev);

  // If minstret was written, then suppress auto-increment.
  if (csrWritten and (csr == MINSTRET_CSR or csr == MINSTRETH_CSR))
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // If mcycle was written, then suppress auto-increment.
  if (csrWritten and (csr == MCYCLE_CSR or csr == MCYCLEH_CSR))
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


template <typename URV>
void
Core<URV>::execLb(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  uint8_t byte;  // Use a signed type.

  if (conIoValid_ and address == conIo_)
    {
      int c = fgetc(stdin);
      SRV value = c;
      intRegs_.write(rd, value);
      return;
    }

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
Core<URV>::execLh(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
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
Core<URV>::execLbu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  uint8_t byte;  // Use an unsigned type.

  if (conIoValid_ and address == conIo_)
    {
      int c = fgetc(stdin);
      URV value = uint8_t(c);
      intRegs_.write(rd, value);
      return;
    }

  if (memory_.readByte(address, byte))
    intRegs_.write(rd, byte); // Zero extend into register.
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execLhu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  uint16_t half;  // Use an unsigned type.
  if (memory_.readHalfWord(address, half))
    intRegs_.write(rd, half); // Zero extend into register.
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execSb(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  URV regVal = intRegs_.read(rs2);
  uint8_t byte = regVal;

  // If we write to special location, end the simulation.
  if (toHostValid_ and address == toHost_)
    {
      if (memory_.writeByte(address, byte))
	lastWrittenWord_ = regVal;   // Compat with spike tracer
      throw std::exception();
    }

  // If we write to special location, then write to console.
  if (conIoValid_ and address == conIo_)
    {
      fputc(byte, stdout);
      return;
    }

  if (not memory_.writeByte(address, byte))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
  else
    lastWrittenWord_ = regVal;  // Compat with spike tracer
}


template <typename URV>
void
Core<URV>::execSh(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
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
Core<URV>::execSw(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
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
  Core<uint32_t>::execMul(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    int32_t a = intRegs_.read(rs1);
    int32_t b = intRegs_.read(rs2);

    int32_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint32_t>::execMulh(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend.
    int64_t b = int32_t(intRegs_.read(rs2));
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhsu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));
    uint64_t b = intRegs_.read(rs2);
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    uint64_t a = intRegs_.read(rs1);
    uint64_t b = intRegs_.read(rs2);
    uint64_t c = a * b;
    uint32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template<>
  void
  Core<uint64_t>::execMul(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend to 64-bit
    __int128_t b = int64_t(intRegs_.read(rs2));

    int64_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint64_t>::execMulh(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend.
    __int128_t b = int64_t(intRegs_.read(rs2));
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhsu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));
    __uint128_t b = intRegs_.read(rs2);
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhu(uint32_t rd, uint32_t rs1, int32_t rs2)
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
Core<URV>::execDiv(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = -1;   // Divide by zero result
  if (b != 0)
    {
      SRV minInt = SRV(1) << (intRegs_.regWidth() - 1);
      if (a == minInt and b == -1)
	c = a;
      else
	c = a / b;  // Per spec: User-Level ISA, Version 2.3, Section 6.2
    }
  intRegs_.write(rd, c);
}


template <typename URV>
void
Core<URV>::execDivu(uint32_t rd, uint32_t rs1, int32_t rs2)
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
Core<URV>::execRem(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = a;  // Divide by zero remainder.
  if (b != 0)
    {
      SRV minInt = SRV(1) << (intRegs_.regWidth() - 1);
      if (a == minInt and b == -1)
	c = 0;   // Per spec: User-Level ISA, Version 2.3, Section 6.2
      else
	c = a % b;
    }
  intRegs_.write(rd, c);
}


// Unsigned remainder instruction.
template <typename URV>
void
Core<URV>::execRemu(uint32_t rd, uint32_t rs1, int32_t rs2)
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
Core<URV>::execLwu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + SRV(imm);
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
Core<URV>::execLd(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + SRV(imm);
  uint64_t value;
  if (memory_.readDoubleWord(address, value))
    intRegs_.write(rd, value);
  else
    initiateException(LOAD_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execSd(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + SRV(imm);
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


template <typename URV>
void
Core<URV>::execSlliw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  if (amount > 0x1f)
    {
      illegalInst();   // Bit 5 is 1 or higher values.
      return;
    }

  int32_t word = intRegs_.read(rs1);
  word <<= amount;

  SRV value = word; // Sign extend to 64-bit.
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSrliw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  if (amount > 0x1f)
    {
      illegalInst();   // Bit 5 is 1 or higher values.
      return;
    }

  uint32_t word = intRegs_.read(rs1);
  word >>= amount;

  SRV value = int32_t(word); // Sign extend to 64-bit.
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSraiw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  if (amount > 0x1f)
    {
      illegalInst();   // Bit 5 is 1 or higher values.
      return;
    }

  int32_t word = intRegs_.read(rs1);
  word >>= amount;

  SRV value = word; // Sign extend to 64-bit.
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execAddiw(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1);
  word += imm;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execAddw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1) + intRegs_.read(rs2);
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSubw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1) - intRegs_.read(rs2);
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}



template <typename URV>
void
Core<URV>::execSllw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  uint32_t shift = intRegs_.read(rs2) & 0x1f;
  int32_t word = intRegs_.read(rs1) << shift;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSrlw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  uint32_t word = intRegs_.read(rs1);
  uint32_t shift = intRegs_.read(rs2) & 0x1f;
  word >>= shift;
  SRV value = int32_t(word);  // sign extend to 64-bits
  intRegs_.write(rd, value);
}

template <typename URV>
void
Core<URV>::execSraw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1);
  uint32_t shift = intRegs_.read(rs2) & 0x1f;
  word >>= shift;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execMulw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);
  int32_t word = word1 * word2;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execDivw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);

  int32_t word = -1;  // Divide by zero resut
  if (word2 != 0)
    word = word1 / word2;

  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execDivuw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  uint32_t word1 = intRegs_.read(rs1);
  uint32_t word2 = intRegs_.read(rs2);

  uint32_t word = ~uint32_t(0);  // Divide by zero result.
  if (word2 != 0)
    word = word1 / word2;

  URV value = word;  // zero extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execRemw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);

  int32_t word = word1;  // Divide by zero remainder
  if (word2 != 0)
    word = word1 % word2;

  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execRemuw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  uint32_t word1 = intRegs_.read(rs1);
  uint32_t word2 = intRegs_.read(rs1);

  uint32_t word = word1;  // Divide by zero remainder
  if (word1 != 0)
    word = word1 % word2;

  URV value = word;  // zero extend to 64-bits
  intRegs_.write(rd, value);
}


template class Core<uint32_t>;
template class Core<uint64_t>;
