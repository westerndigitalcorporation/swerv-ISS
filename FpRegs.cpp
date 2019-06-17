//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2018 Western Digital Corporation or its affiliates.
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

#include "FpRegs.hpp"


using namespace WdRiscv;


template <typename FRV>
FpRegs<FRV>::FpRegs(unsigned regCount)
  : regs_(regCount, 0)
{
  numberToName_.resize(32);

  for (unsigned ix = 0; ix < 32; ++ix)
    {
      std::string name = "f" + std::to_string(ix);
      nameToNumber_[name] = FpRegNumber(ix);
      numberToName_[ix] = name;
    }

  numberToAbiName_ = { "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
		       "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
		       "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
		       "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft0", "ft11" };

  for (unsigned ix = 0; ix < 32; ++ix)
    {
      std::string abiName = numberToAbiName_.at(ix);
      nameToNumber_[abiName] = FpRegNumber(ix);
    }
}


template <typename FRV>
bool
FpRegs<FRV>::findReg(const std::string& name, unsigned& ix) const
{
  const auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  ix = iter->second;
  return true;
}


template class WdRiscv::FpRegs<float>;
template class WdRiscv::FpRegs<double>;
