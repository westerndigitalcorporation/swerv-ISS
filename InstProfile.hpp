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

#pragma once

#include <vector>
#include <unordered_map>
#include "InstId.hpp"


namespace WdRiscv
{

  struct InstProfile
  {
    InstId id_ = InstId::illegal;
    uint64_t freq_ = 0;  // Number of times instruction was executed

    // One entry per interger register: Count of times register was used
    // by instruction as rd.
    std::vector<uint64_t> rd_;

    // One entry per interger register: Count of times register was
    // used by instruction as rs1.
    std::vector<uint64_t> rs1_;

    // One entry per interger register: Count of times register was
    // used by instruction as rs2.
    std::vector<uint64_t> rs2_;

    std::vector<uint64_t> rs1Histo_;  // rs1 value historgram.
    std::vector<uint64_t> rs2Histo_;  // rs2 value historgram.
    std::vector<uint64_t> immHisto_;  // Immediate value historgram.

    bool hasImm_ = false;
    int32_t minImm_ = 0;  // Minumum immediate operand value.
    int32_t maxImm_ = 0;  // Maximum immediage operand value.
  };
}


    
