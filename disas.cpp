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
#include "Hart.hpp"
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
printRdRs1Rs2(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	      const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1(), rs2 = di.op2();

  // Print instruction in a 9 character field.
  stream << std::left << std::setw(9) << inst;

  stream << hart.intRegName(rd) << ", " << hart.intRegName(rs1) << ", "
	 << hart.intRegName(rs2);
}


/// Helper to disassemble method. Print on the given stream given
/// 2-operand floating point instruction.
template <typename URV>
static
void
printFp2(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	 const DecodedInst& di)
{
  stream << std::left << std::setw(9) << inst << hart.fpRegName(di.op0())
	 << ", " << hart.fpRegName(di.op1());
}


/// Helper to disassemble method. Print on the given stream given
/// 3-operand floating point instruction.
template <typename URV>
static
void
printFp3(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	 const DecodedInst& di)
{
  stream << std::left << std::setw(9) << inst << hart.fpRegName(di.op0())
	 << ", " << hart.fpRegName(di.op1())
	 << ", " << hart.fpRegName(di.op3());
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst rd, rs1
template <typename URV>
static
void
printRdRs1(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	   const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1();

  // Print instruction in a 9 character field.
  stream << std::left << std::setw(9) << inst;

  stream << hart.intRegName(rd) << ", " << hart.intRegName(rs1);
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: csrinst rd, csrn, rs1
template <typename URV>
static
void
printCsr(Hart<URV>& hart, std::ostream& stream, const char* inst,
	 const DecodedInst& di)
{
  unsigned rd = di.op0(), csrn = di.op2();

  stream << std::left << std::setw(9) << inst;

  stream << hart.intRegName(rd) << ", ";

  auto csr = hart.findCsr(CsrNumber(csrn));
  if (csr)
    stream << csr->getName();
  else
    stream << "illegal";

  if (di.ithOperandType(1) == OperandType::Imm)
    stream << ", 0x" << std::hex << di.op1() << std::dec;
  else
    stream << ", " << hart.intRegName(di.op1());
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form:  inst reg1, imm(reg2)
template <typename URV>
static
void
printLdSt(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	  const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1();
  int32_t imm = di.op2AsInt();

  stream << std::left << std::setw(8) << inst << ' ';

  const char* sign = imm < 0? "-" : "";
  if (imm < 0)
    imm = -imm;

  // Keep least sig 12 bits.
  imm = imm & 0xfff;

  stream << hart.intRegName(rd) << ", " << sign << "0x"
	 << std::hex << imm << "(" << hart.intRegName(rs1) << ")" << std::dec;
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg1, imm(reg2) where inst
/// is a floating point ld/st instruction.
template <typename URV>
static
void
printFpLdSt(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	    const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1();
  int32_t imm = di.op2AsInt();

  stream << std::left << std::setw(8) << inst << ' ';

  const char* sign = imm < 0? "-" : "";
  if (imm < 0)
    imm = -imm;

  // Keep least sig 12 bits.
  imm = imm & 0xfff;

  stream << hart.fpRegName(rd) << ", " << sign << "0x" << std::hex << imm
	 << std::dec << "(" << hart.intRegName(rs1) << ")";
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, reg, imm where inst is
/// a shift instruction.
template <typename URV>
static
void
printShiftImm(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	      const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1();
  int imm = di.op2AsInt();

  stream << std::left << std::setw(8) << inst << ' ';
  stream << hart.intRegName(rd) << ", " << hart.intRegName(rs1)
	 << ", 0x" << std::hex << imm << std::dec;
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, reg, imm where imm is
/// a 12 bit constant.
template <typename URV>
static
void
printRegRegImm12(const Hart<URV>& hart, std::ostream& stream, const char* inst,
		 const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1();
  int imm = di.op2AsInt();

  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.intRegName(rd) << ", " << hart.intRegName(rs1) << ", ";

  if (imm < 0)
    stream << "-0x" << std::hex << ((-imm) & 0xfff) << std::dec;
  else
    stream << "0x" << std::hex << (imm & 0xfff) << std::dec;
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, reg, uimm where uimm is
/// a 12 bit constant.
template <typename URV>
static
void
printRegRegUimm12(const Hart<URV>& hart, std::ostream& stream, const char* inst,
		  const DecodedInst& di)
{
  uint32_t rd = di.op0(), rs1 = di.op1(), imm = di.op2();

  stream << std::left << std::setw(8) << inst << ' ';
  stream << hart.intRegName(rd) << ", " << hart.intRegName(rs1) << ", ";
  stream << "0x" << std::hex << (imm & 0xfff) << std::dec;
}


/// Helper to disassemble method. Print on the given stream given
/// instruction which is of the form: inst reg, imm where inst is a
/// compressed instruction.
template <typename URV>
static
void
printRegImm(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	    unsigned rs1, int32_t imm)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.intRegName(rs1) << ", ";

  if (imm < 0)
    stream << "-0x" << std::hex << (-imm) << std::dec;
  else
    stream << "0x" << std::hex << imm << std::dec;
}


/// Helper to disassemble method. Print on the given stream given 3
/// operand branch instruction which is of the form: inst reg, reg,
/// imm where imm is a 12 bit constant.
template <typename URV>
static
void
printBranch3(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	     const DecodedInst& di)
{
  unsigned rs1 = di.op0(), rs2 = di.op1();

  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.intRegName(rs1) << ", " << hart.intRegName(rs2) << ", . ";

  char sign = '+';
  int32_t imm = di.op2AsInt();
  if (imm < 0)
    {
      sign = '-';
      imm = -imm;
    }
      
  stream << sign << " 0x" << std::hex << imm << std::dec;
}


/// Helper to disassemble method. Print on the given stream given
/// 2 operand  branch instruction which is of the form: inst reg, imm.
template <typename URV>
static
void
printBranch2(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	     const DecodedInst& di)
{
  unsigned rs1 = di.op0();
  int32_t imm = di.op2AsInt();

  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.intRegName(rs1) << ", . ";

  char sign = '+';
  if (imm < 0)
    {
      sign = '-';
      imm = -imm;
    }
  stream << sign << " 0x" << std::hex << imm << std::dec;
}


/// Helper to disassemble method.
template <typename URV>
static
void
printAmo(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	 const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1(), rs2 = di.op2();
  bool aq = di.isAtomicAcquire(), rl = di.isAtomicRelease();

  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ' << hart.intRegName(rd) << ", " << hart.intRegName(rs2) << ", ("
	 << hart.intRegName(rs1) << ")";
}


/// Helper to disassemble method.
template <typename URV>
static
void
printLr(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1();
  bool aq = di.isAtomicAcquire(), rl = di.isAtomicRelease();

  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ' << hart.intRegName(rd) << ", (" << hart.intRegName(rs1) << ")";
}


/// Helper to disassemble method.
template <typename URV>
static
void
printSc(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	const DecodedInst& di)
{
  unsigned rd = di.op0(), rs1 = di.op1(), rs2 = di.op2();
  bool aq = di.isAtomicAcquire(), rl = di.isAtomicRelease();

  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ' << hart.intRegName(rd) << ", " << hart.intRegName(rs2)
	 << ", (" << hart.intRegName(rs1) << ")";
}


/// Helper to disassemble methods. Print a floating point instruction
/// with 4 operands and the rounding mode.
template <typename URV>
static
void
printFp4Rm(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	   const DecodedInst& di)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.fpRegName(di.op0()) << ", " << hart.fpRegName(di.op1())
	 << ", " << hart.fpRegName(di.op2()) << hart.fpRegName(di.op3())
	 << ", " << roundingModeString(di.roundingMode());
}


/// Helper to disassemble methods. Print a floating point instruction
/// with 3 operands and the rounding mode.
template <typename URV>
static
void
printFp3Rm(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	   const DecodedInst& di)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.fpRegName(di.op0()) << ", " << hart.fpRegName(di.op1())
	 << ", " << hart.fpRegName(di.op2())
	 << ", " << roundingModeString(di.roundingMode());
}


/// Helper to disassemble methods. Print a floating point instruction
/// with 2 operands and the rounding mode.
template <typename URV>
static
void
printFp2Rm(const Hart<URV>& hart, std::ostream& stream, const char* inst,
	   const DecodedInst& di)
{
  // Print instruction in a 8 character field.
  stream << std::left << std::setw(8) << inst << ' ';

  stream << hart.fpRegName(di.op0()) << ", " << hart.fpRegName(di.op1())
	 <<  ", " << roundingModeString(di.roundingMode());
}


template <typename URV>
void
Hart<URV>::disassembleInst(uint32_t inst, std::ostream& stream)
{
  DecodedInst di;
  decode(pc_, inst, di);
  disassembleInst(di, stream);
}


template <typename URV>
void
Hart<URV>::disassembleInst(uint32_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst(inst, oss);
  str = oss.str();
}


template <typename URV>
void
Hart<URV>::disassembleInst(const DecodedInst& di, std::ostream& out)
{
  InstId id = di.instEntry()->instId();
  switch(id)
    {
    case InstId::illegal:
      out << "illegal";
      break;

    case InstId::lui:
      printRegImm(*this, out, "lui", di.op0(), di.op1AsInt() >> 12);
      break;

    case InstId::auipc:
      out << "auipc    " << intRegName(di.op0())
	  << ", 0x" << std::hex << ((di.op1() >> 12) & 0xfffff) << std::dec;
      break;

    case InstId::jal:
      {
	if (di.op0() == 0)
	  out << "j        ";
	else
	  out << "jal      " << intRegName(di.op0()) << ", ";
	char sign = '+';
	int32_t imm = di.op1AsInt();
	if (imm < 0) { sign = '-'; imm = -imm; }
	out << ". " << sign << " 0x" << std::hex << (imm & 0xfffff) << std::dec;
      }
      break;

    case InstId::jalr:
      printLdSt(*this, out, "jalr", di);
      break;

    case InstId::beq:
      printBranch3(*this, out, "beq",  di);
      break;

    case InstId::bne:
      printBranch3(*this, out, "bne",  di);
      break;

    case InstId::blt:
      printBranch3(*this, out, "blt",  di);
      break;

    case InstId::bge:
      printBranch3(*this, out, "bge",  di);
      break;

    case InstId::bltu:
      printBranch3(*this, out, "bltu",  di);
      break;

    case InstId::bgeu:
      printBranch3(*this, out, "bgeu",  di);
      break;

    case InstId::lb:
      printLdSt(*this, out, "lb",  di);
      break;

    case InstId::lh:
      printLdSt(*this, out, "lh",  di);
      break;

    case InstId::lw:
      printLdSt(*this, out, "lw",  di);
      break;

    case InstId::lbu:
      printLdSt(*this, out, "lbu",  di);
      break;

    case InstId::lhu:
      printLdSt(*this, out, "lhu",  di);
      break;

    case InstId::sb:
      printLdSt(*this, out, "sb", di);
      break;

    case InstId::sh:
      printLdSt(*this, out, "sh", di);
      break;

    case InstId::sw:
      printLdSt(*this, out, "sw", di);
      break;

    case InstId::addi:
      printRegRegImm12(*this, out, "addi", di);
      break;

    case InstId::slti:
      printRegRegImm12(*this, out, "slti", di);
      break;

    case InstId::sltiu:
      printRegRegUimm12(*this, out, "sltiu", di);
      break;

    case InstId::xori:
      printRegRegImm12(*this, out, "xori", di);
      break;

    case InstId::ori:
      printRegRegImm12(*this, out, "ori", di);
      break;

    case InstId::andi:
      printRegRegImm12(*this, out, "andi", di);
      break;

    case InstId::slli:
      printShiftImm(*this, out, "slli", di);
      break;

    case InstId::srli:
      printShiftImm(*this, out, "srli", di);
      break;

    case InstId::srai:
      printShiftImm(*this, out, "srai", di);
      break;

    case InstId::add:
      printRdRs1Rs2(*this, out, "add", di);
      break;

    case InstId::sub:
      printRdRs1Rs2(*this, out, "sub", di);
      break;

    case InstId::sll:
      printRdRs1Rs2(*this, out, "sll", di);
      break;

    case InstId::slt:
      printRdRs1Rs2(*this, out, "slt", di);
      break;

    case InstId::sltu:
      printRdRs1Rs2(*this, out, "sltu", di);
      break;

    case InstId::xor_:
      printRdRs1Rs2(*this, out, "xor", di);
      break;

    case InstId::srl:
      printRdRs1Rs2(*this, out, "srl", di);
      break;

    case InstId::sra:
      printRdRs1Rs2(*this, out, "sra", di);
      break;

    case InstId::or_:
      printRdRs1Rs2(*this, out, "or", di);
      break;

    case InstId::and_:
      printRdRs1Rs2(*this, out, "and", di);
      break;

    case InstId::fence:
      out << "fence";
      break;

    case InstId::fencei:
      out << "fencei";
      break;

    case InstId::ecall:
      out << "ecall";
      break;

    case InstId::ebreak:
      out << "ebreak";
      break;

    case InstId::csrrw:
      printCsr(*this, out, "csrrw", di);
      break;

    case InstId::csrrs:
      printCsr(*this, out, "csrrs", di);
      break;

    case InstId::csrrc:
      printCsr(*this, out, "csrrc", di);
      break;

    case InstId::csrrwi:
      printCsr(*this, out, "csrrwi", di);
      break;

    case InstId::csrrsi:
      printCsr(*this, out, "csrrsi", di);
      break;

    case InstId::csrrci:
      printCsr(*this, out, "csrrci", di);
      break;

    case InstId::lwu:
      printLdSt(*this, out, "lwu", di);
      break;

    case InstId::ld:
      printLdSt(*this, out, "ld", di);
      break;

    case InstId::sd:
      printLdSt(*this, out, "sd", di);
      break;

    case InstId::addiw:
      printRegRegImm12(*this, out, "addiw", di);
      break;

    case InstId::slliw:
      printShiftImm(*this, out, "slliw", di);
      break;

    case InstId::srliw:
      printShiftImm(*this, out, "srliw", di);
      break;

    case InstId::sraiw:
      printShiftImm(*this, out, "sraiw", di);
      break;

    case InstId::addw:
      printRdRs1Rs2(*this, out, "addw", di);
      break;

    case InstId::subw:
      printRdRs1Rs2(*this, out, "subw", di);
      break;

    case InstId::sllw:
      printRdRs1Rs2(*this, out, "sllw", di);
      break;

    case InstId::srlw:
      printRdRs1Rs2(*this, out, "srlw", di);
      break;

    case InstId::sraw:
      printRdRs1Rs2(*this, out, "sraw", di);
      break;

    case InstId::mul:
      printRdRs1Rs2(*this, out, "mul", di);
      break;

    case InstId::mulh:
      printRdRs1Rs2(*this, out, "mulh", di);
      break;

    case InstId::mulhsu:
      printRdRs1Rs2(*this, out, "mulhsu", di);
      break;

    case InstId::mulhu:
      printRdRs1Rs2(*this, out, "mulhu", di);
      break;

    case InstId::div:
      printRdRs1Rs2(*this, out, "div", di);
      break;

    case InstId::divu:
      printRdRs1Rs2(*this, out, "divu", di);
      break;

    case InstId::rem:
      printRdRs1Rs2(*this, out, "rem", di);
      break;

    case InstId::remu:
      printRdRs1Rs2(*this, out, "remu", di);
      break;

    case InstId::mulw:
      printRdRs1Rs2(*this, out, "mulw", di);
      break;

    case InstId::divw:
      printRdRs1Rs2(*this, out, "divw", di);
      break;

    case InstId::divuw:
      printRdRs1Rs2(*this, out, "divuw", di);
      break;

    case InstId::remw:
      printRdRs1Rs2(*this, out, "remw", di);
      break;

    case InstId::remuw:
      printRdRs1Rs2(*this, out, "remuw", di);
      break;

    case InstId::lr_w:
      printLr(*this, out, "lr.w", di);
      break;

    case InstId::sc_w:
      printSc(*this, out, "sc.w", di);
      break;

    case InstId::amoswap_w:
      printAmo(*this, out, "amoswap.w", di);
      break;

    case InstId::amoadd_w:
      printAmo(*this, out, "amoadd.w", di);
      break;

    case InstId::amoxor_w:
      printAmo(*this, out, "amoxor.w", di);
      break;

    case InstId::amoand_w:
      printAmo(*this, out, "amoand.w", di);
      break;

    case InstId::amoor_w:
      printAmo(*this, out, "amoor.w", di);
      break;

    case InstId::amomin_w:
      printAmo(*this, out, "amomin.w", di);
      break;

    case InstId::amomax_w:
      printAmo(*this, out, "amomax.w", di);
      break;

    case InstId::amominu_w:
      printAmo(*this, out, "amominu.w", di);
      break;

    case InstId::amomaxu_w:
      printAmo(*this, out, "amomaxu.w", di);
      break;

    case InstId::lr_d:
      printLr(*this, out, "lr.d", di);
      break;

    case InstId::sc_d:
      printSc(*this, out, "sc.d", di);
      break;

    case InstId::amoswap_d:
      printAmo(*this, out, "amoswap.d", di);
      break;

    case InstId::amoadd_d:
      printAmo(*this, out, "amoadd.d", di);
      break;

    case InstId::amoxor_d:
      printAmo(*this, out, "amoxor.d", di);
      break;

    case InstId::amoand_d:
      printAmo(*this, out, "amoand.d", di);
      break;

    case InstId::amoor_d:
      printAmo(*this, out, "amoor.d", di);
      break;

    case InstId::amomin_d:
      printAmo(*this, out, "amomin.d", di);
      break;

    case InstId::amomax_d:
      printAmo(*this, out, "amomax.d", di);
      break;

    case InstId::amominu_d:
      printAmo(*this, out, "amominu.d", di);
      break;

    case InstId::amomaxu_d:
      printAmo(*this, out, "amomaxu.d", di);
      break;

    case InstId::flw:
      printFpLdSt(*this, out, "flw", di);
      break;

    case InstId::fsw:
      printFpLdSt(*this, out, "fsw", di);
      break;

    case InstId::fmadd_s:
      printFp4Rm(*this, out, "fmadd.s", di);
      break;

    case InstId::fmsub_s:
      printFp4Rm(*this, out, "fmsub.s", di);
      break;

    case InstId::fnmsub_s:
      printFp4Rm(*this, out, "fnmsub.s", di);
      break;

    case InstId::fnmadd_s:
      printFp4Rm(*this, out, "fnmadd.s", di);
      break;

    case InstId::fadd_s:
      printFp2Rm(*this, out, "fadd.s", di);
      break;

    case InstId::fsub_s:
      printFp2Rm(*this, out, "fsub.s", di);
      break;

    case InstId::fmul_s:
      printFp2Rm(*this, out, "fmul.s", di);
      break;

    case InstId::fdiv_s:
      printFp2Rm(*this, out, "fdiv.s", di);
      break;

    case InstId::fsqrt_s:
      printFp2(*this, out, "fsqrt.s", di);
      break;

    case InstId::fsgnj_s:
      printFp2(*this, out, "fsgnj.s", di);
      break;

    case InstId::fsgnjn_s:
      printFp2(*this, out, "fsgnjn.s", di);
      break;

    case InstId::fsgnjx_s:
      printFp2(*this, out, "fsgnjx.s", di);
      break;

    case InstId::fmin_s:
      printFp3(*this, out, "fmin.s", di);
      break;

    case InstId::fmax_s:
      printFp3(*this, out, "fmax.s", di);
      break;

    case InstId::fcvt_w_s:
      out << "fcvt.w.s "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_wu_s:
      out << "fcvt.wu.s " << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fmv_x_w:
      out << "fmv.x.w  " << intRegName(di.op0()) << ", " << fpRegName(di.op1());
      break;

    case InstId::feq_s:
      out << "feq.s    " << intRegName(di.op0()) << ", " << fpRegName(di.op1())
	  << ", " << fpRegName(di.op2());
      break;

    case InstId::flt_s:
      out << "flt.s    " << intRegName(di.op0()) << ", " << fpRegName(di.op1())
	  << ", " << fpRegName(di.op2());
      break;

    case InstId::fle_s:
      out << "fle.s    " << intRegName(di.op0()) << ", " << fpRegName(di.op1())
	  << ", " << fpRegName(di.op2());
      break;

    case InstId::fclass_s:
      out << "fclass.s " << intRegName(di.op0()) << ", " << fpRegName(di.op1());
      break;

    case InstId::fcvt_s_w:
      out << "fcvt.s.w " << ", " << fpRegName(di.op0()) << ", "
	  << intRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_s_wu:
      out << "fcvt.s.wu " << fpRegName(di.op0()) << ", "
	  << intRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fmv_w_x:
      out << "fmv.w.x  "<< ", " << fpRegName(di.op0())
	  << ", " << intRegName(di.op1());
      break;

    case InstId::fcvt_l_s:
      out << "fcvt.l.s "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_lu_s:
      out << "fcvt.lu.s "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_s_l:
      out << "fcvt.s.l " << ", " << fpRegName(di.op0()) << ", "
	  << intRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_s_lu:
      out << "fcvt.s.lu " << fpRegName(di.op0()) << ", "
	  << intRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fld:
      printFpLdSt(*this, out, "fld", di);
      break;

    case InstId::fsd:
      printFpLdSt(*this, out, "fsd", di);
      break;

    case InstId::fmadd_d:
      printFp4Rm(*this, out, "fmadd.d", di);
      break;

    case InstId::fmsub_d:
      printFp4Rm(*this, out, "fmsub.d", di);
      break;

    case InstId::fnmsub_d:
      printFp4Rm(*this, out, "fnmsub.d", di);
      break;

    case InstId::fnmadd_d:
      printFp4Rm(*this, out, "fnmadd.d", di);
      break;

    case InstId::fadd_d:
      printFp3Rm(*this, out, "fadd.d", di);
      break;

    case InstId::fsub_d:
      printFp3Rm(*this, out, "fsub.d", di);
      break;

    case InstId::fmul_d:
      printFp3Rm(*this, out, "fmul.d", di);
      break;

    case InstId::fdiv_d:
      printFp3Rm(*this, out, "fdiv.d", di);
      break;

    case InstId::fsqrt_d:
      printFp2Rm(*this, out, "fsqrt.d", di);
      break;

    case InstId::fsgnj_d:
      printFp2(*this, out, "fsgnj.d", di);
      break;

    case InstId::fsgnjn_d:
      printFp2(*this, out, "fsgnjn.d", di);
      break;

    case InstId::fsgnjx_d:
      printFp2(*this, out, "fsgnjx.d", di);
      break;

    case InstId::fmin_d:
      printFp3(*this, out, "fmin.d", di);
      break;

    case InstId::fmax_d:
      printFp3(*this, out, "fmax.d", di);
      break;

    case InstId::fcvt_s_d:
      out << "fcvt.s.d "  << fpRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_d_s:
      out << "fcvt.d.s "  << fpRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::feq_d:
      out << "feq.d    " << intRegName(di.op0()) << ", " << fpRegName(di.op1())
	  << ", " << fpRegName(di.op2());
      break;

    case InstId::flt_d:
      out << "flt.d    " << intRegName(di.op0()) << ", " << fpRegName(di.op1())
	  << ", " << fpRegName(di.op2());
      break;

    case InstId::fle_d:
      out << "fle.d    " << intRegName(di.op0()) << ", " << fpRegName(di.op1())
	  << ", " << fpRegName(di.op2());
      break;

    case InstId::fclass_d:
      out << "fclass.d " << intRegName(di.op0()) << ", " << fpRegName(di.op1());
      break;

    case InstId::fcvt_w_d:
      out << "fcvt.w.d "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_wu_d:
      out << "fcvt.wu.d "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_d_w:
      out << "fcvt.d.w " << fpRegName(di.op0()) << ", " << intRegName(di.op1())
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_d_wu:
      out << "fcvt.d.wu " << fpRegName(di.op0()) << ", " << intRegName(di.op1())
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_l_d:
      out << "fcvt.l.d "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fcvt_lu_d:
      out << "fcvt.lu.s "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1()) << ", "
	  << roundingModeString(di.roundingMode());
      break;

    case InstId::fmv_x_d:
      out << "fmv.x.d "  << intRegName(di.op0()) << ", "
	  << fpRegName(di.op1());
      break;

    case InstId::fcvt_d_l:
      out << "fcvt.d.l " << fpRegName(di.op0()) << ", "
	  << intRegName(di.op1());
      break;

    case InstId::fcvt_d_lu:
      out << "fcvt.d.lu " << fpRegName(di.op0()) << ", "
	  << intRegName(di.op1());
      break;

    case InstId::fmv_d_x:

    case InstId::mret:
      out << "mret";
      break;

    case InstId::uret:
      out << "uret";
      break;

    case InstId::sret:
      out << "sret";
      break;

    case InstId::wfi:
      out << "wfi";
      break;

    case InstId::c_addi4spn:
      printRegImm(*this, out, "c.addi4spn", di.op0(), di.op2AsInt() >> 2);
      break;

    case InstId::c_fld:
      printFpLdSt(*this, out, "fld", di);
      break;

    case InstId::c_lq:
      out << "illegal";
      break;

    case InstId::c_lw:
      printLdSt(*this, out, "c.lw", di);
      break;

    case InstId::c_flw:
      printFpLdSt(*this, out, "c.flw", di);
      break;

    case InstId::c_ld:
      printLdSt(*this, out, "c.ld", di);
      break;

    case InstId::c_fsd:
      printFpLdSt(*this, out, "c.fsd", di);
      break;

    case InstId::c_sq:
      out << "illegal";
      break;

    case InstId::c_sw:
      printLdSt(*this, out, "c.sw", di);
      break;

    case InstId::c_fsw:
      printFpLdSt(*this, out, "c.fsw", di);
      break;

    case InstId::c_sd:
      printFpLdSt(*this, out, "c.sd", di);
      break;

    case InstId::c_addi:
      if (di.op0() == 0)
	out << "c.nop";
      else
	printRegImm(*this, out, "c.addi", di.op0(), di.op2AsInt());
      break;

    case InstId::c_jal:
      {
	out << "c.jal    . ";
	int32_t imm = di.op1AsInt();
	char sign = '+';
	if (imm < 0) { sign = '-'; imm = -imm; }
	out << sign << " 0x" << std::hex << imm << std::dec;
      }
      break;

    case InstId::c_li:
      printRegImm(*this, out, "c.li", di.op0(), di.op2AsInt());
      break;

    case InstId::c_addi16sp:
      {
	int32_t imm = di.op2AsInt();
	out << "c.addi16sp ";
	if (imm < 0) { out << "-"; imm = -imm; }
	out << "0x" << std::hex << (imm >> 4) << std::dec;
      }
      break;

    case InstId::c_lui:
      printRegImm(*this, out, "c.lui", di.op0(), di.op1() >> 12);
      break;

    case InstId::c_srli:
      printRegImm(*this, out, "c.srli", di.op0(), di.op2AsInt());
      break;

    case InstId::c_srli64:
      printRegImm(*this, out, "c.srli64", di.op0(), di.op2AsInt());
      break;

    case InstId::c_srai:
      printRegImm(*this, out, "c.srai", di.op0(), di.op2AsInt());
      break;

    case InstId::c_srai64:
      printRegImm(*this, out, "c.srai64", di.op0(), di.op2AsInt());
      break;

    case InstId::c_andi:
      printRegImm(*this, out, "c.andi", di.op0(), di.op2AsInt());
      break;

    case InstId::c_sub:
      out << "c.sub    " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_xor:
      out << "c.xor    " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_or:
      out << "c.or     " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_and:
      out << "c.and    " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_subw:
      out << "c.subw   " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_addw:
      out << "c.addw   " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_j:
      {
	out << "c.j      . ";
	int32_t imm = di.op1AsInt();
	char sign = '+';
	if (imm < 0) { sign = '-'; imm = -imm; }
	out << sign << " 0x" << std::hex << imm << std::dec;
      }
      break;

    case InstId::c_beqz:
      printBranch2(*this, out, "c.beqz", di);
      break;

    case InstId::c_bnez:
      printBranch2(*this, out, "c.bnez", di);
      break;

    case InstId::c_slli:
      out << "c.slli   " << intRegName(di.op0()) << ", " << di.op2();
      break;

    case InstId::c_slli64:
      out << "c.slli64 " << intRegName(di.op0()) << ", " << di.op2();
      break;

    case InstId::c_fldsp:
      out << "c.ldsp   " << intRegName(di.op0()) << ", 0x" << std::hex
	  << di.op2AsInt() << std::dec;
      break;

    case InstId::c_lwsp:
      out << "c.lwsp   " << intRegName(di.op0()) << ", 0x" << std::hex
	  << di.op2AsInt() << std::dec;
      break;

    case InstId::c_flwsp:
      out << "c.flwsp   " << fpRegName(di.op0()) << ", 0x" << std::hex
	  << di.op2AsInt() << std::dec;
      break;

    case InstId::c_ldsp:
      out << "c.ldsp   " << intRegName(di.op0()) << ", 0x" << std::hex
	  << di.op2AsInt() << std::dec;
      break;

    case InstId::c_jr:
      out << "c.jr     " << intRegName(di.op1());
      break;

    case InstId::c_mv:
      out << "c.mv     " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_ebreak:
      out << "c.ebreak";
      break;

    case InstId::c_jalr:
      out << "c.jalr   " << intRegName(di.op1());
      break;

    case InstId::c_add:
      out << "c.add    " << intRegName(di.op0()) << ", " << intRegName(di.op2());
      break;

    case InstId::c_fsdsp:
      out << "c.sdsp   " << fpRegName(di.op0())
	  << ", 0x" << std::hex << di.op2AsInt() << std::dec;
      break;

    case InstId::c_swsp:
      out << "c.swsp   " << intRegName(di.op0()) << ", 0x"
	  << std::hex << di.op2AsInt() << std::dec;
      break;

    case InstId::c_fswsp:
      out << "c.swsp   " << fpRegName(di.op0()) << ", 0x"
	  << std::hex << di.op2AsInt() << std::dec;
      break;

    case InstId::c_addiw:
      printRegImm(*this, out, "c.addiw", di.op0(), di.op2AsInt());
      break;

    case InstId::c_sdsp:
      out << "c.sdsp   " << intRegName(di.op0()) << ", 0x"
	  << std::hex << di.op2AsInt() << std::dec;
      break;

    case InstId::clz:
      printRdRs1(*this, out, "clz", di);
      break;

    case InstId::ctz:
      printRdRs1(*this, out, "ctz", di);
      break;

    case InstId::pcnt:
      printRdRs1(*this, out, "pcnt", di);
      break;

    case InstId::andn:
      printRdRs1Rs2(*this, out, "andn", di);
      break;

    case InstId::orn:
      printRdRs1Rs2(*this, out, "orn", di);
      break;

    case InstId::xnor:
      printRdRs1Rs2(*this, out, "xnor", di);
      break;

    case InstId::slo:
      printRdRs1Rs2(*this, out, "slo", di);
      break;

    case InstId::sro:
      printRdRs1Rs2(*this, out, "sro", di);
      break;

    case InstId::sloi:
      printShiftImm(*this, out, "sloi", di);
      break;

    case InstId::sroi:
      printShiftImm(*this, out, "sroi", di);
      break;

    case InstId::min:
      printRdRs1Rs2(*this, out, "min", di);
      break;

    case InstId::max:
      printRdRs1Rs2(*this, out, "max", di);
      break;

    case InstId::minu:
      printRdRs1Rs2(*this, out, "minu", di);
      break;

    case InstId::maxu:
      printRdRs1Rs2(*this, out, "maxu", di);
      break;

    case InstId::rol:
      printRdRs1Rs2(*this, out, "rol", di);
      break;

    case InstId::ror:
      printRdRs1Rs2(*this, out, "ror", di);
      break;

    case InstId::rori:
      printShiftImm(*this, out, "rori", di);
      break;

    case InstId::rev8:
      printRdRs1(*this, out, "rev8", di);
      break;

    case InstId::rev:
      printRdRs1(*this, out, "rev", di);
      break;

    case InstId::pack:
      printRdRs1Rs2(*this, out, "pack", di);
      break;

    case InstId::sbset:
      printRdRs1Rs2(*this, out, "sbset", di);
      break;

    case InstId::sbclr:
      printRdRs1Rs2(*this, out, "sbclr", di);
      break;

    case InstId::sbinv:
      printRdRs1Rs2(*this, out, "sbinv", di);
      break;

    case InstId::sbext:
      printRdRs1Rs2(*this, out, "sbext", di);
      break;

     case InstId::sbseti:
      printShiftImm(*this, out, "sbseti", di);
      break;

     case InstId::sbclri:
      printShiftImm(*this, out, "sbclri", di);
      break;

     case InstId::sbinvi:
      printShiftImm(*this, out, "sbinvi", di);
      break;

     case InstId::sbexti:
      printShiftImm(*this, out, "sbexti", di);
      break;

    default:
      out << "illegal";
    }
}


template <typename URV>
void
Hart<URV>::disassembleInst(const DecodedInst& di, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst(di, oss);
  str = oss.str();
}


template class WdRiscv::Hart<uint32_t>;
template class WdRiscv::Hart<uint64_t>;
