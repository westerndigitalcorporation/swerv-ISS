// -*- c++ -*-

// Copyright Western Digital.  All rights reserved.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <type_traits>


namespace WdRiscv
{
  template <typename URV>
  class Core;

  /// Model a RISCV floating point register file.
  /// FRV (floating point register value) is the register value type. For
  /// 32-bit registers, FRV should be float. For 64-bit registers,
  /// it should be double.
  template <typename FRV>
  class FpRegs
  {
  public:

    friend class Core<uint32_t>;
    friend class Core<uint64_t>;

    /// Constructor: Define a register file with the given number of
    /// registers. Each register is of type FRV. All registers
    /// initialized to zero.
    FpRegs(unsigned registerCount);


    /// Destructor.
    ~FpRegs()
    { regs_.clear(); }
    
    /// Return value of ith register.
    FRV read(unsigned i) const
    { return regs_[i]; }

    /// Set value of ith register to the given value.
    void write(unsigned i, FRV value)
    {
      originalValue_ = regs_.at(i);
      regs_.at(i) = value;
      lastWrittenReg_ = i;
    }

    /// Return the count of registers in this register file.
    size_t size() const
    { return regs_.size(); }

    /// Return the number of bits in a register in this register file.
    static constexpr uint32_t regWidth()
    { return sizeof(FRV)*8; }

  protected:

    void reset()
    {
      clearLastWrittenReg();
      for (auto& reg : regs_)
	reg = 0;
    }

    /// Clear the number denoting the last written register.
    void clearLastWrittenReg()
    { lastWrittenReg_ = -1; }

    /// Return the number of the last written register or -1 if no register has
    /// been written since the last clearLastWrittenReg.
    int getLastWrittenReg() const
    { return lastWrittenReg_; }

    /// Set regIx and regValue to the index and previous value (before
    /// write) of the last written register returning true on success
    /// and false if no integer was written by the last executed
    /// instruction (in which case regIx and regVal are left
    /// unmodifed).
    bool getLastWrittenReg(unsigned& regIx, FRV& regValue) const
    {
      if (lastWrittenReg_ < 0) return false;
      regIx = lastWrittenReg_;
      regValue = originalValue_;
      return true;
    }

  private:

    std::vector<FRV> regs_;
    int lastWrittenReg_ = -1;  // Register accessed in most recent write.
    FRV originalValue_ = 0;    // Original value of last written reg.
  };
}
