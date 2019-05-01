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

#include <cfenv>
#include <cmath>
#include "Core.hpp"
#include "instforms.hpp"

using namespace WdRiscv;


template <typename URV>
const InstInfo&
Core<URV>::decodeFp(uint32_t inst, uint32_t& op0, uint32_t& op1, int32_t& op2)
{
  if (not isRvf())
    return instTable_.getInstInfo(InstId::illegal);  

  RFormInst rform(inst);

  op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;

  unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
  instRoundingMode_ = RoundingMode(f3);
  if (f7 & 1)
    {
      if (not isRvd())
	return instTable_.getInstInfo(InstId::illegal);  

      if (f7 == 1)              return instTable_.getInstInfo(InstId::fadd_d);
      if (f7 == 5)              return instTable_.getInstInfo(InstId::fsub_d);
      if (f7 == 9)              return instTable_.getInstInfo(InstId::fmul_d);
      if (f7 == 0xd)            return instTable_.getInstInfo(InstId::fdiv_d);
      if (f7 == 0x11)
	{
	  if (f3 == 0)          return instTable_.getInstInfo(InstId::fsgnj_d);
	  if (f3 == 1)          return instTable_.getInstInfo(InstId::fsgnjn_d);
	  if (f3 == 2)          return instTable_.getInstInfo(InstId::fsgnjx_d);
	}
      if (f7 == 0x15)
	{
	  if (f3 == 0)          return instTable_.getInstInfo(InstId::fmin_d);
	  if (f3 == 1)          return instTable_.getInstInfo(InstId::fmax_d);
	}
      if (f7==0x21 and op2==0)  return instTable_.getInstInfo(InstId::fcvt_d_s);
      if (f7 == 0x2d)           return instTable_.getInstInfo(InstId::fsqrt_d);
      if (f7 == 0x51)
	{
	  if (f3 == 0)          return instTable_.getInstInfo(InstId::fle_d);
	  if (f3 == 1)          return instTable_.getInstInfo(InstId::flt_d);
	  if (f3 == 2)          return instTable_.getInstInfo(InstId::feq_d);
	}
      if (f7 == 0x61)
	{
	  if (op2 == 0)         return instTable_.getInstInfo(InstId::fcvt_w_d);
	  if (op2 == 1)         return instTable_.getInstInfo(InstId::fcvt_wu_d);
	  if (op2 == 2)         return instTable_.getInstInfo(InstId::fcvt_l_d);
	  if (op2 == 3)         return instTable_.getInstInfo(InstId::fcvt_lu_d);
	}
      if (f7 == 0x69)
	{
	  if (op2 == 0)         return instTable_.getInstInfo(InstId::fcvt_d_w);
	  if (op2 == 1)         return instTable_.getInstInfo(InstId::fcvt_d_wu);
	  if (op2 == 2)         return instTable_.getInstInfo(InstId::fcvt_d_l);
	  if (op2 == 3)         return instTable_.getInstInfo(InstId::fcvt_d_lu);
	}
      if (f7 == 0x71)
	{
	  if (op2==0 and f3==0) return instTable_.getInstInfo(InstId::fmv_x_d);
	  if (op2==0 and f3==1) return instTable_.getInstInfo(InstId::fclass_d);
	}
      if (f7 == 0x74)
	if (op2==0 and f3==0)   return instTable_.getInstInfo(InstId::fmv_w_x);
      if (f7 == 0x79)
	if (op2==0 and f3==0)   return instTable_.getInstInfo(InstId::fmv_d_x);

      return instTable_.getInstInfo(InstId::illegal);
    }

  if (f7 == 0)      return instTable_.getInstInfo(InstId::fadd_s);
  if (f7 == 4)      return instTable_.getInstInfo(InstId::fsub_s);
  if (f7 == 8)      return instTable_.getInstInfo(InstId::fmul_s);
  if (f7 == 0xc)    return instTable_.getInstInfo(InstId::fdiv_s);
  if (f7 == 0x2c)   return instTable_.getInstInfo(InstId::fsqrt_s);
  if (f7 == 0x10)
    {
      if (f3 == 0)  return instTable_.getInstInfo(InstId::fsgnj_s);
      if (f3 == 1)  return instTable_.getInstInfo(InstId::fsgnjn_s);
      if (f3 == 2)  return instTable_.getInstInfo(InstId::fsgnjx_s);
    }
  if (f7 == 0x14)
    {
      if (f3 == 0)  return instTable_.getInstInfo(InstId::fmin_s);
      if (f3 == 1)  return instTable_.getInstInfo(InstId::fmax_s);
    }
  if (f7 == 0x50)
    {
      if (f3 == 0)  return instTable_.getInstInfo(InstId::fle_s);
      if (f3 == 1)  return instTable_.getInstInfo(InstId::flt_s);
      if (f3 == 2)  return instTable_.getInstInfo(InstId::feq_s);
      return instTable_.getInstInfo(InstId::illegal);
    }
  if (f7 == 0x60)
    {
      if (op2 == 0) return instTable_.getInstInfo(InstId::fcvt_w_s);
      if (op2 == 1) return instTable_.getInstInfo(InstId::fcvt_wu_s);
      if (op2 == 2) return instTable_.getInstInfo(InstId::fcvt_l_s);
      if (op2 == 3) return instTable_.getInstInfo(InstId::fcvt_lu_s);
      return instTable_.getInstInfo(InstId::illegal);
    }
  if (f7 == 0x68)
    {
      if (op2 == 0) return instTable_.getInstInfo(InstId::fcvt_s_w);
      if (op2 == 1) return instTable_.getInstInfo(InstId::fcvt_s_wu);
      if (op2 == 2) return instTable_.getInstInfo(InstId::fcvt_s_l);
      if (op2 == 3) return instTable_.getInstInfo(InstId::fcvt_s_lu);
      return instTable_.getInstInfo(InstId::illegal);
    }
  if (f7 == 0x70)
    {
      if (op2 == 0)
	{
	  if (f3 == 0) return instTable_.getInstInfo(InstId::fmv_x_w);
	  if (f3 == 1) return instTable_.getInstInfo(InstId::fclass_s);
	}
    }
  if (f7 == 0x74)
    {
      if (op2 == 0)
	if (f3 == 0) return instTable_.getInstInfo(InstId::fmv_w_x);
    }
  return instTable_.getInstInfo(InstId::illegal);
}


