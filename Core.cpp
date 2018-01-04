#include <iostream>
#include <sstream>
#include <boost/format.hpp>
#include <assert.h>
#include "Core.hpp"
#include "Inst.hpp"

using namespace WdRiscv;


template <typename URV>
Core<URV>::Core(size_t memorySize, size_t intRegCount)
  : memory_(memorySize), intRegs_(intRegCount), privilegeMode_(MACHINE_MODE),
    mxlen_(8*sizeof(URV)), snapMemory_(0), snapIntRegs_(intRegCount)
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
  // Inst i0 will be loaded at loc 0x100

  RFormInst i0(0);
  i0.encodeAdd(RegX1, RegX0, RegX0);   // 100 add x1, x0, x0   x1 <- 0

  IFormInst i1(0);
  i1.encodeAddi(RegX2, RegX0, 64);     // 104 addi x2, x0, 16  x2 <- 64

  IFormInst i2(0);
  i2.encodeSlli(RegX2, RegX2, 20);     // 108 slli x2, x2, 20  x2 <- 64*1024*1204

  IFormInst i3(0);
  i3.encodeAddi(RegX1, RegX1, 1);      // 10c addi x1, x1, 1   x1 <- x1 + 1

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

  pc_ = 0x100;

  std::string str;

  disassembleInst(i0.code, str);
  std::cout << str << '\n';
  disassembleInst(i1.code, str);
  std::cout << str << '\n';
  disassembleInst(i2.code, str);
  std::cout << str << '\n';
  disassembleInst(i3.code, str);
  std::cout << str << '\n';
  disassembleInst(i4.code, str);
  std::cout << str << '\n';
  disassembleInst(i5.code, str);
  std::cout << str << '\n';
  disassembleInst(i6.code, str);
  std::cout << str << '\n';
}


template <typename URV>
bool
Core<URV>::loadHexFile(const std::string& file)
{
  return memory_.loadHexFile(file);
}


template <typename URV>
bool
Core<URV>::loadElfFile(const std::string& file, size_t& entryPoint)
{
  return memory_.loadElfFile(file, entryPoint);
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
  execLui(1, 0x01234);    // lui x1, 0x1234     x1 <- 0x01234000
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

  return errors == 0;
}


