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

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <type_traits>
#include <assert.h>

namespace WdRiscv
{

  /// Symbolic names for performance events.
  enum class EventNumber
    {
      None,
      ClockActive,       // 1:  Cycles clock active
      ICacheHits,        // 2:  Instruction cache hits
      ICacheMisses,      // 3:  Instruction cache misses

      InstCommited,      // 4:  Instructions committed
      Inst16Commited,    // 5:  16-bit instructions committed
      Inst32Commited,    // 6:  32-bit instructions committed
      InstAligned,       // 7   4-byte aligned instructions

      InstDecode,        // 8:  Instructions decoded

      Mult,              // 9:  Multiply instructions committed
      Div,               // 10: Divide  instructions committed
      Load,              // 11: Loads committed
      Store,             // 12: stores committed
      MisalignLoad,      // 13: misaligned loads
      MisalignStore,     // 14: misaligned stores
      Alu,               // 15: alu instructions committed
      CsrRead,           // 16: Csr read instructions committed
      CsrReadWrite,      // 17: Csr read/write instructions committed
      CsrWrite,          // 18: Csr write instructions committed
      Ebreak,            // 19: Ebreak instructions committed
      Ecall,             // 20: Ecall instructions committed
      Fence,             // 21: Fence instructions committed
      Fencei,            // 22: Fence.i instructions committed
      Mret,              // 23: Mret instructions committed
      Branch,            // 24: Branch instructions committed

      BranchMiss,        // 25: Mis-predicted branches

      BranchTaken,       // 26: Taken branches

      BranchUnpredict,   // 27: Unpredictable branches
      FetchStall,        // 28: Fetcher stall cycles
      AlignStall,        // 29: Aligner stall cycles
      DecodeStall,       // 30: Decoder stall cycles
      PostSyncStall,     // 31: Post sync stall cycles
      PreSynchStall,     // 32: Pre sync stall cycles
      PipeFrozen,        // 33: Cycles pipeline is frozen
      StoreStall,        // 34: LSU store stalls cycles
      DmaDccmStall,      // 35: DMA DCCM stall cycles
      DmaIccmStall,      // 36: DMA ICCM stall cycles

      Exception,         // 37: Exception count

      TimerInterrupt,    // 38: Timer interrupts

      ExternalInterrupt, // 39: External interrupts

      TluFlush,          // 40: TLU flushes (flush lower) 
      TluFlushError,     // 41: Branch error flushes
      BusFetch,          // 42: Fetch bus transactions
      BustLdSt,          // 43: Load/store bus transactions
      BusMisalign,       // 44: Misaligned load/store bus transactions
      IbusError,         // 45: I-bus errors
      DbusError,         // 46: D-bus errors
      IbusBusy,          // 47: Cycles stalled due to Ibus busy 
      DbusBusy,          // 48: Cycles stalled due to Dbus busy 
      InetrruptDisabled, // 49: Cycles interrupts disabled 
      InterrutpStall,    // 50: Cycles interrupts stalled while disabled
      Atomic,            // 51: Cycles interrupts stalled while disabled
      Lr,                // 52: Load-reserve instruction
      Sc,                // 53: Store-conditional instruction
      _End               // 54: Non-event serving as count of events
    };


  template <typename URV>
  class CsRegs;

  template <typename URV>
  class Core;


  /// Model a set of consecutive performance counters. Theses
  /// correspond to a set of consecutive performance counter CSR.
  class PerfRegs
  {
  public:

    friend class Core<uint32_t>;
    friend class Core<uint64_t>;
    friend class CsRegs<uint32_t>;
    friend class CsRegs<uint64_t>;

    /// Define numCounters counters. These correspond to mhp
    PerfRegs(unsigned numCounters = 0);

    /// Configure numCounters counters initialized to zero.  This
    /// should not be used if some CSR registers are tied to the
    /// counters in here.
    void config(unsigned numCounters);

    /// Update (count-up) all the performance counters currently
    /// associated with the given event.
    bool updateCounters(EventNumber event)
    {
      size_t eventIx = size_t(event);
      if (eventIx >= countersOfEvent_.size())
	return false;
      const auto& counterIndices = countersOfEvent_.at(eventIx);
      for (auto counterIx : counterIndices)
	{
	  counters_.at(counterIx)++;
	  modified_.at(counterIx) = true;
	}
      return true;
    }

    /// Associate given event number with given counter.
    /// Subsequent calls to updatePerofrmanceCounters(en) will cause
    /// given counter to count up by 1. Return true on success. Return
    /// false if counter number is out of bounds.
    bool assignEventToCounter(EventNumber event, unsigned counter);

  protected:

    /// Unmark registers marked as modified by current instruction. This
    /// is done at the end of each instruction.
    void clearModified()
    {
      for (auto& m : modified_)
	m = false;
    }

    /// Return true if given number corresponds to a valid performance
    /// counter and if that counter was modified by the current
    /// instruction.
    bool isModified(unsigned ix)
    {
      if (ix < modified_.size()) return modified_[ix];
      return false;
    }

  private:

    // Map counter index to event currently associated with counter.
    std::vector<EventNumber> eventOfCounter_;

    // Map an event number to a vector containing the indices of the
    // counters currently associated with that event.
    std::vector< std::vector<unsigned> > countersOfEvent_;

    std::vector<uint64_t> counters_;
    std::vector<unsigned> modified_;
  };
}
