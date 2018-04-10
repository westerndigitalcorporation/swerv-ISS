#include "IntRegs.hpp"


using namespace WdRiscv;


template <typename URV>
IntRegs<URV>::IntRegs(unsigned regCount)
  : regs_(regCount), lastWrittenReg_(-1)
{
  for (unsigned ix = 0; ix < regCount; ++ix)
    {
      std::string name = "x" + std::to_string(ix);
      nameToNumber_[name] = IntRegNumber(ix);
    }

  if (RegX0 < regCount)
    nameToNumber_["zero"] = RegX0;

  if (RegX1 < regCount)
    nameToNumber_["ra"] = RegX1;

  if (RegX2 < regCount)
    nameToNumber_["sp"] = RegX2;

  if (RegX3 < regCount)
    nameToNumber_["gp"] = RegX3;

  if (RegX4 < regCount)
    nameToNumber_["tp"] = RegX4;

  if (RegX8 < regCount)
    nameToNumber_["fp"] = RegX8;

  if (RegX8 < regCount)
    nameToNumber_["s0"] = RegX8;

  if (RegX9 < regCount)
    nameToNumber_["s1"] = RegX9;

  if (RegX18 < regCount)
    nameToNumber_["s2"] = RegX18;

  if (RegX19 < regCount)
    nameToNumber_["s3"] = RegX19;

  if (RegX20 < regCount)
    nameToNumber_["s4"] = RegX20;

  if (RegX21 < regCount)
    nameToNumber_["s5"] = RegX21;

  if (RegX22 < regCount)
    nameToNumber_["s6"] = RegX22;

  if (RegX23 < regCount)
    nameToNumber_["s7"] = RegX23;

  if (RegX24 < regCount)
    nameToNumber_["s8"] = RegX24;

  if (RegX25 < regCount)
    nameToNumber_["s9"] = RegX25;

  if (RegX26 < regCount)
    nameToNumber_["s10"] = RegX26;

  if (RegX27 < regCount)
    nameToNumber_["s11"] = RegX27;

  if (RegX5 < regCount)
    nameToNumber_["t0"] = RegX5;

  if (RegX6 < regCount)
    nameToNumber_["t1"] = RegX6;

  if (RegX7 < regCount)
    nameToNumber_["t2"] = RegX7;

  if (RegX28 < regCount)
    nameToNumber_["t3"] = RegX28;

  if (RegX29 < regCount)
    nameToNumber_["t4"] = RegX29;

  if (RegX30 < regCount)
    nameToNumber_["t5"] = RegX30;

  if (RegX31 < regCount)
    nameToNumber_["t6"] = RegX31;
}


template <typename URV>
bool
IntRegs<URV>::findReg(const std::string& name, unsigned& ix) const
{
  const auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  ix = iter->second;
  return true;
}


template class WdRiscv::IntRegs<uint32_t>;
template class WdRiscv::IntRegs<uint64_t>;
