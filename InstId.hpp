// -*- c++ -*-

#pragma once

namespace WdRiscv
{
  /// Associate a unique integer identifier with each instruction.
  enum class InstId
    {
      illegal,

      // Base.
      lui, auipc, jal, jalr,

      beq, bne, blt, bge, bltu, bgeu,

      lb, lh, lw, lbu, lhu,

      sb, sh, sw,

      addi, slti, sltiu, xori, ori, andi, slli, srli,
      srai, add, sub, sll, slt, sltu, xor_, srl,
      sra, or_, and_,

      fence, fencei,

      ecall, ebreak,

      // CSR
      csrrw, csrrs, csrrc, csrrwi, csrrsi, csrrci,

      // rv64i
      lwu, ld, sd, addiw, slliw, srliw, sraiw, addw,
      subw, sllw, srlw, sraw, 

      // Mul/div
      mul, mulh, mulhsu, mulhu, div, divu, rem, remu,

      // 64-bit mul/div
      mulw, divw, divuw, remw, remuw, 

      // Atomic
      lr_w, sc_w, amoswap_w, amoadd_w, amoxor_w, amoand_w, amoor_w,
      amomin_w, amomax_w, amominu_w, amomaxu_w,

      // 64-bit atomic
      lr_d, sc_d, amoswap_d, amoadd_d, amoxor_d, amoand_d, amoor_d,
      amomin_d, amomax_d, amominu_d, amomaxu_d,

      // Priv mode
      mret, uret, sret, wfi,

      // Compressed insts
      c_addi4spn, c_fld, c_lq, c_lw, c_flw, c_ld,
      c_fsd, c_sq, c_sw, c_fsw, c_sd,      

      c_addi, c_jal, c_li, c_addi16sp, c_lui, c_srli,
      c_srli64, c_srai, c_srai64, c_andi, c_sub, c_xor,
      c_or, c_and, c_subw, c_addw, c_j, c_beqz, c_bnez,
      c_slli, c_slli64, c_fldsp, c_lwsp, c_flwsp, c_jr,
      c_ebreak, c_jalr, c_add, c_fsdsp, c_swsp, c_fswsp
    };
}
