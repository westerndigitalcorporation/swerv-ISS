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

#include <vector>
#include <string>
#include <unordered_map>
#include "InstEntry.hpp"
#include "FpRegs.hpp"
#include "InstId.hpp"


namespace WdRiscv
{

  template <typename URV>
  class Core;


  /// Model a decoded instruction: instruction address, opcode, and
  /// operand fields. All instructions are assumed to have the form
  ///   inst op0, op1, op2, op3
  /// where op0 to op3 are optional. For example, in "add x2, x1, x0"
  /// op0 is x2, op1 is x1 and op2 is x0.
  ///
  /// Load instructions of the form "load rd, offset(rs1)" get mapped
  /// to "load rd, rs1, offset" assigning rd to op0 and offset to op2.
  ///
  /// Store instructions of the form "store rs2, offset(rs1)" get mapped
  /// to "store rs2, rs1, offset" assigning rs2 to op0 and offset to op0.
  ///   
  class DecodedInst
  {
  public:
    
    /// Default contructor: Define an invalid object.
    DecodedInst()
      : addr_(0), inst_(0), size_(0), entry_(nullptr),
	op0_(0), op1_(0), op2_(0), op3_(0)
    { }

    /// Constructor.
    DecodedInst(uint64_t addr, uint32_t inst, uint32_t size,
		const InstEntry* entry,
		uint32_t op0, uint32_t op1, uint32_t op2, uint32_t op3)
      : addr_(addr), inst_(inst), size_(size), entry_(entry),
	op0_(op0), op1_(op1), op2_(op2), op3_(op3)
    { }

    /// Return instruction size.
    uint32_t instSize() const
    { return size_; }

    /// Return address of instruction.
    uint64_t address() const
    { return addr_; }

    /// Return instruction code.
    uint32_t inst() const
    { return inst_; }

    /// Return the 1st operand (zero if instruction has no operands).
    /// First operand is typically the destination register.
    uint32_t op0() const
    { return op0_; }

    /// Return 2nd operand (zero if instruction has no 2nd operand).
    /// Second operand is typically source register rs1.
    uint32_t op1() const
    { return op1_; }

    /// Return 2nd operand as a signed integer. This is useful for
    /// instructions where the 2nd operand is a signed immediate
    /// value.
    int32_t op1AsInt() const
    { return op1_; }

    /// Return 3rd operand (zero if instruction has no 3rd operand).
    /// Third operand is typically source register rs2 or immediate
    /// value.
    uint32_t op2() const
    { return op2_; }

    /// Return 3rd operand as a signed integer. This is useful for
    /// instructions where the 3rd operand is a signed immediate
    /// value.
    int32_t op2AsInt() const
    { return op2_; }

    /// Return 4th operand (zero if instruction has no 4th operand).
    /// Fourth operand is typically source register rs3 for
    /// multiply-add like floating point instructions.
    uint32_t op3() const
    { return op3_; }

    /// Return true if this object is valid.
    bool isValid()
    { return entry_ != nullptr; }

    /// Make invalid.
    void invalidate()
    { entry_ = nullptr; }

    /// Return associated instruction table information.
    const InstEntry* instEntry() const
    { return entry_; }

    /// Relevant for floating point instructions.
    RoundingMode roundingMode() const
    { return RoundingMode((inst_ >> 12) & 7); }

    /// Relevant to atomic instructions: Return true if acquire bit is
    /// set.
    bool isAtomicAcquire() const
    { return (inst_ >> 26) & 1; }

    /// Relevant to atomic instructions: Return true if release bit is
    /// set.
    bool isAtomicRelease() const
    { return (inst_ >> 25) & 1; }

    /// Fetch the values of the register operands of this instruction
    /// from the given core.
    template <typename URV>
    void fetchRegisterOperands(Core<URV>& core);

  private:

    uint64_t addr_;
    uint32_t inst_;
    uint32_t size_;
    const InstEntry* entry_;
    uint32_t op0_;    // 1st operand (typically a register number)
    uint32_t op1_;    // 2nd operand (typically a register number) 
    uint32_t op2_;    // 3rd operand (register number or immediate value)
    uint32_t op3_;    // 4th operand (typically a register number)
  };

}
