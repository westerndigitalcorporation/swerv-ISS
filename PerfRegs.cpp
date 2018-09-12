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

