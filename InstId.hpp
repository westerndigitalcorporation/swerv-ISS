// -*- c++ -*-

#pragma once

namespace WdRiscv
{
  /// Associate a unique integer identifier with each instruction.
  enum class InstId
    {
      ILLEGAL_ID,

      // Base.
      LUI_ID, AUIPC_ID, JAL_ID, JALR_ID,

      BEQ_ID, BNE_ID, BLT_ID, BGE_ID, BLTU_ID, BGEU_ID,

      LB_ID, LH_ID, LW_ID, LBU_ID, LHU_ID,

      SB_ID, SH_ID, SW_ID,

      ADDI_ID, SLTI_ID, SLTIU_ID, XORI_ID, ORI_ID, ANDI_ID, SLLI_ID, SRLI_ID,
      SRAI_ID, ADD_ID, SUB_ID, SLL_ID, SLT_ID, SLTU_ID, XOR_ID, SRL_ID,
      SRA_ID, OR_ID, AND_ID,

      FENCE_ID, FENCEI_ID,

      ECALL_ID, EBREAK_ID,

      // CSR
      CSRRW_ID, CSRRS_ID, CSRRC_ID, CSRRWI_ID, CSRRSI_ID, CSRRCI_ID,

      // rv64i
      LWU_ID, LD_ID, SD_ID, ADDIW_ID, SLLIW_ID, SRLIW_ID, SRAIW_ID, ADDW_ID,
      SUBW_ID, SLLW_ID, SRLW_ID, SRAW_ID, 

      // Mul/div
      MUL_ID, MULH_ID, MULHSU_ID, MULHU_ID, DIV_ID, DIVU_ID, REM_ID, REMU_ID,

      // 64-bit mul/div
      MULW_ID, DIVW_ID, DIVUW_ID, REMW_ID, REMUW_ID, 

      // Priv mode
      MRET_ID, URET_ID, SRET_ID,

      WFI_ID,

      // Compressed insts
      C_ADDI4SPN_ID, C_FLD_ID, C_LQ_ID, C_LW_ID, C_FLW_ID, C_LD_ID,
      C_FSD_ID, C_SQ_ID, C_SW_ID, C_FSW_ID, C_SD_ID,      

      C_ADDI_ID, C_JAL_ID, C_LI_ID, C_ADDI16SP_ID, C_LUI_ID, C_SRLI_ID,
      C_SRLI64_ID, C_SRAI_ID, C_SRAI64_ID, C_ANDI_ID, C_SUB_ID, C_XOR_ID,
      C_OR_ID, C_AND_ID, C_SUBW_ID, C_ADDW_ID, C_J_ID, C_BEQZ_ID, C_BNEZ_ID,
      C_SLLI_ID, C_SLLI64_ID, C_FLDSP_ID, C_LWSP_ID, C_FLWSP_ID, C_JR_ID,
      C_EBREAK_ID, C_JALR_ID, C_ADD_ID, C_FSDSP_ID, C_SWSP_ID, C_FSWSP_ID
    };
}
