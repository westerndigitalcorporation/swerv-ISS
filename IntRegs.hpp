// -*- c++ -*-

// Copyright Western Digital.  All rights reserved.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <type_traits>
#include <assert.h>

namespace WdRiscv
{

  /// Model a RISCV integer register file.
  /// URV (unsigned register value) is the register value type. For
  /// 32-bit registers, URV should be uint32_t. For 64-bit integers,
  /// it should be uint64_t.
  template <typename URV>
  class IntRegs
  {
  public:

    /// Constructor: Define a register file with the given number of
    /// registers. Each register is of type URV. All registers initialized
    /// to zero.
    IntRegs(unsigned registerCount)
      : regs_(registerCount)
    {
    }

    /// Destructor.
    ~IntRegs()
    { 
      regs_.clear();
    }
    
    /// Return value of ith register. Register zero always yields zero.
    URV read(unsigned i) const
    {
      if (i == 0)
	return 0;
      return regs_.at(i);
    }

    /// Set value of ith register to the given value. Setting register
    /// zero has no effect.
    void write(unsigned i, URV value)
    {
      if (i != 0)
	regs_.at(i) = value;
    }

    /// Return the count of registers in this register file.
    size_t size() const
    { 
      return regs_.size();
    }

    /// Return the number of bits in a register in this register file.
    static constexpr uint32_t regWidth()
    { return sizeof(URV)*8; }

    /// Return the number of bits used to encode a shift amount in
    /// the RISC-V instruction. For 32-bit registers, this returns 5
    /// (which allows us to encode the amounts 0 to 31),
    /// for 64-bit registers it returns 6 (which allows encoding of 0
    /// to 63).
    static uint32_t log2RegWidth()
    { 
      if (std::is_same<URV, uint32_t>::value)
	return 5;
      if (std::is_same<URV, uint64_t>::value)
	return 6;
      assert(0 and "Register value type must be uint32_t or uint64_t.");
      return 5;
    }

    /// Return a register value with the least significan n-bits set to 1
    /// and all remainig bits set to zero where n is the number of bits
    /// required to encode any bit number in a register. For 32-bit registers
    /// this returns 0x1f, for 64-bit registers it returns 0x3f.
    static URV shiftMask()
    {
      if (std::is_same<URV, uint32_t>::value)
	return 0x1f;
      if (std::is_same<URV, uint64_t>::value)
	return 0x3f;
      assert(0 and "Register value type must be uint32_t or uint64_t.");
      return 0x1f;
    }

  private:
    std::vector<URV> regs_;
  };
}