template <typename URV>
const InstInfo&
Core<URV>::decode16(uint16_t inst, uint32_t& op0, uint32_t& op1, int32_t& op2)
{
  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  uint16_t(inst >> 13);    // Bits 15 14 and 13

  op0 = 0; op1 = 0; op2 = 0;

  if (quadrant == 0)
    {
      if (funct3 == 0)    // illegal, c.addi4spn
	{
	  if (inst == 0)
	    return instTable_.getInstInfo(InstId::illegal);
	  CiwFormInst ciwf(inst);
	  unsigned immed = ciwf.immed();
	  if (immed == 0)
	    return instTable_.getInstInfo(InstId::illegal);
	  op0 = 8 + ciwf.bits.rdp; op1 = RegSp; op2 = immed;
	  return instTable_.getInstInfo(InstId::c_addi4spn);
	}

      if (funct3 == 1) // c.fld c.lq
	{
	  if (not isRvd())
	    return instTable_.getInstInfo(InstId::illegal);
	  ClFormInst clf(inst);
	  op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p; op2 = clf.ldImmed();
	  return instTable_.getInstInfo(InstId::c_fld);
	}

      if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p; op2 = clf.lwImmed();
	  return instTable_.getInstInfo(InstId::c_lw);
	}

      if (funct3 == 3) // c.flw, c.ld
	{
	  ClFormInst clf(inst);
	  if (isRv64())
	    {
	      op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p; op2 = clf.ldImmed();
	      return instTable_.getInstInfo(InstId::c_ld);
	    }

	  // c.flw
	  if (isRvf())
	    {
	      op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p;
	      op2 = clf.lwImmed();
	      return instTable_.getInstInfo(InstId::c_flw);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 6)  // c.sw
	{
	  CsFormInst cs(inst);
	  op0 = 8+cs.bits.rs1p; op1 = 8+cs.bits.rs2p; op2 = cs.swImmed();
	  return instTable_.getInstInfo(InstId::c_sw);
	}

      if (funct3 == 7) // c.fsw, c.sd
	{
	  CsFormInst cs(inst);  // Double check this
	  if (not isRv64())
	    {
	      if (isRvf())
		{
		  op0=8+cs.bits.rs1p; op1=8+cs.bits.rs2p; op2 = cs.swImmed();
		  return instTable_.getInstInfo(InstId::c_fsw);
		}
	      return instTable_.getInstInfo(InstId::illegal);
	    }
	  op0=8+cs.bits.rs1p; op1=8+cs.bits.rs2p; op2 = cs.sdImmed();
	  return instTable_.getInstInfo(InstId::c_sd);
	}

      // funct3 is 1 (c.fld c.lq), or 4 (reserved), or 5 (c.fsd c.sq)
      return instTable_.getInstInfo(InstId::illegal);
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  op0 = cif.bits.rd; op1 = cif.bits.rd; op2 = cif.addiImmed();
	  return instTable_.getInstInfo(InstId::c_addi);
	}
	  
      if (funct3 == 1)  // c.jal,  in rv64 and rv128 this is c.addiw
	{
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      if (cif.bits.rd == 0)
		return instTable_.getInstInfo(InstId::illegal);
	      else
		return instTable_.getInstInfo(InstId::c_addiw);
	    }
	  else
	    {
	      CjFormInst cjf(inst);
	      op0 = RegRa; op1 = cjf.immed(); op2 = 0;
	      return instTable_.getInstInfo(InstId::c_jal);
	    }
	}

      if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  op0 = cif.bits.rd; op1 = RegX0; op2 = cif.addiImmed();
	  return instTable_.getInstInfo(InstId::c_li);
	}

      if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    return instTable_.getInstInfo(InstId::illegal);
	  if (cif.bits.rd == RegSp)  // c.addi16sp
	    {
	      op0 = cif.bits.rd; op1 = cif.bits.rd; op2 = immed16;
	      return instTable_.getInstInfo(InstId::c_addi16sp);
	    }
	  op0 = cif.bits.rd; op1 = cif.luiImmed(); op2 = 0;
	  return instTable_.getInstInfo(InstId::c_lui);
	}

      // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
      // c.subw c.addw
      if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.bits.rdp;
	  unsigned f2 = caf.bits.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		return instTable_.getInstInfo(InstId::illegal);
	      op0 = rd; op1 = rd; op2 = caf.shiftImmed();
	      return instTable_.getInstInfo(InstId::c_srli);
	    }
	  if (f2 == 1)  // srai64, srai
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		return instTable_.getInstInfo(InstId::illegal);
	      op0 = rd; op1 = rd; op2 = caf.shiftImmed();
	      return instTable_.getInstInfo(InstId::c_srai);
	    }
	  if (f2 == 2)  // c.andi
	    {
	      op0 = rd; op1 = rd; op2 = immed;
	      return instTable_.getInstInfo(InstId::c_andi);
	    }

	  // f2 == 3: c.sub c.xor c.or c.subw c.addw
	  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
	  unsigned rs2 = 8 + rs2p;
	  unsigned imm34 = (immed >> 3) & 3; // Bits 3 and 4 of immed
	  op0 = rd; op1 = rd; op2 = rs2;
	  if ((immed & 0x20) == 0)  // Bit 5 of immed
	    {
	      if (imm34 == 0) return instTable_.getInstInfo(InstId::c_sub);
	      if (imm34 == 1) return instTable_.getInstInfo(InstId::c_xor);
	      if (imm34 == 2) return instTable_.getInstInfo(InstId::c_or);
	      return instTable_.getInstInfo(InstId::c_and);
	    }
	  // Bit 5 of immed is 1
	  if (not isRv64())
	    return instTable_.getInstInfo(InstId::illegal);
	  if (imm34 == 0) return instTable_.getInstInfo(InstId::c_subw);
	  if (imm34 == 1) return instTable_.getInstInfo(InstId::c_addw);
	  if (imm34 == 2) return instTable_.getInstInfo(InstId::illegal);
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  op0 = RegX0; op1 = cjf.immed(); op2 = 0;
	  return instTable_.getInstInfo(InstId::c_j);
	}
	  
      if (funct3 == 6) // c.beqz
	{
	  CbFormInst cbf(inst);
	  op0=8+cbf.bits.rs1p; op1=RegX0; op2=cbf.immed();
	  return instTable_.getInstInfo(InstId::c_beqz);
	}
      
      // funct3 == 7: c.bnez
      CbFormInst cbf(inst);
      op0 = 8+cbf.bits.rs1p; op1=RegX0; op2=cbf.immed();
      return instTable_.getInstInfo(InstId::c_bnez);
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.bits.ic5 != 0 and not isRv64())
	    return instTable_.getInstInfo(InstId::illegal);
	  op0 = cif.bits.rd; op1 = cif.bits.rd; op2 = immed;
	  return instTable_.getInstInfo(InstId::c_slli);
	}

      if (funct3 == 1)  // c.fldsp c.lqsp
	{
	  if (isRvd())
	    {
	      CiFormInst cif(inst);
	      op0 = cif.bits.rd; op1 = RegSp, op2 = cif.ldspImmed();
	      return instTable_.getInstInfo(InstId::c_fldsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 2) // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  // rd == 0 is legal per Andrew Watterman
	  op0 = rd; op1 = RegSp; op2 = cif.lwspImmed();
	  return instTable_.getInstInfo(InstId::c_lwsp);
	}

      else  if (funct3 == 3)  // c.ldsp  c.flwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  if (isRv64())
	    {
	      op0 = rd; op1 = RegSp; op2 = cif.ldspImmed();
	      return instTable_.getInstInfo(InstId::c_ldsp);
	    }
	  if (isRvf())
	    {
	      op0 = rd; op1 = RegSp; op2 = cif.lwspImmed();
	      return instTable_.getInstInfo(InstId::c_lwsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 4) // c.jr c.mv c.ebreak c.jalr c.add
	{
	  CiFormInst cif(inst);
	  unsigned immed = cif.addiImmed();
	  unsigned rd = cif.bits.rd;
	  unsigned rs2 = immed & 0x1f;
	  if ((immed & 0x20) == 0)  // c.jr or c.mv
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return instTable_.getInstInfo(InstId::illegal);
		  op0 = RegX0; op1 = rd; op2 = 0;
		  return instTable_.getInstInfo(InstId::c_jr);
		}
	      op0 = rd; op1 = RegX0; op2 = rs2;
	      return instTable_.getInstInfo(InstId::c_mv);
	    }
	  else  // c.ebreak, c.jalr or c.add 
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return instTable_.getInstInfo(InstId::c_ebreak);
		  op0 = RegRa; op1 = rd; op2 = 0;
		  return instTable_.getInstInfo(InstId::c_jalr);
		}
	      op0 = rd; op1 = rd; op2 = rs2;
	      return instTable_.getInstInfo(InstId::c_add);
	    }
	}

      if (funct3 == 5)  // c.fsdsp c.sqsp
	{
	  if (isRvd())
	    {
	      CswspFormInst csw(inst);
	      op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.sdImmed();
	      return instTable_.getInstInfo(InstId::c_fsdsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 6) // c.swsp
	{
	  CswspFormInst csw(inst);
	  op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.swImmed();
	  return instTable_.getInstInfo(InstId::c_swsp);
	}

      if (funct3 == 7)  // c.sdsp  c.fswsp
	{
	  if (isRv64())  // c.sdsp
	    {
	      CswspFormInst csw(inst);
	      op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.sdImmed();
	      return instTable_.getInstInfo(InstId::c_sdsp);
	    }
	  if (isRvf())   // c.fswsp
	    {
	      CswspFormInst csw(inst);
	      op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.swImmed();
	      return instTable_.getInstInfo(InstId::c_fswsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      return instTable_.getInstInfo(InstId::illegal);
    }

  return instTable_.getInstInfo(InstId::illegal); // quadrant 3
}


template <typename URV>
const InstInfo&
Core<URV>::decode(uint32_t inst, uint32_t& op0, uint32_t& op1, int32_t& op2,
		  int32_t& op3)
{
#pragma GCC diagnostic ignored "-Wpedantic"

  static void *opcodeLabels[] = { &&l0, &&l1, &&l2, &&l3, &&l4, &&l5,
				  &&l6, &&l7, &&l8, &&l9, &&l10, &&l11,
				  &&l12, &&l13, &&l14, &&l15, &&l16, &&l17,
				  &&l18, &&l19, &&l20, &&l21, &&l22, &&l23,
				  &&l24, &&l25, &&l26, &&l27, &&l28, &&l29,
				  &&l30, &&l31 };

  if (isCompressedInst(inst))
    {
      // return decode16(inst, op0, op1, op2);
      if (not isRvc())
	inst = 0; // All zeros: illegal 16-bit instruction.
      return decode16(uint16_t(inst), op0, op1, op2);
    }

  op0 = 0; op1 = 0; op2 = 0; op3 = 0;

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
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	uint32_t f3 = iform.fields.funct3;
	if      (f3 == 2)  return instTable_.getInstInfo(InstId::flw);
	else if (f3 == 3)  return instTable_.getInstInfo(InstId::fld);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l2:
    l7:
      return instTable_.getInstInfo(InstId::illegal);

    l9:
      {
	SFormInst sform(inst);
	op0 = sform.bits.rs1;
	op1 = sform.bits.rs2;
	op2 = sform.immed();
	unsigned funct3 = sform.bits.funct3;
	if      (funct3 == 2)  return instTable_.getInstInfo(InstId::fsw);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::fsd);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l10:
    l15:
      return instTable_.getInstInfo(InstId::illegal);

    l16:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    op3 = instRs3_;
	    return instTable_.getInstInfo(InstId::fmadd_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l17:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    op3 = instRs3_;
	    return instTable_.getInstInfo(InstId::fmsub_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l18:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    op3 = instRs3_;
	    return instTable_.getInstInfo(InstId::fnmsub_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l19:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    op3 = instRs3_;
	    return instTable_.getInstInfo(InstId::fnmadd_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l20:
      return decodeFp(inst, op0, op1, op2);

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
	    unsigned topBits = 0, shamt = 0;
	    iform.getShiftFields(isRv64(), topBits, shamt);
	    if (topBits == 0)
	      {
		op2 = shamt;
		return instTable_.getInstInfo(InstId::slli);
	      }
	    else if ((topBits >> 1) == 4)
	      {
		op2 = shamt;
		return instTable_.getInstInfo(InstId::sloi);
	      }
	    else if (op2 == 0x600)
	      return instTable_.getInstInfo(InstId::clz);
	    else if (op2 == 0x601)
	      return instTable_.getInstInfo(InstId::ctz);
	    else if (op2 == 0x601)
	      return instTable_.getInstInfo(InstId::pcnt);
	  }
	else if (funct3 == 2)  return instTable_.getInstInfo(InstId::slti);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::sltiu);
	else if (funct3 == 4)  return instTable_.getInstInfo(InstId::xori);
	else if (funct3 == 5)
	  {
	    unsigned topBits = 0, shamt = 0;
	    iform.getShiftFields(isRv64(), topBits, shamt);
	    op2 = shamt;
	    if (topBits == 0)
	      return instTable_.getInstInfo(InstId::srli);
	    if ((topBits >> 1) == 4)
	      return instTable_.getInstInfo(InstId::sroi);
	    if ((topBits >> 1) == 0xc)
	      return instTable_.getInstInfo(InstId::rori);
	    if (isRv64())
	      topBits <<= 1;
	    if (topBits == 0x20)
	      return instTable_.getInstInfo(InstId::srai);
	  }
	else if (funct3 == 6)  return instTable_.getInstInfo(InstId::ori);
	else if (funct3 == 7)  return instTable_.getInstInfo(InstId::andi);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l5:  // 00101   U-form
      {
	UFormInst uform(inst);
	op0 = uform.bits.rd;
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
	op0 = sform.bits.rs1;
	op1 = sform.bits.rs2;
	op2 = sform.immed();
	uint32_t funct3 = sform.bits.funct3;

	if (funct3 == 0) return instTable_.getInstInfo(InstId::sb);
	if (funct3 == 1) return instTable_.getInstInfo(InstId::sh);
	if (funct3 == 2) return instTable_.getInstInfo(InstId::sw);
	if (funct3 == 3 and isRv64()) return instTable_.getInstInfo(InstId::sd);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l11:  // 01011  R-form atomics
      if (not isRva())
	return instTable_.getInstInfo(InstId::illegal);

      if (false)  // Not implemented
      {
	if (not isRva())
	  return instTable_.getInstInfo(InstId::illegal);
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
	op0 = rf.bits.rd; op1 = rf.bits.rs1; op2 = rf.bits.rs2;

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
	    if (not isRv64()) return instTable_.getInstInfo(InstId::illegal);
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
	op0 = rform.bits.rd;
	op1 = rform.bits.rs1;
	op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
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
	    if      (not isRvm()) return instTable_.getInstInfo(InstId::illegal);
	    else if (funct3 == 0) return instTable_.getInstInfo(InstId::mul);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::mulh);
	    else if (funct3 == 2) return instTable_.getInstInfo(InstId::mulhsu);
	    else if (funct3 == 3) return instTable_.getInstInfo(InstId::mulhu);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::div);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::divu);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::rem);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::remu);
	  }
	else if (funct7 == 4)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::pack);
	  }
	else if (funct7 == 5)
	  {

	    if      (funct3 == 2) return instTable_.getInstInfo(InstId::min);
	    else if (funct3 == 3) return instTable_.getInstInfo(InstId::minu);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::max);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::maxu);
	  }
	else if (funct7 == 0x10)
	  {
	    if      (funct3 == 1) return instTable_.getInstInfo(InstId::slo);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::sro);
	  }
	else if (funct7 == 0x20)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::sub);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::sra);
	  }
	else if (funct7 == 0x30)
	  {
	    if      (funct3 == 1) return instTable_.getInstInfo(InstId::rol);
	    if      (funct3 == 5) return instTable_.getInstInfo(InstId::ror);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l13:  // 01101  U-form
      {
	UFormInst uform(inst);
	op0 = uform.bits.rd;
	op1 = uform.immed();
	return instTable_.getInstInfo(InstId::lui);
      }

    l14: // 01110  R-Form
      {
	const RFormInst rform(inst);
	op0 = rform.bits.rd;
	op1 = rform.bits.rs1;
	op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
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
	op0 = bform.bits.rs1;
	op1 = bform.bits.rs2;
	op2 = bform.immed();
	uint32_t funct3 = bform.bits.funct3;
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
	op0 = jform.bits.rd;
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


template class WdRiscv::Core<uint32_t>;
template class WdRiscv::Core<uint64_t>;
