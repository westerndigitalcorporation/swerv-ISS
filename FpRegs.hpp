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

#include <cstdint>
#include <cstddef>
#include <vector>
#include <type_traits>
#include <unordered_map>

namespace WdRiscv
{

    /// Symbolic names of the integer registers.
    enum FpRegNumber
      {
	RegF0   = 0,
	RegF1   = 1,
	RegF2   = 2,
	RegF3   = 3,
	RegF4   = 4,
	RegF5   = 5,
	RegF6   = 6,
	RegF7   = 7,
	RegF8   = 8,
	RegF9   = 9,
	RegF10  = 10,
	RegF11  = 11,
	RegF12  = 12,
	RegF13  = 13,
	RegF14  = 14,
	RegF15  = 15,
	RegF16  = 16,
	RegF17  = 17,
	RegF18  = 18,
	RegF19  = 19,
	RegF20  = 20,
	RegF21  = 21,
	RegF22  = 22,
	RegF23  = 23,
	RegF24  = 24,
	RegF25  = 25,
	RegF26  = 26,
	RegF27  = 27,
	RegF28  = 28,
	RegF29  = 29,
	RegF30  = 30,
	RegF31  = 31,
	RegFt0  = RegF0,
	RegFt1  = RegF1,
	RegFt2  = RegF2,
	RegFt3  = RegF3,
	RegFt4  = RegF4,
	RegFt5  = RegF5,
	RegFt6  = RegF6,
	RegFt7  = RegF7,
	RegFs0  = RegF8,
	RegFs1  = RegF9,
	RegFa0  = RegF10,
	RegFa1  = RegF11,
	RegFa2  = RegF12,
	RegFa3  = RegF13,
	RegFa4  = RegF14,
	RegFa5  = RegF15,
	RegFa6  = RegF16,
	RegFa7  = RegF17,
	RegFs2  = RegF18,
	RegFs3  = RegF19,
	RegFs4  = RegF20,
	RegFs5  = RegF21,
	RegFs6  = RegF22,
	RegFs7  = RegF23,
	RegFs8  = RegF24,
	RegFs9  = RegF25,
	RegFs10 = RegF26,
	RegFs11 = RegF27,
	RegFt8  = RegF28,
	RegFt9  = RegF29,
	RegFt10 = RegF30,
	RegFt11 = RegF31
      };


  /// RISCV floating point rounding modes.
  enum class RoundingMode
    {
      NearestEven,    // Round to nearest, ties to even
      Zero,           // Round towards zero.
      Down,           // Round down (towards negative infinity)
      Up,             // Round up (towards positive infinity)
      NearestMax,     // Round to nearest, ties to max magnitude
      Invalid1,
      Invalid2,
      Dynamic
    };


  /// RISCV floating point exception flags.
  enum class FpFlags
    {
      None = 0,
      Inexact = 1,
      Underflow = 2,
      Overflow = 4,
      DivByZero = 8,
      Invalid = 16,
    };


  /// RISCV values used to synthesize the results of the classify
  /// instructions (e.g. flcass.s).
  enum class FpClassifyMasks : uint32_t
    {
     NegInfinity  = 1,       // bit 0
     NegNormal    = 1 << 1,  // bit 1
     NegSubnormal = 1 << 2,  // bit 2
     NegZero      = 1 << 3,  // bit 3
     PosZero      = 1 << 4,  // bit 4
     PosSubnormal = 1 << 5,  // bit 5
     PosNormal    = 1 << 6,  // bit 6
     PosInfinity  = 1 << 7,  // bit 7
     SignalingNan = 1 << 8,  // bit 8
     QuietNan     = 1 << 9   // bit 9
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
    FpRegs(unsigned registerCount);

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

    /// Set FP register i to the given value.
    void pokeBits(unsigned i, uint64_t val)
    {
      if (sizeof(FRV) == 4)
	*((uint32_t*) &regs_.at(i)) = val;
      else
	*((uint64_t*) &regs_.at(i)) = val;
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
    /// the register is NAN-boxed). If the register width is 32-bit,
    /// this will simply recover the number in it.
    float readSingle(unsigned i) const;

    /// Write a single precision number into the ith register. NAN-box
    /// the number if the register is 64-bit wide.
    void writeSingle(unsigned i, float x);

    /// Return the count of registers in this register file.
    size_t size() const
    { return regs_.size(); }

    /// Set ix to the number of the register corresponding to the
    /// given name returning true on success and false if no such
    /// register.  For example, if name is "f2" then ix will be set to
    /// 2. If name is "fa0" then ix will be set to 10.
    bool findReg(const std::string& name, unsigned& ix) const;

    /// Return the name of the given register.
    std::string regName(unsigned i, bool abiNames = false) const
    {
      if (abiNames)
	{
	  if (i < numberToAbiName_.size())
	    return numberToAbiName_[i];
	  return std::string("f?");
	}
      if (i < numberToName_.size())
	return numberToName_[i];
      return std::string("f?");
    }

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
    /// unmodified).
    bool getLastWrittenReg(unsigned& regIx, uint64_t& regValue) const
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
    std::unordered_map<std::string, FpRegNumber> nameToNumber_;
    std::vector<std::string> numberToAbiName_;
    std::vector<std::string> numberToName_;
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
    write(i, x);
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
    write(i, u.dp);
  }
}
