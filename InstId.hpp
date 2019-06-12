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

namespace WdRiscv
{
  /// Associate a unique integer identifier with each instruction.
  enum class InstId
    {
     illegal,

     // Base.
     lui,
     auipc,
     jal,
     jalr,
     beq,
     bne,
     blt,
     bge,
     bltu,
     bgeu,
     lb,
     lh,
     lw,
     lbu,
     lhu,
     sb,
     sh,
     sw,
     addi,
     slti,
     sltiu,
     xori,
     ori,
     andi,
     slli,
     srli,
     srai,
     add,
     sub,
     sll,
     slt,
     sltu,
     xor_,
     srl,
     sra,
     or_,
     and_,
     fence,
     fencei,
     ecall,
     ebreak,

     // CSR
     csrrw,
     csrrs,
     csrrc,
     csrrwi,
     csrrsi,
     csrrci,

     // rv64i
     lwu,
     ld,
     sd,
     addiw,
     slliw,
     srliw,
     sraiw,
     addw,
     subw,
     sllw,
     srlw,
     sraw,
 
     // Mul/div
     mul,
     mulh,
     mulhsu,
     mulhu,
     div,
     divu,
     rem,
     remu,

     // 64-bit mul/div
     mulw,
     divw,
     divuw,
     remw,
     remuw,

     // Atomic
     lr_w,
     sc_w,
     amoswap_w,
     amoadd_w,
     amoxor_w,
     amoand_w,
     amoor_w,
     amomin_w,
     amomax_w,
     amominu_w,
     amomaxu_w,

     // 64-bit atomic
     lr_d,
     sc_d,
     amoswap_d,
     amoadd_d,
     amoxor_d,
     amoand_d,
     amoor_d,
     amomin_d,
     amomax_d,
     amominu_d,
     amomaxu_d,

     // rv32f
     flw,
     fsw,
     fmadd_s,
     fmsub_s,
     fnmsub_s,
     fnmadd_s,
     fadd_s,
     fsub_s,
     fmul_s,
     fdiv_s,
     fsqrt_s,
     fsgnj_s,
     fsgnjn_s,
     fsgnjx_s,
     fmin_s,
     fmax_s,
     fcvt_w_s,
     fcvt_wu_s,
     fmv_x_w,
     feq_s,
     flt_s,
     fle_s,
     fclass_s,
     fcvt_s_w,
     fcvt_s_wu,
     fmv_w_x,

     // rv64f
     fcvt_l_s,
     fcvt_lu_s,
     fcvt_s_l,
     fcvt_s_lu,

     // rv32d
     fld,
     fsd,
     fmadd_d,
     fmsub_d,
     fnmsub_d,
     fnmadd_d,
     fadd_d,
     fsub_d,
     fmul_d,
     fdiv_d,
     fsqrt_d,
     fsgnj_d,
     fsgnjn_d,
     fsgnjx_d,
     fmin_d,
     fmax_d,
     fcvt_s_d,
     fcvt_d_s,
     feq_d,
     flt_d,
     fle_d,
     fclass_d,
     fcvt_w_d,
     fcvt_wu_d,
     fcvt_d_w,
     fcvt_d_wu,

     // rv64f
     fcvt_l_d,
     fcvt_lu_d,
     fmv_x_d,
     fcvt_d_l,
     fcvt_d_lu,
     fmv_d_x,

     // Privileged
     mret,
     uret,
     sret,
     wfi,

     // Compressed insts
     c_addi4spn,
     c_fld,
     c_lq,
     c_lw,
     c_flw,
     c_ld,
     c_fsd,
     c_sq,
     c_sw,
     c_fsw,
     c_sd,
     c_addi,
     c_jal,
     c_li,
     c_addi16sp,
     c_lui,
     c_srli,
     c_srli64,
     c_srai,
     c_srai64,
     c_andi,
     c_sub,
     c_xor,
     c_or,
     c_and,
     c_subw,
     c_addw,
     c_j,
     c_beqz,
     c_bnez,
     c_slli,
     c_slli64,
     c_fldsp,
     c_lwsp,
     c_flwsp,
     c_ldsp,
     c_jr,
     c_mv,
     c_ebreak,
     c_jalr,
     c_add,
     c_fsdsp,
     c_swsp,
     c_fswsp,
     c_addiw,
     c_sdsp,

     // zbb
     clz,
     ctz,
     pcnt,
     andn, // was andc
     orn,  // new
     xorn, // new
     slo,
     sro,
     sloi,
     sroi,
     min,
     max,
     minu,
     maxu,
     rol,
     ror,
     rori,
     rev8,  // was bswap
     rev,   // was brev
     pack,

     // zbs
     sbset,
     sbclr,
     sbinv,
     sbext,
     sbseti,
     sbclri,
     sbinvi,
     sbexti,

     maxId = sbexti
    };
}