template <typename URV>
void
Core<URV>::illegalInst()
{
  initiateException(ILLEGAL_INST, currPc_, 0);
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

  // Update status register.
  URV status = 0;
  if (not csRegs_.read(MSTATUS_CSR, privilegeMode_, status))
    assert(0 and "Failed to read STATUS register");

  // Save previous mode.
  MstatusFields<URV> fields(status);

  if (nextMode == MACHINE_MODE)
    {
      fields.fields_.MPP = prevMode;
      fields.fields_.MPIE = fields.fields_.MIE;
      fields.fields_.MIE = 0;
    }
  else if (nextMode == SUPERVISOR_MODE)
    {
      fields.fields_.SPP = prevMode;
      fields.fields_.SPIE = fields.fields_.SIE;
      fields.fields_.SIE = 0;
    }
  else if (nextMode == USER_MODE)
    {
      fields.fields_.UPIE = fields.fields_.UIE;
      fields.fields_.UIE = 0;
    }
  
  // Set program counter.
  URV tvec = 0;
  if (not csRegs_.read(tvecNum, privilegeMode_, tvec))
    assert(0 and "Failed to read TVEC register");

  URV base = (tvec >> 2) << 2;  // Clear least sig 2 bits.
  unsigned tvecMode = tvec & 0x3;

  if (tvecMode == 1 and interrupt)
    base = base + 4*cause;

  pc_ = base;

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
void
Core<URV>::runUntilAddress(URV address)
{
  while(1) 
    {
      if (pc_ == address)
	return;

      // Fetch instruction incrementing program counter. A two-byte
      // value is first loaded. If its least significant bits are
      // 00, 01, or 10 then we have a 2-byte instruction and the fetch
      // is complete. If the least sig bits are 11 then we have a 4-byte
      // instruction and two additional bytes are loaded.
      currPc_ = pc_;
    
      if (pc_ & 1)
	{
	  initiateException(INST_ADDR_MISALIGNED, pc_, pc_ /*info*/);
	  continue; // Next instruction in trap handler.
	}

      uint16_t low;  // Lowest word of instruction.
      if (not memory_.readHalfWord(pc_, low))
	{
	  initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
	  continue; // Next instruction in trap handler.
	}
      pc_ += 2;

      if ((low & 3) != 3)
	{
	  // Compressed (2-byte) instruction.
	  execute16(low);
	}
      else if ((low & 0x1c) != 0x1c)
	{
	  // 4-byte instruction: read upper 2 bytes.
	  uint16_t high;
	  if (not memory_.readHalfWord(pc_, high))
	    {
	      initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
	      continue;
	    }
	  pc_ += 2;
	  uint32_t inst = (uint32_t(high) << 16) | low;
	  execute32(inst);
	}
      else
	illegalInst();
    }
}


template <typename URV>
void
Core<URV>::run()
{
  while(1) 
    {
      // Fetch instruction incrementing program counter. A two-byte
      // value is first loaded. If its least significant bits are
      // 00, 01, or 10 then we have a 2-byte instruction and the fetch
      // is complete. If the least sig bits are 11 then we have a 4-byte
      // instruction and two additional bytes are loaded.
      currPc_ = pc_;
    
      if (pc_ & 1)
	{
	  initiateException(INST_ADDR_MISALIGNED, pc_, pc_ /*info*/);
	  continue; // Next instruction in trap handler.
	}

      uint16_t low;  // Lowest word of instruction.
      if (not memory_.readHalfWord(pc_, low))
	{
	  initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
	  continue; // Next instruction in trap handler.
	}
      pc_ += 2;

      if ((low & 3) != 3)
	{
	  // Compressed (2-byte) instruction.
	  execute16(low);
	}
      else if ((low & 0x1c) != 0x1c)
	{
	  // 4-byte instruction: read upper 2 bytes.
	  uint16_t high;
	  if (not memory_.readHalfWord(pc_, high))
	    {
	      initiateException(INST_ACCESS_FAULT, pc_, pc_ /*info*/);
	      continue;
	    }
	  pc_ += 2;
	  uint32_t inst = (uint32_t(high) << 16) | low;
	  execute32(inst);
	} 
    }
}


template <typename URV>
void
Core<URV>::snapshotState()
{
  snapPc_ = pc_;
  snapIntRegs_ = intRegs_;
  snapCsRegs_ = csRegs_;

  snapMemory_.resize(memory_.size());
  snapMemory_.copy(memory_);
}


template <typename URV>
void
Core<URV>::printStateDiff(std::ostream& out) const
{
  const char* hexForm = "%x %x"; // Formatting string for printing 2 hex vals
  if (sizeof(URV) == 4)
    hexForm = "%08x %08x";
  else if (sizeof(URV) == 8)
    hexForm = "%016x %016x";
  else if (sizeof(URV) == 16)
    hexForm = "%032x %032x";

  // Diff program counter.
  if (pc_ != snapPc_)
    out << "pc: " << (boost::format(hexForm) % snapPc_ % pc_) << '\n';

  // Diff integer registers.
  auto regCount = std::max(intRegs_.size(), snapIntRegs_.size());
  for (unsigned regIx = 0; regIx < regCount; ++regIx)
    {
      URV v1 = snapIntRegs_.read(regIx);
      URV v2 = intRegs_.read(regIx);
      if (v1 != v2)
	out << "x" << regIx << ": "
	    << (boost::format(hexForm) % v1 % v2) << '\n';
    }

  // Diff control and status register.
  for (unsigned ix = MIN_CSR_; ix <= MAX_CSR_; ++ix)
    {
      CsrNumber csrNum = static_cast<CsrNumber>(ix);
      Csr<URV> reg1, reg2;
      if (snapCsRegs_.findCsr(csrNum, reg1) and csRegs_.findCsr(csrNum, reg2))
	{
	  if (reg1.valid_ and reg2.valid_ and reg1.value_ != reg2.value_)
	    {
	      out << reg1.name_
		  << (boost::format(hexForm) % reg1.value_ % reg2.value_)
		  << '\n';
	    }
	}
    }

  // Diff memory.
  for (size_t ix = 0; ix < memory_.size(); ++ix)
    {
      uint8_t v1 = 0;
      snapMemory_.readByte(ix, v1);  // This may be a no-op if no snap ever done.
      uint8_t v2 = 0;
      memory_.readByte(ix, v2);
      if (v1 != v2)
	{
	  out << "@" << std::hex << ix << ' ' 
	      << (boost::format("%02x %02x") % unsigned(v1) % unsigned(v2))
	      << '\n';
	}
    }
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
		case 0: execLb(rd, rs1, imm); break;
		case 1: execLh(rd, rs1, imm); break;
		case 2: execLw(rd, rs1, imm); break;
		case 4: execLbu(rd, rs1, imm); break;
		case 5: execLhu(rd, rs1, imm); break;
		default: illegalInst(); break;
		}
	    }
	  else if (opcode == 3)  // 00011  I-form
	    {
	      // fence, fence.i  -- not yet implemented.
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
	      else if (funct7 == 0x2f)
		{
		  if      (funct3 == 0) execSub(rd, rs1, rs2);
		  else if (funct3 == 5) execSra(rd, rs1, rs2);
		  else                  illegalInst();
		}
	      else
		illegalInst();
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
		    if      (csr == 0)  execEcall();
		    else if (csr == 1)  execEbreak();
		    else                illegalInst();
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
	  illegalInst();
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
	  illegalInst();
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

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      illegalInst();
	    else if (cif.rd == 2)
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
		if (caf.ic5 != 0)
		  illegalInst();  // As of v2.3 of User-Level ISA (Dec 2107).
		else
		  execSrli(rd, rd, caf.shiftImmed());
		break;
	      case 1:
		if (caf.ic5 != 0)
		  illegalInst();
		else
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
	    if (cif.ic5 != 0)
	      illegalInst();  // TBD: ok for RV64
	    else
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
Core<URV>::disassembleInst(uint32_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;

  // Decode and disassemble
  if ((inst & 0x3) == 0x3) 
    disassembleInst32(inst, str);
  else
    disassembleInst16(inst, str);
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
	  return false;
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
	  return false;
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
		if (caf.ic5 != 0)
		  return false;  // // As of v2.3 of User-Level ISA (Dec 2107).
		return IFormInst::encodeSrli(rd, rd, caf.shiftImmed(), code32);
	      case 1:  // srai64, srai
		if (caf.ic5 != 0)
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
	    if (cif.ic5 != 0)
	      return false;  // TBD: ok for RV64
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
Core<URV>::disassembleInst32(uint32_t inst, std::string& str)
{
  str.clear();
  
  std::ostringstream oss;

  if ((inst & 3) != 3)  // Must be in quadrant 3.
    {
      str = "invalid";
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
	    oss << "lb x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 1:
	    oss << "lh x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 2:
	    oss << "lw x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 4:
	    oss << "lbu x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 5:
	    oss << "lhu x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  default: oss << "invalid"; break;
	  }
      }
      break;

    case 3:  // 00011  I-form
      // fence, fence.i  -- not yet implemented.
      oss << "unimplemnted";
      break;

    case 4:  // 00100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	SRV imm = iform.immed<SRV>();
	switch (iform.fields.funct3)
	  {
	  case 0: 
	    oss << "addi x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 1: 
	    if (iform.fields2.top7 == 0)
	      oss << "slli x" << rd << ", x" << rs1 << ", "
		  << iform.fields2.shamt;
	    else
	      oss << "invalid";
	    break;
	  case 2:
	    oss << "slti x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 3:
	    oss << "sltiu x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 4:
	    oss << "xori x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 5:
	    if (iform.fields2.top7 == 0)
	      oss << "srli x" << rd << ", x" << rs1 << ", "
		  << iform.fields2.shamt;
	    else if (iform.fields2.top7 == 0x20)
	      oss << "srai x" << rd << ", x" << rs1 << ", "
		  << iform.fields2.shamt;
	    else
	      oss << "invalid";
	    break;
	  case 6:
	    oss << "ori x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 7:
	    oss << "andi x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  default:
	    oss << "invalid";
	    break;
	  }
      }
      break;

    case 5:  // 00101   U-form
      {
	UFormInst uform(inst);
	oss << "auipc " << uform.rd << ", " << uform.immed<SRV>();
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
	    oss << "sb x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 1:
	    oss << "sh x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 2:
	    oss << "sw x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  default:
	    oss << "invalid";
	    break;
	  }
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
	      oss << "add x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 1)
	      oss << "sll x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 2)
	      oss << "slt x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 3)
	      oss << "sltu x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 4)
	      oss << "xor x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 5)
	      oss << "srl x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 6)
	      oss << "or x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 7)
	      oss << "and x" << rd << ", x" << rs1 << ", x" << rs2;
	  }
	else if (funct7 == 1)
	  {
	    if (funct3 == 0)
	      oss << "mul x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 1)
	      oss << "mulh x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 2)
	      oss << "mulhsu x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 3)
	      oss << "mulhu x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 4)
	      oss << "div x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 5)
	      oss << "divu x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 6)
	      oss << "rem x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 7)
	      oss << "remu x" << rd << ", x" << rs1 << ", " << rs2;
	  }
	else if (funct7 == 0x2f)
	  {
	    if (funct3 == 0)
	      oss << "sub x" << rd << ", x" << rs1 << ", " << rs2;
	    else if (funct3 == 5)
	      oss << "sra x" << rd << ", x" << rs1 << ", " << rs2;
	    else
	      oss << "invalid";
	  }
	else
	  oss << "invalid";
      }
      break;

    case 13:  // 01101  U-form
      {
	UFormInst uform(inst);
	oss << "lui x" << uform.rd << ", " << uform.immed<SRV>();
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
	    oss << "beq x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 1:
	    oss << "bne x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 4:
	    oss << "blt x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 5:
	    oss << "bge x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 6:
	    oss << "bltu x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 7:
	    oss << "bgeu x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  default:
	    oss << "invalid";
	    break;
	  }
      }
      break;

    case 25:  // 11001  I-form
      {
	IFormInst iform(inst);
	if (iform.fields.funct3 == 0)
	  oss << "jalr x" << iform.fields.rd << ", x" << iform.fields.rs1
	      << ", " << iform.immed<SRV>();
	else
	  oss << "invalid";
      }
      break;

    case 27:  // 11011  J-form
      {
	JFormInst jform(inst);
	oss << "jal " << jform.rd << ", " << jform.immed<SRV>();
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
	  csrName = csr.name_;
	else
	  csrName = "invalid";
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      if (csrNum == 0)
		oss << "ecall";
	      else if (csrNum == 1)
		oss << " ebreak";
	      else
		oss << "invalid";
	    }
	    break;
	  case 1:
	    oss << "csrrw x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 2:
	    oss << "csrrs x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 3:
	    oss << "csrrc x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 5:
	    oss << "csrrwi x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 6:
	    oss << "csrrsi x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 7:
	    oss << "csrrci x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  default: 
	    oss << "invalid";
	    break;
	  }
      }
      break;

    default:
      oss << "invlaid";
      break;
    }

  str = oss.str();
}


