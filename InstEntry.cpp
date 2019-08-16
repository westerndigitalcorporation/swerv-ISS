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

#include <cassert>
#include "InstEntry.hpp"

using namespace WdRiscv;

InstEntry::InstEntry(std::string name, InstId id,
		     uint32_t code, uint32_t mask,
		     InstType type,
		     OperandType op0Type, OperandMode op0Mode, uint32_t op0Mask,
		     OperandType op1Type, OperandMode op1Mode, uint32_t op1Mask,
		     OperandType op2Type, OperandMode op2Mode, uint32_t op2Mask,
		     OperandType op3Type, OperandMode op3Mode, uint32_t op3Mask)
  : name_(name), id_(id), code_(code), codeMask_(mask), type_(type),
    op0Mask_(op0Mask), op1Mask_(op1Mask), op2Mask_(op2Mask), op3Mask_(op3Mask),
    op0Type_(op0Type), op1Type_(op1Type), op2Type_(op2Type), op3Type_(op3Type),
    op0Mode_(op0Mode), op1Mode_(op1Mode), op2Mode_(op2Mode), op3Mode_(op3Mode),
    opCount_(0)
{
  unsigned count = 0;

  if (op0Type != OperandType::None) count++;
  if (op1Type != OperandType::None) count++;
  if (op2Type != OperandType::None) count++;
  if (op3Type != OperandType::None) count++;
  opCount_ = count;
}


InstTable::InstTable()
{
  setupInstVec();

  // Sanity check.
  for (unsigned i = 0; InstId(i) < InstId::maxId; ++i)
    {
      InstId id = InstId(i);
      assert(instVec_.at(i).instId() == id);
    }

  for (const auto& instInfo : instVec_)
    instMap_[instInfo.name()] = instInfo.instId();

  // Mark instructions with unsigned source opreands.
  instVec_.at(size_t(InstId::bltu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::bgeu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::sltiu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::sltu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::mulhsu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::mulhu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::divu)).setIsUnsigned(true);
  instVec_.at(size_t(InstId::remu)).setIsUnsigned(true);

  // Set data size of load instructions.
  instVec_.at(size_t(InstId::lb))      .setLoadSize(1);
  instVec_.at(size_t(InstId::lh))      .setLoadSize(2);
  instVec_.at(size_t(InstId::lw))      .setLoadSize(4);
  instVec_.at(size_t(InstId::lbu))     .setLoadSize(1);
  instVec_.at(size_t(InstId::lhu))     .setLoadSize(2);
  instVec_.at(size_t(InstId::lwu))     .setLoadSize(4);
  instVec_.at(size_t(InstId::ld))      .setLoadSize(8);
  instVec_.at(size_t(InstId::lr_w))    .setLoadSize(4);
  instVec_.at(size_t(InstId::lr_d))    .setLoadSize(8);
  instVec_.at(size_t(InstId::flw))     .setLoadSize(4);
  instVec_.at(size_t(InstId::fld))     .setLoadSize(8);
  instVec_.at(size_t(InstId::c_fld))   .setLoadSize(8);
  instVec_.at(size_t(InstId::c_lq))    .setLoadSize(16);
  instVec_.at(size_t(InstId::c_lw))    .setLoadSize(4);
  instVec_.at(size_t(InstId::c_flw))   .setLoadSize(4);
  instVec_.at(size_t(InstId::c_ld))    .setLoadSize(8);
  instVec_.at(size_t(InstId::c_fldsp)) .setLoadSize(8);
  instVec_.at(size_t(InstId::c_lwsp))  .setLoadSize(4);
  instVec_.at(size_t(InstId::c_flwsp)) .setLoadSize(4);
  instVec_.at(size_t(InstId::c_ldsp))  .setLoadSize(8);

  // Set data size of store instructions.
  instVec_.at(size_t(InstId::sb))      .setStoreSize(1);
  instVec_.at(size_t(InstId::sh))      .setStoreSize(2);
  instVec_.at(size_t(InstId::sw))      .setStoreSize(4);
  instVec_.at(size_t(InstId::sd))      .setStoreSize(8);
  instVec_.at(size_t(InstId::sc_w))    .setStoreSize(4);
  instVec_.at(size_t(InstId::sc_d))    .setStoreSize(8);
  instVec_.at(size_t(InstId::fsw))     .setStoreSize(4);
  instVec_.at(size_t(InstId::fsd))     .setStoreSize(8);
  instVec_.at(size_t(InstId::c_fsd))   .setStoreSize(8);
  instVec_.at(size_t(InstId::c_sq))    .setStoreSize(16);
  instVec_.at(size_t(InstId::c_sw))    .setStoreSize(4);
  instVec_.at(size_t(InstId::c_flw))   .setStoreSize(4);
  instVec_.at(size_t(InstId::c_sd))    .setStoreSize(8);
  instVec_.at(size_t(InstId::c_fsdsp)) .setStoreSize(8);
  instVec_.at(size_t(InstId::c_swsp))  .setStoreSize(4);
  instVec_.at(size_t(InstId::c_fswsp)) .setStoreSize(4);
  instVec_.at(size_t(InstId::c_sdsp))  .setStoreSize(8);

  // Mark conditional branch instructions.
  instVec_.at(size_t(InstId::beq))    .setConditionalBranch(true);
  instVec_.at(size_t(InstId::bne))    .setConditionalBranch(true);
  instVec_.at(size_t(InstId::blt))    .setConditionalBranch(true);
  instVec_.at(size_t(InstId::bge))    .setConditionalBranch(true);
  instVec_.at(size_t(InstId::bltu))   .setConditionalBranch(true);
  instVec_.at(size_t(InstId::bgeu))   .setConditionalBranch(true);
  instVec_.at(size_t(InstId::c_beqz)) .setConditionalBranch(true);
  instVec_.at(size_t(InstId::c_bnez)) .setConditionalBranch(true);

  // Mark branch to register instructions.
  instVec_.at(size_t(InstId::jalr))   .setBranchToRegister(true);
  instVec_.at(size_t(InstId::c_jr))   .setBranchToRegister(true);
  instVec_.at(size_t(InstId::c_jalr)) .setBranchToRegister(true);
}


