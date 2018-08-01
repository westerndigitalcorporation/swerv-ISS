#include "Triggers.hpp"


using namespace WdRiscv;


template <typename URV>
Triggers<URV>::Triggers(unsigned count)
  : triggers_(count)
{
  // Define each triggers as a single-element chain.
  for (unsigned i = 0; i < count; ++i)
    triggers_.at(i).setChainBounds(i, i+1);
}


template <typename URV>
bool
Triggers<URV>::readData1(URV trigger, URV& value) const
{
  if (trigger >= triggers_.size())
    return false;

  value = triggers_.at(trigger).readData1();
  return true;
}


template <typename URV>
bool
Triggers<URV>::readData2(URV trigger, URV& value) const
{
  if (trigger >= triggers_.size())
    return false;

  value = triggers_.at(trigger).readData2();
  return true;
}


template <typename URV>
bool
Triggers<URV>::readData3(URV trigger, URV& value) const
{
  if (trigger >= triggers_.size())
    return false;

  value = triggers_.at(trigger).readData3();
  return true;
}


template <typename URV>
bool
Triggers<URV>::writeData1(URV trigIx, bool debugMode, URV value)
{
  if (trigIx >= triggers_.size())
    return false;

  auto& trig = triggers_.at(trigIx);
  bool prevChain = trig.getChain();

  if (not trig.writeData1(debugMode, value))
    return false;

  bool newChain = trig.getChain();
  if (prevChain != newChain)
    defineChainBounds();

  return true;
}


template <typename URV>
bool
Triggers<URV>::writeData2(URV trigger, bool debugMode, URV value)
{
  if (trigger >= triggers_.size())
    return false;

  return triggers_.at(trigger).writeData2(debugMode, value);
}


template <typename URV>
bool
Triggers<URV>::writeData3(URV trigger, bool debugMode, URV value)
{
  if (trigger >= triggers_.size())
    return false;

  return false;
}


template <typename URV>
bool
Triggers<URV>::updateChainHitBit(Trigger<URV>& trigger)
{
  bool chainHit = true;  // True if all items in chain hit
  TriggerTiming  timing = trigger.getTiming();
  bool uniformTiming = true;

  size_t beginChain = 0, endChain = 0;
  trigger.getChainBounds(beginChain, endChain);

  for (size_t i = beginChain; i < endChain; ++i)
    {
      auto& trig = triggers_.at(i);
      chainHit = chainHit and trig.getLocalHit();
      uniformTiming = uniformTiming and (timing == trig.getTiming());
    }

  if (not chainHit or not uniformTiming)
    return false;

  for (size_t i = beginChain; i < endChain; ++i)
    triggers_.at(i).setHit(true);
  return true;
}


template <typename URV>
bool
Triggers<URV>::ldStAddrTriggerHit(URV address, TriggerTiming timing, bool isLoad)
{
  bool hit = false;
  for (auto& trigger : triggers_)
    {
      if (not trigger.matchLdStAddr(address, timing, isLoad))
	continue;

      trigger.setLocalHit(true);

      if (updateChainHitBit(trigger))
	hit = true;
    }
  return hit;
}


template <typename URV>
bool
Triggers<URV>::ldStDataTriggerHit(URV value, TriggerTiming timing, bool isLoad)
{
  bool hit = false;
  for (auto& trigger : triggers_)
    {
      if (not trigger.matchLdStData(value, timing, isLoad))
	continue;

      trigger.setLocalHit(true);

      if (updateChainHitBit(trigger))
	hit = true;
    }

  return hit;
}


template <typename URV>
bool
Triggers<URV>::instAddrTriggerHit(URV address, TriggerTiming timing)
{
  bool hit = false;
  for (auto& trigger : triggers_)
    {
      if (not trigger.matchInstAddr(address, timing))
	continue;

      trigger.setLocalHit(true);

      if (updateChainHitBit(trigger))
	hit = true;
    }
  return hit;
}


template <typename URV>
bool
Triggers<URV>::instOpcodeTriggerHit(URV opcode, TriggerTiming timing)
{
  bool hit = false;
  for (auto& trigger : triggers_)
    {
      if (not trigger.matchInstOpcode(opcode, timing))
	continue;

      trigger.setLocalHit(true);

      if (updateChainHitBit(trigger))
	hit = true;
    }

  return hit;
}


template <typename URV>
bool
Triggers<URV>::icountTriggerHit()
{
  bool hit = false;

  for (auto& trig : triggers_)
    {
      if (trig.isModified())
	continue; // Trigger was written by current instruction.

      if (not trig.instCountdown())
	continue;

      hit = true;
      trig.setHit(true);
      trig.setLocalHit(true);
    }
  return hit;
}


template <typename URV>
bool
Triggers<URV>::reset(URV trigger, URV data1, URV data2, URV data3,
		     URV wm1, URV wm2, URV wm3,
		     URV pm1, URV pm2, URV pm3)
{
  if (trigger >= triggers_.size())
    return false;

  triggers_.at(trigger).resetData1(data1, wm1, pm1);

  triggers_.at(trigger).resetData2(data2, wm2, pm2);
  triggers_.at(trigger).writeData2(true, data2);  // Define compare mask.

  triggers_.at(trigger).resetData3(data3, wm3, pm3);

  defineChainBounds();

  return true;
}


template <typename URV>
bool
Triggers<URV>::peek(URV trigger, URV& data1, URV& data2, URV& data3) const
{
  if (trigger >= triggers_.size())
    return false;

  return triggers_.at(trigger).peek(data1, data2, data3);
}


