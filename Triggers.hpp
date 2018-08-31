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
  enum class TriggerType { None, Legacy, AddrData, InstCount, Unavailable };


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
  class Triggers;

  /// Model a RISCV triger.
  template <typename URV>
  class Trigger
  {
  public:

    friend class Triggers<URV>;

    enum class Mode { DM, D };  // Modes allowed to write trigger regiters.

    enum class Select { MatchAddress, MatchData };

    enum class Action { RaiseBreak, EnterDebug, StartTrace, StopTrace,
			EmitTrace };

    enum class Chain { No, Yes };

    enum class Match { Equal, Masked, GE, LT, MaskHighEqualLow,
		       MaskLowEqualHigh };

    Trigger(URV data1 = 0, URV data2 = 0, URV data3 = 0,
	    URV mask1 = ~URV(0), URV mask2 = ~URV(0), URV mask3 = 0)
      : data1_(data1), data2_(data2), data1WriteMask_(mask1),
	data2WriteMask_(mask2), data3WriteMask_(mask3)
    { }

    /// Read the data1 register of the trigger. This is typically the
    /// control register of the trigger.
    URV readData1() const
    { return data1_.value_; }

    /// Read the data2 register of the trigger. This is typically the
    /// target value of the trigger.
    URV readData2() const
    { return data2_; }

    /// Read the data3 register of the trigger (currently unused).
    URV readData3() const
    { return data3_; }

    /// Write the data1 register of the trigger. This is the interface
    /// for CSR instructions.
    bool writeData1(bool debugMode, URV x)
    {
      if (isDebugModeOnly() and not debugMode)
	return false;
      URV mask = data1WriteMask_;
      if (not debugMode)  // dmode bit writable only in debug mode
	mask &= ~(URV(1) << (8*sizeof(URV) - 5));
      data1_.value_ = (x & mask) | (data1_.value_ & ~mask);
      if (TriggerType(data1_.mcontrol_.type_) == TriggerType::AddrData)
	{
	  // We do not support load-data: If it is attemted, we turn off
	  // the load. We do no support exec-opcode, if it is attempted,
	  // we turn off the exec.
	  if (Select(data1_.mcontrol_.select_) == Select::MatchData)
	    {
	      if (data1_.mcontrol_.load_)
		data1_.mcontrol_.load_ = false;
	      if (data1_.mcontrol_.execute_)
		data1_.mcontrol_.execute_ = false;
	    }

	  // Clearing dmode bit clears action field.
	  if (debugMode and data1_.mcontrol_.dmode_ == 0)
	    data1_.mcontrol_.action_ = 0;
	}
      else if (TriggerType(data1_.mcontrol_.type_) == TriggerType::InstCount)
	{
	  if (data1_.icount_.dmode_ == 0)
	    data1_.icount_.action_ = 0;
	}

      modified_ = true;
      return true;
    }

    /// Write the data2 register of the trigger. This is the interface
    /// for CSR instructions.
    bool writeData2(bool debugMode, URV value)
    {
      if (isDebugModeOnly() and not debugMode)
	return false;

      data2_ = (value & data2WriteMask_) | (data2_ & ~data2WriteMask_);
      modified_ = true;

      updateCompareMask();
      return true;
    }

    /// Write the data3 register of the trigger. This is the interface
    /// for CSR instructions.
    bool writeData3(bool debugMode, URV value)
    {
      if (isDebugModeOnly() and not debugMode)
	return false;

      data3_ = (value & data3WriteMask_) | (data3_ & ~data3WriteMask_);
      modified_ = true;
      return true;
    }

    /// Poke data1. This allows writing of modifiable bits that are
    /// read-only to the CSR instructions.
    void pokeData1(URV x)
    { data1_.value_ = (x & data1PokeMask_) | (data1_.value_ & ~data1PokeMask_); }

    /// Poke data2. This allows writing of modifiable bits that are
    /// read-only to the CSR instructions.
    void pokeData2(URV x)
    {
      data2_ = (x & data2PokeMask_) | (data2_ & ~data2PokeMask_);
      updateCompareMask();
    }

    /// Poke data1. This allows writing of modifiable bits that are
    /// read-only to the CSR instructions.
    void pokeData3(URV x)
    { data3_ = (x & data3PokeMask_) | (data3_ & ~data3PokeMask_); }

    void resetData1(URV val, URV mask, URV pokeMask)
    { data1_.value_ = val; data1WriteMask_ = mask; data1PokeMask_ = pokeMask;}

    void resetData2(URV val, URV mask, URV pokeMask)
    { data2_ = val; data2WriteMask_ = mask; data2PokeMask_ = pokeMask;}

    void resetData3(URV val, URV mask, URV pokeMask)
    { data3_ = val; data3WriteMask_ = mask; data3PokeMask_ = pokeMask;}

    /// Return true if this trigger is enabled.
    bool isEnabled() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	return data1_.mcontrol_.m_;
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	return data1_.icount_.m_;
      return false;
    }

    bool isDebugModeOnly() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	return Mode(data1_.mcontrol_.dmode_) == Mode::D;
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	return Mode(data1_.icount_.dmode_) == Mode::D;
      return true;
    }

    /// Return true if this is an instruction (execute) trigger.
    bool isInst() const
    {
      return (TriggerType(data1_.data1_.type_) == TriggerType::AddrData and
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
    /// the given address. Return false otherwise.
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

    /// Set the hit bit of this trigger. For a chained trigger, this
    /// should be called only if all the triggers in the chain have
    /// tripped.
    void setHit(bool flag)
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	{
	  data1_.mcontrol_.hit_ = flag;
	  modified_ = true;
	  if (flag)
	    chainHit_ = true;
	}
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	{
	  data1_.icount_.hit_ = flag;
	  modified_ = true;
	  if (flag)
	    chainHit_ = true;
	}
    }

    /// Return the hit bit of this trigger.
    bool getHit() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	return data1_.mcontrol_.hit_;
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	return data1_.icount_.hit_;
      return false;
    }

    /// Return the chain bit of this trigger or false if this trigger has
    /// no chain bit.
    bool getChain() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	return data1_.mcontrol_.chain_;
      return false;
    }

    /// Return the timing of this trigger.
    TriggerTiming getTiming() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	return TriggerTiming(data1_.mcontrol_.timing_);
      return TriggerTiming::After;  // icount has "after" timing.
    }

    /// Return true if the chain of this trigger has tripped.
    bool hasTripped() const
    { return chainHit_; }

    /// Return the action fields of the trigger.
    Action getAction() const
    {
      if (TriggerType(data1_.data1_.type_) == TriggerType::AddrData)
	return Action(data1_.mcontrol_.action_);
      if (TriggerType(data1_.data1_.type_) == TriggerType::InstCount)
	return Action(data1_.icount_.action_);
      return Action::RaiseBreak;
    }

  protected:

    void updateCompareMask()
    {
      // Pre-compute mask for a masked compare (match == 1 in mcontrol).
      data2CompareMask_ = ~URV(0);
      unsigned leastSigZeroBit = 0; // Index of least sig zero bit
      URV value = data2_;
      while (value & 1)
	{
	  leastSigZeroBit++;
	  value >>= 1;
	}
      if (leastSigZeroBit < 8*sizeof(URV))
	data2CompareMask_ = data2CompareMask_ << (leastSigZeroBit + 1);
    }

    bool isModified() const
    { return modified_; }

    void setModified(bool flag)
    { modified_ = flag; }

    bool getLocalHit() const
    { return localHit_; }

    void setLocalHit(bool flag)
    { localHit_ = flag; }

    void setChainHit(bool flag)
    { chainHit_ = flag; }

    void setChainBounds(size_t begin, size_t end)
    {
      chainBegin_ = begin;
      chainEnd_ = end;
    }

    void getChainBounds(size_t& begin, size_t& end) const
    {
      begin = chainBegin_;
      end = chainEnd_;
    }

    bool peek(URV& data1, URV& data2, URV& data3) const
    {
      data1 = readData1(); data2 = readData2(); data3 = readData3();
      return true;
    }

    bool peek(URV& data1, URV& data2, URV& data3,
	      URV& wm1, URV& wm2, URV& wm3,
	      URV& pm1, URV& pm2, URV& pm3) const
    {
      bool ok = peek(data1, data2, data3);
      wm1 = data1WriteMask_; wm2 = data2WriteMask_; wm3 = data3WriteMask_;
      pm1 = data1PokeMask_; pm2 = data2PokeMask_; pm3 = data3PokeMask_;
      return ok;
    }

  private:

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
    bool localHit_ = false;  // Trigger tripped in isolation.
    bool chainHit_ = false;   // All entries in chain tripped.
    bool modified_ = false;

    size_t chainBegin_ = 0, chainEnd_ = 0;
  };


  template <typename URV>
  class Triggers
  {
  public:

    Triggers(unsigned count = 0);

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
    bool writeData1(URV trigger, bool debugMode, URV value);

    /// Set the data2 register of the given trigger to the given
    /// value. Return true on success and false (leaving value
    /// unmodified) if trigger is out of bounds or if data2 is not
    /// implemented.
    bool writeData2(URV trigger, bool debugMode, URV value);

    /// Set the data3 register of the given trigger to the given
    /// value. Return true on success and false (leaving value
    /// unmodified) if trigger is out of bounds or if data3 is not
    /// implemented.
    bool writeData3(URV trigger, bool debugMode, URV value);

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
    /// triggers trips. A load/store trigger trips if it matches the
    /// given address and timing and if all the remaining triggers in
    /// its chain have tripped. Set the local-hit bit of any
    /// load/store trigger that matches. If a matching load/store
    /// trigger causes its chain to trip, then set the hit bit of all
    /// the triggers in that chain.
    bool ldStAddrTriggerHit(URV address, TriggerTiming timing, bool isLoad);

    /// Similar to ldStAddrTriggerHit but for data match.
    bool ldStDataTriggerHit(URV value, TriggerTiming timing, bool isLoad);

    /// Simliar to ldStAddrTriggerHit but for instruction address.
    bool instAddrTriggerHit(URV address, TriggerTiming timing);

    /// Similar to instAddrTriggerHit but for instruction opcode.
    bool instOpcodeTriggerHit(URV opcode, TriggerTiming timing);

    /// Make every active icount trigger count down unless it was
    /// written by the current instruction. Set the hit bit of a
    /// counted-down register if its value becomes zero. Return true
    /// if any counted-down register reaches zero; otherwise, return
    /// false.
    bool icountTriggerHit();

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

    bool pokeData1(URV trigger, URV val);
    bool pokeData2(URV trigger, URV val);
    bool pokeData3(URV trigger, URV val);

    /// Clear the remembered indices of the triggers written by the
    /// last instruction.
    void clearLastWrittenTriggers()
    {
      for (auto& trig : triggers_)
	{
	  trig.setLocalHit(false);
	  trig.setChainHit(false);
	  trig.setModified(false);
	}
    }

    /// Fill the trigs vector with the indices of the triggers written
    /// by the last instruction.
    void getLastWrittenTriggers(std::vector<unsigned>& trigs) const
    {
      trigs.clear();
      for (size_t i = 0; i < triggers_.size(); ++i)
	if (triggers_.at(i).isModified())
	  trigs.push_back(i);
    }

    /// Set before/after to the count of tripped triggers with
    /// before/after timing.
    void countTrippedTriggers(unsigned& before, unsigned& after) const
    {
      before = after = 0;
      for (const auto& trig : triggers_)
	if (trig.hasTripped())
	  (trig.getTiming() == TriggerTiming::Before)? before++ : after++;
    }

    /// Return true if there is one or more tripped trigger action set
    /// to "enter debug mode".
    bool hasEnterDebugModeTripped() const
    {
      for (const auto& t : triggers_)
	if (t.hasTripped() and t.getAction() == Trigger<URV>::Action::EnterDebug)
	  return true;
      return false;
    }

    /// Restrict chaining only to pairs of consecutive (even-numbered followed
    /// by odd) triggers.
    void setEvenOddChaining(bool flag)
    { chainPairs_ = flag; }

  protected:

    /// If all the triggers in the chain of the given trigger have
    /// tripped (in isolation using local-hit), then return true
    /// setting the hit bit of these triggers. Otherwise, return
    /// false.
    bool updateChainHitBit(Trigger<URV>& trigger);

    /// Define the chain bounds of each trigger.
    void defineChainBounds();

  private:

    std::vector< Trigger<URV> > triggers_;
    bool chainPairs_ = false;
  };
}
