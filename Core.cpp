#include <iostream>
#include <sstream>
#include <assert.h>
#include "Core.hpp"
#include "Inst.hpp"

using namespace WdRiscv;


template <typename URV>
Core<URV>::Core(size_t memorySize, size_t intRegCount)
  : memory_(memorySize), intRegs_(intRegCount), privilegeMode_(MACHINE_MODE),
    mxlen_(8*sizeof(URV))
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
  // Inst0 will be loaded at loc 0x100
  uint32_t inst0 = 0x000000b3;  // 0x100 add  x1, x0, x0   x1 <- 0
  uint32_t inst1 = 0x01000113;  // 0x104 addi x2, x0, 16   x2 <- 16
  uint32_t inst2 = 0x01411113;  // 0x108 slli x2, x2, 20   x2 <- 16*1024*1204
  uint32_t inst3 = 0x00108093;  // 0x10c addi x1, x1, 1    x1 <- x1 + 1
  uint32_t inst4 = 0xfff10113;  // 0x110 addi x2, x2, -1   x2 <- x2 - 1
  uint32_t inst5 = 0xfe015ce3;  // 0x114 bge  x2, x0, -8   if x2 > 0 goto 0x10c
  uint32_t inst6 = 0x00000033;  // 0x118 add  x0, x0, x0   nop

  memory_.writeWord(0x100, inst0);
  memory_.writeWord(0x104, inst1);
  memory_.writeWord(0x108, inst2);
  memory_.writeWord(0x10c, inst3);
  memory_.writeWord(0x110, inst4);
  memory_.writeWord(0x114, inst5);
  memory_.writeWord(0x118, inst6);

  pc_ = 0x100;

  std::string str;

  disassembleInst(inst0, str);
  std::cout << str << '\n';
  disassembleInst(inst1, str);
  std::cout << str << '\n';
  disassembleInst(inst2, str);
  std::cout << str << '\n';
  disassembleInst(inst3, str);
  std::cout << str << '\n';
  disassembleInst(inst4, str);
  std::cout << str << '\n';
  disassembleInst(inst5, str);
  std::cout << str << '\n';
  disassembleInst(inst6, str);
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
Core<URV>::selfTest()
{
  size_t errors = 0;

  // Writing x0 has no effect. Reading x0 yields zero.
  ori(0, 1, ~URV(0));           // ori x0, x1, 0xffff    
  if (intRegs_.read(0) != 0)
    {
      std::cerr << "Writing to x0 erroneously effectual.\n";
      errors++;
    }
  andi(1, 0, ~URV(0));         // andi x1, x0, 0xffff     x1 <- 0
  if (intRegs_.read(1) != 0)
    {
      std::cerr << "Reading x0 yielded non-zero value\n";
      errors++;
    }

  // All bits of registers (except x0) toggle.
  for (uint32_t ix = 1; ix < intRegs_.size(); ++ix)
    {
      addi(ix, 0, 0);          // reg[ix] <- 0
      xori(ix, 0, ~URV(0));    // reg[ix] <- ~0
      if (intRegs_.read(ix) != ~URV(0))
	{
	  std::cerr << "Failed to write all ones to register x" << ix << '\n';
	  errors++;
	}

      xor_(ix, ix, ix);
      if (intRegs_.read(ix) != 0)
	{
	  std::cerr << "Failed to write all zeros to register x" << ix <<  '\n';
	  errors++;
	}
    }
  if (errors)
    return false;

  // Simple tests of integer instruction.
  lui(1, 0x01234);    // lui x1, 0x1234     x1 <- 0x01234000
  ori(2, 1, 0x567);   // ori x2, x1, 0x567  x2 <- 0x01234567)
  if (intRegs_.read(2) != 0x01234567)
    {
      std::cerr << "lui + ori failed\n";
      errors++;
    }

  addi(1, 0, 0x700);
  addi(1, 1, 0x700);
  addi(1, 1, 0x700);
  addi(1, 1, 0x700);
  if (intRegs_.read(1) != 4 * 0x700)
    {
      std::cerr << "addi positive immediate failed\n";
      errors++;
    }

  addi(1, 0, SRV(-1));
  addi(1, 1, SRV(-1));
  addi(1, 1, SRV(-1));
  addi(1, 1, SRV(-1));
  if (intRegs_.read(1) != URV(-4))
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
		case 0: lb (rd, rs1, imm); break;
		case 1: lh (rd, rs1, imm); break;
		case 2: lw (rd, rs1, imm); break;
		case 4: lbu(rd, rs1, imm); break;
		case 5: lhu(rd, rs1, imm); break;
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
	      switch (iform.fields.funct3)
		{
		case 0: addi  (rd, rs1, imm); break;
		case 1: 
		  if (iform.fields2.top7 == 0)
		    slli(rd, rs1, iform.fields2.shamt);
		  else
		    illegalInst();
		  break;
		case 2: slti  (rd, rs1, imm); break;
		case 3: sltiu (rd, rs1, imm); break;
		case 4: xori  (rd, rs1, imm); break;
		case 5:
		  if (iform.fields2.top7 == 0)
		    srli(rd, rs1, iform.fields2.shamt);
		  else if (iform.fields2.top7 == 0x20)
		    srai(rd, rs1, iform.fields2.shamt);
		  else
		    illegalInst();
		  break;
		case 6: ori   (rd, rs1, imm); break;
		case 7: andi  (rd, rs1, imm); break;
		default: illegalInst(); break;
		}
	    }
	  else if (opcode == 5)  // 00101   U-form
	    {
	      UFormInst uform(inst);
	      auipc(uform.rd, uform.immed<SRV>());
	    }
	  else if (opcode == 8)  // 01000  S-form
	    {
	      SFormInst sform(inst);
	      unsigned rs1 = sform.rs1, rs2 = sform.rs2;
	      SRV imm = sform.immed<SRV>();
	      switch (sform.funct3)
		{
		case 0:  sb(rs1, rs2, imm); break;
		case 1:  sh(rs1, rs2, imm); break;
		case 2:  sw(rs1, rs2, imm); break;
		default: illegalInst(); break;
		}
	    }
	  else if (opcode == 12)  // 01100  R-form
	    {
	      RFormInst rform(inst);
	      unsigned rd = rform.rd, rs1 = rform.rs1, rs2 = rform.rs2;
	      switch (rform.funct3)
		{
		case 0:  
		  if (rform.funct7 == 0)
		    add(rd, rs1, rs2);
		  else if (rform.funct7 == 0x2f)
		    sub(rd, rs1, rs2);
		  else
		    illegalInst();
		  break;
		case 1: sll (rd, rs1, rs2); break;
		case 2: slt (rd, rs1, rs2); break;
		case 3: sltu(rd, rs1, rs2); break;
		case 4: xor_(rd, rs1, rs2); break;
		case 5:
		  if (rform.funct7 == 0)
		    srl(rd, rs1, rs2);
		  else if (rform.funct7 == 0x2f)
		    sra(rd, rs1, rs2);
		  else
		    illegalInst();
		  break;
		case 6: or_ (rd, rs1, rs2); break;
		case 7: and_(rd, rs1, rs2); break;
		}
	    }
	}
      else
	{
	  if (opcode ==  13)  // 01101  U-form
	    {
	      UFormInst uform(inst);
	      lui(uform.rd, uform.immed<SRV>());
	    }
	  else if (opcode ==  24) // 11000   B-form
	    {
	      BFormInst bform(inst);
	      unsigned rs1 = bform.rs1, rs2 = bform.rs2;
	      SRV imm = bform.immed<SRV>();
	      switch (bform.funct3)
		{
		case 0: beq (rs1, rs2, imm); break;
		case 1: bne (rs1, rs2, imm); break;
		case 4: blt (rs1, rs2, imm); break;
		case 5: bge (rs1, rs2, imm); break;
		case 6: bltu(rs1, rs2, imm); break;
		case 7: bgeu(rs1, rs2, imm); break;
		default: illegalInst(); break;
		}
	    }
	  else if (opcode == 25)  // 11001  I-form
	    {
	      IFormInst iform(inst);
	      if (iform.fields.funct3 == 0)
		jalr(iform.fields.rd, iform.fields.rs1, iform.immed<SRV>());
	      else
		illegalInst();
	    }
	  else if (opcode == 27)  // 11011  J-form
	    {
	      JFormInst jform(inst);
	      jal(jform.rd, jform.immed<SRV>());
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
		    if (csr == 0)
		      ecall();
		    else if (csr == 1)
		      ebreak();
		    else
		      illegalInst();
		  }
		  break;
		case 1: csrrw (rd, csr, rs1); break;
		case 2: csrrs (rd, csr, rs1); break;
		case 3: csrrc (rd, csr, rs1); break;
		case 5: csrrwi(rd, csr, rs1); break;
		case 6: csrrsi(rd, csr, rs1); break;
		case 7: csrrci(rd, csr, rs1); break;
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
		  addi((8+ciwf.rdp), 2, immed);  // 2: stack pointer
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  illegalInst();
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    lw((8+clf.rdp), (8+clf.rs1p), clf.lwImmed());
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
	    sw(8+csw.rs1p, 8+csw.rs2p, csw.immed());
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
	    if (inst == 1) // c.nop:  addi x0, x0, 0
	      {
		addi(0, 0, 0);
		break;
	      }
	    CiFormInst cif(inst);
	    if (cif.rd == 0)
	      illegalInst();  // As of v2.3 of User-Level ISA (Dec 2107).
	    else
	      addi(cif.rd, cif.rd, cif.addiImmed());
	  }
	  break;
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 tis is c.addiw
	  {
	    CjFormInst cjf(inst);
	    jal(1, cjf.immed());
	  }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    if (cif.rd == 0)
	      illegalInst(); // As of v2.3 of User-Level ISA (Dec 2107).
	    else
	      addi(cif.rd, 0, cif.addiImmed());
	  }
	  break;

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      illegalInst();
	    else if (cif.rd == 2)
	      addi(cif.rd, cif.rd, cif.addi16spImmed());
	    else if (cif.rd == 0)
	      illegalInst();  // As of v2.3 of User-Level ISA (Dec 2107).
	    else
	      lui(cif.rd, cif.luiImmed());
	  }
	  break;

	case 4:  // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and c.subw c.addw
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    unsigned rd = 8 + caf.rdp;
	    switch (caf.funct2)
	      {
	      case 0:
		if (immed == 0)
		  illegalInst();  // srli64
		else if (caf.ic5 != 0)
		  illegalInst();  // As of v2.3 of User-Level ISA (Dec 2107).
		else
		  srli(rd, rd, caf.shiftImmed());
		break;
	      case 1:
		if (immed == 0)
		  illegalInst();  // srai64
		else if (caf.ic5 != 0)
		  illegalInst();
		else
		  srai(rd, rd, caf.shiftImmed());
		break;
	      case 2:
		andi(rd, rd, immed);
		break;
	      case 3:
		{
		  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
		  unsigned rs2 = 8 + rs2p;
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0: sub(rd, rd, rs2); break;
			case 1: xor_(rd, rd, rs2); break;
			case 2: or_(rd, rd, rs2); break;
			case 3: and_(rd, rd, rs2); break;
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
	    jal(0, cjf.immed());
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    beq(8+cbf.rs1p, 0, cbf.immed());
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    bne(8+cbf.rs1p, 0, cbf.immed());
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
	    if (immed == 0 or cif.ic5 != 0)
	      illegalInst();  // TBD: ok for RV64
	    else
	      slli(cif.rd, cif.rd, immed);
	  }
	  break;

	case 1:  // c.fldsp, c.lqsp
	  illegalInst();
	  break;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.rd;
	    if (rd == 0)
	      illegalInst();
	    else
	      lw(rd, 2, cif.lwspImmed());
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
	    if ((immed & 0x20) == 0)
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      illegalInst();
		    else
		      jalr(0, rd, 0);
		  }
		else
		  {
		    if (rd == 0)
		      illegalInst();
		    else
		      add(rd, 0, rs2);
		  }
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      ebreak();
		    else
		      jalr(1, rd, 0);
		  }
		else
		  {
		    if (rd == 0)
		      illegalInst();
		    else
		      add(rd, rd, rs2);
		  }
	      }
	  }
	  break;

	case 5:
	  illegalInst();
	  break;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    sw(2, csw.rs2, csw.immed());  // imm(x2) <- rs2
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
	    // 2: stack pointer
	    return IFormInst::encodeAddi(8+ciwf.rdp, 2, immed, code32);
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
	    if (inst == 1)  // c.nop: addi x0, x0, 0
	      return IFormInst::encodeAddi(0, 0, 0, code32);
	    CiFormInst c(inst);
	    if (c.rd == 0)
	      return false;  // As of v2.3 of User-Level ISA (Dec 2107).
	    if (c.addiImmed() == 0)
	      return false;  // V2.3 says hint for the future. Illegal now.
	    return IFormInst::encodeAddi(c.rd, c.rd, c.addiImmed(), code32);
	  }
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 tis is c.addiw
	  {
	    // jal(1, cjf.immed());
	    CjFormInst cjf(inst);
	    return JFormInst::encodeJal(1, cjf.immed(), code32);
	  }

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    if (cif.rd == 0)
	      return false; // As of v2.3 of User-Level ISA (Dec 2107).
	    return IFormInst::encodeAddi(cif.rd, 0, cif.addiImmed(), code32);
	  }

	case 3:  // ci.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed = cif.addi16spImmed();
	    if (immed == 0)
	      return false;
	    if (cif.rd == 2)  // c.addi16sp
	      return IFormInst::encodeAddi(cif.rd, cif.rd, cif.addi16spImmed(),
					   code32);
	    if (cif.rd == 0)
	      return false; // As of v2.3 of User-Level ISA (Dec 2107).
	    return UFormInst::encodeLui(cif.rd, cif.luiImmed(), code32);
	  }

	case 4:  // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and c.subw c.addw
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    unsigned rd = 8 + caf.rdp;
	    switch (caf.funct2)
	      {
	      case 0: // srli64, srli
		if (immed == 0)
		  return false;  // srli64
		if (caf.ic5 != 0)
		  return false;  // // As of v2.3 of User-Level ISA (Dec 2107).
		return IFormInst::encodeSrli(rd, rd, caf.shiftImmed(), code32);
	      case 1:  // srai64, srai
		if (immed == 0)
		  return false; // srai64
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
	    return JFormInst:: encodeJal(0, cjf.immed(), code32);
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    return BFormInst::encodeBeq(8+cbf.rs1p, 0, cbf.immed(), code32);
	  }

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    return BFormInst::encodeBne(8+cbf.rs1p, 0, cbf.immed(), code32);
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
	    if (immed == 0 or cif.ic5 != 0)
	      return false;  // TBD: ok for RV64
	    return IFormInst::encodeSlli(cif.rd, cif.rd, immed, code32);
	  }

	case 1:  // c.fldsp, c.lqsp
	  return false;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.rd;
	    if (rd == 0)
	      return false;
	    return IFormInst::encodeLw(rd, 2, cif.lwspImmed(), code32); // 2: sp
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
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      return false;
		    return IFormInst::encodeJalr(0, rd, 0, code32);
		  }
		if (rd == 0)
		  return false;
		return RFormInst::encodeAdd(rd, 0, rs2,  code32);
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      return IFormInst::encodeEbreak(code32);
		    return IFormInst::encodeJalr(1, rd, 0, code32);
		  }
		if (rd == 0)
		  return false;
		return RFormInst::encodeAdd(rd, rd, rs2,  code32);
	      }
	  }

	case 5:
	  return false;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    return SFormInst::encodeSw(2, csw.rs2, csw.immed(), code32);
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
	switch (rform.funct3)
	  {
	  case 0:  
	    if (rform.funct7 == 0)
	      oss << "add x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (rform.funct7 == 0x20)
	      oss << "sub x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      oss << "invalid";
	    break;
	  case 1:
	    oss << "sll x" << rd << ", x" << rs1 << ", x" << rs2;
	    break;
	  case 2:
	    oss << "slt x" << rd << ", x" << rs1 << ", x" << rs2;
	    break;
	  case 3:
	    oss << "sltu x" << rd << ", x" << rs1 << ", x" << rs2;
	    break;
	  case 4:
	    oss << "xor x" << rd << ", x" << rs1 << ", x" << rs2;
	    break;
	  case 5:
	    if (rform.funct7 == 0)
	      oss << "srl x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (rform.funct7 == 0x2f)
	      oss << "sra x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      illegalInst();
	    break;
	  case 6:
	    oss << "or x" << rd << ", x" << rs1 << ", x" << rs2;
	    break;
	  case 7:
	    oss << "and x" << rd << ", x" << rs1 << ", x" << rs2;
	    break;
	  }
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
		  illegalInst();
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
	  illegalInst();
	  break;
	case 6:  // c.sw
	  {
	    CswFormInst csw(inst);
	    oss << "c.sw x" << csw.rs2p << ", " << csw.immed() << "(x"
		<< csw.rs1p << ")";
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
	    if (inst == 1)
	      oss << "c.nop";
	    else
	      {
		CiFormInst cif(inst);
		if (cif.rd == 0)
		  oss << "invalid";
		else
		  oss << "c.addi x" << cif.rd << ", " << cif.addiImmed();
	      }
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
	    if (cif.rd == 0)
	      oss << "invalid";
	    else
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
	    else if (cif.rd == 0)
	      oss << "invalid"; // As of v2.3 of User-Level ISA (Dec 2107).
	    else
	      oss << "c.lui x" << cif.rd << ", " << cif.luiImmed();
	  }
	  break;

	case 4:  // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and c.subw c.addw
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    unsigned rd = 8 + caf.rdp;
	    switch (caf.funct2)
	      {
	      case 0:
		if (immed == 0)
		  oss << "invalid";  // srli64
		else if (caf.ic5 != 0)
		  oss << "invalid";
		else
		  oss << "c.srli x" << caf.rdp << ", " << caf.shiftImmed();
		break;
	      case 1:
		if (immed == 0)
		  oss << "invalid";  // srai64
		else if (caf.ic5 != 0)
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
	    if (immed == 0 or cif.ic5 != 0)
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
	    if (rd == 0)
	      oss << "invalid";
	    else
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
      illegalInst();
      break;

    default:
      illegalInst();
      break;
    }

  str = oss.str();
}


