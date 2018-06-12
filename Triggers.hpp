// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>

namespace WdRiscv
{

  template <typename URV>
  struct Mcontrol;


  /// Bit fields of mcontrol trigger register view. 32-bit version.
  template <>
  struct Mcontrol<uint32_t>
    {
      // SPEC is bogus it has an extra zero bit.
      unsigned load_    : 1;   // trigger on load
      unsigned store_   : 1;   // trigger on store
      unsigned execute_ : 1;   // trigger on instruction
      unsigned u_       : 1;   // enable in user mode
      unsigned s_       : 1;   // enable in supervisor mode
      unsigned          : 1;
      unsigned m_       : 1;   // enable in machine mode
      unsigned match_   : 4;   // controls what is considered to be a match
      unsigned chain_   : 1;
      unsigned action_  : 6;
      unsigned timing_  : 1;
      unsigned select_  : 1;
      unsigned hit_     : 1;
      // URV               : 8*sizeof(URV) - 32;  // zero
      unsigned maskMax_ : 6;
      unsigned dmode_   : 1;
      unsigned type_    : 4;
  };


  /// Bit fields of mcontrol trigger register view. 64-bit version.
  template <>
  struct Mcontrol<uint64_t>
  {
    // SPEC is bogus it has an extra zero bit.
    unsigned load_    : 1;   // trigger on load
    unsigned store_   : 1;   // trigger on store
    unsigned execute_ : 1;   // trigger on instruction
    unsigned u_       : 1;   // enable in user mode
    unsigned s_       : 1;   // enable in supervisor mode
    unsigned          : 1;
    unsigned m_       : 1;   // enable in machine mode
    unsigned match_   : 4;   // controls what is considered to be a match
    unsigned chain_   : 1;
    unsigned action_  : 6;
    unsigned timing_  : 1;
    unsigned select_  : 1;
    unsigned hit_     : 1;
    unsigned          : 32;  // 8*sizeof(URV) - 32;
    unsigned maskMax_ : 6;
    unsigned dmode_   : 1;
    unsigned type_    : 4;
  };


  // Bit fields for Icount trigger registttter view.
  template <typename URV>
  struct Icount
  {
    unsigned action_  : 6;
    unsigned u_       : 1;
    unsigned s_       : 1;
    unsigned          : 1;
    unsigned m_       : 1;
    unsigned count_   : 14;
    unsigned hit_     : 1;
    URV               : 8*sizeof(URV) - 30;
    unsigned dmode_   : 1;
    unsigned type_    : 4;
  } __attribute__((packed));


  /// Bit fields of genertic tdata trigger register view.
  template <typename URV>
  struct GenericData1
    {
      URV data_       : 8*sizeof(URV) - 5;
      unsigned dmode_ : 1;
      unsigned type_  : 4;
    } __attribute__((packed));


  /// TDATA1 trigger register
  template <typename URV>
  union Data1Bits
  {
    Data1Bits(URV value) :
      value_(value)
    { }

    URV value_ = 0;
    GenericData1<URV> data1_;
    Mcontrol<URV> mcontrol_;
    Icount<URV> icount_;
  };
      

  template <typename URV>
  struct Trigger
  {
    enum class Type { None, Legacy, Address, InstCount, Unavailable };

    enum class Mode { DM, D };  // Modes allowed to write trigger regiters.

    enum class Select { MatchAddress, MatchData };

    enum class Timing { BeforeInst, AfterInst };

    enum class Action { RaiseBreak, EnterDebug, StartTrace, StopTrace, EmitTrace };

    enum class Chain { No, Yes };

    enum class Match { Equal, Masked, GE, LT, MaskHighEqualLow, MaskLowEqualHigh };

    Trigger(URV data1 = 0, URV data2 = 0, URV data1Mask = ~URV(0),
	    URV data2Mask = ~URV(0))
      : data1_(data1), data2_(data2), data1Mask_(data1Mask),
	data2Mask_(data2Mask)
    { }

    URV readData1() const
    { return data1_.value_; }

