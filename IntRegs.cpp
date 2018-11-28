//
// SPDX-License-Identifier: GPL-3.0-or-later Copyright 2018 Western Digital
// Corporation or its affiliates.
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
// 
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//

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
