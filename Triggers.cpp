#include "Triggers.hpp"


using namespace WdRiscv;


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

  return false;
}


template <typename URV>
bool
Triggers<URV>::writeData1(URV trigger, URV value)
{
  if (trigger >= triggers_.size())
    return false;

  triggers_.at(trigger).writeData1(value);

  lastWritten_.push_back(trigger);
  return true;
}


template <typename URV>
bool
Triggers<URV>::writeData2(URV trigger, URV value)
{
  if (trigger >= triggers_.size())
    return false;

  triggers_.at(trigger).writeData2(value);
  lastWritten_.push_back(trigger);
  return true;
}


template <typename URV>
bool
Triggers<URV>::writeData3(URV trigger, URV value)
{
  if (trigger >= triggers_.size())
    return false;

  lastWritten_.push_back(trigger);
  return false;
}


template <typename URV>
bool
Triggers<URV>::reset(URV trigger, URV data1, URV data2, URV mask1, URV mask2)
{
  if (trigger >= triggers_.size())
    return false;

  triggers_.at(trigger).data1_.value_ = data1;
  triggers_.at(trigger).data1WriteMask_ = mask1;

  triggers_.at(trigger).data2_ = data2;
  triggers_.at(trigger).data2WriteMask_ = mask2;
  triggers_.at(trigger).writeData2(data2);

  return true;
}


template <typename URV>
bool
Trigger<URV>::matchLdStAddr(URV address, TriggerTiming timing, bool isLoad) const
{
  if (Type(data1_.data1_.type_) != Type::Address)
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
  if (Type(data1_.data1_.type_) != Type::Address)
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
  if (Type(data1_.data1_.type_) != Type::Address)
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
  if (Type(data1_.data1_.type_) != Type::Address)
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
