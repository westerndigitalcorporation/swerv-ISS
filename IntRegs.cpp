#include "IntRegs.hpp"


using namespace WdRiscv;


template <typename URV>
IntRegs<URV>::IntRegs(unsigned regCount)
  : regs_(regCount, 0)
{
  for (unsigned ix = 0; ix < regCount; ++ix)
    {
      std::string name = "x" + std::to_string(ix);
      nameToNumber_[name] = IntRegNumber(ix);
    }

  numberToName_.resize(regCount);

  if (RegX0 < regCount)
    {
      nameToNumber_["zero"] = RegX0;
      numberToName_[RegX0] = "zero";
    }

  if (RegX1 < regCount)
    {
      nameToNumber_["ra"] = RegX1;
      numberToName_[RegX1] = "ra";
    }

  if (RegX2 < regCount)
    {
      nameToNumber_["sp"] = RegX2;
      numberToName_[RegX2] = "sp";
    }

  if (RegX3 < regCount)
    {
      nameToNumber_["gp"] = RegX3;
      numberToName_[RegX3] = "gp";
    }

  if (RegX4 < regCount)
    {
      nameToNumber_["tp"] = RegX4;
      numberToName_[RegX4] = "tp";
    }

  if (RegX5 < regCount)
    {
      nameToNumber_["t0"] = RegX5;
      numberToName_[RegX5] = "t0";
    }

  if (RegX6 < regCount)
    {
      nameToNumber_["t1"] = RegX6;
      numberToName_[RegX6] = "t1";
    }

  if (RegX7 < regCount)
    {
      nameToNumber_["t2"] = RegX7;
      numberToName_[RegX7] = "t2";
    }

  if (RegX8 < regCount)
    {
      nameToNumber_["fp"] = RegX8;
      numberToName_[RegX8] = "fp";
    }

  if (RegX8 < regCount)
    {
      nameToNumber_["s0"] = RegX8;
      numberToName_[RegX8] = "s0";
    }

  if (RegX9 < regCount)
    {
      nameToNumber_["s1"] = RegX9;
      numberToName_[RegX9] = "s1";
    }

  if (RegX10 < regCount)
    {
      nameToNumber_["a0"] = RegX10;
      numberToName_[RegX10] = "a0";
    }

  if (RegX11 < regCount)
    {
      nameToNumber_["a1"] = RegX11;
      numberToName_[RegX11] = "a1";
    }

  if (RegX12 < regCount)
    {
      nameToNumber_["a2"] = RegX12;
      numberToName_[RegX12] = "a2";
    }

  if (RegX13 < regCount)
    {
      nameToNumber_["a3"] = RegX13;
      numberToName_[RegX13] = "a3";
    }

  if (RegX14 < regCount)
    {
      nameToNumber_["a4"] = RegX14;
      numberToName_[RegX14] = "a4";
    }

  if (RegX15 < regCount)
    {
      nameToNumber_["a5"] = RegX15;
      numberToName_[RegX15] = "a5";
    }

  if (RegX16 < regCount)
    {
      nameToNumber_["a6"] = RegX16;
      numberToName_[RegX16] = "a6";
    }

  if (RegX17 < regCount)
    {
      nameToNumber_["a7"] = RegX17;
      numberToName_[RegX17] = "a7";
    }

  if (RegX18 < regCount)
    {
      nameToNumber_["s2"] = RegX18;
      numberToName_[RegX18] = "s2";
    }

  if (RegX19 < regCount)
    {
      nameToNumber_["s3"] = RegX19;
      numberToName_[RegX19] = "s3";
    }

  if (RegX20 < regCount)
    {
      nameToNumber_["s4"] = RegX20;
      numberToName_[RegX20] = "s4";
    }

  if (RegX21 < regCount)
    {
      nameToNumber_["s5"] = RegX21;
      numberToName_[RegX21] = "s5";
    }

  if (RegX22 < regCount)
    {
      nameToNumber_["s6"] = RegX22;
      numberToName_[RegX22] = "s6";
    }

  if (RegX23 < regCount)
    {
      nameToNumber_["s7"] = RegX23;
      numberToName_[RegX23] = "s7";
    }

  if (RegX24 < regCount)
    {
      nameToNumber_["s8"] = RegX24;
      numberToName_[RegX24] = "s8";
    }

  if (RegX25 < regCount)
    {
      nameToNumber_["s9"] = RegX25;
      numberToName_[RegX25] = "s9";
    }

  if (RegX26 < regCount)
    {
      nameToNumber_["s10"] = RegX26;
      numberToName_[RegX26] = "s10";
    }

  if (RegX27 < regCount)
    {
      nameToNumber_["s11"] = RegX27;
      numberToName_[RegX27] = "s11";
    }

  if (RegX28 < regCount)
    {
      nameToNumber_["t3"] = RegX28;
      numberToName_[RegX28] = "t3";
    }

  if (RegX29 < regCount)
    {
      nameToNumber_["t4"] = RegX29;
      numberToName_[RegX29] = "t4";
    }

  if (RegX30 < regCount)
    {
      nameToNumber_["t5"] = RegX30;
      numberToName_[RegX30] = "t5";
    }

  if (RegX31 < regCount)
    {
      nameToNumber_["t6"] = RegX31;
      numberToName_[RegX31] = "t6";
    }
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