template <typename URV>
void
Core<URV>::beq(uint32_t rs1, uint32_t rs2, Core<URV>::SRV offset)
{
  if (intRegs_.read(rs1) == intRegs_.read(rs2))
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::bne(uint32_t rs1, uint32_t rs2, Core<URV>::SRV offset)
{
  if (intRegs_.read(rs1) != intRegs_.read(rs2))
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::blt(uint32_t rs1, uint32_t rs2, SRV offset)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  if (v1 < v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::bltu(uint32_t rs1, uint32_t rs2, SRV offset)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  if (v1 < v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::bge(uint32_t rs1, uint32_t rs2, SRV offset)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::bgeu(uint32_t rs1, uint32_t rs2, SRV offset)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    pc_ = currPc_ + offset;
}


template <typename URV>
void
Core<URV>::jalr(uint32_t rd, uint32_t rs1, SRV offset)
{
  pc_ = (intRegs_.read(rs1) + offset) & ~URV(1);
  intRegs_.write(rd, currPc_);
}


template <typename URV>
void
Core<URV>::jal(uint32_t rd, SRV offset)
{
  intRegs_.write(rd, pc_);
  pc_ = (currPc_ + offset) & ~URV(1);
}


template <typename URV>
void
Core<URV>::lui(uint32_t rd, SRV imm)
{
  intRegs_.write(rd, imm << 12);
}


template <typename URV>
void
Core<URV>::auipc(uint32_t rd, SRV imm)
{
  intRegs_.write(rd, currPc_ + (imm << 12));
}


template <typename URV>
void
Core<URV>::addi(uint32_t rd, uint32_t rs1, SRV imm)
{
  SRV v = intRegs_.read(rs1);
  v += imm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::slli(uint32_t rd, uint32_t rs1, uint32_t amount)
{
  // TBD: check amount.
  URV v = intRegs_.read(rs1) << amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::slti(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV v = SRV(intRegs_.read(rs1)) < imm ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::sltiu(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) < uimm ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::xori(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) ^ uimm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::srli(uint32_t rd, uint32_t rs1, uint32_t amount)
{
  // TBD: check amount.
  URV v = intRegs_.read(rs1) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::srai(uint32_t rd, uint32_t rs1, uint32_t amount)
{
  // TBD: check amount.
  URV v = SRV(intRegs_.read(rs1)) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::ori(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) | uimm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::andi(uint32_t rd, uint32_t rs1, URV uimm)
{
  URV v = intRegs_.read(rs1) & uimm;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::add(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) + intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::sub(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) - intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::sll(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) << (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::slt(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::sltu(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::xor_(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) ^ intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::srl(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::sra(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = SRV(intRegs_.read(rs1)) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::or_(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) | intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::and_(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV v = intRegs_.read(rs1) & intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::ecall()
{
  if (privilegeMode_ == MACHINE_MODE)
    initiateException(M_ENV_CALL, currPc_, 0);
  else if (privilegeMode_ == SUPERVISOR_MODE)
    initiateException(S_ENV_CALL, currPc_, 0);
  else if (privilegeMode_ == USER_MODE)
    initiateException(U_ENV_CALL, currPc_, 0);
  else
    assert(0 and "Invalid privilege mode in ecall");
}


template <typename URV>
void
Core<URV>::ebreak()
{
  initiateException(BREAKPOINT, currPc_, 0);
}


// Set control and status register csr to value of register rs1 and
// save its original value in register rd.
template <typename URV>
void
Core<URV>::csrrw(uint32_t rd, uint32_t csr, uint32_t rs1)
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
Core<URV>::csrrs(uint32_t rd, uint32_t csr, uint32_t rs1)
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
Core<URV>::csrrc(uint32_t rd, uint32_t csr, uint32_t rs1)
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
Core<URV>::csrrwi(uint32_t rd, uint32_t csr, URV imm)
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
Core<URV>::csrrsi(uint32_t rd, uint32_t csr, URV imm)
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
Core<URV>::csrrci(uint32_t rd, uint32_t csr, URV imm)
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
Core<URV>::lb(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint8_t byte;  // Use a signed type.
  if (memory_.readByte(address, byte))
    {
      SRV value = int8_t(byte); // Sign extend.
      intRegs_.write(rd, value);
    }
  // TBD: exception.
}


template <typename URV>
void
Core<URV>::lh(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint16_t half;  // Use a signed type.
  if (memory_.readHalfWord(address, half))
    {
      SRV value = int16_t(half); // Sign extend.
      intRegs_.write(rd, value);
    }
  // TBD: exception.
}


template <typename URV>
void
Core<URV>::lw(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint32_t word;  // Use a signed type.
  if (memory_.readWord(address, word))
    {
      SRV value = int32_t(word); // Sign extend.
      intRegs_.write(rd, value);
    }
  // TBD: exception.
}


template <typename URV>
void
Core<URV>::lbu(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint8_t byte;  // Use an unsigned type.
  if (memory_.readByte(address, byte))
    {
      intRegs_.write(rd, byte); // Zero extend into register.
    }
  // TBD: exception.
}


template <typename URV>
void
Core<URV>::lhu(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint16_t half;  // Use an unsigned type.
  if (memory_.readHalfWord(address, half))
    {
      intRegs_.write(rd, half); // Zero extend into register.
    }
  // TBD: exception.
}


template <typename URV>
void
Core<URV>::lwu(uint32_t rd, uint32_t rs1, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint32_t word;  // Use an unsigned type.
  if (memory_.readWord(address, word))
    {
      intRegs_.write(rd, word); // Zero extend into register.
    }
  // TBD: exception.
}


template <typename URV>
void
Core<URV>::sb(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint8_t byte = intRegs_.read(rs2);
  if (not memory_.writeByte(address, byte))
    ; // TBD: exception.
}


template <typename URV>
void
Core<URV>::sh(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint16_t half = intRegs_.read(rs2);
  if (not memory_.writeHalfWord(address, half))
    ; // TBD: exception.
}


template <typename URV>
void
Core<URV>::sw(uint32_t rs1, uint32_t rs2, SRV imm)
{
  URV address = intRegs_.read(rs1) + imm;
  uint32_t word = intRegs_.read(rs2);
  if (not memory_.writeWord(address, word))
    ; // TBD: exception.
}


namespace WdRiscv
{

  template<>
  void
  Core<uint32_t>::mul(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend to 64-bit
    int64_t b = int32_t(intRegs_.read(rs2));

    int32_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint32_t>::mulh(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend.
    int64_t b = int32_t(intRegs_.read(rs1));
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::mulhsu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));
    uint64_t b = intRegs_.read(rs1);
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::mulhu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    uint64_t a = intRegs_.read(rs1);
    uint64_t b = intRegs_.read(rs1);
    uint64_t c = a * b;
    uint32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template<>
  void
  Core<uint64_t>::mul(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend to 64-bit
    __int128_t b = int64_t(intRegs_.read(rs2));

    int64_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint64_t>::mulh(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend.
    __int128_t b = int64_t(intRegs_.read(rs1));
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::mulhsu(uint32_t rd, uint32_t rs1, uint32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));
    __uint128_t b = intRegs_.read(rs1);
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::mulhu(uint32_t rd, uint32_t rs1, uint32_t rs2)
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
Core<URV>::div(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = a / b;  // TBD. handle b == 0
  intRegs_.write(rd, c);
}


template <typename URV>
void
Core<URV>::divu(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV a = intRegs_.read(rs1);
  URV b = intRegs_.read(rs2);
  URV c = a / b;  // TBD. handle b == 0
  intRegs_.write(rd, c);
}


// Remainder instruction.
template <typename URV>
void
Core<URV>::rem(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = a % b;  // TBD. handle b == 0
  intRegs_.write(rd, c);
}


// Unsigned remainder instruction.
template <typename URV>
void
Core<URV>::remu(uint32_t rd, uint32_t rs1, uint32_t rs2)
{
  URV a = intRegs_.read(rs1);
  URV b = intRegs_.read(rs2);
  URV c = a % b;  // TBD. handle b == 0
  intRegs_.write(rd, c);
}


template class Core<uint32_t>;
template class Core<uint64_t>;
