#include "Triggers.hpp"


using namespace WdRiscv;


template <typename URV>
Triggers<URV>::Triggers(unsigned count)
  : triggers_(count)
{
  for (unsigned i = 0; i < count; ++i)
    {
      triggers_.at(i).chainBegin_ = i;
      triggers_.at(i).chainEnd_ = i + 1;
    }
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
Triggers<URV>::writeData1(URV trigIx, URV value)
{
  if (trigIx >= triggers_.size())
    return false;

  auto& trig = triggers_.at(trigIx);
  bool prevChain = trig.getChain();

  trig.writeData1(value);

  bool newChain = trig.getChain();
  if (prevChain != newChain)
    defineChainBounds();

  return true;
}


template <typename URV>
bool
Triggers<URV>::writeData2(URV trigger, URV value)
{
  if (trigger >= triggers_.size())
    return false;

  triggers_.at(trigger).writeData2(value);

  return true;
}


template <typename URV>
bool
Triggers<URV>::writeData3(URV trigger, URV value)
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

  for (size_t i = trigger.chainBegin_; i < trigger.chainEnd_; ++i)
    {
      auto& trig = triggers_.at(i);
      chainHit = chainHit and trig.localHit_;
      uniformTiming = uniformTiming and (timing == trig.getTiming());
    }

  if (not chainHit or not uniformTiming)
    return false;

  for (size_t i = trigger.chainBegin_; i < trigger.chainEnd_; ++i)
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

      trigger.localHit_ = true;

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

      trigger.localHit_ = true;

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

      trigger.localHit_ = true;

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

      trigger.localHit_ = true;

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
      if (trig.modified_)
	continue; // Trigger was written by current instruction.

      if (trig.instCountdown())
	{
	  hit = true;
	  trig.setHit(true);
	  trig.localHit_ = true;
	}
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

  triggers_.at(trigger).data1_.value_ = data1;
  triggers_.at(trigger).data1WriteMask_ = wm1;
  triggers_.at(trigger).data1PokeMask_ = pm1;

  triggers_.at(trigger).data2_ = data2;
  triggers_.at(trigger).data2WriteMask_ = wm2;
  triggers_.at(trigger).data2PokeMask_ = pm2;
  triggers_.at(trigger).writeData2(data2);  // Define compare mask.

  triggers_.at(trigger).data3_ = data3;
  triggers_.at(trigger).data3WriteMask_ = wm3;
  triggers_.at(trigger).data3PokeMask_ = pm3;

  defineChainBounds();

  return true;
}


template <typename URV>
bool
Triggers<URV>::peek(URV trigger, URV& data1, URV& data2, URV& data3) const
{
  if (trigger >= triggers_.size())
    return false;

  readData1(trigger, data1);
  readData2(trigger, data2);
  readData3(trigger, data3);
  return true;
}


template <typename URV>
bool
Triggers<URV>::peek(URV trigger, URV& data1, URV& data2, URV& data3,
		    URV& wm1, URV& wm2, URV& wm3,
		    URV& pm1, URV& pm2, URV& pm3) const
{
  if (trigger >= triggers_.size())
    return false;

  readData1(trigger, data1);
  readData2(trigger, data2);
  readData2(trigger, data3);

  const Trigger<URV>& trig = triggers_.at(trigger);

  wm1 = trig.data1WriteMask_;
  wm2 = trig.data2WriteMask_;
  wm3 = trig.data3WriteMask_;
  pm1 = trig.data1WriteMask_;
  pm2 = trig.data2WriteMask_;
  pm3 = trig.data3WriteMask_;

  return true;
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
	    {
	      triggers_.at(j).chainBegin_ = begin;
	      triggers_.at(j).chainEnd_ = end;
	    }
	  begin = end;
	}
    }

  end = triggers_.size();
  for  (size_t i = begin; i < end; ++i)
    {
      triggers_.at(i).chainBegin_ = begin;
      triggers_.at(i).chainEnd_ = end;
    }
}



template <typename URV>
bool
Trigger<URV>::matchLdStAddr(URV address, TriggerTiming timing, bool isLoad) const
{
  if (TriggerType(data1_.data1_.type_) != TriggerType::Address)
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
  if (TriggerType(data1_.data1_.type_) != TriggerType::Address)
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
  if (TriggerType(data1_.data1_.type_) != TriggerType::Address)
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
  if (TriggerType(data1_.data1_.type_) != TriggerType::Address)
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
