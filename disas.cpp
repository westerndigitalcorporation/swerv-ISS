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

#include <iomanip>
#include <iostream>
#include <sstream>
#include "Core.hpp"
#include "instforms.hpp"


using namespace WdRiscv;


static
std::string
roundingModeString(RoundingMode mode)
{
  switch (mode)
    {
    case RoundingMode::NearestEven: return "rne";
    case RoundingMode::Zero:        return "rtz";
    case RoundingMode::Down:        return "rdn";
    case RoundingMode::Up:          return "rup";
    case RoundingMode::NearestMax:  return "rmm";
    case RoundingMode::Invalid1:    return "inv1";
    case RoundingMode::Invalid2:    return "inv2";
    case RoundingMode::Dynamic:     return "dyn";
    default:                        return "inv";
    }
  return "inv";
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst rd, rs1, rs2
template <typename URV>
static
void
printRdRs1Rs2(const Core<URV>& core, std::ostream& stream, const char* inst,
	      unsigned rd, unsigned rs1, unsigned rs2)
{
  // Print instruction in a 9 character field.
  stream << std::left << std::setw(9) << inst;

  stream << core.intRegName(rd) << ", " << core.intRegName(rs1) << ", "
	 << core.intRegName(rs2);
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst rd, rs1
template <typename URV>
static
void
printRdRs1(const Core<URV>& core, std::ostream& stream, const char* inst,
	       unsigned rd, unsigned rs1)
{
  // Print instruction in a 9 character field.
  stream << std::left << std::setw(9) << inst;

  stream << core.intRegName(rd) << ", " << core.intRegName(rs1);
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form:  inst reg1, imm(reg2)
template <typename URV>
static
void
printLdSt(const Core<URV>& core, std::ostream& stream, const char* inst,
	  unsigned rd, unsigned rs1, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  const char* sign = imm < 0? "-" : "";
  if (imm < 0)
    imm = -imm;

  // Keep least sig 12 bits.
  imm = imm & 0xfff;

  stream << core.intRegName(rd) << ", " << sign << "0x"
	 << std::hex << imm << "(" << core.intRegName(rs1) << ")";
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg1, imm(reg2) where inst
/// is a floating point ld/st instruction.
template <typename URV>
static
void
printFpLdSt(const Core<URV>& core, std::ostream& stream, const char* inst,
	    unsigned rd, unsigned rs1, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  const char* sign = imm < 0? "-" : "";
  if (imm < 0)
    imm = -imm;

  // Keep least sig 12 bits.
  imm = imm & 0xfff;

  stream << "f" << rd << ", " << sign << "0x" << std::hex << imm
	 << "(" << core.intRegName(rs1) << ")";
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, reg, imm where inst is
/// a shift instruction.
template <typename URV>
static
void
printShiftImm(const Core<URV>& core, std::ostream& stream, const char* inst,
	      unsigned rs1, unsigned rs2, uint32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  if constexpr (sizeof(URV) == 4)
    imm = imm & 0x1f;
  else
    imm = imm & 0x3f;

  stream << core.intRegName(rs1) << ", " << core.intRegName(rs2)
	 << ", 0x" << std::hex << imm;
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, reg, imm where imm is
/// a 12 bit constant.
template <typename URV>
static
void
printRegRegImm12(const Core<URV>& core, std::ostream& stream, const char* inst,
		 unsigned rs1, unsigned rs2, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << core.intRegName(rs1) << ", " << core.intRegName(rs2) << ", ";

  if (imm < 0)
    stream << "-0x" << std::hex << ((-imm) & 0xfff);
  else
    stream << "0x" << std::hex << (imm & 0xfff);
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, imm where inst is a
/// compressed instruction.
template <typename URV>
static
void
printRegImm(const Core<URV>& core, std::ostream& stream, const char* inst,
	    unsigned rs1, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << core.intRegName(rs1) << ", ";

  if (imm < 0)
    stream << "-0x" << std::hex << (-imm);
  else
    stream << "0x" << std::hex << imm;
}


/// Helper to disassemble method. Print on the given stream given
/// branch instruction which is of the form: inst reg, reg, imm where
/// imm is a 12 bit constant.
template <typename URV>
static
void
printBranch(const Core<URV>& core, std::ostream& stream, const char* inst,
	    unsigned rs1, unsigned rs2, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << core.intRegName(rs1) << ", " << core.intRegName(rs2) << ", . ";

  char sign = '+';
  if (imm < 0)
    {
      sign = '-';
      imm = -imm;
    }
      
  stream << sign << " 0x" << std::hex << (imm & 0xfff);
}


/// Helper to disassemble method. Print on the given stream given
/// compressed branch instruction which is of the form: inst reg, imm.
template <typename URV>
static
void
printBranch(const Core<URV>& core, std::ostream& stream, const char* inst,
	    unsigned rs1, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << core.intRegName(rs1) << ", . ";

  char sign = '+';
  if (imm < 0)
    {
      sign = '-';
      imm = -imm;
    }
  stream << sign << " 0x" << std::hex << imm;
}


/// Helper to disassemble method.
template <typename URV>
static
void
printAmo(const Core<URV>& core, std::ostream& stream, const char* inst, bool aq,
	 bool rl, unsigned rd, unsigned rs1, unsigned rs2)
{
  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ' << core.intRegName(rd) << ", " << core.intRegName(rs2) << ", ("
	 << core.intRegName(rs1) << ")";
}


/// Helper to disassemble method.
template <typename URV>
static
void
printLr(const Core<URV>& core, std::ostream& stream, const char* inst,
	bool aq, bool rl, unsigned rd, unsigned rs1)
{
  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ' << core.intRegName(rd) << ", (" << core.intRegName(rs1) << ")";
}


/// Helper to disassemble method.
template <typename URV>
static
void
printSc(const Core<URV>& core, std::ostream& stream, const char* inst,
	bool aq, bool rl, unsigned rd, unsigned rs1, unsigned rs2)
{
  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ' << core.intRegName(rd) << ", " << core.intRegName(rs2)
	 << ", (" << core.intRegName(rs1) << ")";
}


/// Helper to disassemble methods. Print an rv32f floating point
/// instruction with 4 operands.
template <typename URV>
static
void
printFp32f(const Core<URV>& core, std::ostream& stream, const char* inst,
	   unsigned rd, unsigned rs1, unsigned rs2,
	   unsigned rs3, RoundingMode mode)
{
  if (not core.isRvf())
    {
      stream << "illegal";
      return;
    }

  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2 << ", f" << rs3
	 << ", " << roundingModeString(mode);
}


/// Helper to disassemble methods. Print an rv32d floating point
/// instruction with 4 operands.
template <typename URV>
static
void
printFp32d(const Core<URV>& core, std::ostream& stream, const char* inst,
	   unsigned rd, unsigned rs1, unsigned rs2,
	   unsigned rs3, RoundingMode mode)
{
  if (not core.isRvd())
    {
      stream << "illegal";
      return;
    }

  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2 << ", f" << rs3
	 << ", " << roundingModeString(mode);
}


/// Helper to disassemble methods. Print an rv32f floating point
/// instruction with 3 operands.
template <typename URV>
static
void
printFp32f(const Core<URV>& core, std::ostream& stream, const char* inst,
	   unsigned rd, unsigned rs1, unsigned rs2, RoundingMode mode)
{
  if (not core.isRvf())
    {
      stream << "illegal";
      return;
    }

  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2
	   << ", " << roundingModeString(mode);
}


/// Helper to disassemble methods. Print an rv32f floating point
/// instruction with 3 operands.
template <typename URV>
static
void
printFp32d(const Core<URV>&core, std::ostream& stream, const char* inst,
	   unsigned rd, unsigned rs1, unsigned rs2, RoundingMode mode)
{
  if (not core.isRvd())
    {
      stream << "illegal";
      return;
    }

  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2
	   << ", " << roundingModeString(mode);
}


template <typename URV>
void
Core<URV>::disassembleFp(uint32_t inst, std::ostream& os)
{
  if (not isRvf())
    {
      os << "illegal";
      return;
    }

  RFormInst rform(inst);
  unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
  unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
  RoundingMode mode = RoundingMode(f3);
  std::string rms = roundingModeString(mode);

  if (f7 & 1)
    {
      if (not isRvd())
	{
	  os << "illegal";
	  return;
	}

      if (f7 == 1)        printFp32d(*this, os, "fadd.d", rd, rs1, rs2, mode);
      else if (f7 == 5)   printFp32d(*this, os, "fsub.d", rd, rs1, rs2, mode);
      else if (f7 == 9)   printFp32d(*this, os, "fmul.d", rd, rs1, rs2, mode);
      else if (f7 == 0xd) printFp32d(*this, os, "fdiv.d", rd, rs1, rs2, mode);
      else if (f7 == 0x11)
	{
	  if      (f3 == 0) os << "fsgnj.d  f" << rd << ", f" << rs1;
	  else if (f3 == 1) os << "fsgnjn.d f" << rd << ", f" << rs1;
	  else if (f3 == 2) os << "fsgnjx.d f" << rd << ", f" << rs1;
	  else              os << "illegal";
	}
      else if (f7 == 0x15)
	{
	  if      (f3==0) os<< "fmin.d   f" << rd << ", f" << rs1 << ", f" << rs2;
	  else if (f3==1) os<< "fmax.d   f" << rd << ", f" << rs1 << ", f" << rs2;
	  else            os<< "illegal";
	}
      else if (f7 == 0x21 and rs2 == 0)
	os << "fcvt.d.s f" << rd << ", f" << rs1 << ", " << rms;
      else if (f7 == 0x2d)
	os << "fsqrt.d  f" << rd << ", f" << rs1 << ", " << rms;
      else if (f7 == 0x51)
	{
	  std::string rdn = intRegName(rd);
	  if      (f3==0)  os<< "fle.d    " << rdn << ", f" << rs1 << ", f" << rs2;
	  else if (f3==1)  os<< "flt.d    " << rdn << ", f" << rs1 << ", f" << rs2;
	  else if (f3==2)  os<< "feq.d    " << rdn << ", f" << rs1 << ", f" << rs2;
	  else             os<< "illegal";
	}
      else if (f7 == 0x61)
	{
	  std::string rdn = intRegName(rd);
	  if (rs2==0)
	    os << "fcvt.w.d "  << rdn << ", f" << rs1 << ", " << rms;
	  else if (rs2==1)
	    os << "fcvt.wu.d " << rdn << ", f" << rs1 << ", " << rms;
	  else
	    os << "illegal";
	}
      else if (f7 == 0x69)
	{
	  std::string rs1n = intRegName(rs1);
	  if (rs2==0)
	    os << "fcvt.d.w f"  << rd << ", " << rs1n << ", " << rms;
	  else if (rs2==1)
	    os << "fcvt.d.wu f" << rd << ", " << rs1n << ", " << rms;
	  else
	    os << "illegal";
	}
      else if (f7 == 0x71)
	{
	  std::string rdn = intRegName(rd);
	  if (rs2==0 and f3==0)  os << "fmv.x.d " << rdn << ", f" << rs1;
	  if (rs2==0 and f3==1)  os << "fclass.d " << rdn << ", f" << rs1;
	  else                   os << "illegal";
	}
      else if (f7 == 0x79)
	{
	  std::string rs1n = intRegName(rs1);
	  if (rs2 == 0 and f3 == 0)  os << "fmv.d.x  f" << rd << ", " << rs1n;
	  else                       os << "illegal";
	}
      else
	os << "illegal";
      return;
    }

  if (f7 == 0)          printFp32f(*this, os, "fadd.s", rd, rs1, rs2, mode);
  else if (f7 == 4)     printFp32f(*this, os, "fsub.s", rd, rs1, rs2, mode);
  else if (f7 == 8)     printFp32f(*this, os, "fmul.s", rd, rs1, rs2, mode);
  else if (f7 == 0xc)   printFp32f(*this, os, "div.s", rd, rs1, rs2, mode);
  else if (f7 == 0x10)
    {
      if      (f3 == 0) os << "fsgnj.s  f" << rd << ", f" << rs1;
      else if (f3 == 1) os << "fsgnjn.s f" << rd << ", f" << rs1;
      else if (f3 == 2) os << "fsgnjx.s f" << rd << ", f" << rs1;
      else              os << "illegal";
    }
  else if (f7 == 0x14)
    {
      if      (f3 == 0) os << "fmin.s  f" << rd << ", f" << rs1 << ", f" << rs2;
      else if (f3 == 1)	os << "fmax.s  f" << rd << ", f" << rs1 << ", f" << rs2;
      else              os << "illegal";
    }
  
  else if (f7 == 0x20 and rs2 == 1)
    os << "fcvt.s.d f" << rd << ", f" << rs1 << ", " << rms;
  else if (f7 == 0x2c)
    os << "fsqrt.s  f" << rd << ", f" << rs1 << ", " << rms;
  else if (f7 == 0x50)
    {
      std::string rdn = intRegName(rd);
      if      (f3 == 0) os << "fle.s    " << rdn << ", f" << rs1 << ", f" << rs2;
      else if (f3 == 1) os << "flt.s    " << rdn << ", f" << rs1 << ", f" << rs2;
      else if (f3 == 2) os << "feq.s    " << rdn << ", f" << rs1 << ", f" << rs2;
      else              os << "illegal";
    }
  else if (f7 == 0x60)
    {
      std::string rdn = intRegName(rd);
      if      (rs2==0) os << "fcvt.w.s "  << rdn << ", f" << rs1 << ", " << rms;
      else if (rs2==1) os << "fcvt.wu.s " << rdn << ", f" << rs1 << ", " << rms;
      else if (rs2==2) os << "fcvt.l.s "  << rdn << ", f" << rs1 << ", " << rms;
      else if (rs2==3) os << "fcvt.lu.s " << rdn << ", f" << rs1 << ", " << rms;
      else             os << "illegal";
    }
  else if (f7 == 0x68)
    {
      std::string rs1n = intRegName(rs1);

      if      (rs2==0) os << "fcvt.s.w f"  << rd << ", " << rs1n << ", " << rms;
      else if (rs2==1) os << "fcvt.s.wu f" << rd << ", " << rs1n << ", " << rms;
      else if (rs2==2) os << "fcvt.s.l f"  << rd << ", " << rs1n << ", " << rms;
      else if (rs2==3) os << "fcvt.s.lu f" << rd << ", " << rs1n << ", " << rms;
      else             os << "illegal";
    }
  else if (f7 == 0x70)
    {
      std::string rdn = intRegName(rd);
      if      (rs2 == 0 and f3 == 0)  os << "fmv.x.w  " << rdn << ", f" << rs1;
      else if (rs2 == 0 and f3 == 1)  os << "fclass.s " << rdn << ", f" << rs1;
      else                            os << "illegal";
    }
  else if (f7 == 0x74)
    {
      std::string rs1n = intRegName(rs1);
      if (rs2 == 0 and f3 == 0)  os << "fmv.w.x  f" << rd << ", " << rs1n;
      else                       os << "illegal";
    }
  else
    os << "illegal";
}


template <typename URV>
void
Core<URV>::disassembleInst32(uint32_t inst, std::ostream& out)
{
  if (not isFullSizeInst(inst))
    {
      out << "illegal";  // Not a compressed instruction.
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
	  case 0:  printLdSt(*this, out, "lb",  rd, rs1, imm); break;
	  case 1:  printLdSt(*this, out, "lh",  rd, rs1, imm); break;
	  case 2:  printLdSt(*this, out, "lw",  rd, rs1, imm); break;
	  case 3:  printLdSt(*this, out, "ld",  rd, rs1, imm); break;
	  case 4:  printLdSt(*this, out, "lbu", rd, rs1, imm); break;
	  case 5:  printLdSt(*this, out, "lhu", rd, rs1, imm); break;
	  case 6:  printLdSt(*this, out, "lwu", rd, rs1, imm); break;
	  default: out << "illegal";                        break;
	  }
      }
      break;

    case 1:   // 0001 I-Form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	uint32_t f3 = iform.fields.funct3;
	if (f3 == 2)
	  {
	    if (isRvf())
	      printFpLdSt(*this, out, "flw", rd, rs1, imm);
	    else
	      out << "illegal";
	  }
	else if (f3 == 3)
	  {
	    if (isRvd())
	      printFpLdSt(*this, out, "fld", rd, rs1, imm);
	    else
	      out << "illegal";
	  }
	else
	  out << "illegal";
      }
      break;

    case 3:  // 00011  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	unsigned funct3 = iform.fields.funct3;
	if (rd != 0 or rs1 != 0)
	  out << "illegal";
	else if (funct3 == 0)
	  {
	    if (iform.top4() != 0)
	      out << "illegal";
	    else
	      out << "fence  " << iform.pred() << ", " << iform.succ();
	  }
	else if (funct3 == 1)
	  {
	    if (iform.uimmed() != 0)
	      out << "illegal";
	    else
	      out << "fence.i ";
	  }
	else
	  out << "illegal";
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
	    printRegRegImm12(*this, out, "addi", rd, rs1, imm);
	    break;
	  case 1: 
	    {
	      unsigned topBits = 0, shamt = 0;
	      iform.getShiftFields(isRv64(), topBits, shamt);
	      if (topBits == 0)
		printShiftImm(*this, out, "slli", rd, rs1, shamt);
	      else if ((topBits >> 1) == 4)
		printShiftImm(*this, out, "sloi", rd, rs1, shamt);
	      else if (topBits == 0x600)
		printRdRs1(*this, out, "clz", rd, rs1);
	      else if (topBits == 0x601)
		printRdRs1(*this, out, "ctz", rd, rs1);
	      else if (topBits == 0x602)
		printRdRs1(*this, out, "pcnt", rd, rs1);
	      else
		out << "illegal";
	    }
	    break;
	  case 2:
	    printRegRegImm12(*this, out, "slti", rd, rs1, imm);
	    break;
	  case 3:
	    printRegRegImm12(*this, out, "sltiu", rd, rs1, imm);
	    break;
	  case 4:
	    printRegRegImm12(*this, out, "xori", rd, rs1, imm);
	    break;
	  case 5:
	    {
	      unsigned topBits = 0, shamt = 0;
	      iform.getShiftFields(isRv64(), topBits, shamt);
	      if (topBits == 0)
		printShiftImm(*this, out, "srli", rd, rs1, shamt);
	      else if ((topBits >> 1) == 4)
		printShiftImm(*this, out, "sroi", rd, rs1, shamt);
	      else if ((topBits >> 1) == 0xc)
		printShiftImm(*this, out, "rori", rd, rs1, shamt);
	      else
		{
		  if (isRv64())
		    topBits <<= 1;
		  if (topBits == 0x20)
		    printShiftImm(*this, out, "srai", rd, rs1, shamt);
		  else
		    out << "illegal";
		}
	    }
	    break;
	  case 6:
	    printRegRegImm12(*this, out, "ori", rd, rs1, imm);
	    break;
	  case 7:
	    printRegRegImm12(*this, out, "andi", rd, rs1, imm);
	    break;
	  default:
	    out << "illegal";
	    break;
	  }
      }
      break;

    case 5:  // 00101   U-form
      {
	UFormInst uform(inst);
	out << "auipc    " << intRegName(uform.bits.rd)
	       << ", 0x" << std::hex << ((uform.immed() >> 12) & 0xfffff);
      }
      break;

    case 6:  // 00110  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	unsigned funct3 = iform.fields.funct3;
	if (funct3 == 0)
	  {
	    if (isRv64())
	      printRegRegImm12(*this, out, "addi", rd, rs1, imm);
	    else
	      out << "illegal";
	  }
	else if (funct3 == 1)
	  {
	    if (iform.top7() != 0)
	      out << "illegal";
	    else if (isRv64())
	      printShiftImm(*this, out, "slliw", rd, rs1, iform.fields2.shamt);
	    else
	      out << "illegal";
	  }
	else if (funct3 == 5)
	  {
	    if (iform.top7() == 0)
	      printShiftImm(*this, out, "srliw", rd, rs1, iform.fields2.shamt);
	    else if (iform.top7() == 0x20)
	      printShiftImm(*this, out, "sraiw", rd, rs1, iform.fields2.shamt);
	    else
	      out << "illegal";
	  }
	else
	  out << "illegal";
      }
      break;

    case 8:  // 01000  S-form
      {
	SFormInst sform(inst);
	unsigned rs1 = sform.bits.rs1, rs2 = sform.bits.rs2;
	int32_t imm = sform.immed();
	switch (sform.bits.funct3)
	  {
	  case 0:  printLdSt(*this, out, "sb", rs2, rs1, imm); break;
	  case 1:  printLdSt(*this, out, "sh", rs2, rs1, imm); break;
	  case 2:  printLdSt(*this, out, "sw", rs2, rs1, imm); break;
	  default:
	    if (not isRv64())
	      out << "illegal";
	    else
	      printLdSt(*this, out, "sd", rs2, rs1, imm);
	    break;
	  }
      }
      break;

    case 9:   // 01001  S-form
      {
	SFormInst sf(inst);
	unsigned rs1 = sf.bits.rs1, rs2 = sf.bits.rs2, f3 = sf.bits.funct3;
	int32_t imm = sf.immed();
	if (f3 == 2)
	  {
	    if (isRvf())
	      printFpLdSt(*this, out, "fsw", rs2, rs1, imm);
	    else
	      out << "illegal";
	  }
	else if (f3 == 3)
	  {
	    if (isRvd())
	      printFpLdSt(*this, out, "fsd", rs2, rs1, imm);
	    else
	      out << "illegal";
	  }
	else
	  out << "illegal";
      }
      break;

    case 11:  // 01011  R-form  atomics
      {
	if (not isRva())
	  {
	    out << "illegal";
	    break;
	  }

	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
	uint32_t rd = rf.bits.rd, rs1 = rf.bits.rs1, rs2 = rf.bits.rs2;
	bool rl = rf.rl(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if (top5 == 0)
	      printAmo(*this, out, "amoadd.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 1)
	      printAmo(*this, out, "amoswap.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 2)
	      printLr(*this, out, "lr.w", aq, rl, rd, rs1);
	    else if (top5 == 3)
	      printSc(*this, out, "sc.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 4)
	      printAmo(*this, out, "amoxor.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 8)
	      printAmo(*this, out, "amoor.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x0c)
	      printAmo(*this, out, "amoand.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x10)
	      printAmo(*this, out, "amomin.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x14)
	      printAmo(*this, out, "amomax.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x18)
	      printAmo(*this, out, "amominu.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x1c)
	      printAmo(*this, out, "amomaxu.w", aq, rl, rd, rs1, rs2);
	    else
	      out << "illegal";
	  }
	else if (f3 == 3)
	  {
	    if (top5 == 0)
	      printAmo(*this, out, "amoadd.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 1)
	      printAmo(*this, out, "amoswap.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 2)
	      printLr(*this, out, "lr.d", aq, rl, rd, rs1);
	    else if (top5 == 3)
	      printSc(*this, out, "sc.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 4)
	      printAmo(*this, out, "amoxor.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 8)
	      printAmo(*this, out, "amoor.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x0c)
	      printAmo(*this, out, "amoand.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x10)
	      printAmo(*this, out, "amomin.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x14)
	      printAmo(*this, out, "amomax.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x18)
	      printAmo(*this, out, "amominu.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x1c)
	      printAmo(*this, out, "amomaxu.d", aq, rl, rd, rs1, rs2);
	    else
	      out << "illegal";
	  }
	else out << "illegal";
      }
      break;

    case 12:  // 01100  R-form
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	if (f7 == 0)
	  {
	    if      (f3 == 0) printRdRs1Rs2(*this, out, "add",  rd, rs1, rs2);
	    else if (f3 == 1) printRdRs1Rs2(*this, out, "sll",  rd, rs1, rs2);
	    else if (f3 == 2) printRdRs1Rs2(*this, out, "slt",  rd, rs1, rs2);
	    else if (f3 == 3) printRdRs1Rs2(*this, out, "sltu", rd, rs1, rs2);
	    else if (f3 == 4) printRdRs1Rs2(*this, out, "xor",  rd, rs1, rs2);
	    else if (f3 == 5) printRdRs1Rs2(*this, out, "srl",  rd, rs1, rs2);
	    else if (f3 == 6) printRdRs1Rs2(*this, out, "or",   rd, rs1, rs2);
	    else if (f3 == 7) printRdRs1Rs2(*this, out, "and",  rd, rs1, rs2);
	  }
	else if (f7 == 1)
	  {
	    if      (not isRvm())  out << "illegal";
	    else if (f3 == 0) printRdRs1Rs2(*this, out, "mul",    rd, rs1, rs2);
	    else if (f3 == 1) printRdRs1Rs2(*this, out, "mulh",   rd, rs1, rs2);
	    else if (f3 == 2) printRdRs1Rs2(*this, out, "mulhsu", rd, rs1, rs2);
	    else if (f3 == 3) printRdRs1Rs2(*this, out, "mulhu",  rd, rs1, rs2);
	    else if (f3 == 4) printRdRs1Rs2(*this, out, "div",    rd, rs1, rs2);
	    else if (f3 == 5) printRdRs1Rs2(*this, out, "divu",   rd, rs1, rs2);
	    else if (f3 == 6) printRdRs1Rs2(*this, out, "rem",    rd, rs1, rs2);
	    else if (f3 == 7) printRdRs1Rs2(*this, out, "remu",   rd, rs1, rs2);
	  }
	else if (f7 == 4)
	  {
	    if      (f3 == 0) printRdRs1Rs2(*this, out, "pack", rd, rs1, rs2);
	    else              out << "illegal";
	  }
	else if (f7 == 5)
	  {
	    if      (f3 == 2) printRdRs1Rs2(*this, out, "min",  rd, rs1, rs2);
	    else if (f3 == 3) printRdRs1Rs2(*this, out, "minu", rd, rs1, rs2);
	    else if (f3 == 6) printRdRs1Rs2(*this, out, "max",  rd, rs1, rs2);
	    else if (f3 == 7) printRdRs1Rs2(*this, out, "maxu", rd, rs1, rs2);
	    else              out << "illegal";
	  }
	else if (f7 == 0x10)
	  {
	    if      (f3 == 1) printRdRs1Rs2(*this, out, "slo", rd, rs1, rs2);
	    else if (f3 == 5) printRdRs1Rs2(*this, out, "sro", rd, rs1, rs2);
	    else              out << "illegal";
	  }
	else if (f7 == 0x20)
	  {
	    if      (f3 == 0)  printRdRs1Rs2(*this, out, "sub", rd, rs1, rs2);
	    else if (f3 == 5)  printRdRs1Rs2(*this, out, "sra", rd, rs1, rs2);
	    else if (f3 == 7)  printRdRs1Rs2(*this, out, "andc", rd, rs1, rs2);
	    else               out << "illegal";
	  }
	else if (f7 == 0x30)
	  {
	    if      (f3 == 1)  printRdRs1Rs2(*this, out, "rol", rd, rs1, rs2);
	    else if (f3 == 5)  printRdRs1Rs2(*this, out, "ror", rd, rs1, rs2);
	    else               out << "illegal";
	  }
	else
	  out << "illegal";
      }
      break;

    case 13:  // 01101  U-form
      {
	UFormInst uform(inst);
	uint32_t imm = uform.immed();
	out << "lui      x" << uform.bits.rd << ", ";
	if (imm < 0) { out << "-"; imm = -imm; }
	out << "0x" << std::hex << ((imm >> 12) & 0xfffff);
      }
      break;

    case 14:  // 01110  R-Form
      {
	const RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	if (f7 == 0)
	  {
	    if      (f3 == 0)  printRdRs1Rs2(*this, out, "addw", rd, rs1, rs2);
	    else if (f3 == 1)  printRdRs1Rs2(*this, out, "sllw", rd, rs1, rs2);
	    else if (f3 == 5)  printRdRs1Rs2(*this, out, "srlw", rd, rs1, rs2);
	    else               out << "illegal";
	  }
	else if (f7 == 1)
	  {
	    if      (f3 == 0)  printRdRs1Rs2(*this, out, "mulw",  rd, rs1, rs2);
	    else if (f3 == 4)  printRdRs1Rs2(*this, out, "divw",  rd, rs1, rs2);
	    else if (f3 == 5)  printRdRs1Rs2(*this, out, "divuw", rd, rs1, rs2);
	    else if (f3 == 6)  printRdRs1Rs2(*this, out, "remw",  rd, rs1, rs2);
	    else if (f3 == 7)  printRdRs1Rs2(*this, out, "remuw", rd, rs1, rs2);
	    else               out << "illegal";
	  }
	else if (f7 == 0x20)
	  {
	    if      (f3 == 0)  printRdRs1Rs2(*this, out, "subw", rd, rs1, rs2);
	    else if (f3 == 5)  printRdRs1Rs2(*this, out, "sraw", rd, rs1, rs2);
	    else               out << "illegal";
	  }
	else
	  out << "illegal";
      }
      break;

    case 16:  // 10000   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(*this, out, "fmadd_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(*this, out, "fmadd_d", rd, rs1, rs2, rs3, mode);
	else
	  out << "illegal";
      }
      break;

    case 17:  // 10001   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(*this, out, "fmsub_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(*this, out, "fmsub_d", rd, rs1, rs2, rs3, mode);
	else
	  out << "illegal";
      }
      break;

    case 18:  // 10010   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(*this, out, "fnmsub_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(*this, out, "fnmsub_d", rd, rs1, rs2, rs3, mode);
	else
	  out << "illegal";
      }
      break;

    case 19:  // 10011   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(*this, out, "fnmadd_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(*this, out, "fnmadd_d", rd, rs1, rs2, rs3, mode);
	else
	  out << "illegal";
      }
      break;

    case 20:  // 10100   rform
      disassembleFp(inst, out);
      break;

    case 24:  // 11000   B-form
      {
	BFormInst bform(inst);
	unsigned rs1 = bform.bits.rs1, rs2 = bform.bits.rs2;
	int32_t imm = bform.immed();
	switch (bform.bits.funct3)
	  {
	  case 0:  printBranch(*this, out, "beq",  rs1, rs2, imm); break;
	  case 1:  printBranch(*this, out, "bne",  rs1, rs2, imm); break;
	  case 4:  printBranch(*this, out, "blt",  rs1, rs2, imm); break;
	  case 5:  printBranch(*this, out, "bge",  rs1, rs2, imm); break;
	  case 6:  printBranch(*this, out, "bltu", rs1, rs2, imm); break;
	  case 7:  printBranch(*this, out, "bgeu", rs1, rs2, imm); break;
	  default: out << "illegal";                               break;
	  }
      }
      break;

    case 25:  // 11001  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	if (iform.fields.funct3 == 0)
	  printLdSt(*this, out, "jalr", rd, rs1, iform.immed());
	else
	  out << "illegal";
      }
      break;

    case 27:  // 11011  J-form
      {
	JFormInst jform(inst);
	int32_t imm = jform.immed();
	out << "jal      " << intRegName(jform.bits.rd) << ", . ";
	char sign = '+';
	if (imm < 0)
	  {
	    sign = '-';
	    imm = -imm;
	  }
	out << sign << " 0x" << std::hex << (imm & 0xfffff);
      }
      break;

    case 28:  // 11100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	unsigned csrNum = iform.uimmed();
	std::string rdn = intRegName(rd);
	std::string rs1n = intRegName(rs1);
	std::string csrn;  // csr name
	auto csr = csRegs_.findCsr(CsrNumber(csrNum));
	if (csr)
	  csrn = csr->getName();
	else
	  csrn = "illegal";
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      uint32_t func7 = iform.top7();
	      if (func7 == 0)
		{
		  if (rs1 != 0 or rd != 0)  out << "illegal";
		  else if (csrNum == 0)     out << "ecall";
		  else if (csrNum == 1)     out << "ebreak";
		  else if (csrNum == 2)     out << "uret";
		  else                      out << "illegal";
		}
	      else if (func7 == 9)
		{
		  uint32_t rs2 = iform.rs2();
		  if (rd != 0) out << "illegal";
		  else         out << "SFENCE.VMA " << rs1 << ", " << rs2;
		}
	      else if (csrNum == 0x102) out << "sret";
	      else if (csrNum == 0x302) out << "mret";
	      else if (csrNum == 0x105) out << "wfi";
	      else                      out << "illegal";
	    }
	    break;
	  case 1:
	    out << "csrrw    " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 2:
	    out << "csrrs    " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 3:
	    out << "csrrc    " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 5:
	    out << "csrrwi   " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 6:
	    out << "csrrsi   " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 7:
	    out << "csrrci   " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  default: 
	    out << "illegal";
	    break;
	  }
      }
      break;

    default:
      out << "illegal";
      break;
    }
}


template <typename URV>
void
Core<URV>::disassembleInst16(uint16_t inst, std::ostream& stream)
{
  if (not isRvc())
    {
      stream << "illegal";
      return;
    }

  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  uint16_t(inst >> 13);    // Bits 15 14 and 13

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
		  printRegImm(*this, stream, "c.addi4spn", 8+ciwf.bits.rdp,
			      immed >> 2);
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  stream << "illegal";
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    unsigned rd = 8+clf.bits.rdp, rs1 = (8+clf.bits.rs1p);
	    printLdSt(*this, stream, "c.lw", rd, rs1, clf.lwImmed());
	  }
	  break;
	case 3:  // c.flw, c.ld
	  {
	    ClFormInst clf(inst);
	    unsigned rd = 8+clf.bits.rdp, rs1 = (8+clf.bits.rs1p);
	    if (isRv64())
	      printLdSt(*this, stream, "c.ld", rd, rs1, clf.ldImmed()); 
	    else
	      {
		if (isRvf())
		  printFpLdSt(*this, stream, "c.flw", rd, rs1, clf.lwImmed());
		else
		  stream << "illegal";
	      }
	  }
	  break;
	case 4:  // reserved
	  stream << "illegal";
	  break;
	case 5:  // c.fsd, c.sq
	  if (isRvd())
	    {
	      ClFormInst clf(inst);
	      unsigned rd = 8+clf.bits.rdp, rs1 = (8+clf.bits.rs1p);
	      printFpLdSt(*this, stream, "c.fsd", rd, rs1, clf.ldImmed());
	    }
	  else
	    stream << "illegal";
	  break;
	case 6:  // c.sw
	  {
	    CsFormInst cs(inst);
	    unsigned rd = (8+cs.bits.rs2p), rs1 = 8+cs.bits.rs1p;
	    printLdSt(*this, stream, "c.sw", rd, rs1, cs.swImmed());
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  {
	    CsFormInst cs(inst);
	    unsigned rd = (8+cs.bits.rs2p), rs1 = (8+cs.bits.rs1p);
	    if (isRv64())
	      printLdSt(*this, stream, "c.sd", rd, rs1, cs.sdImmed());
	    else
	      {
		if (isRvf())
		  printFpLdSt(*this, stream, "c.fsw", rd, rs1, cs.swImmed());
		else
		  stream << "illegal"; // c.fsw
	      }
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
	    if (cif.bits.rd == 0)
	      stream << "c.nop";
	    else
	      printRegImm(*this, stream, "c.addi", cif.bits.rd, cif.addiImmed());
	  }
	  break;
	  
	case 1:  // c.jal, in rv64 and rv128 this is c.addiw
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      if (cif.bits.rd == 0)
		stream << "illegal";
	      else
		printRegImm(*this, stream, "c.addiw", cif.bits.rd, cif.addiImmed());
	    }
	  else
	    {
	      CjFormInst cjf(inst);
	      int32_t imm = cjf.immed();
	      stream << "c.jal    . ";
	      char sign = '+';
	      if (imm < 0) { sign = '-'; imm = -imm; }
	      stream << sign << " 0x" << std::hex << imm;
	    }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    printRegImm(*this, stream, "c.li", cif.bits.rd, cif.addiImmed());
	  }
	  break;

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      stream << "illegal";
	    else if (cif.bits.rd == RegSp)
	      {
		stream << "c.addi16sp ";
		if (immed16 < 0) { stream << "-"; immed16 = -immed16; }
		stream << "0x" << std::hex << (immed16 >> 4);
	      }
	    else
	      printRegImm(*this, stream, "c.lui", cif.bits.rd, cif.luiImmed()>>12);
	  }
	  break;

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
	case 4:
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    unsigned rd = 8 + caf.bits.rdp;
	    std::string rdName = intRegName(rd);
	    int immed = caf.andiImmed();
	    switch (caf.bits.funct2)
	      {
	      case 0:
		if (caf.bits.ic5 != 0 and not isRv64())
		  stream << "illegal";
		else
		  printRegImm(*this, stream, "c.srli", rd, caf.shiftImmed());
		break;
	      case 1:
		if (caf.bits.ic5 != 0 and not isRv64())
		  stream << "illegal";
		else
		  printRegImm(*this, stream, "c.srai", rd, caf.shiftImmed());
		break;
	      case 2:
		printRegImm(*this, stream, "c.andi", rd, immed);
		break;
	      case 3:
		{
		  unsigned rs2 = 8+(immed & 0x7); // Lowest 3 bits of immed
		  std::string rs2n = intRegName(rs2);
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0:
			  stream << "c.sub    " << rdName << ", " << rs2n; break;
			case 1:
			  stream << "c.xor    " << rdName << ", " << rs2n; break;
			case 2:
			  stream << "c.or     " << rdName << ", " << rs2n; break;
			case 3:
			  stream << "c.and    " << rdName << ", " << rs2n; break;
			}
		    }
		  else
		    {
		      if (not isRv64())
			stream << "illegal";
		      else
			switch ((immed >> 3) & 3)
			  {
			  case 0: stream << "c.subw " << rdName << ", " << rs2n;
			    break;
			  case 1: stream << "c.addw " << rdName << ", " << rs2n;
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
	    int32_t imm = cjf.immed();
	    stream << "c.j      . ";
	    char sign = '+';
	    if (imm < 0) { sign = '-'; imm = -imm; }
	    stream << sign << " 0x" << std::hex << imm;
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    printBranch(*this, stream, "c.beqz", 8+cbf.bits.rs1p, cbf.immed());
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    printBranch(*this, stream, "c.bnez", 8+cbf.bits.rs1p, cbf.immed());
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
	    unsigned immed = unsigned(cif.slliImmed()), rd = cif.bits.rd;
	    if (cif.bits.ic5 != 0 and not isRv64())
	      stream << "illegal";
	    else
	      stream << "c.slli   " << intRegName(rd) << ", " << immed;
	  }
	  break;

	case 1:   // c.fldsp, c.lqsp
	  stream << "illegal";
	  break;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.bits.rd;
	    // rd == 0 is legal per Andrew Watterman
	    stream << "c.lwsp   " << intRegName(rd) << ", 0x"
		   << std::hex << cif.lwspImmed();
	  }
	break;

	case 3:  // c.flwsp c.ldsp
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      unsigned rd = cif.bits.rd;
	      stream << "c.ldsp   " << intRegName(rd) << ", 0x"
		     << std::hex << cif.ldspImmed();
	    }
	  else
	    {
	      stream << "illegal";  // c.flwsp
	    }
	  break;

	case 4:  // c.jr c.mv c.ebreak c.jalr c.add
	  {
	    CiFormInst cif(inst);
	    unsigned immed = cif.addiImmed();
	    unsigned rd = cif.bits.rd;
	    unsigned rs2 = immed & 0x1f;
	    std::string rdName = intRegName(rd);
	    std::string rs2Name = intRegName(rs2);
	    if ((immed & 0x20) == 0)
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.jr     " << rdName;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.mv     " << rdName << ", " << rs2Name;
		  }
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "c.ebreak";
		    else
		      stream << "c.jalr   " << rdName;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.add    " << rdName << ", " << rs2Name;
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
	    stream << "c.swsp   " << intRegName(csw.bits.rs2)
		   << ", 0x" << std::hex << csw.swImmed();
	  }
	  break;

	case 7:  // c.fswsp c.sdsp
	  {
	    if (isRv64())  // c.sdsp
	      {
		CswspFormInst csw(inst);
		stream << "c.sdsp   " << intRegName(csw.bits.rs2)
		       << ", 0x" << std::hex << csw.sdImmed();
	      }
	  else
	    {
	      stream << "illegal";    // c.fwsp
	    }
	  }
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


template class WdRiscv::Core<uint32_t>;
template class WdRiscv::Core<uint64_t>;