template <typename URV>
void
Core<URV>::disassembleInst16(uint16_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;

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
	      oss << "invalid";
	    else
	      {
		CiwFormInst ciwf(inst);
		unsigned immed = ciwf.immed();
		if (immed == 0)
		  oss << "invalid";
		else
		  oss << "c.addi4spn x" << ciwf.rdp << ", " << (immed >> 2);
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  oss << "invalid";
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    oss << "c.lw x" << clf.rdp << ", " << clf.lwImmed() << "(x"
		<< clf.rs1p << ")";
	  }
	  break;
	case 3:  // c.flw, c.ld
	  oss << "invalid";
	  break;
	case 4:  // reserver
	  oss << "invalid";
	  break;
	case 5:  // c.fsd, c.sq
	  oss << "invalid";
	  break;
	case 6:  // c.sw
	  {
	    CswFormInst csw(inst);
	    oss << "c.sw x" << csw.rs2p << ", " << csw.immed() << "(x"
		<< csw.rs1p << ")";
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  oss << "invalid";
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
	      oss << "c.nop";
	    else
	      oss << "c.addi x" << cif.rd << ", " << cif.addiImmed();
	  }
	  break;
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 tis is c.addiw
	  {
	    CjFormInst cjf(inst);
	    oss << "c.jal " << cjf.immed();
	  }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    oss << "c.li x" << cif.rd << ", " << cif.addiImmed();
	  }
	  break;

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      oss << "invalid";
	    else if (cif.rd == 2)
	      oss << "c.addi16sp" << ' ' << (immed16 >> 4);
	    else
	      oss << "c.lui x" << cif.rd << ", " << cif.luiImmed();
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
		if (caf.ic5 != 0)
		  oss << "invalid";
		else
		  oss << "c.srli x" << caf.rdp << ", " << caf.shiftImmed();
		break;
	      case 1:
		if (caf.ic5 != 0)
		  oss << "invalid";
		else
		  oss << "c.srai x" << caf.rdp << ", " << caf.shiftImmed();
		break;
	      case 2:
		oss << "c.andi x" << caf.rdp << ", " << immed;
		break;
	      case 3:
		{
		  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0:
			  oss << "c.sub x" << caf.rdp << ", x" << rs2p; break;
			case 1:
			  oss << "c.xor x" << caf.rdp << ", x" << rs2p; break;
			case 2:
			  oss << "c.or x"  << caf.rdp << ", x" << rs2p; break;
			case 3:
			  oss << "c.and x" << caf.rdp << ", x" << rs2p; break;
			}
		    }
		  else
		    {
		      switch ((immed >> 3) & 3)
			{
			case 0: oss << "invalid"; break; // subw
			case 1: oss << "invalid"; break; // addw
			case 3: oss << "invalid"; break; // reserved
			case 4: oss << "invalid"; break; // reserved
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
	    oss << "c.j " << cjf.immed();
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    oss << "c.beqz x" << cbf.rs1p << ", " << cbf.immed();
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    oss << "c.bnez x" << cbf.rs1p << ", " << cbf.immed();
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
	    if (cif.ic5 != 0)
	      oss << "invalid";  // TBD: ok for RV64
	    else
	      oss << "c.slli x" << cif.rd << ", " << immed;
	  }
	  break;

	case 1:   // c.fldsp, c.lqsp
	  oss << "invalid";
	  break;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.rd;
	    // rd == 0 is legal per Andrew Watterman
	    oss << "c.lwsp x" << rd << ", " << (cif.lwspImmed() >> 2);
	  }
	break;

	case 3:  // c.flwsp c.ldsp
	  oss << "invalid";
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
		      oss << "invalid";
		    else
		      oss << "c.jr x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      oss << "invalid";
		    else
		      oss << "c.mv x" << rd << ", x" << rs2;
		  }
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      oss << "c.ebreak";
		    else
		      oss << "c.jalr x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      oss << "invalid";
		    else
		      oss << "c.add x" << rd << ", x" << rs2;
		  }
	      }
	  }
	  break;

	case 5:  // c.fsdsp  c.sqsp
	  oss << "invalid";
	  break;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    oss << "c.swsp x" << csw.rs2 << ", " << (csw.immed() >> 2);
	  }
	  break;

	case 7:  // c.fswsp c.sdsp
	  oss << "invalid";
	  break;
	}
      break;

    case 3:  // quadrant 3
      oss << "invalid";
      break;

    default:
      oss << "invalid";
      break;
    }

  str = oss.str();
}


