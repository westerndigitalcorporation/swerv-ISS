// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>

namespace WdRiscv
{

  /// Trigger timing control: Before instruction or after.
  enum class TriggerTiming { Before, After };

  /// Trigger type.
  enum class TriggerType { None, Legacy, Address, InstCount, Unavailable };


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
    URV data_         : 8*sizeof(URV) - 5;
    unsigned dmode_   : 1;
    unsigned type_    : 4;
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
    enum class Mode { DM, D };  // Modes allowed to write trigger regiters.

    enum class Select { MatchAddress, MatchData };

    enum class Action { RaiseBreak, EnterDebug, StartTrace, StopTrace, EmitTrace };

    enum class Chain { No, Yes };

    enum class Match { Equal, Masked, GE, LT, MaskHighEqualLow, MaskLowEqualHigh };

    Trigger(URV data1 = 0, URV data2 = 0, URV data3 = 0,
	    URV mask1 = ~URV(0), URV mask2 = ~URV(0), URV mask3 = 0)
      : data1_(data1), data2_(data2), data1WriteMask_(mask1),
	data2WriteMask_(mask2), data3WriteMask_(mask3)
    { }

    URV readData1() const
    { return data1_.value_; }

    URV readData2() const
    { return data2_; }

    URV readData3() const
    { return data3_; }

    void writeData1(URV x)
    {
      data1_.value_ = (x & data1WriteMask_) | (data1_.value_ & ~data1WriteMask_);

      if (TriggerType(data1_.data1_.type_) == TriggerType::Address)
	{
	  // Temporary: Match RTL.
	  if (Select(data1_.mcontrol_.select_) == Select::MatchData and
	      data1_.mcontrol_.load_)
	    data1_.mcontrol_.timing_ = unsigned(TriggerTiming::After);
	  else
	    data1_.mcontrol_.timing_ = unsigned(TriggerTiming::Before);
	}
    }

    void writeData2(URV value)
    {
      data2_ = (value & data2WriteMask_) | (data2_ & ~data2WriteMask_);

      data2CompareMask_ = ~URV(0);
      unsigned leastSigZeroBit = 0; // Index of least sig zero bit
      value = data2_;
      while (value & 1)
	{
	  leastSigZeroBit++;
	  value >>= 1;
	}
      if (leastSigZeroBit < 8*sizeof(URV))
	{
	  data2CompareMask_ = data2CompareMask_ << (leastSigZeroBit + 1);
	}
    }

    void writeData3(URV value)
    {
      data3_ = (value & data3WriteMask_) | (data3_ & ~data3WriteMask_);
    }

    void pokeData1(URV x)
    {
      data1_.value_ = (x & data1PokeMask_) | (data1_.value_ & ~data1PokeMask_);
    }

    void pokeData2(URV x)
    {
      data2_ = (x & data2PokeMask_) | (data2_ & ~data2PokeMask_);
    }

    void pokeData3(URV x)
    {
      data3_ = (x & data3PokeMask_) | (data3_ & ~data3PokeMask_);
    }


