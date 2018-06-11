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
Triggers<URV>::reset(URV trigger, URV data1, URV data2, URV mask1, URV mask2)
{
  if (trigger >= triggers_.size())
    return false;

  triggers_.at(trigger).data1_.value_ = data1;
  triggers_.at(trigger).data2_ = data2;

  triggers_.at(trigger).data1Mask_ = mask1;
  triggers_.at(trigger).data2Mask_ = mask2;

  return true;
}


template class WdRiscv::Triggers<uint32_t>;
template class WdRiscv::Triggers<uint64_t>;