const InstEntry&
InstTable::getEntry(InstId id) const
{
  if (size_t(id) >= instVec_.size())
    return instVec_.front();
  return instVec_.at(size_t(id));
}


const InstEntry&
InstTable::getEntry(const std::string& name) const
{
  const auto iter = instMap_.find(name);
  if (iter == instMap_.end())
    return instVec_.front();
  auto id = iter->second;
  return getEntry(id);
}


void
InstTable::setupInstVec()
{
  uint32_t rdMask = 0x1f << 7;
  uint32_t rs1Mask = 0x1f << 15;
  uint32_t rs2Mask = 0x1f << 20;
  uint32_t rs3Mask = 0x1f << 27;
  uint32_t immTop20 = 0xffff << 12;  // Immidiate: top 20 bits.
  uint32_t immTop12 = 0xfff << 20;   // Immidiate: top 12 bits.
  uint32_t immBeq = 0xfe000f80;
  uint32_t shamtMask = 0x01f00000;

  uint32_t low7Mask = 0x7f;                 // Opcode mask: lowest 7 bits
  uint32_t funct3Low7Mask = 0x707f;         // Funct3 and lowest 7 bits
  uint32_t fmaddMask = 0x0600007f;          // fmadd-like opcode mask.
  uint32_t faddMask = 0xfe00007f;           // fadd-like opcode mask
  uint32_t fsqrtMask = 0xfff0007f;          // fsqrt-like opcode mask
  uint32_t top7Funct3Low7Mask = 0xfe00707f; // Top7, Funct3 and lowest 7 bits

  instVec_ =
    {
      // Base instructions
      { "illegal", InstId::illegal, 0xffffffff, 0xffffffff },

      { "lui", InstId::lui, 0x37, low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::Imm, OperandMode::None, immTop20 },

      { "auipc", InstId::auipc, 0x17, low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::Imm, OperandMode::None, immTop20 },

      { "jal", InstId::jal, 0x6f, low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::Imm, OperandMode::None, immTop20 },

      { "jalr", InstId::jalr, 0x0067, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "beq", InstId::beq, 0x0063, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "bne", InstId::bne, 0x1063, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "blt", InstId::blt, 0x4063, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "bge", InstId::bge, 0x5063, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "bltu", InstId::bltu, 0x6063, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "bgeu", InstId::bgeu, 0x7063, funct3Low7Mask,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "lb", InstId::lb, 0x0003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "lh", InstId::lh, 0x1003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "lw", InstId::lw, 0x2003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "lbu", InstId::lbu, 0x4003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "lhu", InstId::lhu, 0x5003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "sb", InstId::sb, 0x0023, funct3Low7Mask,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "sh", InstId::sh, 0x1023, funct3Low7Mask,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      // Stored register is op0.
      { "sw", InstId::sw, 0x2023, funct3Low7Mask,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "addi", InstId::addi, 0x0013, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "slti", InstId::slti, 0x2013, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "sltiu", InstId::sltiu, 0x3013, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "xori", InstId::xori, 0x4013, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "ori", InstId::ori, 0x6013, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "andi", InstId::andi, 0x7013, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "slli", InstId::slli, 0x1013, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "srli", InstId::srli, 0x5013, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "srai", InstId::srai, 0x40005013, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "add", InstId::add, 0x0033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sub", InstId::sub, 0x40000033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sll", InstId::sll, 0x1033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "slt", InstId::slt, 0x2033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sltu", InstId::sltu, 0x3033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "xor", InstId::xor_, 0x4033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "srl", InstId::srl, 0x5033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sra", InstId::sra, 0x40005033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "or", InstId::or_, 0x6033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "and", InstId::and_, 0x7033, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "fence", InstId::fence, 0x000f, 0xf00fffff,
	InstType::Int,
	OperandType::Imm, OperandMode::None, 0x0f000000,
	OperandType::Imm, OperandMode::None, 0x00f00000 },

      { "fencei", InstId::fencei, 0x100f, 0xffffffff },

      { "ecall", InstId::ecall, 0x00000073, 0xffffffff },
      { "ebreak", InstId::ebreak, 0x00100073, 0xffffffff },

      // CSR
      { "csrrw", InstId::csrrw, 0x1073, funct3Low7Mask,
	InstType::Csr,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::CsReg, OperandMode::ReadWrite, immTop12 },

      { "csrrs", InstId::csrrs, 0x2073, funct3Low7Mask,
	InstType::Csr,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::CsReg, OperandMode::ReadWrite, immTop12 },

      { "csrrc", InstId::csrrc, 0x3073, funct3Low7Mask,
	InstType::Csr,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::CsReg, OperandMode::ReadWrite, immTop12 },


      { "csrrwi", InstId::csrrwi,  0x5073, funct3Low7Mask,
	InstType::Csr,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::Imm, OperandMode::None, rs1Mask,
	OperandType::CsReg, OperandMode::ReadWrite, immTop12 },

      { "csrrsi", InstId::csrrsi, 0x6073, funct3Low7Mask,
	InstType::Csr,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::Imm, OperandMode::None, rs1Mask,
	OperandType::CsReg, OperandMode::ReadWrite, immTop12 },

      { "csrrci", InstId::csrrci, 0x7073, funct3Low7Mask,
	InstType::Csr,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::Imm, OperandMode::None, rs1Mask,
	OperandType::CsReg, OperandMode::ReadWrite, immTop12 },

      // rv64i
      { "lwu", InstId::lwu, 0x06003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },
	
      { "ld", InstId::ld, 0x3003, funct3Low7Mask,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "sd", InstId::sd, 0x3023, funct3Low7Mask,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, rs2Mask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "addiw", InstId::addiw, 0x001b, funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "slliw", InstId::slliw, 0x101b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "srliw", InstId::srliw, 0x501b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "sraiw", InstId::sraiw, 0x4000501b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "addw", InstId::addw, 0x003b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "subw", InstId::subw, 0x4000003b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sllw", InstId::sllw, 0x103b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "srlw", InstId::srlw, 0x503b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sraw", InstId::sraw, 0x4000503b, top7Funct3Low7Mask,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      // Mul/div
      { "mul", InstId::mul, 0x02000033, top7Funct3Low7Mask,
	InstType::Multiply,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "mulh", InstId::mulh, 0x02001033, top7Funct3Low7Mask,
	InstType::Multiply,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "mulhsu", InstId::mulhsu, 0x02002033, top7Funct3Low7Mask,
	InstType::Multiply,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "mulhu", InstId::mulhu, 0x02003033, top7Funct3Low7Mask,
	InstType::Multiply,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "div", InstId::div, 0x02004033, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "divu", InstId::divu, 0x02005033, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "rem", InstId::rem, 0x02006033, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "remu", InstId::remu, 0x02007033, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      // 64-bit mul/div
      { "mulw", InstId::mulw, 0x0200003b, top7Funct3Low7Mask,
	InstType::Multiply,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "divw", InstId::divw, 0x0200403b, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "divuw", InstId::divuw, 0x0200503b, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "remw", InstId::remw, 0x0200603b, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "remuw", InstId::remuw, 0x0200703b, top7Funct3Low7Mask,
	InstType::Divide,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      // Atomic
      { "lr.w", InstId::lr_w, 0x1000202f, 0xf9f0707f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "sc.w", InstId::sc_w, 0x1800202f, 0xf800707f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoswap.w", InstId::amoswap_w, 0x0800202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoadd.w", InstId::amoadd_w, 0x0000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoxor.w", InstId::amoxor_w, 0x2000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoand.w", InstId::amoand_w, 0x6000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoor.w", InstId::amoor_w, 0x4000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amomin.w", InstId::amomin_w, 0x8000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amomax.w", InstId::amomax_w, 0xa000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amominu.w", InstId::amominu_w, 0xc000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amomaxu.w", InstId::amomaxu_w, 0xe000202f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      // 64-bit atomic
      { "lr.d", InstId::lr_d, 0x1000302f, 0xf9f0707f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "sc.d", InstId::sc_d, 0x1800302f, 0xf800707f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoswap.d", InstId::amoswap_d, 0x0800302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoadd.d", InstId::amoadd_d, 0x0000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoxor.d", InstId::amoxor_d, 0x2000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoand.d", InstId::amoand_d, 0x6000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amoor.d", InstId::amoor_d, 0x4000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amomin.d", InstId::amomin_d, 0x8000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amomax.d", InstId::amomax_d, 0xa000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amominu.d", InstId::amominu_d, 0xc000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "amomaxu.d", InstId::amomaxu_d, 0xe000302f, 0xf800070f,
	InstType::Atomic,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      // rv32f
      { "flw", InstId::flw, 0x2007, funct3Low7Mask,
	InstType::Load,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      // Stored register is in op0.
      { "fsw", InstId::fsw, 0x2027, funct3Low7Mask,
	InstType::Store,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "fmadd.s", InstId::fmadd_s, 0x43, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fmsub.s", InstId::fmsub_s, 0x47, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fnmsub.s", InstId::fnmsub_s, 0x4b, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fnmadd.s", InstId::fnmadd_s, 0x4f, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fadd.s", InstId::fadd_s, 0x0053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsub.s", InstId::fsub_s, 0x08000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fmul.s", InstId::fmul_s, 0x10000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fdiv.s", InstId::fdiv_s, 0x18000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsqrt.s", InstId::fsqrt_s, 0x58000053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fsgnj.s", InstId::fsgnj_s, 0x20000053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsgnjn.s", InstId::fsgnjn_s, 0x20001053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsgnjx.s", InstId::fsgnjx_s, 0x20002053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fmin.s", InstId::fmin_s, 0x28000053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fmax.s", InstId::fmax_s, 0x28001053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fcvt.w.s", InstId::fcvt_w_s, 0xc0000053, fsqrtMask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.wu.s", InstId::fcvt_wu_s, 0xc0100053, fsqrtMask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fmv.x.w", InstId::fmv_x_w, 0xe0900053, 0xfff1c07f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "feq.s", InstId::feq_s, 0xa0002053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "flt.s", InstId::flt_s, 0xa0001053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fle.s", InstId::fle_s, 0xa0000053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fclass.s", InstId::fclass_s, 0xe0001053, 0xfff1c07f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.s.w", InstId::fcvt_s_w, 0xd0000053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "fcvt.s.wu", InstId::fcvt_s_wu, 0xd0100053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "fmv.w.x", InstId::fmv_w_x, 0xf0000053, 0xfff1c07f,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      // rv64f
      { "fcvt.l.s", InstId::fcvt_l_s, 0xc0200053, 0xfff0007f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.lu.s", InstId::fcvt_lu_s, 0xc0300053, 0xfff0007f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.s.l", InstId::fcvt_s_l, 0xd0200053, 0xfff0007f,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "fcvt.s.lu", InstId::fcvt_s_lu, 0xd0300053, 0xfff0007f,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      // rv32d
      { "fld", InstId::fld, 0x3007, funct3Low7Mask,
	InstType::Load,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immTop12 },

      { "fsd", InstId::fsd, 0x3027, funct3Low7Mask,
	InstType::Store,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, immBeq },

      { "fmadd.d", InstId::fmadd_d, 0x02000043, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fmsub.d", InstId::fmsub_d, 0x02000047, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fnmsub.d", InstId::fnmsub_d, 0x0200004b, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fnmadd.d", InstId::fnmadd_d, 0x0200004f, fmaddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask,
	OperandType::FpReg, OperandMode::Read, rs3Mask },

      { "fadd.d", InstId::fadd_d, 0x02000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsub.d", InstId::fsub_d, 0x0a000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fmul.d", InstId::fmul_d, 0x12000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fdiv.d", InstId::fdiv_d, 0x1b000053, faddMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsqrt.d", InstId::fsqrt_d, 0x5b000053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fsgnj.d", InstId::fsgnj_d, 0x22000053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsgnjn.d", InstId::fsgnjn_d, 0x22001053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fsgnjx.d", InstId::fsgnjx_d, 0x22002053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fmin.d", InstId::fmin_d, 0x2b000053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fmax.d", InstId::fmax_d, 0x2b001053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fcvt.s.d", InstId::fcvt_s_d, 0x40100053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.d.s", InstId::fcvt_d_s, 0x42000053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "feq.d", InstId::feq_d, 0xa2002053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "flt.d", InstId::flt_d, 0xa2001053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fle.d", InstId::fle_d, 0xa2000053, top7Funct3Low7Mask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask,
	OperandType::FpReg, OperandMode::Read, rs2Mask },

      { "fclass.d", InstId::fclass_d, 0xe2001053, 0xfff1c07f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.w.d", InstId::fcvt_w_d, 0xf2000053, 0xfff1c07f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.wu.d", InstId::fcvt_wu_d, 0xc2100053, fsqrtMask,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.d.w", InstId::fcvt_d_w, 0xd2000053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "fcvt.d.wu", InstId::fcvt_d_wu, 0xd2100053, fsqrtMask,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      // rv64f + rv32d
      { "fcvt.l.d", InstId::fcvt_l_d, 0xc2200053, 0xfff0007f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.lu.d", InstId::fcvt_lu_d, 0xc2300053, 0xfff0007f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fmv.x.d", InstId::fmv_x_d, 0xe2000053, 0xfff0707f,
	InstType::Fp,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::FpReg, OperandMode::Read, rs1Mask },

      { "fcvt.d.l", InstId::fcvt_d_l, 0xd2200053, 0xfff0007f,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "fcvt.d.lu", InstId::fcvt_d_lu, 0xd2300053, 0xfff0007f,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "fmv.d.x", InstId::fmv_d_x, 0xef000053, 0xfff0707f,
	InstType::Fp,
	OperandType::FpReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      // Privileged
      { "mret", InstId::mret, 0x30100073, 0xffffffff, InstType::Int },
      { "uret", InstId::uret, 0x00100073, 0xffffffff, InstType::Int },
      { "sret", InstId::sret, 0x10100073, 0xffffffff, InstType::Int },
      { "wfi", InstId::wfi, 0x10280073, 0xffffffff, InstType::Int },

      // Compressed insts. The operand bits are "swizzled" and the
      // operand masks are not used for obtaining operands. We set the
      // operand masks to zero.
      { "c.addi4spn", InstId::c_addi4spn, 0x0000, 0xe003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.fld", InstId::c_fld, 0x2000, 0xe003,
	InstType::Load,
	OperandType::FpReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.lq", InstId::c_lq, 0x2000, 0xe003, 
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.lw", InstId::c_lw, 04000, 0xe003,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.flw", InstId::c_flw, 0x6000, 0xe003,
	InstType::Load,
	OperandType::FpReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.ld", InstId::c_ld, 0x6000, 0xe003,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.fsd", InstId::c_fsd, 0xa000, 0xe003,
	InstType::Store,
	OperandType::FpReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.sq", InstId::c_sq, 0xa000, 0xe003,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.sw", InstId::c_sw, 0xc000, 0xe003,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.fsw", InstId::c_fsw, 0xe000, 0xe003,
	InstType::Store,
	OperandType::FpReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.sd", InstId::c_sd, 0xe000, 0xe003,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.addi", InstId::c_addi, 0x0001, 0xe003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0xf80,
	OperandType::IntReg, OperandMode::Read, 0xf80,
	OperandType::Imm, OperandMode::None, 0x107c },

      { "c.jal", InstId::c_jal, 0x0001, 0xe003,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.li", InstId::c_li, 0x4001, 0xe003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.addi16sp", InstId::c_addi16sp, 0x6006, 0xef83,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.lui", InstId::c_lui, 0x6001, 0xe003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.srli", InstId::c_srli, 0x8001, 0xec03,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.srli64", InstId::c_srli64, 0x8001, 0xfc83,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.srai", InstId::c_srai, 0x8401, 0xec03,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.srai64", InstId::c_srai64, 0x8401, 0xfc83,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.andi", InstId::c_andi, 0x8801, 0xec03,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.sub", InstId::c_sub, 0x8c01, 0xfc63,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.xor", InstId::c_xor, 0x8c21, 0xfc63,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.or", InstId::c_or, 0x8c41, 0xfc63,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.and", InstId::c_and, 0x8c61, 0xfc63,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.subw", InstId::c_subw, 0x9c01, 0xfc63,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.addw", InstId::c_addw, 0x9c21, 0xfc63,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.j", InstId::c_j, 0xa001, 0xe003,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.beqz", InstId::c_beqz, 0xc001, 0xe003,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.bnez", InstId::c_bnez, 0xe001, 0xe003,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.slli", InstId::c_slli, 0x0002, 0xe003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.slli64", InstId::c_slli64, 0x0002, 0xf083,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.fldsp", InstId::c_fldsp, 0x2002, 0xe003,
	InstType::Load,
	OperandType::FpReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.lwsp", InstId::c_lwsp, 0x4002, 0xe003,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.flwsp", InstId::c_flwsp, 0x6002, 0xe003,
	InstType::Load,
	OperandType::FpReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.ldsp", InstId::c_ldsp, 0x6002, 0xe003,
	InstType::Load,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.jr", InstId::c_jr, 0x8002, 0xf07f,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.mv", InstId::c_mv, 0x8002, 0xf003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0 },

      { "c.ebreak", InstId::c_ebreak, 0x9002, 0xffff },

      { "c.jalr", InstId::c_jalr, 0x9002, 0xf07f,
	InstType::Branch,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.add", InstId::c_add, 0x9002, 0xf003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0xf80,
	OperandType::IntReg, OperandMode::Read, 0xf80,
	OperandType::IntReg, OperandMode::Read, 0x7c0 },

      { "c.fsdsp", InstId::c_fsdsp, 0xa002, 0xe003,
	InstType::Store,
	OperandType::FpReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.swsp", InstId::c_swsp, 0xc002, 0xe003,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.fswsp", InstId::c_fswsp, 0xe002, 0xe003,
	InstType::Store,
	OperandType::FpReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.addiw", InstId::c_addiw, 0x0001, 0xe003,
	InstType::Int,
	OperandType::IntReg, OperandMode::Write, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "c.sdsp", InstId::c_sdsp, 0xe002, 0xe003,
	InstType::Store,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::IntReg, OperandMode::Read, 0,
	OperandType::Imm, OperandMode::None, 0 },

      { "clz", InstId::clz, 0xc0001013, 0xfff0707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "ctz", InstId::ctz, 0xc0101013, 0xfff0707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "pcnt", InstId::pcnt, 0xc0201013, 0xfff0707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "andn", InstId::andn, 0x40007033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "orn", InstId::orn, 0x40006033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "xnor", InstId::xnor, 0x40004033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "slo", InstId::slo, 0x20001033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sro", InstId::sro, 0x20005033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sloi", InstId::sloi, 0x20001013, 0xf800707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "sroi", InstId::sroi, 0x20005013, 0xf800707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "min", InstId::min, 0x0a004033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "max", InstId::max, 0x0a005033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "minu", InstId::minu, 0x0a006033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "maxu", InstId::maxu, 0x0a007033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "rol", InstId::rol, 0x60001023, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "ror", InstId::ror, 0x60005023, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "rori", InstId::rori, 0x60005023, 0xf800707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "rev8", InstId::rev8, 0x41801013, 0xfff0707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "rev", InstId::rev, 0x41f01013, 0xfff0707f,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask },

      { "pack", InstId::pack, 0x08000033, top7Funct3Low7Mask,
	InstType::Zbb,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sbset", InstId::sbset, 0x24001023, top7Funct3Low7Mask,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sbclr", InstId::sbclr, 0x44001023, top7Funct3Low7Mask,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sbinv", InstId::sbinv, 0x64001023, top7Funct3Low7Mask,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sbext", InstId::sbext, 0x24005023, top7Funct3Low7Mask,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::IntReg, OperandMode::Read, rs2Mask },

      { "sbseti", InstId::sbseti, 0x28001023, 0xf800707f,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "sbclri", InstId::sbclri, 0x48001023, 0xf800707f,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "sbinvi", InstId::sbinvi, 0x68001023, 0xf800707f,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

      { "sbexti", InstId::sbexti, 0x48005023, 0xf800707f,
	InstType::Zbs,
	OperandType::IntReg, OperandMode::Write, rdMask,
	OperandType::IntReg, OperandMode::Read, rs1Mask,
	OperandType::Imm, OperandMode::None, shamtMask },

    };
}