template <typename URV>
void
Core<URV>::execBeq(uint32_t rs1, uint32_t rs2, Core<URV>::SRV offset)
{
  if (intRegs_.read(rs1) == intRegs_.read(rs2))
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::execBne(uint32_t rs1, uint32_t rs2, Core<URV>::SRV offset)
{
  if (intRegs_.read(rs1) != intRegs_.read(rs2))
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::execBlt(uint32_t rs1, uint32_t rs2, SRV offset)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  if (v1 < v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::execBltu(uint32_t rs1, uint32_t rs2, SRV offset)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  if (v1 < v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::execBge(uint32_t rs1, uint32_t rs2, SRV offset)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::execBgeu(uint32_t rs1, uint32_t rs2, SRV offset)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::execJalr(uint32_t rd, uint32_t rs1, SRV offset)
{
  pc_ = (intRegs_.read(rs1) + offset) & ~URV(1);
  intRegs_.write(rd, currPc_);
}


template <typename URV>
void
Core<URV>::execJal(uint32_t rd, SRV offset)
{
  intRegs_.write(rd, pc_);
  pc_ = (currPc_ + offset) & ~URV(1);
}


template <typename URV>
void
Core<URV>::execLui(uint32_t rd, SRV imm)
{
  intRegs_.write(rd, imm << 12);
}


template <typename URV>
void
Core<URV>::execAuipc(uint32_t rd, SRV imm)
{
  intRegs_.write(rd, currPc_ + (imm << 12));
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
  URV v = intRegs_.read(rs1) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrai(uint32_t rd, uint32_t rs1, uint32_t amount)
{
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
  initiateException(BREAKPOINT, currPc_, 0);
}


// Set control and status register csr to value of register rs1 and
// save its original value in register rd.
template <typename URV>
void
Core<URV>::execCsrrw(uint32_t rd, uint32_t csr, uint32_t rs1)
{
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
}


template <typename URV>
void
Core<URV>::execCsrrs(uint32_t rd, uint32_t csr, uint32_t rs1)
{
  URV prev;
  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev | intRegs_.read(rs1);

  if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
    {
      illegalInst();
      return;
    }

  intRegs_.write(rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrc(uint32_t rd, uint32_t csr, uint32_t rs1)
{
  URV prev;

  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev & (~ intRegs_.read(rs1));

  if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
    {
      illegalInst();
      return;
    }

  intRegs_.write(rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrwi(uint32_t rd, uint32_t csr, URV imm)
{
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
}


template <typename URV>
void
Core<URV>::execCsrrsi(uint32_t rd, uint32_t csr, URV imm)
{
  URV prev;
  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev | imm;

  if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
    {
      illegalInst();
      return;
    }

  intRegs_.write(rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrci(uint32_t rd, uint32_t csr, URV imm)
{
  URV prev;

  if (not csRegs_.read(CsrNumber(csr), privilegeMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = prev & (~ imm);

  if (not csRegs_.write(CsrNumber(csr), privilegeMode_, next))
    {
      illegalInst();
      return;
    }

  intRegs_.write(rd, prev);
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
Core<URV>::execLwu(uint32_t rd, uint32_t rs1, SRV imm)
{
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
Core<URV>::execSb(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint8_t byte = intRegs_.read(rs2);
  if (not memory_.writeByte(address, byte))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execSh(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint16_t half = intRegs_.read(rs2);
  if (not memory_.writeHalfWord(address, half))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
}


template <typename URV>
void
Core<URV>::execSw(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint32_t word = intRegs_.read(rs2);
  if (not memory_.writeWord(address, word))
    initiateException(STORE_ACCESS_FAULT, currPc_, address);
}


namespace WdRiscv
{

  template<>
  void
  Core<uint32_t>::execMul(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend to 64-bit
    int64_t b = int32_t(intRegs_.read(rs2));

    int32_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint32_t>::execMulh(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend.
    int64_t b = int32_t(intRegs_.read(rs1));
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhsu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));
    uint64_t b = intRegs_.read(rs1);
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    uint64_t a = intRegs_.read(rs1);
    uint64_t b = intRegs_.read(rs1);
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
    __int128_t b = int64_t(intRegs_.read(rs1));
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhsu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));
    __uint128_t b = intRegs_.read(rs1);
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __uint128_t a = intRegs_.read(rs1);
    __uint128_t b = intRegs_.read(rs1);
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


template class Core<uint32_t>;
template class Core<uint64_t>;