    URV readData2() const
    { return data2_; }

    void writeData1(URV x)
    { data1_.value_ = (x & data1Mask_) | (data1_.value_ & ~data1Mask_); }

    void writeData2(URV x)
    { data2_ = (x & data2Mask_) | (data2_ & ~data2Mask_); }

    bool isEnabled() const
    {
      if (Type(data1_.data1_.type_) == Type::Address)
	return data1_.mcontrol_.m_;
      if (Type(data1_.data1_.type_) == Type::InstCount)
	return data1_.icount_.m_;
      return false;
    }

    bool matchLoadAddressBefore(URV address) const
    {
      if (Type(data1_.data1_.type_) == Type::Address and
	  Timing(data1_.mcontrol_.timing_) == Timing::BeforeInst and
	  Select(data1_.mcontrol_.select_) == Select::MatchAddress and
	  data1_.mcontrol_.load_)
	{
	  switch (Match(data1_.mcontrol_.match_))
	    {
	    case Match::Equal: return address == data2_;
	    case Match::Masked: return false; // FIX
	    case Match::GE: return address >= data2_;
	    case Match::LT: return address < data2_;
	    case Match::MaskHighEqualLow: return false; // FIX
	    case Match::MaskLowEqualHigh: return false; // FIX
	    }
	}
      return false;
    }

    bool matchLoadAddressAfter(URV address) const
    {
      if (Type(data1_.data1_.type_) == Type::Address and
	  Timing(data1_.mcontrol_.timing_) == Timing::AfterInst and
	  Select(data1_.mcontrol_.select_) == Select::MatchAddress and
	  data1_.mcontrol_.load_)
	{
	  switch (Match(data1_.mcontrol_.match_))
	    {
	    case Match::Equal: return address == data2_;
	    case Match::Masked: return false; // FIX
	    case Match::GE: return address >= data2_;
	    case Match::LT: return address < data2_;
	    case Match::MaskHighEqualLow: return false; // FIX
	    case Match::MaskLowEqualHigh: return false; // FIX
	    }
	}
      return false;
    }

    void setHit(bool flag)
    {
      if (Type(data1_.data1_.type_) == Type::Address)
	data1_.mcontrol_.hit_ = flag;
      if (Type(data1_.data1_.type_) == Type::InstCount)
	data1_.icount_.hit_ = flag;
    }

    Data1Bits<URV> data1_ = Data1Bits<URV> (0);
    URV data2_ = 0;
    URV data1Mask_ = ~URV(0);
    URV data2Mask_ = ~URV(0);
  };


  template <typename URV>
  class Triggers
  {
  public:

    Triggers(unsigned count)
      : triggers_(count)
    { }

    unsigned size() const
    { return triggers_.size(); }

    bool readData1(URV trigger, URV& value) const;

    bool readData2(URV trigger, URV& value) const;

    bool readData3(URV trigger, URV& value) const;

    bool writeData1(URV trigger, URV value);

    bool writeData2(URV trigger, URV value);

    bool writeData3(URV trigger, URV value);

    /// Return true if one or more triggers are enabled.
    bool hasActiveTrigger() const
    {
      for (const auto& trigger : triggers_)
	if (trigger.isEnabled())
	  return true;
      return false;
    }

    bool loadAddressBeforeTriggerHit(URV address)
    {
      bool hit = false;
      for (auto& trigger : triggers_)
	if (trigger.matchLoadAddressBefore(address))
	  {
	    hit = true;
	    trigger.setHit(true);
	  }
      return hit;
    }

    bool loadAddressAfterTriggerHit(URV address)
    {
      bool hit = false;
      for (auto& trigger : triggers_)
	if (trigger.matchLoadAddressAfter(address))
	  {
	    hit = true;
	    trigger.setHit(true);
	  }
      return hit;
    }

    bool reset(URV trigger, URV data1, URV data2, URV mask1, URV mask2);

  private:
    std::vector< Trigger<URV> > triggers_;
  };
}
