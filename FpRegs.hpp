// -*- c++ -*-

// Copyright Western Digital.  All rights reserved.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <type_traits>


namespace WdRiscv
{

  enum class RoundingMode
    {
      NearestEven,    // Round to nearest, ties to even
      Zero,           // Round towards zero.
      Down,           // Round down (towards negative infinitiy)
      Up,             // Round up (towards positive infinity)
      NearestMax,     // Round to neareset, ties to max magnitude
      Invalid1,
      Invalid2,
      Dynamic
    };

  enum class FpFlags
    {
      None = 0,
      Inexact = 1,
      Underflow = 2,
      Overflow = 4,
      DivByZero = 8,
      Invalid = 16,
    };

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
    FpRegs(unsigned registerCount)
      : regs_(registerCount, 0)
    { }

    /// Destructor.
    ~FpRegs()
    { regs_.clear(); }
    
    /// Return value of ith register.
    FRV read(unsigned i) const
    { return regs_[i]; }

    /// Return the bit pattern of the ith register as an unsigned integer.
    uint64_t readBits(unsigned i) const
    {
      if (sizeof(FRV) == 4)
	return *((uint32_t*) &regs_.at(i));

      // If nan-boxed return the single precision number.
      uint32_t* words = (uint32_t*) &regs_.at(i);
      if (words[1] == ~uint32_t(0))
	return words[0];

      return *((uint64_t*) &regs_.at(i));
    }

    /// Set value of ith register to the given value.
    void write(unsigned i, FRV value)
    {
      originalValue_ = regs_.at(i);
      regs_.at(i) = value;
      lastWrittenReg_ = i;
    }

    /// Read a single precision floating point number from the ith
    /// register.  If the register width is 64-bit, this will recover
    /// the least significant 32 bits (it assumes that the number in
    /// the register is NAN-boxed). If the register withd is 32-bit,
    /// this will simply recover the number in it.
    float readSingle(unsigned i) const;

    /// Write a single precision number into the ith register. NAN-box
    /// the number if the regiser is 64-bit wide.
    void writeSingle(unsigned i, float x);

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

    // Single precision number with a 32-bit padding.
    struct SpPad
    {
      float sp;
      uint32_t pad;
    };

    // Union of double and single precision numbers used for NAN boxing.
    union FpUnion
    {
      SpPad sp;
      double dp;
    };
	
  private:

    std::vector<FRV> regs_;
    int lastWrittenReg_ = -1;  // Register accessed in most recent write.
    FRV originalValue_ = 0;    // Original value of last written reg.
  };


  template<>
  inline
  float
  FpRegs<float>::readSingle(unsigned i) const
  {
    return regs_.at(i);
  }


  template<>
  inline
  void
  FpRegs<float>::writeSingle(unsigned i, float x)
  {
    regs_.at(i) = x;
  }


  template<>
  inline
  float
  FpRegs<double>::readSingle(unsigned i) const
  {
    FpUnion u;
    u.dp = regs_.at(i);
    return u.sp.sp;
  }


  template<>
  inline
  void
  FpRegs<double>::writeSingle(unsigned i, float x)
  {
    FpUnion u;
    u.sp.sp = x;
    u.sp.pad = ~uint32_t(0);  // Bit pattern for negative quiet NAN.
    regs_.at(i) = u.dp;
  }
}
