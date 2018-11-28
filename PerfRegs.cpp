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

#include <algorithm>
#include "PerfRegs.hpp"


using namespace WdRiscv;


PerfRegs::PerfRegs(unsigned numCounters)
{
  // 29 counters: MHPMCOUNTER3 to MHPMCOUNTER31
  counters_.resize(29);

  config(numCounters);
}


void
PerfRegs::config(unsigned numCounters)
{
  assert(numCounters < counters_.size());

  eventOfCounter_.resize(numCounters);

  unsigned numEvents = unsigned(EventNumber::_End);
  countersOfEvent_.resize(numEvents);
  modified_.resize(numEvents);
}


bool
PerfRegs::assignEventToCounter(EventNumber event, unsigned counter)
{
  if (counter >= eventOfCounter_.size())
    return false;

  if (size_t(event) >= countersOfEvent_.size())
    return false;

  // Disassociate counter from its previous event.
  EventNumber prevEvent = eventOfCounter_.at(counter);
  if (prevEvent != EventNumber::None)
    {
      auto& vec = countersOfEvent_.at(size_t(prevEvent));
      vec.erase(std::remove(vec.begin(), vec.end(), counter), vec.end());
    }

  if (event != EventNumber::None)
    countersOfEvent_.at(size_t(event)).push_back(counter);

  eventOfCounter_.at(counter) = event;
  return true;
}