template <typename URV>
bool
Triggers<URV>::peek(URV trigger, URV& data1, URV& data2, URV& data3,
		    URV& wm1, URV& wm2, URV& wm3,
		    URV& pm1, URV& pm2, URV& pm3) const
{
  if (trigger >= triggers_.size())
    return false;

  const Trigger<URV>& trig = triggers_.at(trigger);
  return trig.peek(data1, data2, data3, wm1, wm2, wm3, pm1, pm2, pm3);
}


template <typename URV>
bool
Triggers<URV>::poke(URV trigger, URV v1, URV v2, URV v3)
{
  if (trigger >= triggers_.size())
    return false;

  Trigger<URV>& trig = triggers_.at(trigger);

  trig.pokeData1(v1);
  trig.pokeData2(v2);
  trig.pokeData3(v3);

  return true;
}


template <typename URV>
bool
Triggers<URV>::pokeData1(URV trigger, URV val)
{
  if (trigger >= triggers_.size())
    return false;

  Trigger<URV>& trig = triggers_.at(trigger);

  trig.pokeData1(val);
  return true;
}


template <typename URV>
bool
Triggers<URV>::pokeData2(URV trigger, URV val)
{
  if (trigger >= triggers_.size())
    return false;

  Trigger<URV>& trig = triggers_.at(trigger);

  trig.pokeData2(val);
  return true;
}


template <typename URV>
bool
Triggers<URV>::pokeData3(URV trigger, URV val)
{
  if (trigger >= triggers_.size())
    return false;

  Trigger<URV>& trig = triggers_.at(trigger);

  trig.pokeData3(val);
  return true;
}


template <typename URV>
void
Triggers<URV>::defineChainBounds()
{
  size_t begin = 0, end = 0;

  for (size_t i = 0; i < triggers_.size(); ++i)
    {
      if (not triggers_.at(i).getChain())
	{
	  end = i + 1;
	  for (size_t j = begin; j < end; j++)
	    triggers_.at(j).setChainBounds(begin, end);
	  begin = end;
	}
    }

  end = triggers_.size();
  for  (size_t i = begin; i < end; ++i)
    triggers_.at(i).setChainBounds(begin, end);
}


template <typename URV>
bool
Trigger<URV>::matchLdStAddr(URV address, TriggerTiming timing, bool isLoad) const
{
  if (TriggerType(data1_.data1_.type_) != TriggerType::AddrData)
    return false;  // Not an address trigger.

  if (not data1_.mcontrol_.m_)
    return false;  // Not enabled;

  bool isStore = not isLoad;
  const Mcontrol<URV>& ctl = data1_.mcontrol_;

  if (TriggerTiming(ctl.timing_) == timing and
      Select(ctl.select_) == Select::MatchAddress and
      ((isLoad and ctl.load_) or (isStore and ctl.store_)))
    return doMatch(address);

  return false;
}


template <typename URV>
bool
Trigger<URV>::matchLdStData(URV value, TriggerTiming timing, bool isLoad) const
{
  if (TriggerType(data1_.data1_.type_) != TriggerType::AddrData)
    return false;  // Not an address trigger.

  if (not data1_.mcontrol_.m_)
    return false;  // Not enabled;

  bool isStore = not isLoad;
  const Mcontrol<URV>& ctl = data1_.mcontrol_;

  if (TriggerTiming(ctl.timing_) == timing and
      Select(ctl.select_) == Select::MatchData and
      ((isLoad and ctl.load_) or (isStore and ctl.store_)))
    return doMatch(value);

  return false;
}


template <typename URV>
bool
Trigger<URV>::doMatch(URV item) const
{
  switch (Match(data1_.mcontrol_.match_))
    {
    case Match::Equal:
      return item == data2_;

    case Match::Masked:
      return (item & data2CompareMask_) == (data2_ & data2CompareMask_);

    case Match::GE:
      return item >= data2_;

    case Match::LT:
      return item < data2_;

    case Match::MaskHighEqualLow:
      {
	unsigned halfBitCount = 4*sizeof(URV);
	// Mask low half of item with data2_ high half
	item = item & (data2_ >> halfBitCount);
	// Compare low halfs
	return (item << halfBitCount) == (data2_ << halfBitCount);
      }

    case Match::MaskLowEqualHigh:
      {
	unsigned halfBitCount = 4*sizeof(URV);
	// Mask high half of item with data2_ low half
	item = item & (data2_ << halfBitCount);
	// Compare high halfs
	return (item >> halfBitCount) == (data2_ >> halfBitCount);
      }
    }

  return false;
}


template <typename URV>
bool
Trigger<URV>::matchInstAddr(URV address, TriggerTiming timing) const
{
  if (TriggerType(data1_.data1_.type_) != TriggerType::AddrData)
    return false;  // Not an address trigger.

  if (not data1_.mcontrol_.m_)
    return false;  // Not enabled;

  const Mcontrol<URV>& ctl = data1_.mcontrol_;

  if (TriggerTiming(ctl.timing_) == timing and
      Select(ctl.select_) == Select::MatchAddress and
      ctl.execute_)
    return doMatch(address);

  return false;
}


template <typename URV>
bool
Trigger<URV>::matchInstOpcode(URV opcode, TriggerTiming timing) const
{
  if (TriggerType(data1_.data1_.type_) != TriggerType::AddrData)
    return false;  // Not an address trigger.

  if (not data1_.mcontrol_.m_)
    return false;  // Not enabled;

  const Mcontrol<URV>& ctl = data1_.mcontrol_;

  if (TriggerTiming(ctl.timing_) == timing and
      Select(ctl.select_) == Select::MatchData and
      ctl.execute_)
    return doMatch(opcode);

  return false;
}


template class WdRiscv::Trigger<uint32_t>;
template class WdRiscv::Trigger<uint64_t>;

template class WdRiscv::Triggers<uint32_t>;
template class WdRiscv::Triggers<uint64_t>;
