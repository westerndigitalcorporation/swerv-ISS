#include "IntRegs.hpp"


using namespace WdRiscv;


template <typename URV>
IntRegs<URV>::IntRegs(unsigned regCount)
  : regs_(regCount, 0)
{
  numberToName_.resize(32);

  for (unsigned ix = 0; ix < 32; ++ix)
    {
      std::string name = "x" + std::to_string(ix);
      nameToNumber_[name] = IntRegNumber(ix);
      numberToName_[ix] = name;
    }

  numberToAbiName_ = { "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
		       "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
		       "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
		       "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };

  for (unsigned ix = 0; ix < 32; ++ix)
    {
      std::string abiName = numberToAbiName_.at(ix);
      nameToNumber_[abiName] = IntRegNumber(ix);
    }

  nameToNumber_["fp"] = RegX8;   // Fp, s0 and x8 name the same reg.
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