    /// Return true if this trigger is enabled.
    bool isEnabled() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::Address)
	return data1_.mcontrol_.m_;
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	return data1_.icount_.m_;
      return false;
    }

    /// Return true if this is an instruction (execute) trigger.
    bool isInst() const
    {
      return (TriggerType(data1_.data1_.type_) == TriggerType::Address and
	      data1_.mcontrol_.execute_);
    }

    /// Return true if this trigger is enabled for loads (or stores if
    /// isLoad is false), for addresses, for the given timing and if
    /// it matches the given data address.  Return false otherwise.
    bool matchLdStAddr(URV address, TriggerTiming timing, bool isLoad) const;

    /// Return true if this trigger is enabled for loads (or stores if
    /// isLoad is false), for data, for the given timing and if it
    /// matches the given value address.  Return false otherwise.
    bool matchLdStData(URV value, TriggerTiming timing, bool isLoad) const;

    /// Return true if this trigger is enabled for instruction
    /// addresses (execution), for the given timing and if it matches
    /// the given address.  Return false otherwise.
    bool matchInstAddr(URV address, TriggerTiming timing) const;

    /// Return true if this trigger is enabled for instruction opcodes
    /// (execution), for the given timing and if it matches the given
    /// opcode.  Return false otherwise.
    bool matchInstOpcode(URV opcode, TriggerTiming timing) const;

    /// If this trigger is enabled and is of type icount, then make it
    /// count down returning true if its value becomes zero. Return
    /// false otherwise.
    bool instCountdown()
    {
      if (TriggerType(data1_.data1_.type_) != TriggerType::InstCount)
	return false;  // Not an icount trigger.
      Icount<URV>& icount = data1_.icount_;
      if (not icount.m_)
	return false;  // Trigger is not enabled.
      icount.count_--;
      return icount.count_ == 0;
    }

    /// Perform a match on the given item (maybe an address or a value)
    /// and the data2 component of this trigger (assumed to be of type Address)
    /// according to the match field.
    bool doMatch(URV item) const;

    /// Set the hit bit of this trigger.
    void setHit(bool flag)
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::Address)
	data1_.mcontrol_.hit_ = flag;
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	data1_.icount_.hit_ = flag;
    }

    Data1Bits<URV> data1_ = Data1Bits<URV> (0);
    URV data2_ = 0;
    URV data3_ = 0;

    URV data1WriteMask_ = ~URV(0);
    URV data2WriteMask_ = ~URV(0);
    URV data3WriteMask_ = 0;              // Place holder.

    URV data1PokeMask_ = ~URV(0);
    URV data2PokeMask_ = ~URV(0);
    URV data3PokeMask_ = 0;              // Place holder.

    URV data2CompareMask_ = ~URV(0);
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

    /// Set value to the data1 register of the given trigger. Return
    /// true on success and false (leaving value unmodified) if
    /// trigger is out of bounds.
    bool readData1(URV trigger, URV& value) const;

    /// Set value to the data2 register of the given trigger. Return
    /// true on success and false (leaving value unmodified) if
    /// trigger is out of bounds or if data2 is not implemented.
    bool readData2(URV trigger, URV& value) const;

    /// Set value to the data3 register of the given trigger. Return
    /// true on success and false (leaving value unmodified) if
    /// trigger is out of bounds of if data3 is not implemented.
    bool readData3(URV trigger, URV& value) const;

    /// Set the data1 register of the given trigger to the given
    /// value. Return true on success and false (leaving value
    /// unmodified) if trigger is out of bounds.
    bool writeData1(URV trigger, URV value);

    /// Set the data2 register of the given trigger to the given
    /// value. Return true on success and false (leaving value
    /// unmodified) if trigger is out of bounds or if data2 is not
    /// implemented.
    bool writeData2(URV trigger, URV value);

    /// Set the data3 register of the given trigger to the given
    /// value. Return true on success and false (leaving value
    /// unmodified) if trigger is out of bounds or if data3 is not
    /// implemented.
    bool writeData3(URV trigger, URV value);

    /// Return true if given trigger is enabled. Return false if
    /// trigger is not enabled or if it is out of bounds.
    bool isEnabled(URV trigger) const
    {
      if (trigger >= triggers_.size())
	return false;
      return triggers_.at(trigger).isEnabled();
    }

    /// Return true if one or more triggers are enabled.
    bool hasActiveTrigger() const
    {
      for (const auto& trigger : triggers_)
	if (trigger.isEnabled())
	  return true;
      return false;
    }

    /// Return true if one or more instruction (execute) triggers are
    /// enabled.
    bool hasActiveInstTrigger() const
    {
      for (const auto& trigger : triggers_)
	if (trigger.isEnabled() and trigger.isInst())
	  return true;
      return false;
    }

    /// Return true if any of the load (store if isLoad is true)
    /// triggers matches the given address and timing. Set the hit bit
    /// for any trigger that matches.
    bool ldStAddrTriggerHit(URV address, TriggerTiming timing, bool isLoad)
    {
      bool hit = false;
      for (size_t i = 0; i < triggers_.size(); ++i)
	{
	  auto& trigger = triggers_.at(i);
	  if (trigger.matchLdStAddr(address, timing, isLoad))
	    {
	      hit = true;
	      trigger.setHit(true);
	      lastWritten_.push_back(i);
	    }
	}
      return hit;
    }

    /// Return true if any of the load (store if isLoad is true)
    /// triggers matches the given value and timing. Set the hit bit
    /// for any trigger that matches.
    bool ldStDataTriggerHit(URV value, TriggerTiming timing, bool isLoad)
    {
      bool hit = false;
      for (size_t i = 0; i < triggers_.size(); ++i)
	{
	  auto& trigger = triggers_.at(i);
	  if (trigger.matchLdStData(value, timing, isLoad))
	    {
	      hit = true;
	      trigger.setHit(true);
	      lastWritten_.push_back(i);
	    }
	}
      return hit;
    }

    /// Return true if any of the instruction (execute) address
    /// triggers matches the given address and timing. Set the hit bit
    /// for any trigger that matches.
    bool instAddrTriggerHit(URV address, TriggerTiming timing)
    {
      bool hit = false;
      for (size_t i = 0; i < triggers_.size(); ++i)
	{
	  auto& trigger = triggers_.at(i);
	  if (trigger.matchInstAddr(address, timing))
	    {
	      hit = true;
	      trigger.setHit(true);
	      lastWritten_.push_back(i);
	    }
	}
      return hit;
    }

    /// Return true if any of the instruction (execute) opcode
    /// triggers matches the given address and timing. Set the hit bit
    /// for any trigger that matches.
    bool instOpcodeTriggerHit(URV opcode, TriggerTiming timing)
    {
      bool hit = false;
      for (size_t i = 0; i < triggers_.size(); ++i)
	{
	  auto& trigger = triggers_.at(i);
	  if (trigger.matchInstOpcode(opcode, timing))
	    {
	      hit = true;
	      trigger.setHit(true);
	      lastWritten_.push_back(i);
	    }
	}
      return hit;
    }

    /// Make every active icount trigger count down. If any active
    /// icoutn triggers reaches zero after the count-down, its hit
    /// bit is set to 1.  Return true if any of the active icount triggers
    /// reaches zero after the count-down; otherwise, return false.
    bool icountTriggerHit()
    {
      bool hit = false;
      for (size_t i = 0; i < triggers_.size(); ++i)
	{
	  auto& trigger = triggers_.at(i);
	  if (trigger.instCountdown())
	    {
	      hit = true;
	      trigger.setHit(true);
	      lastWritten_.push_back(i);
	    }
	}
      return hit;
    }

    /// Reset the given trigger with the given data1, data2, and data3
    /// values and corresponding write and poke masks. Values are applied
    /// without maksing. Subsequent writes will be masked.
    bool reset(URV trigger, URV data1, URV data2, URV data3,
	       URV writeMask1, URV writeMask2, URV writeMask3,
	       URV pokeMask1, URV pokeMask2, URV pokeMask3);

    /// Configure given trigger with given reset values, write masks and
    /// and poke masks.
    bool config(unsigned trigger, URV val1, URV val2, URV val3,
		URV wm1, URV wm2, URV wm3,
		URV pm1, URV pm2, URV pm3)
    {
      if (trigger <= triggers_.size())
	triggers_.resize(trigger + 1);
      return reset(trigger, val1, val2, val3, wm1, wm2, wm3, pm1, pm2, pm3);
    }

    /// Get the values of the three components of the given debug
    /// trigger. Return true on success and false if trigger is out of
    /// bounds.
    bool peek(URV trigger, URV& data1, URV& data2, URV& data3) const;

    /// Get the values of the three components of the given debug
    /// trigger as well as the components write and poke masks. Return
    /// true on success and false if trigger is out of bounds.
    bool peek(URV trigger, URV& data1, URV& data2, URV& data3,
	      URV& wm1, URV& wm2, URV& wm3,
	      URV& pm1, URV& pm2, URV& pm3) const;

    /// Set the values of the three components of the given debug
    /// trigger. Return true on success and false if trigger is out of
    /// bounds.
    bool poke(URV trigger, URV v1, URV v2, URV v3);

    /// Clear the remembered indices of the triggers written by the
    /// last instruction.
    void clearLastWrittenTriggers()
    { lastWritten_.clear(); }

    /// Fill the trigs vector with the indices of the triggers written
    /// by the last instruction.
    void getLastWrittenTriggers(std::vector<unsigned>& trigs) const
    { trigs = lastWritten_; }

  private:
    std::vector< Trigger<URV> > triggers_;
    std::vector<unsigned> lastWritten_;  // Indices written registers.
  };
}
