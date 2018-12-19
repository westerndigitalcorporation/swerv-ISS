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

#include <iomanip>
#include <iostream>
#include <sstream>
#include <cfenv>
#include <cmath>
#include <map>
#include <boost/format.hpp>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <assert.h>
#include <signal.h>
#include "Core.hpp"
#include "instforms.hpp"

using namespace WdRiscv;


template <typename TYPE>
static
bool
parseNumber(const std::string& numberStr, TYPE& number)
{
  bool good = not numberStr.empty();

  if (good)
    {
      char* end = nullptr;
      if (sizeof(TYPE) == 4)
	number = strtoul(numberStr.c_str(), &end, 0);
      else if (sizeof(TYPE) == 8)
	number = strtoull(numberStr.c_str(), &end, 0);
      else
	{
	  std::cerr << "parseNumber: Only 32/64-bit RISCV cores supported\n";
	  return false;
	}
      if (end and *end)
	good = false;  // Part of the string are non parseable.
    }
  return good;
}


template <typename URV>
Core<URV>::Core(unsigned hartId, size_t memorySize, unsigned intRegCount)
  : hartId_(hartId), memory_(memorySize), intRegs_(intRegCount), fpRegs_(32)
{
  regionHasLocalMem_.resize(16);

  // Tie the retired instruction and cycle counter CSRs to variable
  // held in the core.
  if constexpr (sizeof(URV) == 4)
    {
      URV* low = reinterpret_cast<URV*> (&retiredInsts_);
      URV* high = low + 1;

      auto& mirLow = csRegs_.regs_.at(size_t(CsrNumber::MINSTRET));
      mirLow.tie(low);

      auto& mirHigh = csRegs_.regs_.at(size_t(CsrNumber::MINSTRETH));
      mirHigh.tie(high);

      low = reinterpret_cast<URV*> (&cycleCount_);
      high = low + 1;

      auto& mcycleLow = csRegs_.regs_.at(size_t(CsrNumber::MCYCLE));
      mcycleLow.tie(low);

      auto& mcycleHigh = csRegs_.regs_.at(size_t(CsrNumber::MCYCLEH));
      mcycleHigh.tie(high);
    }
  else
    {
      csRegs_.regs_.at(size_t(CsrNumber::MINSTRET)).tie(&retiredInsts_);
      csRegs_.regs_.at(size_t(CsrNumber::MCYCLE)).tie(&cycleCount_);
    }
}


template <typename URV>
Core<URV>::~Core()
{
}


template <typename URV>
void
Core<URV>::reset()
{
  intRegs_.reset();
  csRegs_.reset();

  // Suppress resetting memory mapped register on initial resets sent
  // by the test bench. Otherwise, initial resets obliterate memory
  // mapped register data loaded from the ELF file.
  if (counter_ > 0)
    memory_.resetMemoryMappedRegisters();

  clearTraceData();
  clearPendingNmi();

  storeQueue_.clear();
  loadQueue_.clear();

  pc_ = resetPc_;
  currPc_ = resetPc_;

  // Enable M (multiply/divide) and C (compressed-instruction), F
  // (single precision floating point) and D (double precision
  // floating point) extensions if corresponding bits are set in the
  // MISA CSR.  D requires F and is enabled only if F is enabled.
  rvm_ = false;
  rvc_ = false;

  URV value = 0;
  if (peekCsr(CsrNumber::MISA, value))
    {
      if (value & 1)    // Atomic ('a') option.
	rva_ = true;

      if (value & (URV(1) << ('c' - 'a')))  // Compress option.
	rvc_ = true;

      if (value & (URV(1) << ('f' - 'a')))  // Single precision FP
	{
	  rvf_ = true;

	  bool isDebug = false;

	  // Make sure FCSR/FRM/FFLAGS are enabled if F extension is on.
	  if (not csRegs_.getImplementedCsr(CsrNumber::FCSR))
	    csRegs_.configCsr("fcsr", true, 0, 0xff, 0xff, isDebug);
	  if (not csRegs_.getImplementedCsr(CsrNumber::FRM))
	    csRegs_.configCsr("frm", true, 0, 0x7, 0x7, isDebug);
	  if (not csRegs_.getImplementedCsr(CsrNumber::FFLAGS))
	    csRegs_.configCsr("fflags", true, 0, 0x1f, 0x1f, isDebug);
	}

      if (value & (URV(1) << ('d' - 'a')))  // Double precision FP.
	{
	  if (rvf_)
	    rvd_ = true;
	  else
	    std::cerr << "Bit 3 (d) is set in the MISA register but f "
		      << "extension (bit 5) is not enabled -- ignored\n";
	}

      if (not (value & (URV(1) << ('i' - 'a'))))
	std::cerr << "Bit 8 (i extension) is cleared in the MISA register "
		  << " but extension is mandatory -- assuming bit 8 set\n";

      if (value & (URV(1) << ('m' - 'a')))  // Multiply/divide option.
	rvm_ = true;

      if (value & (URV(1) << ('u' - 'a')))  // User-mode option.
	rvu_ = true;

      if (value & (URV(1) << ('s' - 'a')))  // Supervisor-mode option.
	rvs_ = true;

      for (auto ec : { 'b', 'e', 'g', 'h', 'j', 'k', 'l', 'n', 'o', 'p',
	    'q', 'r', 't', 'v', 'w', 'x', 'y', 'z' } )
	{
	  unsigned bit = ec - 'a';
	  if (value & (URV(1) << bit))
	    std::cerr << "Bit " << bit << " (" << ec << ") set in the MISA "
		      << "register but extension is not supported "
		      << "-- ignored\n";
	}
    }
  
  prevCountersCsrOn_ = true;
  countersCsrOn_ = true;
  if (peekCsr(CsrNumber::MGPMC, value))
    {
      countersCsrOn_ = (value & 1) == 1;
      prevCountersCsrOn_ = countersCsrOn_;
    }

  debugStep_ = false;
  debugStepIe_ = false;
  debugMode_ = false;

  if (csRegs_.peek(CsrNumber::DCSR, value))
    {
      debugStep_ = (value >> 2) & 1;
      debugStepIe_ = (value >> 11) & 1;
    }
}


template <typename URV>
bool
Core<URV>::loadHexFile(const std::string& file)
{
  return memory_.loadHexFile(file);
}


template <typename URV>
bool
Core<URV>::loadElfFile(const std::string& file, size_t& entryPoint,
		       size_t& exitPoint,
		       std::unordered_map<std::string, ElfSymbol >& symbols)
{
  return memory_.loadElfFile(file, entryPoint, exitPoint, symbols);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint8_t& val) const
{
  return memory_.readByte(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint16_t& val) const
{
  if (memory_.readHalfWord(address, val))
    return true;

  // We may have failed because location is in instruction space.
  return memory_.readInstHalfWord(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint32_t& val) const
{
  if (memory_.readWord(address, val))
    return true;

  // We may have failed because location is in instruction space.
  return memory_.readInstWord(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint64_t& val) const
{
  uint32_t high = 0, low = 0;

  if (memory_.readWord(address, low) and memory_.readWord(address + 4, high))
    {
      val = (uint64_t(high) << 32) | low;
      return true;
    }

  // We may have failed because location is in instruction space.
  if (memory_.readInstWord(address, low) and
      memory_.readInstWord(address + 4, high))
    {
      val = (uint64_t(high) << 32) | low;
      return true;
    }

  return true;
}


template <typename URV>
bool
Core<URV>::pokeMemory(size_t address, uint8_t val)
{
  return memory_.pokeByte(address, val);
}


template <typename URV>
bool
Core<URV>::pokeMemory(size_t address, uint16_t val)
{
  return memory_.poke(address, val);
}


template <typename URV>
bool
Core<URV>::pokeMemory(size_t address, uint32_t val)
{
  // We allow poke to bypass masking for memory mapped registers
  // otherwise, there is no way for external driver to clear bits that
  // are read-only to this core.
  return memory_.poke(address, val);
}


template <typename URV>
bool
Core<URV>::pokeMemory(size_t address, uint64_t val)
{
  return memory_.poke(address, val);
}


template <typename URV>
void
Core<URV>::setPendingNmi(NmiCause cause)
{
  nmiPending_ = true;

  if (nmiCause_ == NmiCause::STORE_EXCEPTION or
      nmiCause_ == NmiCause::LOAD_EXCEPTION)
    ;  // Load/store exception is sticky -- do not over-write it.
  else
    nmiCause_ = cause;

  URV val = 0;  // DCSR value
  if (peekCsr(CsrNumber::DCSR, val))
    {
      val |= 1 << 3;  // nmip bit
      pokeCsr(CsrNumber::DCSR, val);
      recordCsrWrite(CsrNumber::DCSR);
    }
}


template <typename URV>
void
Core<URV>::clearPendingNmi()
{
  nmiPending_ = false;
  nmiCause_ = NmiCause::UNKNOWN;

  URV val = 0;  // DCSR value
  if (peekCsr(CsrNumber::DCSR, val))
    {
      val &= ~(URV(1) << 3);  // nmip bit
      pokeCsr(CsrNumber::DCSR, val);
      recordCsrWrite(CsrNumber::DCSR);
    }
}


template <typename URV>
void
Core<URV>::setToHostAddress(size_t address)
{
  toHost_ = address;
  toHostValid_ = true;
}


template <typename URV>
void
Core<URV>::clearToHostAddress()
{
  toHost_ = 0;
  toHostValid_ = false;
}


template <typename URV>
void
Core<URV>::putInStoreQueue(unsigned size, size_t addr, uint64_t data,
			   uint64_t prevData)
{
  if (maxStoreQueueSize_ == 0 or memory_.isLastWriteToDccm())
    return;

  if (storeQueue_.size() >= maxStoreQueueSize_)
    {
      for (size_t i = 1; i < maxStoreQueueSize_; ++i)
	storeQueue_[i-1] = storeQueue_[i];
      storeQueue_[maxStoreQueueSize_-1] = StoreInfo(size, addr, data,
						    prevData);
    }
  else
    storeQueue_.push_back(StoreInfo(size, addr, data, prevData));
}


template <typename URV>
void
Core<URV>::putInLoadQueue(unsigned size, size_t addr, unsigned regIx,
			  uint64_t data)
{
  if (not loadQueueEnabled_)
    return;

  if (memory_.isAddrInDccm(addr))
    {
      // Blocking load. Invalidate target register in load queue so
      // that it will not be reverted.
      invalidateInLoadQueue(regIx);
      return;
    }

  if (loadQueue_.size() >= maxLoadQueueSize_)
    {
      for (size_t i = 1; i < maxLoadQueueSize_; ++i)
	loadQueue_[i-1] = loadQueue_[i];
      loadQueue_[maxLoadQueueSize_-1] = LoadInfo(size, addr, regIx, data);
    }
  else
    loadQueue_.push_back(LoadInfo(size, addr, regIx, data));
}


template <typename URV>
void
Core<URV>::invalidateInLoadQueue(unsigned regIx)
{
  // Replace entry containing target register with x0 so that load exception
  // matching entry will not revert target register.
  for (unsigned i = 0; i < loadQueue_.size(); ++i)
    if (loadQueue_[i].regIx_ == regIx)
      loadQueue_[i].regIx_ = RegX0;
}


template <typename URV>
void
Core<URV>::removeFromLoadQueue(unsigned regIx)
{
  if (regIx == 0)
    return;

  size_t newSize = 0;

  for (size_t i = 0; i < loadQueue_.size(); ++i)
    {
      if (loadQueue_[i].regIx_ == regIx)
	continue;
      if (i != newSize)
	loadQueue_[newSize] = loadQueue_[i];
      newSize++;
    }

  loadQueue_.resize(newSize);
}


template <typename URV>
inline
void
Core<URV>::execBeq(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  if (intRegs_.read(rs1) != intRegs_.read(rs2))
    return;
  pc_ = currPc_ + SRV(offset);
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  lastBranchTaken_ = true;
}


template <typename URV>
inline
void
Core<URV>::execBne(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  if (intRegs_.read(rs1) == intRegs_.read(rs2))
    return;
  pc_ = currPc_ + SRV(offset);
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  lastBranchTaken_ = true;
}


template <typename URV>
inline
void
Core<URV>::execAddi(uint32_t rd, uint32_t rs1, int32_t imm)
{
  SRV v = intRegs_.read(rs1) + SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
inline
void
Core<URV>::execAdd(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) + intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
inline
void
Core<URV>::execAndi(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) & SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
bool
Core<URV>::isIdempotentRegion(size_t addr) const
{
  unsigned region = addr >> (sizeof(URV)*8 - 4);
  URV mracVal = 0;
  if (csRegs_.read(CsrNumber::MRAC, PrivilegeMode::Machine, debugMode_,
		   mracVal))
    {
      unsigned bit = (mracVal >> (region*2 + 1)) & 1;
      return bit == 0  or regionHasLocalMem_.at(region);
    }
  return true;
}


template <typename URV>
bool
Core<URV>::applyStoreException(URV addr, unsigned& matches)
{
  bool prevLocked = csRegs_.mdseacLocked();
  if (not prevLocked)
    {
      pokeCsr(CsrNumber::MDSEAC, addr); // MDSEAC is read only: Poke it.
      csRegs_.lockMdseac(true);
      setPendingNmi(NmiCause::STORE_EXCEPTION);
    }
  recordCsrWrite(CsrNumber::MDSEAC); // Always record change (per Ajay Nath)

  if (not storeErrorRollback_)
    {
      matches = 1;
      return true;
    }

  matches = 0;

  for (const auto& entry : storeQueue_)
    if (entry.size_ > 0 and addr >= entry.addr_ and
	addr < entry.addr_ + entry.size_)
      matches++;

  if (matches != 1)
    {
      std::cerr << "Error: Store exception at 0x" << std::hex << addr;
      if (matches == 0)
	std::cerr << " does not match any address in the store queue\n";
      else
	std::cerr << " matches " << std::dec << matches << " entries"
		  << " in the store queue\n";
      return false;
    }

  // Undo matching item and remove it from queue (or replace with
  // portion crossing double-word boundary). Restore previous
  // bytes up to a double-word boundary.
  bool hit = false; // True when address is found.
  size_t undoBegin = addr, undoEnd = 0;
  size_t removeIx = storeQueue_.size();
  for (size_t ix = 0; ix < storeQueue_.size(); ++ix)
    {
      auto& entry = storeQueue_.at(ix);

      size_t entryEnd = entry.addr_ + entry.size_;
      if (hit)
	{
	  // Re-play portions of subsequent (to one with exception)
	  // transactions covering undone bytes.
	  uint64_t data = entry.newData_;
	  for (size_t ba = entry.addr_; ba < entryEnd; ++ba, data >>= 8)
	    if (ba >= undoBegin and ba < undoEnd)
	      pokeMemory(ba, uint8_t(data));
	}
      else if (addr >= entry.addr_ and addr < entryEnd)
	{
	  uint64_t prevData = entry.prevData_, newData = entry.newData_;
	  hit = true;
	  removeIx = ix;
	  size_t offset = addr - entry.addr_;
	  prevData >>= offset*8; newData >>= offset*8;
	  for (size_t i = offset; i < entry.size_; ++i)
	    {
	      pokeMemory(addr++, uint8_t(prevData));
	      prevData >>= 8; newData >>= 8;
	      undoEnd = addr;
	      if ((addr & 7) != 0)
		continue;  // Keep undoing store till double word boundary
	      // Reached double word boundary: trim & keep rest of store record
	      if (i + 1 < entry.size_)
		{
		  entry = StoreInfo(entry.size_-i-1, addr, newData, prevData);
		  removeIx = storeQueue_.size(); // Squash entry removal.
		  break;
		}
	    }
	}
    }

  if (removeIx < storeQueue_.size())
    {
      for (size_t i = removeIx + 1; i < storeQueue_.size(); ++i)
	storeQueue_.at(i-1) = storeQueue_.at(i);
      storeQueue_.resize(storeQueue_.size() - 1);
    }

  return true;
}


template <typename URV>
bool
Core<URV>::applyLoadException(URV addr, unsigned& matches)
{
  bool prevLocked = csRegs_.mdseacLocked();

  if (not prevLocked)
    {
      pokeCsr(CsrNumber::MDSEAC, addr); // MDSEAC is read only: Poke it.
      csRegs_.lockMdseac(true);
      setPendingNmi(NmiCause::LOAD_EXCEPTION);
    }
  recordCsrWrite(CsrNumber::MDSEAC);  // Always record change (per Ajay Nath)

  if (not loadErrorRollback_)
    {
      matches = 1;
      return true;
    }

  // Count matching records. Determine if there is an entry with the
  // same register as the first match but younger.
  bool hasYounger = false;
  unsigned targetReg = 0;  // Register of 1st match.
  matches = 0;
  unsigned zMatches = 0;  // Matching records where target register is zero.
  for (const LoadInfo& li : loadQueue_)
    {
      if (matches and targetReg == li.regIx_)
	hasYounger = true;

      if (li.size_ > 0 and addr >= li.addr_ and addr < li.addr_ + li.size_)
	{
	  if (li.regIx_ != 0)
	    {
	      targetReg = li.regIx_;
	      matches++;
	    }
	  else
	    zMatches++;
	}
    }

  if (matches != 1)
    {
      if (zMatches)
	{
	  matches = 1;
	  return true;
	}

      std::cerr << "Error: Load exception at 0x" << std::hex << addr;
      if (matches == 0)
	std::cerr << " does not match any address in the load queue\n";
      else
	std::cerr << " matches " << std::dec << matches << " entries"
		  << " in the load queue\n";
      return false;
    }

  // Revert register of matching item unless there are younger entries
  // with same resister, update prev-data of 1st older item with same
  // target register, and remove item from queue.
  size_t removeIx = loadQueue_.size();
  for (size_t ix = 0; ix < loadQueue_.size(); ++ix)
    {
      auto& entry = loadQueue_.at(ix);
      size_t entryEnd = entry.addr_ + entry.size_;
      bool match = entry.regIx_ and addr >= entry.addr_ and addr < entryEnd;
      if (not match)
	continue;

      removeIx = ix;
      if (not hasYounger)
	pokeIntReg(entry.regIx_, entry.prevData_);

      for (size_t ix2 = removeIx + 1; ix2 < loadQueue_.size(); ++ix2)
	{
	  auto& entry2 = loadQueue_.at(ix2);
	  if (entry2.regIx_ == entry.regIx_)
	    {
	      entry2.prevData_ = entry.prevData_;
	      break;
	    }
	}
      break;
    }

  if (removeIx < loadQueue_.size())
    {
      for (size_t i = removeIx + 1; i < loadQueue_.size(); ++i)
	loadQueue_.at(i-1) = loadQueue_.at(i);
      loadQueue_.resize(loadQueue_.size() - 1);
    }

  return true;
}


template <typename URV>
bool
Core<URV>::applyLoadFinished(URV addr, unsigned& matches)
{
  matches = 0;
  size_t size = loadQueue_.size();
  for (size_t i = 0; i < size; ++i)
    {
      if (addr == loadQueue_.at(i).addr_)
	{
	  matches = 1;

	  // Remove entry from queue.
	  for (size_t j = i + 1; j < size; ++j)
	    loadQueue_.at(j-1) = loadQueue_.at(j);
	  loadQueue_.resize(size - 1);

	  return true;
	}
    }

  return false;
}


static
void
printUnsignedHisto(const char* tag, const std::vector<uint64_t>& histo,
		   FILE* file)
{
  if (histo.size() < 7)
    return;

  if (histo.at(0))
    fprintf(file, "    %s  0          %ld\n", tag, histo.at(0));
  if (histo.at(1))
    fprintf(file, "    %s  1          %ld\n", tag, histo.at(1));
  if (histo.at(2))
    fprintf(file, "    %s  2          %ld\n", tag, histo.at(2));
  if (histo.at(3))
    fprintf(file, "    %s  (2,   16]  %ld\n", tag, histo.at(3));
  if (histo.at(4))
    fprintf(file, "    %s  (16,  1k]  %ld\n", tag, histo.at(4));
  if (histo.at(5))
    fprintf(file, "    %s  (1k, 64k]  %ld\n", tag, histo.at(5));
  if (histo.at(6))
    fprintf(file, "    %s  > 64k      %ld\n", tag, histo.at(6));
}


static
void
printSignedHisto(const char* tag, const std::vector<uint64_t>& histo,
		 FILE* file)
{
  if (histo.size() < 13)
    return;

  if (histo.at(0))
    fprintf(file, "    %s <= 64k      %ld\n", tag, histo.at(0));
  if (histo.at(1))
    fprintf(file, "    %s (-64k, -1k] %ld\n", tag, histo.at(1));
  if (histo.at(2))
    fprintf(file, "    %s (-1k,  -16] %ld\n", tag, histo.at(2));
  if (histo.at(3))
    fprintf(file, "    %s (-16,   -3] %ld\n", tag, histo.at(3));
  if (histo.at(4))
    fprintf(file, "    %s -2          %ld\n", tag, histo.at(4));
  if (histo.at(5))
    fprintf(file, "    %s -1          %ld\n", tag, histo.at(5));
  if (histo.at(6))
    fprintf(file, "    %s 0           %ld\n", tag, histo.at(6));
  if (histo.at(7))
    fprintf(file, "    %s 1           %ld\n", tag, histo.at(7));
  if (histo.at(8))
    fprintf(file, "    %s 2           %ld\n", tag, histo.at(8));
  if (histo.at(9))
    fprintf(file, "    %s (2,     16] %ld\n", tag, histo.at(9));
  if (histo.at(10))
    fprintf(file, "    %s (16,    1k] %ld\n", tag, histo.at(10));
  if (histo.at(11))	              
    fprintf(file, "    %s (1k,   64k] %ld\n", tag, histo.at(11));
  if (histo.at(12))	              
    fprintf(file, "    %s > 64k       %ld\n", tag, histo.at(12));
}


template <typename URV>
inline
void
Core<URV>::reportInstructionFrequency(FILE* file) const
{
  struct CompareFreq
  {
    CompareFreq(const std::vector<InstProfile>& profileVec)
      : profileVec(profileVec)
    { }

    bool operator()(size_t a, size_t b) const
    { return profileVec.at(a).freq_ < profileVec.at(b).freq_; }

    const std::vector<InstProfile>& profileVec;
  };

  std::vector<size_t> indices(instProfileVec_.size());
  for (size_t i = 0; i < indices.size(); ++i)
    indices.at(i) = i;
  std::sort(indices.begin(), indices.end(), CompareFreq(instProfileVec_));

  for (size_t i = 0; i < indices.size(); ++i)
    {
      size_t ix = indices.at(i);
      InstId id = InstId(ix);

      const InstInfo& info = instTable_.getInstInfo(id);
      const InstProfile& prof = instProfileVec_.at(ix);
      uint64_t freq = prof.freq_;
      if (not freq)
	continue;

      fprintf(file, "%s %ld\n", info.name().c_str(), freq);

      unsigned regCount = intRegCount();

      uint64_t count = 0;
      for (auto n : prof.rd_) count += n;
      if (count)
	{
	  fprintf(file, "  +rd");
	  for (unsigned i = 0; i < regCount; ++i)
	    if (prof.rd_.at(i))
	      fprintf(file, " %d:%ld", i, prof.rd_.at(i));
	  fprintf(file, "\n");
	}

      uint64_t count1 = 0;
      for (auto n : prof.rs1_) count1 += n;
      if (count1)
	{
	  fprintf(file, "  +rs1");
	  for (unsigned i = 0; i < regCount; ++i)
	    if (prof.rs1_.at(i))
	      fprintf(file, " %d:%ld", i, prof.rs1_.at(i));
	  fprintf(file, "\n");

	  const auto& histo = prof.rs1Histo_;
	  if (info.isUnsigned())
	    printUnsignedHisto("+hist1", histo, file);
	  else
	    printSignedHisto("+hist1", histo, file);
	}

      uint64_t count2 = 0;
      for (auto n : prof.rs2_) count2 += n;
      if (count2)
	{
	  fprintf(file, "  +rs2");
	  for (unsigned i = 0; i < regCount; ++i)
	    if (prof.rs2_.at(i))
	      fprintf(file, " %d:%ld", i, prof.rs2_.at(i));
	  fprintf(file, "\n");

	  const auto& histo = prof.rs2Histo_;
	  if (info.isUnsigned())
	    printUnsignedHisto("+hist2", histo, file);
	  else
	    printSignedHisto("+hist2", histo, file);
	}

      if (prof.hasImm_)
	{
	  fprintf(file, "  +imm  min:%d max:%d\n", prof.minImm_, prof.maxImm_);
	  printSignedHisto("+hist ", prof.immHisto_, file);
	}
    }
}


template <typename URV>
bool
Core<URV>::misalignedAccessCausesException(URV addr, unsigned accessSize) const
{
  size_t addr2 = addr + accessSize - 1;

  // Crossing region boundary causes misaligned exception.
  if (memory_.getRegionIndex(addr) != memory_.getRegionIndex(addr2))
    return true;

  // Misaligned access to a region with side effect causes misaligned
  // exception.
  if (not isIdempotentRegion(addr) or not isIdempotentRegion(addr2))
    return true;

  return false;
}


template <typename URV>
template <typename LOAD_TYPE>
void
Core<URV>::load(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV addr = intRegs_.read(rs1) + SRV(imm);

  loadAddr_ = addr;    // For reporting load addr in trace-mode.
  loadAddrValid_ = true;  // For reporting load addr in trace-mode.

  if (hasActiveTrigger())
    {
      typedef TriggerTiming Timing;

      bool isLoad = true;
      if (ldStAddrTriggerHit(addr, Timing::Before, isLoad, isInterruptEnabled()))
	triggerTripped_ = true;
      if (triggerTripped_)
	return;
    }

  // Unsigned version of LOAD_TYPE
  typedef typename std::make_unsigned<LOAD_TYPE>::type ULT;

  if constexpr (std::is_same<ULT, uint8_t>::value)
    {
      // Loading a byte from special address results in a byte read
      // from standard input.
      if (conIoValid_ and addr == conIo_)
	{
	  int c = fgetc(stdin);
	  SRV val = c;
	  intRegs_.write(rd, val);
	  return;
	}
    }

  // Misaligned load from io section triggers an exception. Crossing
  // dccm to non-dccm causes an exception.
  constexpr unsigned alignMask = sizeof(LOAD_TYPE) - 1;
  bool misaligned = addr & alignMask;
  misalignedLdSt_ = misaligned;
  if (misaligned)
    {
      if (misalignedAccessCausesException(addr, sizeof(LOAD_TYPE)))
	{
	  forceAccessFail_ = false;
	  ldStException_ = true;
	  initiateException(ExceptionCause::LOAD_ADDR_MISAL, currPc_, addr);
	  return;
	}
    }

  ULT uval = 0;
  if (memory_.read(addr, uval) and not forceAccessFail_)
    {
      URV value;
      if constexpr (std::is_same<ULT, LOAD_TYPE>::value)
        value = uval;
      else
        value = SRV(LOAD_TYPE(uval)); // Sign extend.

      if (loadQueueEnabled_)
	{
	  URV prev = 0;
	  peekIntReg(rd, prev);
	  putInLoadQueue(sizeof(LOAD_TYPE), addr, rd, prev);
	}

      intRegs_.write(rd, value);
    }
  else
    {
      forceAccessFail_ = false;
      ldStException_ = true;
      initiateException(ExceptionCause::LOAD_ACC_FAULT, currPc_, addr);
    }
}


template <typename URV>
inline
void
Core<URV>::execLw(uint32_t rd, uint32_t rs1, int32_t imm)
{
  load<int32_t>(rd, rs1, imm);
}


template <typename URV>
inline
void
Core<URV>::execLh(uint32_t rd, uint32_t rs1, int32_t imm)
{
  load<int16_t>(rd, rs1, imm);
}


template <typename URV>
inline
void
Core<URV>::execSw(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV addr = intRegs_.read(rs1) + SRV(imm);
  URV value = intRegs_.read(rs2);

  store<uint32_t>(addr, value);
}


template <typename URV>
bool
Core<URV>::readInst(size_t address, uint32_t& inst)
{
  inst = 0;

  uint16_t low;  // Low 2 bytes of instruction.
  if (not memory_.readInstHalfWord(address, low))
    return false;

  inst = low;

  if ((inst & 0x3) == 3)  // Non-compressed instruction.
    {
      uint16_t high;
      if (not memory_.readInstHalfWord(address + 2, high))
	return false;
      inst |= (uint32_t(high) << 16);
    }

  return true;
}


template <typename URV>
bool
Core<URV>::defineIccm(size_t region, size_t offset, size_t size)
{
  bool ok = memory_.defineIccm(region, offset, size);
  if (ok)
    regionHasLocalMem_.at(region) = true;
  return ok;
}
    

template <typename URV>
bool
Core<URV>::defineDccm(size_t region, size_t offset, size_t size)
{
  bool ok = memory_.defineDccm(region, offset, size);
  if (ok)
    regionHasLocalMem_.at(region) = true;
  return ok;
}


template <typename URV>
bool
Core<URV>::defineMemoryMappedRegisterRegion(size_t region, size_t offset,
					  size_t size)
{
  bool ok = memory_.defineMemoryMappedRegisterRegion(region, offset, size);
  if (ok)
    regionHasLocalMem_.at(region) = true;
  return ok;
}


template <typename URV>
bool
Core<URV>::defineMemoryMappedRegisterWriteMask(size_t region,
					       size_t regionOffset,
					       size_t registerBlockOffset,
					       size_t registerIx,
					       uint32_t mask)
{
  return memory_.defineMemoryMappedRegisterWriteMask(region, regionOffset,
						     registerBlockOffset,
						     registerIx, mask);
}


template <typename URV>
inline
bool
Core<URV>::fetchInst(size_t addr, uint32_t& inst)
{
  if (addr & 1)
    {
      initiateException(ExceptionCause::INST_ADDR_MISAL, addr, addr);
      return false;
    }

  if (memory_.readInstWord(addr, inst))
    return true;

  uint16_t half;
  if (not memory_.readInstHalfWord(addr, half))
    {
      initiateException(ExceptionCause::INST_ACC_FAULT, addr, addr);
      return false;
    }

  inst = half;
  if (isCompressedInst(inst))
    return true;

  // 4-byte instruction but 4-byte fetch failed.
  initiateException(ExceptionCause::INST_ACC_FAULT, addr, addr);
  return false;
}


template <typename URV>
bool
Core<URV>::fetchInstPostTrigger(size_t addr, uint32_t& inst, FILE* traceFile)
{
  // Fetch will fail if forced or if address is misaligned or if
  // memory read fails.
  if (forceFetchFail_)
    forceFetchFail_ = false;
  else if ((addr & 1) == 0)
    {
      if (memory_.readInstWord(addr, inst))
	return true;  // Read 4 bytes: success.

      uint16_t half;
      if (memory_.readInstHalfWord(addr, half))
	{
	  if (isCompressedInst(inst))
	    return true; // Read 2 bytes and compressed inst: success.
	}
    }

  // Fetch failed: take pending trigger-exception.
  takeTriggerAction(traceFile, addr, addr, counter_, true);
  return false;
}


template <typename URV>
void
Core<URV>::illegalInst()
{
  if (triggerTripped_)
    return;

  // Check if stuck because of lack of illegal instruction exception handler.
  if (counterAtLastIllegal_ + 1 == retiredInsts_)
    consecutiveIllegalCount_++;
  else
    consecutiveIllegalCount_ = 0;

  if (consecutiveIllegalCount_ > 64)  // FIX: Make a parameter
    {
      throw CoreException(CoreException::Stop,
			  "64 consecutive illegal instructions",
			  0, 0);
    }

  counterAtLastIllegal_ = retiredInsts_;

  uint32_t currInst;
  if (not readInst(currPc_, currInst))
    assert(0 and "Failed to re-read current instruction");

  initiateException(ExceptionCause::ILLEGAL_INST, currPc_, currInst);
}


template <typename URV>
void
Core<URV>::unimplemented()
{
  illegalInst();
}


// Start an asynchronous exception.
template <typename URV>
void
Core<URV>::initiateInterrupt(InterruptCause cause, URV pc)
{
  bool interrupt = true;
  URV info = 0;  // This goes into mtval.
  interruptCount_++;
  initiateTrap(interrupt, URV(cause), pc, info);

  PerfRegs& pregs = csRegs_.mPerfRegs_;
  if (cause == InterruptCause::M_EXTERNAL)
    pregs.updateCounters(EventNumber::ExternalInterrupt);
  else if (cause == InterruptCause::M_TIMER)
    pregs.updateCounters(EventNumber::TimerInterrupt);
}


// Start a synchronous exception.
template <typename URV>
void
Core<URV>::initiateException(ExceptionCause cause, URV pc, URV info)
{
  bool interrupt = false;
  exceptionCount_++;
  initiateTrap(interrupt, URV(cause), pc, info);

  PerfRegs& pregs = csRegs_.mPerfRegs_;
  pregs.updateCounters(EventNumber::Exception);
}


template <typename URV>
void
Core<URV>::initiateTrap(bool interrupt, URV cause, URV pcToSave, URV info)
{
  PrivilegeMode origMode = privMode_;

  // Exceptions are taken in machine mode.
  privMode_ = PrivilegeMode::Machine;
  PrivilegeMode nextMode = PrivilegeMode::Machine;

  // But they can be delegated. TBD: handle delegation to S/U modes
  // updating nextMode.

  CsrNumber epcNum = CsrNumber::MEPC;
  CsrNumber causeNum = CsrNumber::MCAUSE;
  CsrNumber tvalNum = CsrNumber::MTVAL;
  CsrNumber tvecNum = CsrNumber::MTVEC;

  if (nextMode == PrivilegeMode::Supervisor)
    {
      epcNum = CsrNumber::SEPC;
      causeNum = CsrNumber::SCAUSE;
      tvalNum = CsrNumber::STVAL;
      tvecNum = CsrNumber::STVEC;
    }
  else if (nextMode == PrivilegeMode::User)
    {
      epcNum = CsrNumber::UEPC;
      causeNum = CsrNumber::UCAUSE;
      tvalNum = CsrNumber::UTVAL;
      tvecNum = CsrNumber::UTVEC;
    }

  // Save address of instruction that caused the exception or address
  // of interrupted instruction.
  if (not csRegs_.write(epcNum, privMode_, debugMode_, pcToSave & ~(URV(1))))
    assert(0 and "Failed to write EPC register");

  // Save the exception cause.
  URV causeRegVal = cause;
  if (interrupt)
    causeRegVal |= 1 << (mxlen_ - 1);
  if (not csRegs_.write(causeNum, privMode_, debugMode_, causeRegVal))
    assert(0 and "Failed to write CAUSE register");

  // Clear mtval on interrupts. Save synchronous exception info.
  if (not csRegs_.write(tvalNum, privMode_, debugMode_, info))
    assert(0 and "Failed to write TVAL register");

  // Update status register saving xIE in xPIE and previous privilege
  // mode in xPP by getting current value of mstatus ...
  URV status = 0;
  if (not csRegs_.read(CsrNumber::MSTATUS, privMode_, debugMode_, status))
    assert(0 and "Failed to read MSTATUS register");

  // ... updating its fields
  MstatusFields<URV> msf(status);

  if (nextMode == PrivilegeMode::Machine)
    {
      msf.bits_.MPP = unsigned(origMode);
      msf.bits_.MPIE = msf.bits_.MIE;
      msf.bits_.MIE = 0;
    }
  else if (nextMode == PrivilegeMode::Supervisor)
    {
      msf.bits_.SPP = unsigned(origMode);
      msf.bits_.SPIE = msf.bits_.SIE;
      msf.bits_.SIE = 0;
    }
  else if (nextMode == PrivilegeMode::User)
    {
      msf.bits_.UPIE = msf.bits_.UIE;
      msf.bits_.UIE = 0;
    }

  // ... and putting it back
  if (not csRegs_.write(CsrNumber::MSTATUS, privMode_, debugMode_, msf.value_))
    assert(0 and "Failed to write MSTATUS register");
  
  // Set program counter to trap handler address.
  URV tvec = 0;
  if (not csRegs_.read(tvecNum, privMode_, debugMode_, tvec))
    assert(0 and "Failed to read TVEC register");

  URV base = (tvec >> 2) << 2;  // Clear least sig 2 bits.
  unsigned tvecMode = tvec & 0x3;

  if (tvecMode == 1 and interrupt)
    base = base + 4*cause;

  pc_ = (base >> 1) << 1;  // Clear least sig bit

  // Change privilege mode.
  privMode_ = nextMode;
}


template <typename URV>
void
Core<URV>::initiateNmi(URV cause, URV pcToSave)
{
  PrivilegeMode origMode = privMode_;

  // NMI is taken in machine mode.
  privMode_ = PrivilegeMode::Machine;

  // Save address of instruction that caused the exception or address
  // of interrupted instruction.
  if (not csRegs_.write(CsrNumber::MEPC, privMode_, debugMode_,
			pcToSave & ~(URV(1))))
    assert(0 and "Failed to write EPC register");

  // Save the exception cause.
  if (not csRegs_.write(CsrNumber::MCAUSE, privMode_, debugMode_, cause))
    assert(0 and "Failed to write CAUSE register");

  // Clear mtval
  if (not csRegs_.write(CsrNumber::MTVAL, privMode_, debugMode_, 0))
    assert(0 and "Failed to write MTVAL register");

  // Update status register saving xIE in xPIE and previous privilege
  // mode in xPP by getting current value of mstatus ...
  URV status = 0;
  if (not csRegs_.read(CsrNumber::MSTATUS, privMode_, debugMode_, status))
    assert(0 and "Failed to read MSTATUS register");
  // ... updating its fields
  MstatusFields<URV> msf(status);

  msf.bits_.MPP = unsigned(origMode);
  msf.bits_.MPIE = msf.bits_.MIE;
  msf.bits_.MIE = 0;

  // ... and putting it back
  if (not csRegs_.write(CsrNumber::MSTATUS, privMode_, debugMode_, msf.value_))
    assert(0 and "Failed to write MSTATUS register");
  
  // Clear pending nmi bit in dcsr
  URV dcsrVal = 0;
  if (peekCsr(CsrNumber::DCSR, dcsrVal))
    {
      dcsrVal &= ~(URV(1) << 3);
      pokeCsr(CsrNumber::DCSR, dcsrVal);
      recordCsrWrite(CsrNumber::DCSR);
    }

  pc_ = (nmiPc_ >> 1) << 1;  // Clear least sig bit
}


template <typename URV>
bool
Core<URV>::peekIntReg(unsigned ix, URV& val) const
{ 
  if (ix < intRegs_.size())
    {
      val = intRegs_.read(ix);
      return true;
    }
  return false;
}


template <typename URV>
bool
Core<URV>::peekIntReg(unsigned ix, URV& val, std::string& name) const
{ 
  if (ix < intRegs_.size())
    {
      val = intRegs_.read(ix);
      name = intRegs_.regName(ix, abiNames_);
      return true;
    }
  return false;
}


template <typename URV>
bool
Core<URV>::peekFpReg(unsigned ix, uint64_t& val) const
{ 
  if (not isRvf() and not isRvd())
    return false;

  if (ix < fpRegs_.size())
    {
      val = fpRegs_.readBits(ix);
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::pokeFpReg(unsigned ix, uint64_t val)
{ 
  if (not isRvf() and not isRvd())
    return false;

  if (ix < fpRegs_.size())
    {
      fpRegs_.pokeBits(ix, val);
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::pokeIntReg(unsigned ix, URV val)
{ 
  if (ix < intRegs_.size())
    {
      intRegs_.poke(ix, val);
      return true;
    }
  return false;
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val) const
{ 
  return csRegs_.peek(csrn, val);
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val, URV& reset, URV& writeMask,
		   URV& pokeMask) const
{ 
  const Csr<URV>* csr = csRegs_.getImplementedCsr(csrn);
  if (not csr)
    return false;

  if (not csRegs_.peek(csrn, val))
    return false;

  reset = csr->getResetValue();
  writeMask = csr->getWriteMask();
  pokeMask = csr->getPokeMask();
  return true;
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val, std::string& name) const
{ 
  const Csr<URV>* csr = csRegs_.getImplementedCsr(csrn);
  if (not csr)
    return false;

  if (not csRegs_.peek(csrn, val))
    return false;

  name = csr->getName();
  return true;
}


template <typename URV>
bool
Core<URV>::pokeCsr(CsrNumber csr, URV val)
{ 
  // Direct write to MEIHAP will not affect claimid field. Poking
  // MEIHAP will only affect the claimid field.
  if (csr == CsrNumber::MEIHAP)
    {
      URV claimIdMask = 0x3fc;
      URV prev = 0;
      if (not csRegs_.read(CsrNumber::MEIHAP, PrivilegeMode::Machine,
			   debugMode_, prev))
	return false;
      URV newVal = (prev & ~claimIdMask) | (val & claimIdMask);
      csRegs_.poke(CsrNumber::MEIHAP, newVal);
      return true;
    }

  // Some/all bits of some CSRs are read only to CSR instructions but
  // are modifiable. Use the poke method (instead of write) to make
  // sure modifiable value are changed.
  bool result = csRegs_.poke(csr, val);

  if (csr == CsrNumber::DCSR)
    {
      debugStep_ = (val >> 2) & 1;
      debugStepIe_ = (val >> 11) & 1;
    }
  else if (csr == CsrNumber::MGPMC)
    {
      URV value = 0;
      if (csRegs_.peek(CsrNumber::MGPMC, value))
	{
	  prevCountersCsrOn_ = countersCsrOn_;
	  countersCsrOn_ = (value & 1) == 1;
	}
    }

  return result;
}


template <typename URV>
URV
Core<URV>::peekPc() const
{
  return pc_;
}


template <typename URV>
void
Core<URV>::pokePc(URV address)
{
  pc_ = (address >> 1) << 1; // Clear least sig big
}


template <typename URV>
bool
Core<URV>::findIntReg(const std::string& name, unsigned& num) const
{
  if (intRegs_.findReg(name, num))
    return true;

  unsigned n = 0;
  if (parseNumber<unsigned>(name, n) and n < intRegs_.size())
    {
      num = n;
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::findFpReg(const std::string& name, unsigned& num) const
{
  if (not isRvf())
    return false;   // Floating point extension not enabled.

  if (name.empty())
    return false;

  if (name.at(0) == 'f')
    {
      std::string numStr = name.substr(1);
      unsigned n = 0;
      if (parseNumber<unsigned>(numStr, num) and n < fpRegCount())
	return true;
    }

  unsigned n = 0;
  if (parseNumber<unsigned>(name, n) and n < fpRegCount())
    {
      num = n;
      return true;
    }

  return false;
}



template <typename URV>
const Csr<URV>*
Core<URV>::findCsr(const std::string& name) const
{
  const Csr<URV>* csr = csRegs_.findCsr(name);

  if (not csr)
    {
      unsigned n = 0;
      if (parseNumber<unsigned>(name, n))
	csr = csRegs_.findCsr(CsrNumber(n));
    }

  return csr;
}


template <typename URV>
bool
Core<URV>::configCsr(const std::string& name, bool implemented,
		     URV resetValue, URV mask, URV pokeMask, bool debug)
{
  return csRegs_.configCsr(name, implemented, resetValue, mask, pokeMask,
			   debug);
}


template <typename URV>
bool
Core<URV>::defineCsr(const std::string& name, CsrNumber num,
		     bool implemented, URV resetVal, URV mask,
		     URV pokeMask, bool isDebug)
{
  bool mandatory = false, quiet = true;
  auto c = csRegs_.defineCsr(name, num, mandatory, implemented, resetVal,
			     mask, pokeMask, isDebug, quiet);
  return c != nullptr;
}


template <typename URV>
bool
Core<URV>::configMachineModePerfCounters(unsigned numCounters)
{
  return csRegs_.configMachineModePerfCounters(numCounters);
}


template <typename URV>
void
formatInstTrace(FILE* out, uint64_t tag, unsigned hartId, URV currPc,
		const char* opcode, char resource, URV addr,
		URV value, const char* assembly)
{
  if constexpr (sizeof(URV) == 4)
    {
      if (resource == 'r')
	fprintf(out, "#%ld %d %08x %8s r %02x         %08x  %s",
		tag, hartId, currPc, opcode, addr, value, assembly);

      else
	fprintf(out, "#%ld %d %08x %8s %c %08x   %08x  %s", tag, hartId,
		currPc, opcode, resource, addr, value, assembly);
    }
  else
    fprintf(out, "#%ld %d %016lx %8s %c %016lx %016lx  %s",
	    tag, hartId, currPc, opcode, resource, addr, value, assembly);
}


template <typename URV>
void
formatFpInstTrace(FILE* out, uint64_t tag, unsigned hartId, URV currPc,
		  const char* opcode, unsigned fpReg,
		  uint64_t fpVal, const char* assembly)
{
  if constexpr (sizeof(URV) == 4)
    fprintf(out, "#%ld %d %08x %8s f %02x %016lx  %s",
	    tag, hartId, currPc, opcode, fpReg, fpVal, assembly);

  else
    fprintf(out, "#%ld %d %016lx %8s f %016lx %016lx  %s",
	    tag, hartId, currPc, opcode, uint64_t(fpReg), fpVal, assembly);
}


template <typename URV>
void
Core<URV>::printInstTrace(uint32_t inst, uint64_t tag, std::string& tmp,
			  FILE* out, bool interrupt)
{
  disassembleInst(inst, tmp);
  if (interrupt)
    tmp += " (interrupted)";

  if (traceLoad_ and loadAddrValid_)
    {
      std::ostringstream oss;
      oss << "0x" << std::hex << loadAddr_;
      tmp += " [" + oss.str() + "]";
    }

  char instBuff[128];
  if ((inst & 0x3) == 3)
    sprintf(instBuff, "%08x", inst);
  else
    {
      // 2-byte instruction: Clear top 16 bits
      uint16_t low = inst;
      inst = low;
      sprintf(instBuff, "%04x", inst);
    }

  bool pending = false;  // True if a printed line need to be terminated.

  // Process integer register diff.
  int reg = intRegs_.getLastWrittenReg();
  URV value = 0;
  if (reg > 0)
    {
      value = intRegs_.read(reg);
      formatInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'r', reg,
			   value, tmp.c_str());
      pending = true;
    }

  // Process floating point register diff.
  int fpReg = fpRegs_.getLastWrittenReg();
  if (fpReg >= 0)
    {
      uint64_t val = fpRegs_.readBits(fpReg);
      if (pending) fprintf(out, "  +\n");
      formatFpInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, fpReg,
			     val, tmp.c_str());
      pending = true;
    }

  // Process CSR diffs.
  std::vector<CsrNumber> csrs;
  std::vector<unsigned> triggers;
  csRegs_.getLastWrittenRegs(csrs, triggers);

  std::vector<bool> tdataChanged(3);

  std::map<URV, URV> csrMap; // Map csr-number to its value.

  for (CsrNumber csr : csrs)
    {
      if (not csRegs_.read(csr, PrivilegeMode::Machine, debugMode_, value))
	continue;

      if (csr >= CsrNumber::TDATA1 and csr <= CsrNumber::TDATA3)
	{
	  size_t ix = size_t(csr) - size_t(CsrNumber::TDATA1);
	  tdataChanged.at(ix) = true;
	  continue; // Debug triggers printed separately below
	}
      csrMap[URV(csr)] = value;
    }

  // Process trigger register diffs.
  for (unsigned trigger : triggers)
    {
      URV data1(0), data2(0), data3(0);
      if (not peekTrigger(trigger, data1, data2, data3))
	continue;
      if (tdataChanged.at(0))
	{
	  URV ecsr = (trigger << 16) | URV(CsrNumber::TDATA1);
	  csrMap[ecsr] = data1;
	}
      if (tdataChanged.at(1))
	{
	  URV ecsr = (trigger << 16) | URV(CsrNumber::TDATA2);
	  csrMap[ecsr] = data2;
	}
      if (tdataChanged.at(2))
	{
	  URV ecsr = (trigger << 16) | URV(CsrNumber::TDATA3);
	  csrMap[ecsr] = data3;
	}
    }

  for (const auto& [key, val] : csrMap)
    {
      if (pending) fprintf(out, "  +\n");
      formatInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'c',
			   key, val, tmp.c_str());
      pending = true;
    }

  // Process memory diff.
  size_t address = 0;
  uint64_t memValue = 0;
  unsigned writeSize = memory_.getLastWriteNewValue(address, memValue);
  if (writeSize > 0)
    {
      if (pending)
	fprintf(out, "  +\n");

      formatInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'm',
			   address, memValue, tmp.c_str());
      pending = true;
    }

  if (pending) 
    fprintf(out, "\n");
  else
    {
      // No diffs: Generate an x0 record.
      formatInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'r', 0, 0,
			  tmp.c_str());
      fprintf(out, "\n");
    }
}


template <typename URV>
void
Core<URV>::undoForTrigger()
{
  unsigned regIx = 0;
  URV value = 0;
  if (intRegs_.getLastWrittenReg(regIx, value))
    pokeIntReg(regIx, value);

  intRegs_.clearLastWrittenReg();

  pc_ = currPc_;
}


void
addToSignedHistogram(std::vector<uint64_t>& histo, int64_t val)
{
  if (histo.size() < 13)
    histo.resize(13);

  if (val < 0)
    {
      if      (val <= -64*1024) histo.at(0)++;
      else if (val <= -1024)    histo.at(1)++;
      else if (val <= -16)      histo.at(2)++;
      else if (val < -2)        histo.at(3)++;
      else if (val == -2)       histo.at(4)++;
      else if (val == -1)       histo.at(5)++;
    }
  else
    {
      if      (val == 0)       histo.at(6)++;
      else if (val == 1)       histo.at(7)++;
      else if (val == 2)       histo.at(8)++;
      else if (val <= 16)      histo.at(9)++;
      else if (val <= 1024)    histo.at(10)++;
      else if (val <= 64*1024) histo.at(11)++;
      else                     histo.at(12)++;
    }
}


void
addToUnsignedHistogram(std::vector<uint64_t>& histo, uint64_t val)
{
  if (histo.size() < 13)
    histo.resize(13);

  if      (val == 0)       histo.at(0)++;
  else if (val == 1)       histo.at(1)++;
  else if (val == 2)       histo.at(2)++;
  else if (val <= 16)      histo.at(3)++;
  else if (val <= 1024)    histo.at(4)++;
  else if (val <= 64*1024) histo.at(5)++;
  else                     histo.at(6)++;
}


template <typename URV>
void
Core<URV>::accumulateInstructionStats(uint32_t inst)
{
  uint32_t op0 = 0, op1 = 0; int32_t op2 = 0;
  const InstInfo& info = decode(inst, op0, op1, op2);
  InstId id = info.instId();

  if (enableCounters_ and prevCountersCsrOn_)
    {
      PerfRegs& pregs = csRegs_.mPerfRegs_;
      pregs.updateCounters(EventNumber::InstCommited);

      if (isCompressedInst(inst))
	pregs.updateCounters(EventNumber::Inst16Commited);
      else
	pregs.updateCounters(EventNumber::Inst32Commited);

      if ((currPc_ & 3) == 0)
	pregs.updateCounters(EventNumber::InstAligned);

      if (info.type() == InstType::Int)
	{
	  if (id == InstId::ebreak or id == InstId::c_ebreak)
	    pregs.updateCounters(EventNumber::Ebreak);
	  else if (id == InstId::ecall)
	    pregs.updateCounters(EventNumber::Ecall);
	  else if (id == InstId::fence)
	    pregs.updateCounters(EventNumber::Fence);
	  else if (id == InstId::fencei)
	    pregs.updateCounters(EventNumber::Fencei);
	  else if (id == InstId::mret)
	    pregs.updateCounters(EventNumber::Mret);
	  else if (id != InstId::illegal)
	    pregs.updateCounters(EventNumber::Alu);
	}
      else if (info.isMultiply())
	pregs.updateCounters(EventNumber::Mult);
      else if (info.isDivide())
	pregs.updateCounters(EventNumber::Div);
      else if (info.isLoad())
	{
	  pregs.updateCounters(EventNumber::Load);
	  if (misalignedLdSt_)
	    pregs.updateCounters(EventNumber::MisalignLoad);
	}
      else if (info.isStore())
	{
	  pregs.updateCounters(EventNumber::Store);
	  if (misalignedLdSt_)
	    pregs.updateCounters(EventNumber::MisalignStore);
	}
      else if (info.isCsr() and not csrException_)
	{
	  if ((id == InstId::csrrw or id == InstId::csrrwi))
	    {
	      if (op0 == 0)
		pregs.updateCounters(EventNumber::CsrWrite);
	      else
		pregs.updateCounters(EventNumber::CsrReadWrite);
	    }
	  else
	    {
	      if (op1 == 0)
		pregs.updateCounters(EventNumber::CsrRead);
	      else
		pregs.updateCounters(EventNumber::CsrReadWrite);
	    }

	  // Counter modified by csr instruction is not updated.
	  std::vector<CsrNumber> csrs;
	  std::vector<unsigned> triggers;
	  csRegs_.getLastWrittenRegs(csrs, triggers);
	  for (auto& csr : csrs)
	    if (pregs.isModified(unsigned(csr) - unsigned(CsrNumber::MHPMCOUNTER3)))
	      {
		URV val;
		peekCsr(csr, val);
		pokeCsr(csr, val - 1);
	      }
	    else if (csr >= CsrNumber::MHPMEVENT3 and csr <= CsrNumber::MHPMEVENT31)
	      {
		unsigned id = unsigned(csr) - unsigned(CsrNumber::MHPMEVENT3);
		if (pregs.isModified(id))
		  {
		    URV val;
		    CsrNumber csr2 = CsrNumber(id + unsigned(CsrNumber::MHPMCOUNTER3));
		    if (pregs.isModified(unsigned(csr2) - unsigned(CsrNumber::MHPMCOUNTER3)))
		      {
			peekCsr(csr2, val);
			pokeCsr(csr2, val - 1);
		      }
		  }
	      }
	}
      else if (info.isBranch())
	{
	  pregs.updateCounters(EventNumber::Branch);
	  if (lastBranchTaken_)
	    pregs.updateCounters(EventNumber::BranchTaken);
	}

      pregs.clearModified();
    }

  prevCountersCsrOn_ = countersCsrOn_;

  misalignedLdSt_ = false;
  lastBranchTaken_ = false;

  if (not instFreq_)
    return;

  InstProfile& entry = instProfileVec_.at(size_t(id));

  entry.freq_++;

  bool hasRd = false;

  unsigned rs1 = 0, rs2 = 0;
  bool hasRs1 = false, hasRs2 = false;

  if (info.ithOperandType(0) == OperandType::IntReg)
    {
      hasRd = info.isIthOperandWrite(0);
      if (hasRd)
	entry.rd_.at(op0)++;
      else
	{
	  rs1 = op0;
	  entry.rs1_.at(rs1)++;
	  hasRs1 = true;
	}
    }

  bool hasImm = false;  // True if instruction has an immediate operand.
  int32_t imm = 0;     // Value of immediate operand.

  if (info.ithOperandType(1) == OperandType::IntReg)
    {
      if (hasRd)
	{
	  rs1 = op1;
	  entry.rs1_.at(rs1)++;
	  hasRs1 = true;
	}
      else
	{
	  rs2 = op1;
	  entry.rs2_.at(rs2)++;
	  hasRs2 = true;
	}
    }
  else if (info.ithOperandType(1) == OperandType::Imm)
    {
      hasImm = true;
      imm = op1;
    }

  if (info.ithOperandType(2) == OperandType::IntReg)
    {
      if (hasRd)
	{
	  rs2 = op2;
	  entry.rs2_.at(rs2)++;
	  hasRs2 = true;
	}
      else
	assert(0);
    }
  else if (info.ithOperandType(2) == OperandType::Imm)
    {
      hasImm = true;
      imm = op2;
    }

  if (hasImm)
    {
      entry.hasImm_ = true;

      if (entry.freq_ == 1)
	{
	  entry.minImm_ = entry.maxImm_ = imm;
	}
      else
	{
	  entry.minImm_ = std::min(entry.minImm_, imm);
	  entry.maxImm_ = std::max(entry.maxImm_, imm);
	}
      addToSignedHistogram(entry.immHisto_, imm);
    }

  unsigned rd = intRegCount() + 1;
  URV rdOrigVal = 0;
  intRegs_.getLastWrittenReg(rd, rdOrigVal);

  if (hasRs1)
    {
      URV val1 = intRegs_.read(rs1);
      if (rs1 == rd)
	val1 = rdOrigVal;
      if (info.isUnsigned())
	addToUnsignedHistogram(entry.rs1Histo_, val1);
      else
	addToSignedHistogram(entry.rs1Histo_, SRV(val1));
    }

  if (hasRs2)
    {
      URV val2 = intRegs_.read(rs2);
      if (rs2 == rd)
	val2 = rdOrigVal;
      if (info.isUnsigned())
	addToUnsignedHistogram(entry.rs2Histo_, val2);
      else
	addToSignedHistogram(entry.rs2Histo_, SRV(val2));
    }
}


template <typename URV>
inline
void
Core<URV>::clearTraceData()
{
  intRegs_.clearLastWrittenReg();
  fpRegs_.clearLastWrittenReg();
  csRegs_.clearLastWrittenRegs();
  memory_.clearLastWriteInfo();
}


template <typename URV>
inline
void
Core<URV>::setTargetProgramBreak(URV addr)
{
  progBreak_ = addr;

  size_t pageAddr = memory_.getPageStartAddr(addr);
  if (pageAddr != addr)
    progBreak_ = pageAddr + memory_.pageSize();
}


template <typename URV>
inline
bool
Core<URV>::setTargetProgramArgs(const std::vector<std::string>& args)
{
  URV sp = 0;

  if (not peekIntReg(RegSp, sp))
    return false;

  // Push the arguments on the stack recording their addresses.
  std::vector<URV> addresses;  // Address of the argv strings.
  for (const auto& arg : args)
    {
      sp -= URV(arg.size() + 1);  // Make room for arg and null char.
      addresses.push_back(sp);

      size_t ix = 0;

      for (uint8_t c : arg)
	if (not memory_.pokeByte(sp + ix++, c))
	  return false;

      if (not memory_.pokeByte(sp + ix++, uint8_t(0))) // Null char.
	return false;
    }

  addresses.push_back(0);  // Null pointer at end of argv.

  // Push argv entries on the stack.
  sp -= URV(addresses.size()) * sizeof(URV); // Make room for argv
  URV ix = 0;
  for (const auto addr : addresses)
    {
      if (not memory_.poke(sp + ix++*sizeof(URV), addr))
	return false;
    }

  // Push argc on the stack.
  sp -= sizeof(URV);
  if (not memory_.poke(sp, URV(args.size())))
    return false;

  if (not pokeIntReg(RegSp, sp))
    return false;

  return true;
}


template <typename URV>
URV
Core<URV>::lastPc() const
{
  return currPc_;
}


template <typename URV>
int
Core<URV>::lastIntReg() const
{
  return intRegs_.getLastWrittenReg();
}


template <typename URV>
int
Core<URV>::lastFpReg() const
{
  return fpRegs_.getLastWrittenReg();
}


template <typename URV>
void
Core<URV>::lastCsr(std::vector<CsrNumber>& csrs,
		   std::vector<unsigned>& triggers) const
{
  csRegs_.getLastWrittenRegs(csrs, triggers);
}


template <typename URV>
void
Core<URV>::lastMemory(std::vector<size_t>& addresses,
		      std::vector<uint32_t>& words) const
{
  addresses.clear();
  words.clear();

  size_t address = 0;
  uint64_t value;
  unsigned writeSize = memory_.getLastWriteNewValue(address, value);

  if (not writeSize)
    return;

  addresses.clear();
  words.clear();
  addresses.push_back(address);
  words.push_back(value);

  if (writeSize == 8)
    {
      addresses.push_back(address + 4);
      words.push_back(value >> 32);
    }
}


template <typename URV>
void
handleExceptionForGdb(WdRiscv::Core<URV>& core);


// Return true if debug mode is entered and false otherwise.
template <typename URV>
bool
Core<URV>::takeTriggerAction(FILE* traceFile, URV pc, URV info,
			     uint64_t& counter, bool beforeTiming)
{
  // Check triggers configuration to determine action: take breakpoint
  // exception or enter debugger.

  if (csRegs_.hasEnterDebugModeTripped())
    {
      enterDebugMode(DebugModeCause::TRIGGER, pc);
      return true;
    }
  else
    initiateException(ExceptionCause::BREAKP, pc, info);

  if (beforeTiming and traceFile)
    {
      uint32_t inst = 0;
      readInst(currPc_, inst);

      std::string instStr;
      printInstTrace(inst, counter, instStr, traceFile);
    }

  return false; // Did not enter debug mode
}


// This is set to false when user hits control-c to interrupt a long
// run.
volatile static bool userOk = true;

static
void keyboardInterruptHandler(int)
{
  userOk = false;
}



template <typename URV>
bool
Core<URV>::untilAddress(URV address, FILE* traceFile)
{
  std::string instStr;
  instStr.reserve(128);

  // Need csr history when tracing or for triggers
  bool trace = traceFile != nullptr or enableTriggers_;
  clearTraceData();

  uint64_t counter = counter_;
  uint64_t limit = instCountLim_;
  bool success = true;
  bool doStats = instFreq_ or enableCounters_;

  if (enableGdb_)
    handleExceptionForGdb(*this);

  uint32_t inst = 0;

  while (pc_ != address and counter < limit and userOk)
    {
      inst = 0;

      try
	{
	  currPc_ = pc_;

	  loadAddrValid_ = false;
	  triggerTripped_ = false;
	  ldStException_ = false;
	  csrException_ = false;

	  ++counter;

	  // Process pre-execute address trigger and fetch instruction.
	  bool hasTrig = hasActiveInstTrigger();
	  if (hasTrig and instAddrTriggerHit(currPc_, TriggerTiming::Before,
					     isInterruptEnabled()))
	    triggerTripped_ = true;

	  // Fetch instruction.
	  bool fetchOk = true;
	  if (triggerTripped_)
	    fetchOk = fetchInstPostTrigger(pc_, inst, traceFile);
	  else
	    fetchOk = fetchInst(pc_, inst);
	  if (not fetchOk)
	    {
	      cycleCount_++;
	      continue;  // Next instruction in trap handler.
	    }

	  // Process pre-execute opcode trigger.
	  if (hasTrig and instOpcodeTriggerHit(inst, TriggerTiming::Before,
					       isInterruptEnabled()))
	    triggerTripped_ = true;

	  // Increment pc and execute instruction
	  if (isFullSizeInst(inst))
	    {
	      // 4-byte instruction
	      pc_ += 4;
	      execute32(inst);
	    }
	  else
	    {
	      // Compressed (2-byte) instruction.
	      pc_ += 2;
	      execute16(inst);
	    }

	  cycleCount_++;

	  if (ldStException_)
	    {
	      if (traceFile)
		{
		  printInstTrace(inst, counter, instStr, traceFile);
		  clearTraceData();
		}
	      continue;
	    }

	  if (triggerTripped_)
	    {
	      undoForTrigger();
	      if (takeTriggerAction(traceFile, currPc_, currPc_,
				    counter, true))
		return true;
	      continue;
	    }

	  ++retiredInsts_;
	  if (doStats)
	    accumulateInstructionStats(inst);

	  bool icountHit = (enableTriggers_ and isInterruptEnabled() and
			    icountTriggerHit());

	  if (trace)
	    {
	      if (traceFile)
		printInstTrace(inst, counter, instStr, traceFile);
	      clearTraceData();
	    }

	  if (icountHit)
	    if (takeTriggerAction(traceFile, pc_, pc_, counter, false))
	      return true;
	}
      catch (const CoreException& ce)
	{
	  if (ce.type() == CoreException::Stop)
	    {
	      if (trace)
		{
		  uint32_t inst = 0;
		  readInst(currPc_, inst);
		  if (traceFile)
		    printInstTrace(inst, counter, instStr, traceFile);
		  clearTraceData();
		}
	      success = ce.value() == 1; // Anything besides 1 is a fail.
	      std::cerr << (success? "Successful " : "Error: Failed ")
			<< "stop: " << std::dec << ce.what() << "\n";
	      break;
	    }
	  if (ce.type() == CoreException::Exit)
	    {
	      std::cerr << "Target program exited with code " << ce.value() << '\n';
	      break;
	    }
	  std::cerr << "Stopped -- unexpected exception\n";
	}
    }

  // Update retired-instruction and cycle count registers.
  counter_ = counter;

  return success;
}


template <typename URV>
bool
Core<URV>::runUntilAddress(URV address, FILE* traceFile)
{
  struct timeval t0;
  gettimeofday(&t0, nullptr);

  uint64_t limit = instCountLim_;
  uint64_t counter0 = counter_;

  struct sigaction oldAction;
  struct sigaction newAction;
  memset(&newAction, 0, sizeof(newAction));
  newAction.sa_handler = keyboardInterruptHandler;

  userOk = true;
  sigaction(SIGINT, &newAction, &oldAction);

  bool success = untilAddress(address, traceFile);

  sigaction(SIGINT, &oldAction, nullptr);

  if (counter_ == limit)
    std::cerr << "Stopped -- Reached instruction limit\n";
  else if (pc_ == address)
    std::cerr << "Stopped -- Reached end address\n";

  // Simulator stats.
  struct timeval t1;
  gettimeofday(&t1, nullptr);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)*1e-6;

  uint64_t numInsts = counter_ - counter0;

  std::cout.flush();
  if (not userOk)
    std::cerr << "Keyboard interrupt\n";
  std::cerr << "Retired " << numInsts << " instruction"
	    << (numInsts > 1? "s" : "") << " in "
	    << (boost::format("%.2fs") % elapsed);
  if (elapsed > 0)
    std::cerr << "  " << size_t(numInsts/elapsed) << " inst/s";
  std::cerr << '\n';

  return success;
}


template <typename URV>
bool
Core<URV>::simpleRun()
{
  bool success = true;

  try
    {
      while (userOk) 
	{
	  // Fetch instruction
	  currPc_ = pc_;
	  ++cycleCount_;
	  ldStException_ = false;

	  uint32_t inst;
	  if (not fetchInst(pc_, inst))
	    continue; // Next instruction in trap handler.

	  // Increment pc and execute instruction
	  if (isFullSizeInst(inst))
	    {
	      pc_ += 4;  // 4-byte instruction
	      execute32(inst);
	    }
	  else
	    {
	      pc_ += 2;  // Compressed (2-byte) instruction.
	      execute16(inst);
	    }

	  if (not ldStException_)
	    ++retiredInsts_;
	}
    }
  catch (const CoreException& ce)
    {
      if (ce.type() == CoreException::Stop)
	{
	  success = ce.value() == 1; // Anything besides 1 is a fail.
	  std::cerr << (success? "Successful " : "Error: Failed ")
		    << "stop: " << std::dec << ce.what() << '\n';
	}
      else if (ce.type() == CoreException::Exit)
	{
	  std::cerr << "Target program exited with code " << ce.value() << '\n';
	  success = ce.value() == 0;
	}
      else
	{
	  success = false;
	  std::cerr << "Stopped -- unexpected exception\n";
	}
    }

  return success;
}


/// Run indefinitely.  If the tohost address is defined, then run till
/// a write is attempted to that address.
template <typename URV>
bool
Core<URV>::run(FILE* file)
{
  // If test has toHost defined then use that as the stopping criteria
  // and ignore the stop address. Not having to check for the stop
  // address gives us about an 10 percent boost in speed.
  if (stopAddrValid_ and not toHostValid_)
    return runUntilAddress(stopAddr_, file);

  // To run fast, this method does not do much besides straight-forward
  // execution. If any option is turned on, we switch to
  // runUntilAdress which runs slower but is full-featured.
  if (file or instCountLim_ < ~uint64_t(0) or instFreq_ or enableTriggers_ or
      enableCounters_ or enableGdb_)
    {
      URV address = ~URV(0);  // Invalid stop PC.
      return runUntilAddress(address, file);
    }

  struct timeval t0;
  gettimeofday(&t0, nullptr);

  struct sigaction oldAction;
  struct sigaction newAction;
  memset(&newAction, 0, sizeof(newAction));
  newAction.sa_handler = keyboardInterruptHandler;

  userOk = true;
  sigaction(SIGINT, &newAction, &oldAction);

  bool success = simpleRun();

  sigaction(SIGINT, &oldAction, nullptr);

  // Simulator stats.
  struct timeval t1;
  gettimeofday(&t1, nullptr);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)*1e-6;

  std::cout.flush();
  if (not userOk)
    std::cerr << "Keyboard interrupt\n";
  std::cerr << "Retired " << retiredInsts_ << " instruction"
	    << (retiredInsts_ > 1? "s" : "") << " in "
	    << (boost::format("%.2fs") % elapsed);
  if (elapsed > 0)
    std::cerr << "  " << size_t(retiredInsts_/elapsed) << " inst/s";
  std::cerr << '\n';

  return success;
}


template <typename URV>
bool
Core<URV>::isInterruptPossible(InterruptCause& cause)
{
  if (debugMode_)
    return false;

  URV mstatus;
  if (not csRegs_.read(CsrNumber::MSTATUS, PrivilegeMode::Machine,
		       debugMode_, mstatus))
    return false;

  MstatusFields<URV> fields(mstatus);
  if (not fields.bits_.MIE)
    return false;

  URV mip, mie;
  if (csRegs_.read(CsrNumber::MIP, PrivilegeMode::Machine, debugMode_, mip)
      and
      csRegs_.read(CsrNumber::MIE, PrivilegeMode::Machine, debugMode_, mie))
    {
      if ((mie & mip) == 0)
	return false;  // Nothing enabled is pending.

      // Order of priority: machine, supervisor, user and then
      // external, software, timer and internal timers.
      if (mie & (1 << unsigned(InterruptCause::M_EXTERNAL)) & mip)
	{
	  cause = InterruptCause::M_EXTERNAL;
	  return true;
	}
      if (mie & (1 << unsigned(InterruptCause::M_LOCAL)) & mip)
	{
	  cause = InterruptCause::M_LOCAL;
	  return true;
	}
      if (mie & (1 << unsigned(InterruptCause::M_SOFTWARE)) & mip)
	{
	  cause = InterruptCause::M_SOFTWARE;
	  return true;
	}
      if (mie & (1 << unsigned(InterruptCause::M_TIMER)) & mip)
	{
	  cause = InterruptCause::M_TIMER;
	  return true;
	}
      if (mie & (1 << unsigned(InterruptCause::M_INT_TIMER0)) & mip)
	{
	  cause = InterruptCause::M_INT_TIMER0;
	  return true;
	}
      if (mie & (1 << unsigned(InterruptCause::M_INT_TIMER1)) & mip)
	{
	  cause = InterruptCause::M_INT_TIMER1;
	  return true;
	}
    }

  return false;
}


template <typename URV>
bool
Core<URV>::processExternalInterrupt(FILE* traceFile, std::string& instStr)
{
  if (debugStep_ and not debugStepIe_)
    return false;

  // If a non-maskable interrupt was signaled by the test-bench, take it.
  if (nmiPending_)
    {
      initiateNmi(URV(nmiCause_), pc_);
      nmiPending_ = false;
      nmiCause_ = NmiCause::UNKNOWN;
      uint32_t inst = 0; // Load interrupted inst.
      readInst(currPc_, inst);
      if (traceFile)  // Trace interrupted instruction.
	printInstTrace(inst, counter_, instStr, traceFile, true);
      return true;
    }

  // If interrupts enabled and one is pending, take it.
  InterruptCause cause;
  if (isInterruptPossible(cause))
    {
      // Attach changes to interrupted instruction.
      initiateInterrupt(cause, pc_);
      uint32_t inst = 0; // Load interrupted inst.
      readInst(currPc_, inst);
      if (traceFile)  // Trace interrupted instruction.
	printInstTrace(inst, counter_, instStr, traceFile, true);
      ++cycleCount_;
      return true;
    }
  return false;
}


/// Return true if given core is in debug mode and the stop count bit of
/// the DSCR register is set.
template <typename URV>
bool
isDebugModeStopCount(const Core<URV>& core)
{
  if (not core.inDebugMode())
    return false;

  URV dcsrVal = 0;
  if (not core.peekCsr(CsrNumber::DCSR, dcsrVal))
    return false;

  if ((dcsrVal >> 10) & 1)
    return true;  // stop count bit is set
  return false;
}


template <typename URV>
void
Core<URV>::singleStep(FILE* traceFile)
{
  std::string instStr;

  // Single step is mostly used for follow-me mode where we want to
  // know the changes after the execution of each instruction.
  bool doStats = instFreq_ or enableCounters_;

  try
    {
      uint32_t inst = 0;
      currPc_ = pc_;

      loadAddrValid_ = false;
      triggerTripped_ = false;
      ldStException_ = false;
      csrException_ = false;
      ebreakInst_ = false;

      ++counter_;

      if (processExternalInterrupt(traceFile, instStr))
	{
	  if (debugStep_)
	    enterDebugMode(DebugModeCause::STEP, pc_);
	  ++cycleCount_;
	  return;  // Next instruction in interrupt handler.
	}

      // Process pre-execute address trigger and fetch instruction.
      bool hasTrig = hasActiveInstTrigger();
      triggerTripped_ = hasTrig and instAddrTriggerHit(pc_,
						       TriggerTiming::Before,
						       isInterruptEnabled());
      // Fetch instruction.
      bool fetchOk = true;
      if (triggerTripped_)
	fetchOk = fetchInstPostTrigger(pc_, inst, traceFile);
      else if (forceFetchFail_)
	{
	  forceFetchFail_ = false;
	  initiateException(ExceptionCause::INST_ACC_FAULT, pc_, pc_);
	  fetchOk = false;
	}
      else
	fetchOk = fetchInst(pc_, inst);
      if (not fetchOk)
	{
	  ++cycleCount_;
	  return; // Next instruction in trap handler
	}

      // Process pre-execute opcode trigger.
      if (hasTrig and instOpcodeTriggerHit(inst, TriggerTiming::Before,
					   isInterruptEnabled()))
	triggerTripped_ = true;

      // Execute instruction
      if (isFullSizeInst(inst))
	{
	  // 4-byte instruction
	  pc_ += 4;
	  execute32(inst);
	}
      else
	{
	  // Compressed (2-byte) instruction.
	  pc_ += 2;
	  execute16(inst);
	}

      ++cycleCount_;

      if (ldStException_)
	{
	  if (traceFile)
	    printInstTrace(inst, counter_, instStr, traceFile);
	  return;
	}

      if (triggerTripped_)
	{
	  undoForTrigger();
	  takeTriggerAction(traceFile, currPc_, currPc_, counter_, true);
	  return;
	}

      if (not isDebugModeStopCount(*this))
	++retiredInsts_;

      if (doStats)
	accumulateInstructionStats(inst);

      if (traceFile)
	printInstTrace(inst, counter_, instStr, traceFile);

      // If a register is used as a source by an instruction then any
      // pending load with same register as target is removed from the
      // load queue (because in such a case the hardware will stall
      // till load is completed).
      {
	uint32_t op0 = 0, op1 = 0; int32_t op2 = 0;
	const InstInfo& info = decode(inst, op0, op1, op2);
	if (info.isIthOperandIntRegSource(0))
	  removeFromLoadQueue(op0);
	if (info.isIthOperandIntRegSource(1))
	  removeFromLoadQueue(op1);
	if (info.isIthOperandIntRegSource(2))
	  removeFromLoadQueue(op2);
      }

      // If a register is written by a non-load instruction, then its
      // entry is invalidated in the load queue.
      if (not loadAddrValid_)
	{
	  int regIx = intRegs_.getLastWrittenReg();
	  if (regIx > 0)
	    invalidateInLoadQueue(regIx);
	}

      bool icountHit = (enableTriggers_ and isInterruptEnabled() and
			icountTriggerHit());
      if (icountHit)
	{
	  takeTriggerAction(traceFile, pc_, pc_, counter_, false);
	  return;
	}

      // If step bit set in dcsr then enter debug mode unless already there.
      if (debugStep_ and not ebreakInst_)
	enterDebugMode(DebugModeCause::STEP, pc_);
    }
  catch (const CoreException& ce)
    {
      uint32_t inst = 0;
      readInst(currPc_, inst);
      if (ce.type() == CoreException::Stop)
	{
	  if (traceFile)
	    printInstTrace(inst, counter_, instStr, traceFile);
	  std::cerr << "Stopped...\n";
	}
      else if (ce.type() == CoreException::Exit)
	std::cerr << "Target program exited with code " << ce.value() << '\n';
      else
	std::cerr << "Unexpected exception\n";
    }
}


template <typename URV>
bool
Core<URV>::whatIfSingleStep(uint32_t inst, ChangeRecord& record)
{
  uint64_t prevExceptionCount = exceptionCount_;
  URV prevPc = pc_;

  clearTraceData();
  triggerTripped_ = false;

  // Note: triggers not yet supported.

  // Execute instruction
  currPc_ = pc_;
  if (isFullSizeInst(inst))
    {
      // 4-byte instruction
      pc_ += 4;
      execute32(inst);
    }
  else
    {
      // Compressed (2-byte) instruction.
      pc_ += 2;
      execute16(inst);
    }

  bool result = exceptionCount_ == prevExceptionCount;

  // If step bit set in dcsr then enter debug mode unless already there.
  if (debugStep_ and not ebreakInst_)
    enterDebugMode(DebugModeCause::STEP, pc_);

  // Collect changes. Undo each collected change.
  exceptionCount_ = prevExceptionCount;

  collectAndUndoWhatIfChanges(prevPc, record);
  
  return result;
}


template <typename URV>
bool
Core<URV>::whatIfSingleStep(URV whatIfPc, uint32_t inst, ChangeRecord& record)
{
  URV prevPc = pc_;
  pc_ = whatIfPc;

  // Note: triggers not yet supported.
  triggerTripped_ = false;

  // Fetch instruction. We don't care about what we fetch. Just checking
  // if there is a fetch exception.
  uint32_t dummyInst = 0;
  bool fetchOk = fetchInst(pc_, dummyInst);

  if (not fetchOk)
    {
      collectAndUndoWhatIfChanges(prevPc, record);
      return false;
    }

  bool res = whatIfSingleStep(inst, record);

  pc_ = prevPc;
  return res;
}


template <typename URV>
void
Core<URV>::collectAndUndoWhatIfChanges(URV prevPc, ChangeRecord& record)
{
  record.clear();

  record.newPc = pc_;
  pc_ = prevPc;

  unsigned regIx = 0;
  URV oldValue = 0;
  if (intRegs_.getLastWrittenReg(regIx, oldValue))
    {
      URV newValue = 0;
      peekIntReg(regIx, newValue);
      pokeIntReg(regIx, oldValue);

      record.hasIntReg = true;
      record.intRegIx = regIx;
      record.intRegValue = newValue;
    }

  uint64_t oldFpValue = 0;
  if (fpRegs_.getLastWrittenReg(regIx, oldFpValue))
    {
      uint64_t newFpValue = 0;
      peekFpReg(regIx, newFpValue);
      pokeFpReg(regIx, oldFpValue);

      record.hasFpReg = true;
      record.fpRegIx = regIx;
      record.fpRegValue = newFpValue;
    }

  record.memSize = memory_.getLastWriteNewValue(record.memAddr, record.memValue);

  size_t addr = 0;
  uint64_t value = 0;
  size_t byteCount = memory_.getLastWriteOldValue(addr, value);
  for (size_t i = 0; i < byteCount; ++i)
    {
      uint8_t byte = value & 0xff;
      memory_.poke(addr, byte);
      addr++;
      value = value >> 8;
    }

  std::vector<CsrNumber> csrNums;
  std::vector<unsigned> triggerNums;
  csRegs_.getLastWrittenRegs(csrNums, triggerNums);

  for (auto csrn : csrNums)
    {
      Csr<URV>* csr = csRegs_.getImplementedCsr(csrn);
      if (not csr)
	continue;

      URV newVal = csr->read();
      URV oldVal = csr->prevValue();
      csr->write(oldVal);

      record.csrIx.push_back(csrn);
      record.csrValue.push_back(newVal);
    }

  clearTraceData();
}


template <typename URV>
void
Core<URV>::executeFp(uint32_t inst)
{
  RFormInst rform(inst);
  unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
  unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
  instRoundingMode_ = RoundingMode(f3);

  if (f7 & 1)
    {
      if      (f7 == 1)                   execFadd_d(rd, rs1, rs2);
      else if (f7 == 5)                   execFsub_d(rd, rs1, rs2);
      else if (f7 == 9)                   execFmul_d(rd, rs1, rs2);
      else if (f7 == 0xd)                 execFdiv_d(rd, rs1, rs2);
      else if (f7 == 0x11)
	{
	  if      (f3 == 0)               execFsgnj_d(rd, rs1, rs2);
	  else if (f3 == 1)               execFsgnjn_d(rd, rs1, rs2);
	  else if (f3 == 2)               execFsgnjx_d(rd, rs1, rs2);
	  else                            illegalInst();
	}
      else if (f7 == 0x15)
	{
	  if      (f3 == 0)               execFmin_d(rd, rs1, rs2);
	  else if (f3 == 1)               execFmax_d(rd, rs1, rs2);
	  else                            illegalInst();
	}
      else if (f7 == 0x21 and rs2 == 0)   execFcvt_d_s(rd, rs1, rs2);
      else if (f7 == 0x2d)                execFsqrt_d(rd, rs1, rs2);
      else if (f7 == 0x51)
	{
	  if      (f3 == 0)               execFle_d(rd, rs1, rs2);
	  else if (f3 == 1)               execFlt_d(rd, rs1, rs2);
	  else if (f3 == 2)               execFeq_d(rd, rs1, rs2);
	  else                            illegalInst();
	}
      else if (f7 == 0x61)
	{
	  if      (rs2 == 0)              execFcvt_w_d(rd, rs1, rs2);
	  else if (rs2 == 1)              execFcvt_wu_d(rd, rs1, rs2);
	  else                            illegalInst();
	}
      else if (f7 == 0x69)
	{
	  if      (rs2 == 0)              execFcvt_d_w(rd, rs1, rs2);
	  else if (rs2 == 1)              execFcvt_d_wu(rd, rs1, rs2);
	  else                            illegalInst();
	}
      else if (f7 == 0x71)
	{
	  if (rs2 == 0 and f3 == 0)       execFmv_x_d(rd, rs1, rs2);
	  if (rs2 == 0 and f3 == 1)       execFclass_d(rd, rs1, rs2);
	  else                            illegalInst();
	}
      else
	illegalInst();
      return;
    }

  if (f7 == 0)                        execFadd_s(rd, rs1, rs2);
  else if (f7 == 4)                   execFsub_s(rd, rs1, rs2);
  else if (f7 == 8)                   execFmul_s(rd, rs1, rs2);
  else if (f7 == 0xc)                 execFdiv_s(rd, rs1, rs2);
  else if (f7 == 0x10)
    {
      if      (f3 == 0)               execFsgnj_s(rd, rs1, rs2);
      else if (f3 == 1)               execFsgnjn_s(rd, rs1, rs2);
      else if (f3 == 2)               execFsgnjx_s(rd, rs1, rs2);
      else                            illegalInst();
    }
  else if (f7 == 0x14)
    {
      if      (f3 == 0)               execFmin_s(rd, rs1, rs2);
      else if (f3 == 1)               execFmax_s(rd, rs1, rs2);
      else                            illegalInst();
    }
  else if (f7 == 0x20 and rs2 == 1)   execFcvt_s_d(rd, rs1, rs2);
  else if (f7 == 0x2c)                execFsqrt_s(rd, rs1, rs2);
  else if (f7 == 0x50)
    {
      if      (f3 == 0)               execFle_s(rd, rs1, rs2);
      else if (f3 == 1)               execFlt_s(rd, rs1, rs2);
      else if (f3 == 2)               execFeq_s(rd, rs1, rs2);
      else                            illegalInst();
    }
  else if (f7 == 0x60)
    {
      if      (rs2 == 0)              execFcvt_w_s(rd, rs1, rs2);
      else if (rs2 == 1)              execFcvt_wu_s(rd, rs1, rs2);
      else if (rs2 == 2)              execFcvt_l_s(rd, rs1, rs2);
      else if (rs2 == 3)              execFcvt_lu_s(rd, rs1, rs2);      
      else                            illegalInst();
    }
  else if (f7 == 0x68)
    {
      if      (rs2 == 0)              execFcvt_s_w(rd, rs1, rs2);
      else if (rs2 == 1)              execFcvt_s_wu(rd, rs1, rs2);
      else if (rs2 == 2)              execFcvt_s_l(rd, rs1, rs2);
      else if (rs2 == 3)              execFcvt_s_lu(rd, rs1, rs2);      
      else                            illegalInst();
    }
  else if (f7 == 0x70)
    {
      if      (rs2 == 0 and f3 == 0)  execFmv_x_w(rd, rs1, rs2);
      else if (rs2 == 0 and f3 == 1)  execFclass_s(rd, rs1, rs2);
      else                            illegalInst();
    }
  else if (f7 == 0x78)
    {
      if (rs2 == 0 and f3 == 0)       execFmv_w_x(rd, rs1, rs2);
      else                            illegalInst();
    }
  else
    illegalInst();
}


template <typename URV>
void
Core<URV>::execute32(uint32_t inst)
{
#pragma GCC diagnostic ignored "-Wpedantic"
  
  static void *opcodeLabels[] = { &&l0, &&l1, &&l2, &&l3, &&l4, &&l5,
				  &&l6, &&l7, &&l8, &&l9, &&l10, &&l11,
				  &&l12, &&l13, &&l14, &&l15, &&l16, &&l17,
				  &&l18, &&l19, &&l20, &&l21, &&l22, &&l23,
				  &&l24, &&l25, &&l26, &&l27, &&l28, &&l29,
				  &&l30, &&l31 };

  // Decode and execute.
  unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.
  goto *opcodeLabels[opcode];

 l0:  // 00000   I-form
  {
    IFormInst iform(inst);
    unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
    int32_t imm = iform.immed();
    uint32_t f3 = iform.fields.funct3;
    if      (f3 == 0) execLb(rd, rs1, imm);
    else if (f3 == 1) execLh(rd, rs1, imm);
    else if (f3 == 2) execLw(rd, rs1, imm);
    else if (f3 == 3) execLd(rd, rs1, imm);
    else if (f3 == 4) execLbu(rd, rs1, imm);
    else if (f3 == 5) execLhu(rd, rs1, imm);
    else if (f3 == 6) execLwu(rd, rs1, imm);
    else              illegalInst();
  }
  return;

 l1:
  {
    IFormInst iform(inst);
    unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
    int32_t imm = iform.immed();
    uint32_t f3 = iform.fields.funct3;
    if      (f3 == 2)   { execFlw(rd, rs1, imm); }
    else if (f3 == 3)   { execFld(rd, rs1, imm); }
    else                { illegalInst(); }
  }
  return;

 l2:
 l7:
  illegalInst();
  return;

 l9:
  {
    SFormInst sform(inst);
    unsigned rs1 = sform.bits.rs1, rs2 = sform.bits.rs2;
    unsigned funct3 = sform.bits.funct3;
    int32_t imm = sform.immed();
    if      (funct3 == 2)  execFsw(rs1, rs2, imm);
    else if (funct3 == 3)  execFsd(rs1, rs2, imm);
    else                   illegalInst();
  }
  return;

 l10:
 l15:
  illegalInst();
  return;

 l16:
  {
    RFormInst rform(inst);
    unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
    unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
    instRoundingMode_ = RoundingMode(funct3);
    if ((funct7 & 3) == 0)
      {
	instRs3_ = funct7 >> 2;
	execFmadd_s(rd, rs1, rs2);
      }
    else if ((funct7 & 3) == 1)
      {
	instRs3_ = funct7 >> 2;
	execFmadd_d(rd, rs1, rs2);
      }
    else
      illegalInst();
  }
  return;

 l17:
  {
    RFormInst rform(inst);
    unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
    unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
    instRoundingMode_ = RoundingMode(funct3);
    if ((funct7 & 3) == 0)
      {
	instRs3_ = funct7 >> 2;
	execFmsub_s(rd, rs1, rs2);
      }
    else if ((funct7 & 3) == 0)
      {
	instRs3_ = funct7 >> 2;
	execFmsub_d(rd, rs1, rs2);
      }
    else
      illegalInst();
  }
  return;

 l18:
  {
    RFormInst rform(inst);
    unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
    unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
    instRoundingMode_ = RoundingMode(funct3);
    if ((funct7 & 3) == 0)
      {
	instRs3_ = funct7 >> 2;
	execFnmsub_s(rd, rs1, rs2);
      }
    else if ((funct7 & 3) == 1)
      {
	instRs3_ = funct7 >> 2;
	execFnmsub_d(rd, rs1, rs2);
      }
    else
      illegalInst();
  }
  return;

 l19:
  {
    RFormInst rform(inst);
    unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
    unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
    instRoundingMode_ = RoundingMode(funct3);
    if ((funct7 & 3) == 0)
      {
	instRs3_ = funct7 >> 2;
	execFnmadd_s(rd, rs1, rs2);
      }
    else if ((funct7 & 3) == 1)
      {
	instRs3_ = funct7 >> 2;
	execFnmadd_d(rd, rs1, rs2);
      }
    else
      illegalInst();
  }
  return;

 l20:
  executeFp(inst);
  return;

 l21:
 l22:
 l23:
 l26:
 l29:
 l30:
 l31:
  illegalInst();
  return;

 l3: // 00011  I-form
  {
    IFormInst iform(inst);
    unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
    unsigned funct3 = iform.fields.funct3;
    if (rd != 0 or rs1 != 0)
      illegalInst();
    else if (funct3 == 0)
      {
	if (iform.top4() != 0)
	  illegalInst();
	else
	  execFence(iform.pred(), iform.succ());
      }
    else if (funct3 == 1)
      {
	if (iform.uimmed() != 0)
	  illegalInst();
	else
	  execFencei();
      }
    else
      illegalInst();
  }
  return;

 l4:  // 00100  I-form
  {
    IFormInst iform(inst);
    unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
    int32_t imm = iform.immed();
    unsigned funct3 = iform.fields.funct3;

    if      (funct3 == 0)  execAddi(rd, rs1, imm);
    else if (funct3 == 1)
      {
	unsigned topBits = 0, shamt = 0;
	iform.getShiftFields(isRv64(), topBits, shamt);
	if (topBits == 0)
	  execSlli(rd, rs1, shamt);
	else
	  illegalInst();
      }
    else if (funct3 == 2)  execSlti(rd, rs1, imm);
    else if (funct3 == 3)  execSltiu(rd, rs1, imm);
    else if (funct3 == 4)  execXori(rd, rs1, imm);
    else if (funct3 == 5)
      {
	unsigned topBits = 0, shamt = 0;
	iform.getShiftFields(isRv64(), topBits, shamt);
	if (topBits == 0)
	  execSrli(rd, rs1, shamt);
	else
	  {
	    if (isRv64())
	      topBits <<= 1;
	    if (topBits == 0x20)
	      execSrai(rd, rs1, shamt);
	    else
	      illegalInst();
	  }
      }
    else if (funct3 == 6)  execOri(rd, rs1, imm);
    else if (funct3 == 7)  execAndi(rd, rs1, imm);
    else                   illegalInst();
  }
  return;

 l5:  // 00101   U-form
  {
    UFormInst uform(inst);
    execAuipc(uform.bits.rd, uform.immed());
  }
  return;

 l6:  // 00110  I-form
  {
    IFormInst iform(inst);
    unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
    int32_t imm = iform.immed();
    unsigned funct3 = iform.fields.funct3;
    if (funct3 == 0)
      execAddiw(rd, rs1, imm);
    else if (funct3 == 1)
      {
	if (iform.top7() != 0)
	  illegalInst();
	else
	  execSlliw(rd, rs1, iform.fields2.shamt);
      }
    else if (funct3 == 5)
      {
	if (iform.top7() == 0)
	  execSrliw(rd, rs1, iform.fields2.shamt);
	else if (iform.top7() == 0x20)
	  execSraiw(rd, rs1, iform.fields2.shamt);
	else
	  illegalInst();
      }
    else
      illegalInst();
  }
  return;

 l8:  // 01000  S-form
  {
    SFormInst sform(inst);
    unsigned rs1 = sform.bits.rs1, rs2 = sform.bits.rs2;
    unsigned funct3 = sform.bits.funct3;
    int32_t imm = sform.immed();
    if      (funct3 == 2)  execSw(rs1, rs2, imm);
    else if (funct3 == 0)  execSb(rs1, rs2, imm);
    else if (funct3 == 1)  execSh(rs1, rs2, imm);
    else if (funct3 == 3)  execSd(rs1, rs2, imm);
    else                   illegalInst();
  }
  return;

 l11:  // 01011  R-form atomics
  {
    if (not isRva())
      {
	illegalInst();
	return;
      }

    RFormInst rf(inst);
    uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
    uint32_t rd = rf.bits.rd, rs1 = rf.bits.rs1, rs2 = rf.bits.rs2;
    amoRl_ = rf.rl(); amoAq_ = rf.aq();
    if (f3 == 2)
      {
	if      (top5 == 0)     execAmoadd_w(rd, rs1, rs2);
	else if (top5 == 1)     execAmoswap_w(rd, rs1, rs2);
	else if (top5 == 2)     execLr_w(rd, rs1, rs2);
	else if (top5 == 3)     execSc_w(rd, rs1, rs2);
	else if (top5 == 4)     execAmoxor_w(rd, rs1, rs2);
	else if (top5 == 8)     execAmoor_w(rd, rs1, rs2);
	else if (top5 == 0x10)  execAmomin_w(rd, rs1, rs2);
	else if (top5 == 0x14)  execAmomax_w(rd, rs1, rs2);
	else if (top5 == 0x18)  execAmominu_w(rd, rs1, rs2);
	else if (top5 == 0x1c)  execAmomaxu_w(rd, rs1, rs2);
	else                    illegalInst();
      }
    else if (f3 == 3)
      {
	if      (not isRv64())  illegalInst();
	else if (top5 == 0)     execAmoadd_d(rd, rs1, rs2);
	else if (top5 == 1)     execAmoswap_d(rd, rs1, rs2);
	else if (top5 == 2)     execLr_d(rd, rs1, rs2);
	else if (top5 == 3)     execSc_d(rd, rs1, rs2);
	else if (top5 == 4)     execAmoxor_d(rd, rs1, rs2);
	else if (top5 == 8)     execAmoor_d(rd, rs1, rs2);
	else if (top5 == 0x10)  execAmomin_d(rd, rs1, rs2);
	else if (top5 == 0x14)  execAmomax_d(rd, rs1, rs2);
	else if (top5 == 0x18)  execAmominu_d(rd, rs1, rs2);
	else if (top5 == 0x1c)  execAmomaxu_d(rd, rs1, rs2);
	else                    illegalInst();
      }
    else illegalInst();
  }
  return;

 l12:  // 01100  R-form
  {
    RFormInst rform(inst);
    unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
    unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
    if (funct7 == 0)
      {
	if      (funct3 == 0) execAdd(rd, rs1, rs2);
	else if (funct3 == 1) execSll(rd, rs1, rs2);
	else if (funct3 == 2) execSlt(rd, rs1, rs2);
	else if (funct3 == 3) execSltu(rd, rs1, rs2);
	else if (funct3 == 4) execXor(rd, rs1, rs2);
	else if (funct3 == 5) execSrl(rd, rs1, rs2);
	else if (funct3 == 6) execOr(rd, rs1, rs2);
	else if (funct3 == 7) execAnd(rd, rs1, rs2);
      }
    else if (funct7 == 1)
      {
	if      (not isRvm()) illegalInst();
	else if (funct3 == 0) execMul(rd, rs1, rs2);
	else if (funct3 == 1) execMulh(rd, rs1, rs2);
	else if (funct3 == 2) execMulhsu(rd, rs1, rs2);
	else if (funct3 == 3) execMulhu(rd, rs1, rs2);
	else if (funct3 == 4) execDiv(rd, rs1, rs2);
	else if (funct3 == 5) execDivu(rd, rs1, rs2);
	else if (funct3 == 6) execRem(rd, rs1, rs2);
	else if (funct3 == 7) execRemu(rd, rs1, rs2);
      }
    else if (funct7 == 0x20)
      {
	if      (funct3 == 0) execSub(rd, rs1, rs2);
	else if (funct3 == 5) execSra(rd, rs1, rs2);
	else                  illegalInst();
      }
    else
      illegalInst();
  }
  return;

 l13:  // 01101  U-form
  {
    UFormInst uform(inst);
    execLui(uform.bits.rd, uform.immed());
  }
  return;

 l14: // 01110  R-Form
  {
    const RFormInst rform(inst);
    unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
    unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
    if (funct7 == 0)
      {
	if      (funct3 == 0)  execAddw(rd, rs1, rs2);
	else if (funct3 == 1)  execSllw(rd, rs1, rs2);
	else if (funct3 == 5)  execSrlw(rd, rs1, rs2);
	else                   illegalInst();
      }
    else if (funct7 == 1)
      {
	if      (funct3 == 0)  execMulw(rd, rs1, rs2);
	else if (funct3 == 4)  execDivw(rd, rs1, rs2);
	else if (funct3 == 5)  execDivuw(rd, rs1, rs2);
	else if (funct3 == 6)  execRemw(rd, rs1, rs2);
	else if (funct3 == 7)  execRemuw(rd, rs1, rs2);
	else                   illegalInst();
      }
    else if (funct7 == 0x20)
      {
	if      (funct3 == 0)  execSubw(rd, rs1, rs2);
	else if (funct3 == 5)  execSraw(rd, rs1, rs2);
	else                   illegalInst();
      }
    else
      illegalInst();
  }
  return;

 l24: // 11000   B-form
  {
    BFormInst bform(inst);
    unsigned rs1 = bform.bits.rs1, rs2 = bform.bits.rs2;
    unsigned funct3 = bform.bits.funct3;
    int32_t imm = bform.immed();
    if      (funct3 == 0)  execBeq(rs1, rs2, imm);
    else if (funct3 == 1)  execBne(rs1, rs2, imm);
    else if (funct3 == 4)  execBlt(rs1, rs2, imm);
    else if (funct3 == 5)  execBge(rs1, rs2, imm);
    else if (funct3 == 6)  execBltu(rs1, rs2, imm);
    else if (funct3 == 7)  execBgeu(rs1, rs2, imm);
    else                   illegalInst();
  }
  return;

 l25:  // 11001  I-form
  {
    IFormInst iform(inst);
    if (iform.fields.funct3 == 0)
      execJalr(iform.fields.rd, iform.fields.rs1, iform.immed());
    else
      illegalInst();
  }
  return;

 l27:  // 11011  J-form
  {
    JFormInst jform(inst);
    execJal(jform.bits.rd, jform.immed());
  }
  return;

 l28:  // 11100  I-form
  {
    IFormInst iform(inst);
    unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
    uint32_t csr = iform.uimmed();
    switch (iform.fields.funct3)
      {
      case 0:
	{
	  uint32_t funct7 = csr >> 5;
	  if (funct7 == 0) // ecall ebreak uret
	    {
	      if (rs1 != 0 or rd != 0) illegalInst();
	      else if (csr == 0)     execEcall();
	      else if (csr == 1)     execEbreak();
	      else if (csr == 2)     execUret();
	      else                   illegalInst();
	    }
	  else if (funct7 == 9)
	    {
	      if (rd != 0) illegalInst();
	      else         unimplemented();  // sfence.vma
	    }
	  else if (csr == 0x102) execSret();
	  else if (csr == 0x302) execMret();
	  else if (csr == 0x105) execWfi();
	  else                   illegalInst();
	}
	break;
      case 1: execCsrrw(rd, rs1, csr); break;
      case 2: execCsrrs(rd, rs1, csr); break;
      case 3: execCsrrc(rd, rs1, csr); break;
      case 5: execCsrrwi(rd, rs1, csr); break;
      case 6: execCsrrsi(rd, rs1, csr); break;
      case 7: execCsrrci(rd, rs1, csr); break;
      default: illegalInst(); break;
      }
  }
}



template <typename URV>
void
Core<URV>::execute16(uint16_t inst)
{
  if (not isRvc())
    {
      illegalInst();
      return;
    }

  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  if (quadrant == 0)
    {
      if (funct3 == 0)   // illegal, c.addi4spn
	{
	  if (inst == 0)
	    illegalInst();
	  else
	    {
	      CiwFormInst ciwf(inst);
	      unsigned immed = ciwf.immed();
	      if (immed == 0)
		illegalInst();  // As of v2.3 of User-Level ISA (Dec 2107).
	      else
		execAddi(8+ciwf.bits.rdp, RegSp, immed);  // c.addi4spn
	    }
	  return;
	}

      if (funct3 == 1) // c.fld c.lq
	{
	  if (not isRvd())
	    illegalInst();
	  else
	    {
	      ClFormInst clf(inst);
	      execFld(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.ldImmed());
	    }
	  return;
	}

      if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  execLw(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed());
	  return;
	}

      if (funct3 == 3)  // c.flw, c.ld
	{
	  ClFormInst clf(inst);
	  if (isRv64())
	    execLd(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.ldImmed());
	  else
	    {  // c.flw
	      if (isRvf())
		execFlw(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed());
	      else
		illegalInst();
	    }
	  return;
	}

      if (funct3 == 5)  // c.fsd
	{
	  if (isRvd())
	    {
	      ClFormInst clf(inst);
	      execFsd(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.ldImmed());
	    }
	  else
	    illegalInst();
	  return;
	}

      if (funct3 == 6)  // c.sw
	{
	  CsFormInst cs(inst);
	  execSw(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.swImmed());
	  return;
	}

      if (funct3 == 7) // c.fsw, c.sd
	{
	  CsFormInst cs(inst);
	  if (isRv64())
	    execSd(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.sdImmed());
	  else
	    {
	      if (isRvf())
		execFsw(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.swImmed());
	      else
		illegalInst(); // c.fsw
	    }
	  return;
	}

      // funct3 is 4 (reserved).
      illegalInst();
      return;
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  execAddi(cif.bits.rd, cif.bits.rd, cif.addiImmed());
	  return;
	}
	  
      if (funct3 == 1)  // c.jal, in rv64 and rv128 this is c.addiw
	{
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      if (cif.bits.rd == 0)
		illegalInst();
	      else
		execAddiw(cif.bits.rd, cif.bits.rd, cif.addiImmed());
	    }
	  else
	    {
	      CjFormInst cjf(inst);
	      execJal(RegRa, cjf.immed());
	    }
	  return;
	}

      if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  execAddi(cif.bits.rd, RegX0, cif.addiImmed());
	  return;
	}

      if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    illegalInst();
	  else if (cif.bits.rd == RegSp)  // c.addi16sp
	    execAddi(cif.bits.rd, cif.bits.rd, immed16);
	  else
	    execLui(cif.bits.rd, cif.luiImmed());
	  return;
	}

      // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
      // c.subw c.addw
      if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.bits.rdp;
	  unsigned f2 = caf.bits.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		illegalInst(); // As of v2.3 of User-Level ISA (Dec 2107).
	      else
		execSrli(rd, rd, caf.shiftImmed());
	    }
	  else if (f2 == 1) // srai64, srai
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		illegalInst(); // As of v2.3 of User-Level ISA (Dec 2107).
	      else
		execSrai(rd, rd, caf.shiftImmed());
	    }
	  else if (f2 == 2)  // c.andi
	    execAndi(rd, rd, immed);
	  else  // f2 == 3: c.sub c.xor c.or c.subw c.addw
	    {
	      unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
	      unsigned rs2 = 8 + rs2p;
	      unsigned imm34 = (immed >> 3) & 3; // Bits 3 and 4 of immed
	      if ((immed & 0x20) == 0)  // Bit 5 of immed
		{
		  if      (imm34 == 0) execSub(rd, rd, rs2);
		  else if (imm34 == 1) execXor(rd, rd, rs2);
		  else if (imm34 == 2) execOr(rd, rd, rs2);
		  else                 execAnd(rd, rd, rs2);
		}
	      else
		{
		  if      (imm34 == 0) execSubw(rd, rd, rs2);
		  else if (imm34 == 1) execAddw(rd, rd, rs2);
		  else if (imm34 == 2) illegalInst(); // reserved
		  else                 illegalInst(); // reserved
		}
	    }
	  return;
	}

      if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  execJal(RegX0, cjf.immed());
	  return;
	}
	  
      if (funct3 == 6)  // c.beqz
	{
	  CbFormInst cbf(inst);
	  execBeq(8+cbf.bits.rs1p, RegX0, cbf.immed());
	  return;
	}

      // (funct3 == 7)  // c.bnez
      CbFormInst cbf(inst);
      execBne(8+cbf.bits.rs1p, RegX0, cbf.immed());
      return;
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.bits.ic5 != 0 and not isRv64())
	    illegalInst();
	  else
	    execSlli(cif.bits.rd, cif.bits.rd, immed);
	  return;
	}

      if (funct3 == 1)  // c.fldsp c.lqsp
	{
	  if (isRvd())
	    {
	      CiFormInst cif(inst);
	      execFld(cif.bits.rd, RegSp, cif.ldspImmed());
	    }
	  else
	    illegalInst();
	  return;
	}

      if (funct3 == 2)  // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  execLw(rd, RegSp, cif.lwspImmed());
	  return;
	}

      if (funct3 == 3)  // c.ldsp  c.flwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  if (isRv64())  // c.ldsp
	    execLd(rd, RegSp, cif.ldspImmed());
	  else if (isRvf())  // c.flwsp
	    execFlw(rd, RegSp, cif.lwspImmed());
	  else
	    illegalInst();
	  return;
	}

      if (funct3 == 4)   // c.jr c.mv c.ebreak c.jalr c.add
	{
	  CiFormInst cif(inst);
	  unsigned immed = cif.addiImmed();
	  unsigned rd = cif.bits.rd;
	  unsigned rs2 = immed & 0x1f;
	  if ((immed & 0x20) == 0)  // c.jr or c.mv
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    illegalInst();
		  else
		    execJalr(RegX0, rd, 0);
		}
	      else
		execAdd(rd, RegX0, rs2);
	    }
	  else  // c.ebreak, c.jalr or c.add 
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    execEbreak();
		  else
		    execJalr(RegRa, rd, 0);
		}
	      else
		execAdd(rd, rd, rs2);
	    }
	  return;
	}

      if (funct3 == 5)  // c.fsdsp c.sqsp
	{
	  if (isRvd())
	    {
	      CswspFormInst csw(inst);
	      execFsd(RegSp, csw.bits.rs2, csw.sdImmed());
	    }
	  else
	    illegalInst();
	  return;
	}

      if (funct3 == 6)  // c.swsp
	{
	  CswspFormInst csw(inst);
	  execSw(RegSp, csw.bits.rs2, csw.swImmed());  // imm(sp) <- rs2
	  return;
	}

      if (funct3 == 7)  // c.sdsp  c.fswsp
	{
	  if (isRv64())  // c.sdsp
	    {
	      CswspFormInst csw(inst);
	      execSd(RegSp, csw.bits.rs2, csw.sdImmed());
	    }
	  else if (isRvf())   // c.fswsp
	    {
	      CswspFormInst csw(inst);
	      execFsw(RegSp, csw.bits.rs2, csw.swImmed());  // imm(sp) <- rs2
	    }
	  else
	    illegalInst();
	  return;
	}
    }

  // quadrant 3
  illegalInst();
}


template <typename URV>
void
Core<URV>::disassembleInst(uint32_t inst, std::ostream& stream)
{
  // Decode and disassemble
  if ((inst & 0x3) == 0x3) 
    disassembleInst32(inst, stream);
  else
    disassembleInst16(inst, stream);
}


template <typename URV>
void
Core<URV>::disassembleInst(uint32_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst(inst, oss);
  str = oss.str();
}


template <typename URV>
bool
Core<URV>::expandInst(uint16_t inst, uint32_t& code32) const
{
  code32 = 0; // Start with an illegal instruction.

  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  if (quadrant == 0)
    {
      if (funct3 == 0)    // illegal, c.addi4spn
	{
	  if (inst == 0)
	    return false;
	  CiwFormInst ciwf(inst);
	  unsigned immed = ciwf.immed();
	  if (immed == 0)
	    return false;
	  return encodeAddi(8+ciwf.bits.rdp, RegSp, immed, code32);
	}

      if (funct3 == 1) // c.fld c.lq
	{
	  if (not isRvd())
	    return false;
	  ClFormInst clf(inst);
	  return encodeFld(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.ldImmed(),
			  code32);
	}

      if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  return encodeLw(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed(),
			  code32);
	}

      if (funct3 == 3) // c.flw, c.ld
	{
	  ClFormInst clf(inst);
	  if (isRv64())
	    return encodeLd(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.ldImmed(),
			    code32);
	  // c.flw
	  if (not isRvf())
	    return false;
	  return encodeFlw(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed(),
			   code32);
	}

      if (funct3 == 5)  // c.fsd, c.sq
	{
	  if (not isRvd())
	    return false;
	  CsFormInst cs(inst);
	  return encodeFsd(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.sdImmed(),
			   code32);
	}

      if (funct3 == 6)  // c.sw
	{
	  CsFormInst cs(inst);
	  return encodeSw(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.swImmed(),
			  code32);
	}

      if (funct3 == 7) // c.fsw, c.sd
	{
	  CsFormInst cs(inst);
	  if (not isRv64())
	    {
	      if (not isRvf())
		return false;
	      return encodeFsw(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.swImmed(),
			       code32);
	    }
	  return encodeSd(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.sdImmed(), code32);
	}

      // funct3 is 4 (reserved)
      return false;
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  return encodeAddi(cif.bits.rd, cif.bits.rd, cif.addiImmed(), code32);
	}
	  
      if (funct3 == 1)  // c.jal, in rv64 and rv128 this is c.addiw
	{
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      if (cif.bits.rd == 0)
		return false;
	      return encodeAddiw(cif.bits.rd, cif.bits.rd, cif.addiImmed(), code32);
	    }
	  else
	    {
	      CjFormInst cjf(inst);
	      return encodeJal(RegRa, cjf.immed(), 0, code32);
	    }
	}

      if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  return encodeAddi(cif.bits.rd, RegX0, cif.addiImmed(), code32);
	}

      if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    return false;
	  if (cif.bits.rd == RegSp)  // c.addi16sp
	    return encodeAddi(cif.bits.rd, cif.bits.rd, immed16, code32);
	  return encodeLui(cif.bits.rd, cif.luiImmed(), 0, code32);
	}

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
      if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.bits.rdp;
	  unsigned f2 = caf.bits.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		return false;  // As of v2.3 of User-Level ISA (Dec 2107).
	      return encodeSrli(rd, rd, caf.shiftImmed(), code32);
	    }
	  if (f2 == 1)  // srai64, srai
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		return false; // As of v2.3 of User-Level ISA (Dec 2107).
	      return encodeSrai(rd, rd, caf.shiftImmed(), code32);
	    }
	  if (f2 == 2)  // c.andi
	    return encodeAndi(rd, rd, immed, code32);

	  // f2 == 3: c.sub c.xor c.or c.subw c.addw
	  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
	  unsigned rs2 = 8 + rs2p;
	  unsigned imm34 = (immed >> 3) & 3; // Bits 3 and 4 of immed
	  if ((immed & 0x20) == 0)  // Bit 5 of immed
	    {
	      if (imm34 == 0) return encodeSub(rd, rd, rs2, code32);
	      if (imm34 == 1) return encodeXor(rd, rd, rs2, code32);
	      if (imm34 == 2) return encodeOr(rd, rd, rs2, code32);
	      return encodeAnd(rd, rd, rs2,  code32);
	    }
	  // Bit 5 of immed is 1
	  if (not isRv64())
	    return false;
	  if (imm34 == 0) return encodeSubw(rd, rd, rs2, code32);
	  if (imm34 == 1) return encodeAddw(rd, rd, rs2, code32);
	  if (imm34 == 2) return false; // reserved
	  return false; // reserved
	}

      if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  return encodeJal(RegX0, cjf.immed(), 0, code32);
	}
	  
      if (funct3 == 6) // c.beqz
	{
	  CbFormInst cbf(inst);
	  return encodeBeq(8+cbf.bits.rs1p, RegX0, cbf.immed(), code32);
	}

      // funct3 == 7: c.bnez
      CbFormInst cbf(inst);
      return encodeBne(8+cbf.bits.rs1p, RegX0, cbf.immed(), code32);
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.bits.ic5 != 0 and not isRv64())
	    return false;
	  return encodeSlli(cif.bits.rd, cif.bits.rd, immed, code32);
	}

      if (funct3 == 1) // c.fldsp c.lqsp
	{
	  if (isRvd())
	    {
	      CiFormInst cif(inst);
	      return encodeFld(cif.bits.rd, RegSp, cif.ldspImmed(), code32);
	    }
	  return false;
	}

      if (funct3 == 2) // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  // rd == 0 is legal per Andrew Watterman
	  return encodeLw(rd, RegSp, cif.lwspImmed(), code32);
	}

      if (funct3 == 3)  // c.ldsp  c.flwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  if (isRv64())  // c.ldsp
	    return encodeLd(rd, RegSp, cif.ldspImmed(), code32);
	  if (isRvf())  // c.flwsp
	    return encodeLw(rd, RegSp, cif.lwspImmed(), code32);
	  return false;
	}

      if (funct3 == 4) // c.jr c.mv c.ebreak c.jalr c.add
	{
	  CiFormInst cif(inst);
	  unsigned immed = cif.addiImmed();
	  unsigned rd = cif.bits.rd;
	  unsigned rs2 = immed & 0x1f;
	  if ((immed & 0x20) == 0)  // c.jr or c.mv
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return false;
		  return encodeJalr(RegX0, rd, 0, code32);
		}
	      return encodeAdd(rd, RegX0, rs2, code32);
	    }
	  else  // c.ebreak, c.jalr or c.add 
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return encodeEbreak(0, 0, 0, code32);
		  return encodeJalr(RegRa, rd, 0, code32);
		}
	      return encodeAdd(rd, rd, rs2, code32);
	    }
	}

      if (funct3 == 5)  // c.fsdsp c.sqsp
	{
	  if (isRvd())
	    {
	      CswspFormInst csw(inst);
	      return encodeFsd(RegSp, csw.bits.rs2, csw.sdImmed(), code32);
	    }
	  return false;
	}

      if (funct3 == 6) // c.swsp
	{
	  CswspFormInst csw(inst);
	  return encodeSw(RegSp, csw.bits.rs2, csw.swImmed(), code32);
	}

      if (funct3 == 7)  // c.sdsp  c.fswsp
	{
	  if (isRv64())  // c.sdsp
	    {
	      CswspFormInst csw(inst);
	      return encodeSd(RegSp, csw.bits.rs2, csw.sdImmed(), code32);
	    }
	  if (isRvf())   // c.fswsp
	    {
	      CswspFormInst csw(inst);
	      return encodeSw(RegSp, csw.bits.rs2, csw.swImmed(), code32);
	    }
	  return false;
	}

      return false;
    }

  return false; // quadrant 3
}


template <typename URV>
const InstInfo&
Core<URV>::decodeFp(uint32_t inst, uint32_t& op0, uint32_t& op1, int32_t& op2)
{
  if (not isRvf())
    return instTable_.getInstInfo(InstId::illegal);  

  RFormInst rform(inst);
  op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
  unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
  instRoundingMode_ = RoundingMode(f3);
  if (f7 & 1)
    {
      if (not isRvd())
	return instTable_.getInstInfo(InstId::illegal);  

      if (f7 == 1)              return instTable_.getInstInfo(InstId::fadd_d);
      if (f7 == 5)              return instTable_.getInstInfo(InstId::fsub_d);
      if (f7 == 9)              return instTable_.getInstInfo(InstId::fmul_d);
      if (f7 == 0xd)            return instTable_.getInstInfo(InstId::fdiv_d);
      if (f7 == 0x11)
	{
	  if (f3 == 0)          return instTable_.getInstInfo(InstId::fsgnj_d);
	  if (f3 == 1)          return instTable_.getInstInfo(InstId::fsgnjn_d);
	  if (f3 == 2)          return instTable_.getInstInfo(InstId::fsgnjx_d);
	}
      if (f7 == 0x15)
	{
	  if (f3 == 0)          return instTable_.getInstInfo(InstId::fmin_d);
	  if (f3 == 1)          return instTable_.getInstInfo(InstId::fmax_d);
	}
      if (f7==0x21 and op2==0)  return instTable_.getInstInfo(InstId::fcvt_d_s);
      if (f7 == 0x2d)           return instTable_.getInstInfo(InstId::fsqrt_d);
      if (f7 == 0x51)
	{
	  if (f3 == 0)          return instTable_.getInstInfo(InstId::fle_d);
	  if (f3 == 1)          return instTable_.getInstInfo(InstId::flt_d);
	  if (f3 == 2)          return instTable_.getInstInfo(InstId::feq_d);
	}
      if (f7 == 0x61)
	{
	  if (op2 == 0)         return instTable_.getInstInfo(InstId::fcvt_w_d);
	  if (op2 == 1)         return instTable_.getInstInfo(InstId::fcvt_wu_d);
	  if (op2 == 2)         return instTable_.getInstInfo(InstId::fcvt_l_d);
	  if (op2 == 3)         return instTable_.getInstInfo(InstId::fcvt_lu_d);
	}
      if (f7 == 0x69)
	{
	  if (op2 == 0)         return instTable_.getInstInfo(InstId::fcvt_d_w);
	  if (op2 == 1)         return instTable_.getInstInfo(InstId::fcvt_d_wu);
	  if (op2 == 2)         return instTable_.getInstInfo(InstId::fcvt_d_l);
	  if (op2 == 3)         return instTable_.getInstInfo(InstId::fcvt_d_lu);
	}
      if (f7 == 0x71)
	{
	  if (op2==0 and f3==0) return instTable_.getInstInfo(InstId::fmv_x_d);
	  if (op2==0 and f3==1) return instTable_.getInstInfo(InstId::fclass_d);
	}
      if (f7 == 0x74)
	if (op2==0 and f3==0)   return instTable_.getInstInfo(InstId::fmv_w_x);
      if (f7 == 0x79)
	if (op2==0 and f3==0)   return instTable_.getInstInfo(InstId::fmv_d_x);

      return instTable_.getInstInfo(InstId::illegal);
    }

  if (f7 == 0)      return instTable_.getInstInfo(InstId::fadd_s);
  if (f7 == 4)      return instTable_.getInstInfo(InstId::fsub_s);
  if (f7 == 8)      return instTable_.getInstInfo(InstId::fmul_s);
  if (f7 == 0xc)    return instTable_.getInstInfo(InstId::fdiv_s);
  if (f7 == 0x2c)   return instTable_.getInstInfo(InstId::fsqrt_s);
  if (f7 == 0x10)
    {
      if (f3 == 0)  return instTable_.getInstInfo(InstId::fsgnj_s);
      if (f3 == 1)  return instTable_.getInstInfo(InstId::fsgnjn_s);
      if (f3 == 2)  return instTable_.getInstInfo(InstId::fsgnjx_s);
    }
  if (f7 == 0x14)
    {
      if (f3 == 0)  return instTable_.getInstInfo(InstId::fmin_s);
      if (f3 == 1)  return instTable_.getInstInfo(InstId::fmax_s);
    }
  if (f7 == 0x50)
    {
      if (f3 == 0)  return instTable_.getInstInfo(InstId::fle_s);
      if (f3 == 1)  return instTable_.getInstInfo(InstId::flt_s);
      if (f3 == 2)  return instTable_.getInstInfo(InstId::feq_s);
      return instTable_.getInstInfo(InstId::illegal);
    }
  if (f7 == 0x60)
    {
      if (op2 == 0) return instTable_.getInstInfo(InstId::fcvt_w_s);
      if (op2 == 1) return instTable_.getInstInfo(InstId::fcvt_wu_s);
      if (op2 == 2) return instTable_.getInstInfo(InstId::fcvt_l_s);
      if (op2 == 3) return instTable_.getInstInfo(InstId::fcvt_lu_s);
      return instTable_.getInstInfo(InstId::illegal);
    }
  if (f7 == 0x68)
    {
      if (op2 == 0) return instTable_.getInstInfo(InstId::fcvt_s_w);
      if (op2 == 1) return instTable_.getInstInfo(InstId::fcvt_s_wu);
      if (op2 == 2) return instTable_.getInstInfo(InstId::fcvt_s_l);
      if (op2 == 3) return instTable_.getInstInfo(InstId::fcvt_s_lu);
      return instTable_.getInstInfo(InstId::illegal);
    }
  if (f7 == 0x70)
    {
      if (op2 == 0)
	{
	  if (f3 == 0) return instTable_.getInstInfo(InstId::fmv_x_w);
	  if (f3 == 1) return instTable_.getInstInfo(InstId::fclass_s);
	}
    }
  if (f7 == 0x74)
    {
      if (op2 == 0)
	if (f3 == 0) return instTable_.getInstInfo(InstId::fmv_w_x);
    }
  return instTable_.getInstInfo(InstId::illegal);
}


template <typename URV>
const InstInfo&
Core<URV>::decode16(uint32_t inst, uint32_t& op0, uint32_t& op1, int32_t& op2)
{
  inst = (inst << 16) >> 16;  // Clear top 16 bits.

  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  op0 = 0; op1 = 0; op2 = 0;

  if (quadrant == 0)
    {
      if (funct3 == 0)    // illegal, c.addi4spn
	{
	  if (inst == 0)
	    return instTable_.getInstInfo(InstId::illegal);
	  CiwFormInst ciwf(inst);
	  unsigned immed = ciwf.immed();
	  if (immed == 0)
	    return instTable_.getInstInfo(InstId::illegal);
	  op0 = 8 + ciwf.bits.rdp; op1 = RegSp; op2 = immed;
	  return instTable_.getInstInfo(InstId::c_addi4spn);
	}

      if (funct3 == 1) // c.fld c.lq
	{
	  if (not isRvd())
	    return instTable_.getInstInfo(InstId::illegal);
	  ClFormInst clf(inst);
	  op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p; op2 = clf.ldImmed();
	  return instTable_.getInstInfo(InstId::c_fld);
	}

      if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p; op2 = clf.lwImmed();
	  return instTable_.getInstInfo(InstId::c_lw);
	}

      if (funct3 == 3) // c.flw, c.ld
	{
	  ClFormInst clf(inst);
	  if (isRv64())
	    {
	      op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p; op2 = clf.ldImmed();
	      return instTable_.getInstInfo(InstId::c_ld);
	    }

	  // c.flw
	  if (isRvf())
	    {
	      op0 = 8+clf.bits.rdp; op1 = 8+clf.bits.rs1p;
	      op2 = clf.lwImmed();
	      return instTable_.getInstInfo(InstId::c_flw);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 6)  // c.sw
	{
	  CsFormInst cs(inst);
	  op0 = 8+cs.bits.rs1p; op1 = 8+cs.bits.rs2p; op2 = cs.swImmed();
	  return instTable_.getInstInfo(InstId::c_sw);
	}

      if (funct3 == 7) // c.fsw, c.sd
	{
	  CsFormInst cs(inst);  // Double check this
	  if (not isRv64())
	    {
	      if (isRvf())
		{
		  op0=8+cs.bits.rs1p; op1=8+cs.bits.rs2p; op2 = cs.swImmed();
		  return instTable_.getInstInfo(InstId::c_fsw);
		}
	      return instTable_.getInstInfo(InstId::illegal);
	    }
	  op0=8+cs.bits.rs1p; op1=8+cs.bits.rs2p; op2 = cs.sdImmed();
	  return instTable_.getInstInfo(InstId::c_sd);
	}

      // funct3 is 1 (c.fld c.lq), or 4 (reserved), or 5 (c.fsd c.sq)
      return instTable_.getInstInfo(InstId::illegal);
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  op0 = cif.bits.rd; op1 = cif.bits.rd; op2 = cif.addiImmed();
	  return instTable_.getInstInfo(InstId::c_addi);
	}
	  
      if (funct3 == 1)  // c.jal,  in rv64 and rv128 this is c.addiw
	{
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      if (cif.bits.rd == 0)
		return instTable_.getInstInfo(InstId::illegal);
	      else
		return instTable_.getInstInfo(InstId::c_addiw);
	    }
	  else
	    {
	      CjFormInst cjf(inst);
	      op0 = RegRa; op1 = cjf.immed(); op2 = 0;
	      return instTable_.getInstInfo(InstId::c_jal);
	    }
	}

      if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  op0 = cif.bits.rd; op1 = RegX0; op2 = cif.addiImmed();
	  return instTable_.getInstInfo(InstId::c_li);
	}

      if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    return instTable_.getInstInfo(InstId::illegal);
	  if (cif.bits.rd == RegSp)  // c.addi16sp
	    {
	      op0 = cif.bits.rd; op1 = cif.bits.rd; op2 = immed16;
	      return instTable_.getInstInfo(InstId::c_addi16sp);
	    }
	  op0 = cif.bits.rd; op1 = cif.luiImmed(); op2 = 0;
	  return instTable_.getInstInfo(InstId::c_lui);
	}

      // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
      // c.subw c.addw
      if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.bits.rdp;
	  unsigned f2 = caf.bits.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		return instTable_.getInstInfo(InstId::illegal);
	      op0 = rd; op1 = rd; op2 = caf.shiftImmed();
	      return instTable_.getInstInfo(InstId::c_srli);
	    }
	  if (f2 == 1)  // srai64, srai
	    {
	      if (caf.bits.ic5 != 0 and not isRv64())
		return instTable_.getInstInfo(InstId::illegal);
	      op0 = rd; op1 = rd; op2 = caf.shiftImmed();
	      return instTable_.getInstInfo(InstId::c_srai);
	    }
	  if (f2 == 2)  // c.andi
	    {
	      op0 = rd; op1 = rd; op2 = immed;
	      return instTable_.getInstInfo(InstId::c_andi);
	    }

	  // f2 == 3: c.sub c.xor c.or c.subw c.addw
	  unsigned rs2p = (immed & 0x7); // Lowest 3 bits of immed
	  unsigned rs2 = 8 + rs2p;
	  unsigned imm34 = (immed >> 3) & 3; // Bits 3 and 4 of immed
	  op0 = rd; op1 = rd; op2 = rs2;
	  if ((immed & 0x20) == 0)  // Bit 5 of immed
	    {
	      if (imm34 == 0) return instTable_.getInstInfo(InstId::c_sub);
	      if (imm34 == 1) return instTable_.getInstInfo(InstId::c_xor);
	      if (imm34 == 2) return instTable_.getInstInfo(InstId::c_or);
	      return instTable_.getInstInfo(InstId::c_and);
	    }
	  // Bit 5 of immed is 1
	  if (not isRv64())
	    return instTable_.getInstInfo(InstId::illegal);
	  if (imm34 == 0) return instTable_.getInstInfo(InstId::c_subw);
	  if (imm34 == 1) return instTable_.getInstInfo(InstId::c_addw);
	  if (imm34 == 2) return instTable_.getInstInfo(InstId::illegal);
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  op0 = RegX0; op1 = cjf.immed(); op2 = 0;
	  return instTable_.getInstInfo(InstId::c_j);
	}
	  
      if (funct3 == 6) // c.beqz
	{
	  CbFormInst cbf(inst);
	  op0=8+cbf.bits.rs1p; op1=RegX0; op2=cbf.immed();
	  return instTable_.getInstInfo(InstId::c_beqz);
	}
      
      // funct3 == 7: c.bnez
      CbFormInst cbf(inst);
      op0 = 8+cbf.bits.rs1p; op1=RegX0; op2=cbf.immed();
      return instTable_.getInstInfo(InstId::c_bnez);
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.bits.ic5 != 0 and not isRv64())
	    return instTable_.getInstInfo(InstId::illegal);
	  op0 = cif.bits.rd; op1 = cif.bits.rd; op2 = immed;
	  return instTable_.getInstInfo(InstId::c_slli);
	}

      if (funct3 == 1)  // c.fldsp c.lqsp
	{
	  if (isRvd())
	    {
	      CiFormInst cif(inst);
	      op0 = cif.bits.rd; op1 = RegSp, op2 = cif.ldspImmed();
	      return instTable_.getInstInfo(InstId::c_fldsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 2) // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  // rd == 0 is legal per Andrew Watterman
	  op0 = rd; op1 = RegSp; op2 = cif.lwspImmed();
	  return instTable_.getInstInfo(InstId::c_lwsp);
	}

      else  if (funct3 == 3)  // c.ldsp  c.flwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  if (isRv64())
	    {
	      op0 = rd; op1 = RegSp; op2 = cif.ldspImmed();
	      return instTable_.getInstInfo(InstId::c_ldsp);
	    }
	  if (isRvf())
	    {
	      op0 = rd; op1 = RegSp; op2 = cif.lwspImmed();
	      return instTable_.getInstInfo(InstId::c_lwsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 4) // c.jr c.mv c.ebreak c.jalr c.add
	{
	  CiFormInst cif(inst);
	  unsigned immed = cif.addiImmed();
	  unsigned rd = cif.bits.rd;
	  unsigned rs2 = immed & 0x1f;
	  if ((immed & 0x20) == 0)  // c.jr or c.mv
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return instTable_.getInstInfo(InstId::illegal);
		  op0 = RegX0; op1 = rd; op2 = 0;
		  return instTable_.getInstInfo(InstId::c_jr);
		}
	      op0 = rd; op1 = RegX0; op2 = rs2;
	      return instTable_.getInstInfo(InstId::c_mv);
	    }
	  else  // c.ebreak, c.jalr or c.add 
	    {
	      if (rs2 == RegX0)
		{
		  if (rd == RegX0)
		    return instTable_.getInstInfo(InstId::c_ebreak);
		  op0 = RegRa; op1 = rd; op2 = 0;
		  return instTable_.getInstInfo(InstId::c_jalr);
		}
	      op0 = rd; op1 = rd; op2 = rs2;
	      return instTable_.getInstInfo(InstId::c_add);
	    }
	}

      if (funct3 == 5)  // c.fsdsp c.sqsp
	{
	  if (isRvd())
	    {
	      CswspFormInst csw(inst);
	      op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.sdImmed();
	      return instTable_.getInstInfo(InstId::c_fsdsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      if (funct3 == 6) // c.swsp
	{
	  CswspFormInst csw(inst);
	  op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.swImmed();
	  return instTable_.getInstInfo(InstId::c_swsp);
	}

      if (funct3 == 7)  // c.sdsp  c.fswsp
	{
	  if (isRv64())  // c.sdsp
	    {
	      CswspFormInst csw(inst);
	      op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.sdImmed();
	      return instTable_.getInstInfo(InstId::c_sdsp);
	    }
	  if (isRvf())   // c.fswsp
	    {
	      CswspFormInst csw(inst);
	      op0 = RegSp; op1 = csw.bits.rs2; op2 = csw.swImmed();
	      return instTable_.getInstInfo(InstId::c_fswsp);
	    }
	  return instTable_.getInstInfo(InstId::illegal);
	}

      return instTable_.getInstInfo(InstId::illegal);
    }

  return instTable_.getInstInfo(InstId::illegal); // quadrant 3
}


template <typename URV>
const InstInfo&
Core<URV>::decode(uint32_t inst, uint32_t& op0, uint32_t& op1, int32_t& op2)
{
  static void *opcodeLabels[] = { &&l0, &&l1, &&l2, &&l3, &&l4, &&l5,
				  &&l6, &&l7, &&l8, &&l9, &&l10, &&l11,
				  &&l12, &&l13, &&l14, &&l15, &&l16, &&l17,
				  &&l18, &&l19, &&l20, &&l21, &&l22, &&l23,
				  &&l24, &&l25, &&l26, &&l27, &&l28, &&l29,
				  &&l30, &&l31 };

  if (isCompressedInst(inst))
    {
      // return decode16(inst, op0, op1, op2);
      if (not isRvc())
	inst = 0; // All zeros: illegal 16-bit instruction.
      return decode16(inst, op0, op1, op2);
    }

  op0 = 0; op1 = 0; op2 = 0;

  bool quad3 = (inst & 0x3) == 0x3;
  if (quad3)
    {
      unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

      goto *opcodeLabels[opcode];


    l0:  // 00000   I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed(); 
	switch (iform.fields.funct3)
	  {
	  case 0:  return instTable_.getInstInfo(InstId::lb);
	  case 1:  return instTable_.getInstInfo(InstId::lh);
	  case 2:  return instTable_.getInstInfo(InstId::lw);
	  case 3:  return instTable_.getInstInfo(InstId::ld);
	  case 4:  return instTable_.getInstInfo(InstId::lbu);
	  case 5:  return instTable_.getInstInfo(InstId::lhu);
	  case 6:  return instTable_.getInstInfo(InstId::lwu);
	  default: return instTable_.getInstInfo(InstId::illegal);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l1:
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	uint32_t f3 = iform.fields.funct3;
	if      (f3 == 2)  return instTable_.getInstInfo(InstId::flw);
	else if (f3 == 3)  return instTable_.getInstInfo(InstId::fld);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l2:
    l7:
      return instTable_.getInstInfo(InstId::illegal);

    l9:
      {
	SFormInst sform(inst);
	op0 = sform.bits.rs1;
	op1 = sform.bits.rs2;
	op2 = sform.immed();
	unsigned funct3 = sform.bits.funct3;
	if      (funct3 == 2)  return instTable_.getInstInfo(InstId::fsw);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::fsd);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l10:
    l15:
      return instTable_.getInstInfo(InstId::illegal);

    l16:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    return instTable_.getInstInfo(InstId::fmadd_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l17:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    return instTable_.getInstInfo(InstId::fmsub_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l18:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    return instTable_.getInstInfo(InstId::fnmsub_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l19:
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd, op1 = rform.bits.rs1, op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	instRoundingMode_ = RoundingMode(funct3);
	if ((funct7 & 3) == 0)
	  {
	    instRs3_ = funct7 >> 2;
	    return instTable_.getInstInfo(InstId::fnmadd_s);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l20:
      return decodeFp(inst, op0, op1, op2);

    l21:
    l22:
    l23:
    l26:
    l29:
    l30:
    l31:
      return instTable_.getInstInfo(InstId::illegal);

    l3: // 00011  I-form
      {
	IFormInst iform(inst);
	unsigned funct3 = iform.fields.funct3;
	if (iform.fields.rd == 0 and iform.fields.rs1 == 0)
	  {
	    if (funct3 == 0)
	      {
		if (iform.top4() == 0)
		  {
		    op0 = iform.pred();
		    op1 = iform.succ();
		    return instTable_.getInstInfo(InstId::fence);
		  }
	      }
	    else if (funct3 == 1)
	      {
		if (iform.uimmed() == 0)
		  return instTable_.getInstInfo(InstId::fencei);
	      }
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l4:  // 00100  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	unsigned funct3 = iform.fields.funct3;

	if      (funct3 == 0)  return instTable_.getInstInfo(InstId::addi);
	else if (funct3 == 1)
	  {
	    unsigned topBits = 0, shamt = 0;
	    iform.getShiftFields(isRv64(), topBits, shamt);
	    if (topBits == 0)
	      {
		op2 = shamt;
		return instTable_.getInstInfo(InstId::slli);
	      }
	  }
	else if (funct3 == 2)  return instTable_.getInstInfo(InstId::slti);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::sltiu);
	else if (funct3 == 4)  return instTable_.getInstInfo(InstId::xori);
	else if (funct3 == 5)
	  {
	    unsigned topBits = 0, shamt = 0;
	    iform.getShiftFields(isRv64(), topBits, shamt);
	    op2 = shamt;
	    if (topBits == 0)
	      return instTable_.getInstInfo(InstId::srli);
	    if (isRv64())
	      topBits <<= 1;
	    if (topBits == 0x20)
	      return instTable_.getInstInfo(InstId::srai);
	  }
	else if (funct3 == 6)  return instTable_.getInstInfo(InstId::ori);
	else if (funct3 == 7)  return instTable_.getInstInfo(InstId::andi);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l5:  // 00101   U-form
      {
	UFormInst uform(inst);
	op0 = uform.bits.rd;
	op1 = uform.immed();
	return instTable_.getInstInfo(InstId::auipc);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l6:  // 00110  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	unsigned funct3 = iform.fields.funct3;
	if (funct3 == 0)
	  return instTable_.getInstInfo(InstId::addiw);
	else if (funct3 == 1)
	  {
	    if (iform.top7() == 0)
	      {
		op2 = iform.fields2.shamt;
		return instTable_.getInstInfo(InstId::slliw);
	      }
	  }
	else if (funct3 == 5)
	  {
	    op2 = iform.fields2.shamt;
	    if (iform.top7() == 0)
	      return instTable_.getInstInfo(InstId::srliw);
	    else if (iform.top7() == 0x20)
	      return instTable_.getInstInfo(InstId::sraiw);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l8:  // 01000  S-form
      {
	SFormInst sform(inst);
	op0 = sform.bits.rs1;
	op1 = sform.bits.rs2;
	op2 = sform.immed();
	uint32_t funct3 = sform.bits.funct3;

	if (funct3 == 0) return instTable_.getInstInfo(InstId::sb);
	if (funct3 == 1) return instTable_.getInstInfo(InstId::sh);
	if (funct3 == 2) return instTable_.getInstInfo(InstId::sw);
	if (funct3 == 3 and isRv64()) return instTable_.getInstInfo(InstId::sd);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l11:  // 01011  R-form atomics
      if (not isRva())
	return instTable_.getInstInfo(InstId::illegal);

      if (false)  // Not implemented
      {
	if (not isRva())
	  return instTable_.getInstInfo(InstId::illegal);
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
	op0 = rf.bits.rd; op1 = rf.bits.rs1; op2 = rf.bits.rs2;

	if (f3 == 2)
	  {
	    if (top5 == 0)    return instTable_.getInstInfo(InstId::amoadd_w);
	    if (top5 == 1)    return instTable_.getInstInfo(InstId::amoswap_w);
	    if (top5 == 2)    return instTable_.getInstInfo(InstId::lr_w);
	    if (top5 == 3)    return instTable_.getInstInfo(InstId::sc_w);
	    if (top5 == 4)    return instTable_.getInstInfo(InstId::amoxor_w);
	    if (top5 == 8)    return instTable_.getInstInfo(InstId::amoor_w);
	    if (top5 == 0x0c) return instTable_.getInstInfo(InstId::amoand_w);
	    if (top5 == 0x10) return instTable_.getInstInfo(InstId::amomin_w);
	    if (top5 == 0x14) return instTable_.getInstInfo(InstId::amomax_w);
	    if (top5 == 0x18) return instTable_.getInstInfo(InstId::amominu_w);
	    if (top5 == 0x1c) return instTable_.getInstInfo(InstId::amomaxu_w);
	  }
	else if (f3 == 3)
	  {
	    if (not isRv64()) return instTable_.getInstInfo(InstId::illegal);
	    if (top5 == 0)    return instTable_.getInstInfo(InstId::amoadd_d);
	    if (top5 == 1)    return instTable_.getInstInfo(InstId::amoswap_d);
	    if (top5 == 2)    return instTable_.getInstInfo(InstId::lr_d);
	    if (top5 == 3)    return instTable_.getInstInfo(InstId::sc_d);
	    if (top5 == 4)    return instTable_.getInstInfo(InstId::amoxor_d);
	    if (top5 == 8)    return instTable_.getInstInfo(InstId::amoor_d);
	    if (top5 == 0xc)  return instTable_.getInstInfo(InstId::amoand_d);
	    if (top5 == 0x10) return instTable_.getInstInfo(InstId::amomin_d);
	    if (top5 == 0x14) return instTable_.getInstInfo(InstId::amomax_d);
	    if (top5 == 0x18) return instTable_.getInstInfo(InstId::amominu_d);
	    if (top5 == 0x1c) return instTable_.getInstInfo(InstId::amomaxu_d);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l12:  // 01100  R-form
      {
	RFormInst rform(inst);
	op0 = rform.bits.rd;
	op1 = rform.bits.rs1;
	op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	if (funct7 == 0)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::add);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::sll);
	    else if (funct3 == 2) return instTable_.getInstInfo(InstId::slt);
	    else if (funct3 == 3) return instTable_.getInstInfo(InstId::sltu);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::xor_);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::srl);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::or_);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::and_);
	  }
	else if (funct7 == 1)
	  {
	    if      (not isRvm()) return instTable_.getInstInfo(InstId::illegal);
	    else if (funct3 == 0) return instTable_.getInstInfo(InstId::mul);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::mulh);
	    else if (funct3 == 2) return instTable_.getInstInfo(InstId::mulhsu);
	    else if (funct3 == 3) return instTable_.getInstInfo(InstId::mulhu);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::div);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::divu);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::rem);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::remu);
	  }
	else if (funct7 == 0x20)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::sub);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::sra);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l13:  // 01101  U-form
      {
	UFormInst uform(inst);
	op0 = uform.bits.rd;
	op1 = uform.immed();
	return instTable_.getInstInfo(InstId::lui);
      }

    l14: // 01110  R-Form
      {
	const RFormInst rform(inst);
	op0 = rform.bits.rd;
	op1 = rform.bits.rs1;
	op2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	if (funct7 == 0)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::addw);
	    else if (funct3 == 1) return instTable_.getInstInfo(InstId::sllw);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::srlw);
	  }
	else if (funct7 == 1)
	  {
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::mulw);
	    else if (funct3 == 4) return instTable_.getInstInfo(InstId::divw);
	    else if (funct3 == 5) return instTable_.getInstInfo(InstId::divuw);
	    else if (funct3 == 6) return instTable_.getInstInfo(InstId::remw);
	    else if (funct3 == 7) return instTable_.getInstInfo(InstId::remuw);
	  }
	else if (funct7 == 0x20)
	  {
	    if      (funct3 == 0)  return instTable_.getInstInfo(InstId::subw);
	    else if (funct3 == 5)  return instTable_.getInstInfo(InstId::sraw);
	  }
      }
      return instTable_.getInstInfo(InstId::illegal);

    l24: // 11000   B-form
      {
	BFormInst bform(inst);
	op0 = bform.bits.rs1;
	op1 = bform.bits.rs2;
	op2 = bform.immed();
	uint32_t funct3 = bform.bits.funct3;
	if      (funct3 == 0)  return instTable_.getInstInfo(InstId::beq);
	else if (funct3 == 1)  return instTable_.getInstInfo(InstId::bne);
	else if (funct3 == 4)  return instTable_.getInstInfo(InstId::blt);
	else if (funct3 == 5)  return instTable_.getInstInfo(InstId::bge);
	else if (funct3 == 6)  return instTable_.getInstInfo(InstId::bltu);
	else if (funct3 == 7)  return instTable_.getInstInfo(InstId::bgeu);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l25:  // 11001  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.immed();
	if (iform.fields.funct3 == 0)
	  return instTable_.getInstInfo(InstId::jalr);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l27:  // 11011  J-form
      {
	JFormInst jform(inst);
	op0 = jform.bits.rd;
	op1 = jform.immed();
	return instTable_.getInstInfo(InstId::jal);
      }

    l28:  // 11100  I-form
      {
	IFormInst iform(inst);
	op0 = iform.fields.rd;
	op1 = iform.fields.rs1;
	op2 = iform.uimmed(); // csr
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      uint32_t funct7 = op2 >> 5;
	      if (funct7 == 0) // ecall ebreak uret
		{
		  if (op1 != 0 or op0 != 0)
		    return instTable_.getInstInfo(InstId::illegal);
		  else if (op2 == 0)
		    return instTable_.getInstInfo(InstId::ecall);
		  else if (op2 == 1)
		    return instTable_.getInstInfo(InstId::ebreak);
		  else if (op2 == 2)
		    return instTable_.getInstInfo(InstId::uret);
		}
	      else if (funct7 == 9)
		{
		  if (op0 != 0)
		    return instTable_.getInstInfo(InstId::illegal);
		  else // sfence.vma
		    return instTable_.getInstInfo(InstId::illegal);
		}
	      else if (op2 == 0x102)
		return instTable_.getInstInfo(InstId::sret);
	      else if (op2 == 0x302)
		return instTable_.getInstInfo(InstId::mret);
	      else if (op2 == 0x105)
		return instTable_.getInstInfo(InstId::wfi);
	    }
	    break;
	  case 1:  return instTable_.getInstInfo(InstId::csrrw);
	  case 2:  return instTable_.getInstInfo(InstId::csrrs);
	  case 3:  return instTable_.getInstInfo(InstId::csrrc);
	  case 5:  return instTable_.getInstInfo(InstId::csrrwi);
	  case 6:  return instTable_.getInstInfo(InstId::csrrsi);
	  case 7:  return instTable_.getInstInfo(InstId::csrrci);
	  default: return instTable_.getInstInfo(InstId::illegal);
	  }
	return instTable_.getInstInfo(InstId::illegal);
      }
    }
  else
    return instTable_.getInstInfo(InstId::illegal);
}


std::string
roundingModeString(RoundingMode mode)
{
  switch (mode)
    {
    case RoundingMode::NearestEven: return "rne";
    case RoundingMode::Zero:        return "rtz";
    case RoundingMode::Down:        return "rdn";
    case RoundingMode::Up:          return "rup";
    case RoundingMode::NearestMax:  return "rmm";
    case RoundingMode::Invalid1:    return "inv1";
    case RoundingMode::Invalid2:    return "inv2";
    case RoundingMode::Dynamic:     return "dyn";
    default:                        return "inv";
    }
  return "inv";
}


template <typename URV>
void
Core<URV>::printInstRdRs1Rs2(std::ostream& stream, const char* inst,
			     unsigned rd, unsigned rs1, unsigned rs2)
{
  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 9 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << intRegs_.regName(rd, abiNames_) << ", "
	 << intRegs_.regName(rs1, abiNames_) << ", "
	 << intRegs_.regName(rs2, abiNames_);
}


template <typename URV>
void
Core<URV>::printInstLdSt(std::ostream& stream, const char* inst,
			 unsigned rd, unsigned rs1, int32_t imm)
{
  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  const char* sign = imm < 0? "-" : "";
  if (imm < 0)
    imm = -imm;

  // Keep least sig 12 bits.
  imm = imm & 0xfff;

  stream << intRegs_.regName(rd, abiNames_) << ", " << sign << "0x"
	 << std::hex << imm << "(" << intRegs_.regName(rs1, abiNames_) << ")";
}


template <typename URV>
void
Core<URV>::printInstFpLdSt(std::ostream& stream, const char* inst,
			   unsigned rd, unsigned rs1, int32_t imm)
{
  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  const char* sign = imm < 0? "-" : "";
  if (imm < 0)
    imm = -imm;

  // Keep least sig 12 bits.
  imm = imm & 0xfff;

  stream << "f" << rd << ", " << sign << "0x" << std::hex << imm
	 << "(" << intRegs_.regName(rs1, abiNames_) << ")";
}


template <typename URV>
void
Core<URV>::printInstShiftImm(std::ostream& stream, const char* inst,
			     unsigned rs1, unsigned rs2, uint32_t imm)
{
  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  if constexpr (sizeof(URV) == 4)
    imm = imm & 0x1f;
  else
    imm = imm & 0x3f;

  stream << intRegs_.regName(rs1, abiNames_) << ", "
	 << intRegs_.regName(rs2, abiNames_) << ", 0x"
	 << std::hex << imm;
}


template <typename URV>
void
Core<URV>::printInstRegRegImm12(std::ostream& stream, const char* inst,
				unsigned rs1, unsigned rs2, int32_t imm)
{
  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << intRegs_.regName(rs1, abiNames_) << ", "
	 << intRegs_.regName(rs2, abiNames_) << ", ";

  if (imm < 0)
    stream << "-0x" << std::hex << ((-imm) & 0xfff);
  else
    stream << "0x" << std::hex << (imm & 0xfff);
}


template <typename URV>
void
Core<URV>::printInstRegImm(std::ostream& stream, const char* inst,
			   unsigned rs1, int32_t imm)
{
  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << intRegs_.regName(rs1, abiNames_) << ", ";

  if (imm < 0)
    stream << "-0x" << std::hex << (-imm);
  else
    stream << "0x" << std::hex << imm;
}


template <typename URV>
void
Core<URV>::printFp32f(std::ostream& stream, const char* inst,
		      unsigned rd, unsigned rs1, unsigned rs2,
		      unsigned rs3, RoundingMode mode)
{
  if (not isRvf())
    {
      stream << "illegal";
      return;
    }

  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2 << ", f" << rs3
	 << ", " << roundingModeString(mode);
}


template <typename URV>
void
Core<URV>::printFp32d(std::ostream& stream, const char* inst,
		      unsigned rd, unsigned rs1, unsigned rs2,
		      unsigned rs3, RoundingMode mode)
{
  if (not isRvd())
    {
      stream << "illegal";
      return;
    }

  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2 << ", f" << rs3
	 << ", " << roundingModeString(mode);
}


template <typename URV>
void
Core<URV>::printFp32f(std::ostream& stream, const char* inst,
		      unsigned rd, unsigned rs1, unsigned rs2,
		      RoundingMode mode)
{
  if (not isRvf())
    {
      stream << "illegal";
      return;
    }

  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2
	   << ", " << roundingModeString(mode);
}


template <typename URV>
void
Core<URV>::printFp32d(std::ostream& stream, const char* inst,
		      unsigned rd, unsigned rs1, unsigned rs2,
		      RoundingMode mode)
{
  if (not isRvd())
    {
      stream << "illegal";
      return;
    }

  stream << inst;
  size_t len = strlen(inst);

  // Print instruction in a 8 character field.
  for (size_t i = len; i < 8; ++i)
    stream << ' ';
  stream << ' ';

  stream << "f" << rd << ", f" << rs1 << ", f" << rs2
	   << ", " << roundingModeString(mode);
}


template <typename URV>
void
Core<URV>::disassembleFp(uint32_t inst, std::ostream& os)
{
  if (not isRvf())
    {
      os << "illegal";
      return;
    }

  RFormInst rform(inst);
  unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
  unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
  RoundingMode mode = RoundingMode(f3);
  std::string rms = roundingModeString(mode);

  if (f7 & 1)
    {
      if (not isRvd())
	{
	  os << "illegal";
	  return;
	}

      if (f7 == 1)        printFp32d(os, "fadd.d", rd, rs1, rs2, mode);
      else if (f7 == 5)   printFp32d(os, "fsub.d", rd, rs1, rs2, mode);
      else if (f7 == 9)   printFp32d(os, "fmul.d", rd, rs1, rs2, mode);
      else if (f7 == 0xd) printFp32d(os, "fdiv.d", rd, rs1, rs2, mode);
      else if (f7 == 0x11)
	{
	  if      (f3 == 0) os << "fsgnj.d  f" << rd << ", f" << rs1;
	  else if (f3 == 1) os << "fsgnjn.d f" << rd << ", f" << rs1;
	  else if (f3 == 2) os << "fsgnjx.d f" << rd << ", f" << rs1;
	  else              os << "illegal";
	}
      else if (f7 == 0x15)
	{
	  if      (f3==0) os<< "fmin.d   f" << rd << ", f" << rs1 << ", f" << rs2;
	  else if (f3==1) os<< "fmax.d   f" << rd << ", f" << rs1 << ", f" << rs2;
	  else            os<< "illegal";
	}
      else if (f7 == 0x21 and rs2 == 0)
	os << "fcvt.d.s f" << rd << ", f" << rs1 << ", " << rms;
      else if (f7 == 0x2d)
	os << "fsqrt.d  f" << rd << ", f" << rs1 << ", " << rms;
      else if (f7 == 0x51)
	{
	  std::string rdn = intRegs_.regName(rd, abiNames_);
	  if      (f3==0)  os<< "fle.d    " << rdn << ", f" << rs1 << ", f" << rs2;
	  else if (f3==1)  os<< "flt.d    " << rdn << ", f" << rs1 << ", f" << rs2;
	  else if (f3==2)  os<< "feq.d    " << rdn << ", f" << rs1 << ", f" << rs2;
	  else             os<< "illegal";
	}
      else if (f7 == 0x61)
	{
	  std::string rdn = intRegs_.regName(rd, abiNames_);
	  if (rs2==0)
	    os << "fcvt.w.d "  << rdn << ", f" << rs1 << ", " << rms;
	  else if (rs2==1)
	    os << "fcvt.wu.d " << rdn << ", f" << rs1 << ", " << rms;
	  else
	    os << "illegal";
	}
      else if (f7 == 0x69)
	{
	  std::string rs1n = intRegs_.regName(rs1, abiNames_);
	  if (rs2==0)
	    os << "fcvt.d.w f"  << rd << ", " << rs1n << ", " << rms;
	  else if (rs2==1)
	    os << "fcvt.d.wu f" << rd << ", " << rs1n << ", " << rms;
	  else
	    os << "illegal";
	}
      else if (f7 == 0x71)
	{
	  std::string rdn = intRegs_.regName(rd, abiNames_);
	  if (rs2==0 and f3==0)  os << "fmv.x.d " << rdn << ", f" << rs1;
	  if (rs2==0 and f3==1)  os << "fclass.d " << rdn << ", f" << rs1;
	  else                   os << "illegal";
	}
      else if (f7 == 0x79)
	{
	  std::string rs1n = intRegs_.regName(rs1, abiNames_);
	  if (rs2 == 0 and f3 == 0)  os << "fmv.d.x  f" << rd << ", " << rs1n;
	  else                       os << "illegal";
	}
      else
	os << "illegal";
      return;
    }

  if (f7 == 0)          printFp32f(os, "fadd.s", rd, rs1, rs2, mode);
  else if (f7 == 4)     printFp32f(os, "fsub.s", rd, rs1, rs2, mode);
  else if (f7 == 8)     printFp32f(os, "fmul.s", rd, rs1, rs2, mode);
  else if (f7 == 0xc)   printFp32f(os, "div.s", rd, rs1, rs2, mode);
  else if (f7 == 0x10)
    {
      if      (f3 == 0) os << "fsgnj.s  f" << rd << ", f" << rs1;
      else if (f3 == 1) os << "fsgnjn.s f" << rd << ", f" << rs1;
      else if (f3 == 2) os << "fsgnjx.s f" << rd << ", f" << rs1;
      else              os << "illegal";
    }
  else if (f7 == 0x14)
    {
      if      (f3 == 0) os << "fmin.s  f" << rd << ", f" << rs1 << ", f" << rs2;
      else if (f3 == 1)	os << "fmax.s  f" << rd << ", f" << rs1 << ", f" << rs2;
      else              os << "illegal";
    }
  
  else if (f7 == 0x20 and rs2 == 1)
    os << "fcvt.s.d f" << rd << ", f" << rs1 << ", " << rms;
  else if (f7 == 0x2c)
    os << "fsqrt.s  f" << rd << ", f" << rs1 << ", " << rms;
  else if (f7 == 0x50)
    {
      std::string rdn = intRegs_.regName(rd, abiNames_);
      if      (f3 == 0) os << "fle.s    " << rdn << ", f" << rs1 << ", f" << rs2;
      else if (f3 == 1) os << "flt.s    " << rdn << ", f" << rs1 << ", f" << rs2;
      else if (f3 == 2) os << "feq.s    " << rdn << ", f" << rs1 << ", f" << rs2;
      else              os << "illegal";
    }
  else if (f7 == 0x60)
    {
      std::string rdn = intRegs_.regName(rd, abiNames_);
      if      (rs2==0) os << "fcvt.w.s "  << rdn << ", f" << rs1 << ", " << rms;
      else if (rs2==1) os << "fcvt.wu.s " << rdn << ", f" << rs1 << ", " << rms;
      else if (rs2==2) os << "fcvt.l.s "  << rdn << ", f" << rs1 << ", " << rms;
      else if (rs2==3) os << "fcvt.lu.s " << rdn << ", f" << rs1 << ", " << rms;
      else             os << "illegal";
    }
  else if (f7 == 0x68)
    {
      std::string rs1n = intRegs_.regName(rs1, abiNames_);

      if      (rs2==0) os << "fcvt.s.w f"  << rd << ", " << rs1n << ", " << rms;
      else if (rs2==1) os << "fcvt.s.wu f" << rd << ", " << rs1n << ", " << rms;
      else if (rs2==2) os << "fcvt.s.l f"  << rd << ", " << rs1n << ", " << rms;
      else if (rs2==3) os << "fcvt.s.lu f" << rd << ", " << rs1n << ", " << rms;
      else             os << "illegal";
    }
  else if (f7 == 0x70)
    {
      std::string rdn = intRegs_.regName(rd, abiNames_);
      if      (rs2 == 0 and f3 == 0)  os << "fmv.x.w  " << rdn << ", f" << rs1;
      else if (rs2 == 0 and f3 == 1)  os << "fclass.s " << rdn << ", f" << rs1;
      else                            os << "illegal";
    }
  else if (f7 == 0x74)
    {
      std::string rs1n = intRegs_.regName(rs1, abiNames_);
      if (rs2 == 0 and f3 == 0)  os << "fmv.w.x  f" << rd << ", " << rs1n;
      else                       os << "illegal";
    }
  else
    os << "illegal";
}


template <typename URV>
void
Core<URV>::printAmoInst(std::ostream& stream, const char* inst, bool aq,
			bool rl, unsigned rd, unsigned rs1, unsigned rs2)
{
  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ';
  stream << intRegs_.regName(rd,  abiNames_) << ", "
	 << intRegs_.regName(rs1, abiNames_) << ", "
    	 << intRegs_.regName(rs2, abiNames_);
}


template <typename URV>
void
Core<URV>::printLrInst(std::ostream& stream, const char* inst, bool aq,
		       bool rl, unsigned rd, unsigned rs1)
{
  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ';
  stream << intRegs_.regName(rd, abiNames_)
	 << ", (" << intRegs_.regName(rs1, abiNames_) << ")";
}


template <typename URV>
void
Core<URV>::printScInst(std::ostream& stream, const char* inst, bool aq,
		       bool rl, unsigned rd, unsigned rs1, unsigned rs2)
{
  stream << inst;

  if (aq)
    stream << ".aq";

  if (rl)
    stream << ".rl";

  stream << ' ';

  stream << intRegs_.regName(rd, abiNames_)
	 << ", " << intRegs_.regName(rs2, abiNames_)
	 << ", (" << intRegs_.regName(rs1) << ")";
}


template <typename URV>
void
Core<URV>::disassembleInst32(uint32_t inst, std::ostream& stream)
{
  if (not isFullSizeInst(inst))
    {
      stream << "illegal";  // Not a compressed instruction.
      return;
    }

  unsigned opcode = (inst & 0x7f) >> 2;  // Upper 5 bits of opcode.

  switch (opcode)
    {
    case 0:  // 00000   I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	switch (iform.fields.funct3)
	  {
	  case 0:  printInstLdSt(stream, "lb",  rd, rs1, imm); break;
	  case 1:  printInstLdSt(stream, "lh",  rd, rs1, imm); break;
	  case 2:  printInstLdSt(stream, "lw",  rd, rs1, imm); break;
	  case 3:  printInstLdSt(stream, "ld",  rd, rs1, imm); break;
	  case 4:  printInstLdSt(stream, "lbu", rd, rs1, imm); break;
	  case 5:  printInstLdSt(stream, "lhu", rd, rs1, imm); break;
	  case 6:  printInstLdSt(stream, "lwu", rd, rs1, imm); break;
	  default: stream << "illegal";                        break;
	  }
      }
      break;

    case 1:   // 0001 I-Form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	uint32_t f3 = iform.fields.funct3;
	if (f3 == 2)
	  {
	    if (isRvf())
	      printInstFpLdSt(stream, "flw", rd, rs1, imm);
	    else
	      stream << "illegal";
	  }
	else if (f3 == 3)
	  {
	    if (isRvd())
	      printInstFpLdSt(stream, "fld", rd, rs1, imm);
	    else
	      stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 3:  // 00011  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	unsigned funct3 = iform.fields.funct3;
	if (rd != 0 or rs1 != 0)
	  stream << "illegal";
	else if (funct3 == 0)
	  {
	    if (iform.top4() != 0)
	      stream << "illegal";
	    else
	      stream << "fence  " << iform.pred() << ", " << iform.succ();
	  }
	else if (funct3 == 1)
	  {
	    if (iform.uimmed() != 0)
	      stream << "illegal";
	    else
	      stream << "fence.i ";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 4:  // 00100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	switch (iform.fields.funct3)
	  {
	  case 0: 
	    printInstRegRegImm12(stream, "addi", rd, rs1, imm);
	    break;
	  case 1: 
	    {
	      unsigned topBits = 0, shamt = 0;
	      iform.getShiftFields(isRv64(), topBits, shamt);
	      if (topBits == 0)
		printInstShiftImm(stream, "slli", rd, rs1, shamt);
	      else
		stream << "illegal";
	    }
	    break;
	  case 2:
	    printInstRegRegImm12(stream, "slti", rd, rs1, imm);
	    break;
	  case 3:
	    printInstRegRegImm12(stream, "sltiu", rd, rs1, imm);
	    break;
	  case 4:
	    printInstRegRegImm12(stream, "xori", rd, rs1, imm);
	    break;
	  case 5:
	    {
	      unsigned topBits = 0, shamt = 0;
	      iform.getShiftFields(isRv64(), topBits, shamt);
	      if (topBits == 0)
		printInstShiftImm(stream, "srli", rd, rs1, shamt);
	      else
		{
		  if (isRv64())
		    topBits <<= 1;
		  if (topBits == 0x20)
		    printInstShiftImm(stream, "srai", rd, rs1, shamt);
		  else
		    stream << "illegal";
		}
	    }
	    break;
	  case 6:
	    printInstRegRegImm12(stream, "ori", rd, rs1, imm);
	    break;
	  case 7:
	    printInstRegRegImm12(stream, "andi", rd, rs1, imm);
	    break;
	  default:
	    stream << "illegal";
	    break;
	  }
      }
      break;

    case 5:  // 00101   U-form
      {
	UFormInst uform(inst);
	stream << "auipc    " << intRegs_.regName(uform.bits.rd, abiNames_)
	       << ", 0x" << std::hex << ((uform.immed() >> 12) & 0xfffff);
      }
      break;

    case 6:  // 00110  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	int32_t imm = iform.immed();
	unsigned funct3 = iform.fields.funct3;
	if (funct3 == 0)
	  {
	    if (isRv64())
	      printInstRegRegImm12(stream, "addi", rd, rs1, imm);
	    else
	      stream << "illegal";
	  }
	else if (funct3 == 1)
	  {
	    if (iform.top7() != 0)
	      stream << "illegal";
	    else if (isRv64())
	      printInstShiftImm(stream, "slliw", rd, rs1, iform.fields2.shamt);
	    else
	      stream << "illegal";
	  }
	else if (funct3 == 5)
	  {
	    if (iform.top7() == 0)
	      printInstShiftImm(stream, "srliw", rd, rs1, iform.fields2.shamt);
	    else if (iform.top7() == 0x20)
	      printInstShiftImm(stream, "sraiw", rd, rs1, iform.fields2.shamt);
	    else
	      stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 8:  // 01000  S-form
      {
	SFormInst sform(inst);
	unsigned rs1 = sform.bits.rs1, rs2 = sform.bits.rs2;
	int32_t imm = sform.immed();
	switch (sform.bits.funct3)
	  {
	  case 0:  printInstLdSt(stream, "sb", rs2, rs1, imm); break;
	  case 1:  printInstLdSt(stream, "sh", rs2, rs1, imm); break;
	  case 2:  printInstLdSt(stream, "sw", rs2, rs1, imm); break;
	  default:
	    if (not isRv64())
	      stream << "illegal";
	    else
	      printInstLdSt(stream, "sd", rs2, rs1, imm);
	    break;
	  }
      }
      break;

    case 9:   // 01001  S-form
      {
	SFormInst sf(inst);
	unsigned rs1 = sf.bits.rs1, rs2 = sf.bits.rs2, f3 = sf.bits.funct3;
	int32_t imm = sf.immed();
	if (f3 == 2)
	  {
	    if (isRvf())
	      printInstFpLdSt(stream, "fsw", rs2, rs1, imm);
	    else
	      stream << "illegal";
	  }
	else if (f3 == 3)
	  {
	    if (isRvd())
	      printInstFpLdSt(stream, "fsd", rs2, rs1, imm);
	    else
	      stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 11:  // 01011  R-form  atomics
      {
	if (not isRva())
	  {
	    stream << "illegal";
	    break;
	  }

	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
	uint32_t rd = rf.bits.rd, rs1 = rf.bits.rs1, rs2 = rf.bits.rs2;
	bool rl = rf.rl(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if (top5 == 0)
	      printAmoInst(stream, "amoadd.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 1)
	      printAmoInst(stream, "amoswap.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 2)
	      printLrInst(stream, "lr.w", aq, rl, rd, rs1);
	    else if (top5 == 3)
	      printScInst(stream, "sc.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 4)
	      printAmoInst(stream, "amoxor.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 8)
	      printAmoInst(stream, "amoor.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x0c)
	      printAmoInst(stream, "amoand.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x10)
	      printAmoInst(stream, "ammin.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x14)
	      printAmoInst(stream, "amomax.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x18)
	      printAmoInst(stream, "amominu.w", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x1c)
	      printAmoInst(stream, "amomaxu.w", aq, rl, rd, rs1, rs2);
	    else
	      stream << "illegal";
	  }
	else if (f3 == 3)
	  {
	    if (top5 == 0)
	      printAmoInst(stream, "amoadd.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 1)
	      printAmoInst(stream, "amoswap.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 2)
	      printLrInst(stream, "lr.d", aq, rl, rd, rs1);
	    else if (top5 == 3)
	      printScInst(stream, "sc.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 4)
	      printAmoInst(stream, "amoxor.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 8)
	      printAmoInst(stream, "amoor.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x0c)
	      printAmoInst(stream, "amoand.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x10)
	      printAmoInst(stream, "ammin.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x14)
	      printAmoInst(stream, "amomax.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x18)
	      printAmoInst(stream, "amominu.d", aq, rl, rd, rs1, rs2);
	    else if (top5 == 0x1c)
	      printAmoInst(stream, "amomaxu.d", aq, rl, rd, rs1, rs2);
	    else
	      stream << "illegal";
	  }
	else stream << "illegal";
      }
      break;

    case 12:  // 01100  R-form
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	if (f7 == 0)
	  {
	    if      (f3 == 0)  printInstRdRs1Rs2(stream, "add",  rd, rs1, rs2);
	    else if (f3 == 1)  printInstRdRs1Rs2(stream, "sll",  rd, rs1, rs2);
	    else if (f3 == 2)  printInstRdRs1Rs2(stream, "slt",  rd, rs1, rs2);
	    else if (f3 == 3)  printInstRdRs1Rs2(stream, "sltu", rd, rs1, rs2);
	    else if (f3 == 4)  printInstRdRs1Rs2(stream, "xor",  rd, rs1, rs2);
	    else if (f3 == 5)  printInstRdRs1Rs2(stream, "srl",  rd, rs1, rs2);
	    else if (f3 == 6)  printInstRdRs1Rs2(stream, "or",   rd, rs1, rs2);
	    else if (f3 == 7)  printInstRdRs1Rs2(stream, "and",  rd, rs1, rs2);
	  }
	else if (f7 == 1)
	  {
	    if      (not isRvm())  stream << "illegal";
	    else if (f3 == 0) printInstRdRs1Rs2(stream, "mul",    rd, rs1, rs2);
	    else if (f3 == 1) printInstRdRs1Rs2(stream, "mulh",   rd, rs1, rs2);
	    else if (f3 == 2) printInstRdRs1Rs2(stream, "mulhsu", rd, rs1, rs2);
	    else if (f3 == 3) printInstRdRs1Rs2(stream, "mulhu",  rd, rs1, rs2);
	    else if (f3 == 4) printInstRdRs1Rs2(stream, "div",    rd, rs1, rs2);
	    else if (f3 == 5) printInstRdRs1Rs2(stream, "divu",   rd, rs1, rs2);
	    else if (f3 == 6) printInstRdRs1Rs2(stream, "rem",    rd, rs1, rs2);
	    else if (f3 == 7) printInstRdRs1Rs2(stream, "remu",   rd, rs1, rs2);
	  }
	else if (f7 == 0x20)
	  {
	    if      (f3 == 0)  printInstRdRs1Rs2(stream, "sub", rd, rs1, rs2);
	    else if (f3 == 5)  printInstRdRs1Rs2(stream, "sra", rd, rs1, rs2);
	    else               stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 13:  // 01101  U-form
      {
	UFormInst uform(inst);
	uint32_t imm = uform.immed();
	stream << "lui      x" << uform.bits.rd << ", ";
	if (imm < 0) { stream << "-"; imm = -imm; }
	stream << "0x" << std::hex << ((imm >> 12) & 0xfffff);
      }
      break;

    case 14:  // 01110  R-Form
      {
	const RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	if (f7 == 0)
	  {
	    if      (f3 == 0)  printInstRdRs1Rs2(stream, "addw", rd, rs1, rs2);
	    else if (f3 == 1)  printInstRdRs1Rs2(stream, "sllw", rd, rs1, rs2);
	    else if (f3 == 5)  printInstRdRs1Rs2(stream, "srlw", rd, rs1, rs2);
	    else               stream << "illegal";
	  }
	else if (f7 == 1)
	  {
	    if      (f3 == 0)  printInstRdRs1Rs2(stream, "mulw",  rd, rs1, rs2);
	    else if (f3 == 4)  printInstRdRs1Rs2(stream, "divw",  rd, rs1, rs2);
	    else if (f3 == 5)  printInstRdRs1Rs2(stream, "divuw", rd, rs1, rs2);
	    else if (f3 == 6)  printInstRdRs1Rs2(stream, "remw",  rd, rs1, rs2);
	    else if (f3 == 7)  printInstRdRs1Rs2(stream, "remuw", rd, rs1, rs2);
	    else               stream << "illegal";
	  }
	else if (f7 == 0x20)
	  {
	    if      (f3 == 0)  printInstRdRs1Rs2(stream, "subw", rd, rs1, rs2);
	    else if (f3 == 5)  printInstRdRs1Rs2(stream, "sraw", rd, rs1, rs2);
	    else               stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 16:  // 10000   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(stream, "fmadd_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(stream, "fmadd_d", rd, rs1, rs2, rs3, mode);
	else
	  stream << "illegal";
      }
      break;

    case 17:  // 10001   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(stream, "fmsub_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(stream, "fmsub_d", rd, rs1, rs2, rs3, mode);
	else
	  stream << "illegal";
      }
      break;

    case 18:  // 10010   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(stream, "fnmsub_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(stream, "fnmsub_d", rd, rs1, rs2, rs3, mode);
	else
	  stream << "illegal";
      }
      break;

    case 19:  // 10011   rform
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned f7 = rform.bits.funct7, f3 = rform.bits.funct3;
	unsigned rs3 = f7 >> 2;
	RoundingMode mode = RoundingMode(f3);
	if ((f7 & 3) == 0)
	  printFp32f(stream, "fnmadd_s", rd, rs1, rs2, rs3, mode);
	else if ((f7 & 3) == 1)
	  printFp32d(stream, "fnmadd_d", rd, rs1, rs2, rs3, mode);
	else
	  stream << "illegal";
      }
      break;

    case 20:  // 10100   rform
      disassembleFp(inst, stream);
      break;

    case 24:  // 11000   B-form
      {
	BFormInst bform(inst);
	unsigned rs1 = bform.bits.rs1, rs2 = bform.bits.rs2;
	int32_t imm = bform.immed();
	switch (bform.bits.funct3)
	  {
	  case 0:  printInstRegRegImm12(stream, "beq",  rs1, rs2, imm); break;
	  case 1:  printInstRegRegImm12(stream, "bne",  rs1, rs2, imm); break;
	  case 4:  printInstRegRegImm12(stream, "blt",  rs1, rs2, imm); break;
	  case 5:  printInstRegRegImm12(stream, "bge",  rs1, rs2, imm); break;
	  case 6:  printInstRegRegImm12(stream, "bltu", rs1, rs2, imm); break;
	  case 7:  printInstRegRegImm12(stream, "bgeu", rs1, rs2, imm); break;
	  default: stream << "illegal";                                 break;
	  }
      }
      break;

    case 25:  // 11001  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	if (iform.fields.funct3 == 0)
	  printInstLdSt(stream, "jalr", rd, rs1, iform.immed());
	else
	  stream << "illegal";
      }
      break;

    case 27:  // 11011  J-form
      {
	JFormInst jform(inst);
	int32_t imm = jform.immed();
	stream << "jal      " << intRegs_.regName(jform.bits.rd, abiNames_)
	       << ", ";
	if (imm < 0)
	  {
	    stream << "-";
	    imm = -imm;
	  }
	stream << "0x" << std::hex << (imm & 0xfffff);
      }
      break;

    case 28:  // 11100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	unsigned csrNum = iform.uimmed();
	std::string rdn = intRegs_.regName(rd, abiNames_);   // rd name
	std::string rs1n = intRegs_.regName(rs1, abiNames_); // rs1 name
	std::string csrn;  // csr name
	auto csr = csRegs_.findCsr(CsrNumber(csrNum));
	if (csr)
	  csrn = csr->getName();
	else
	  csrn = "illegal";
	switch (iform.fields.funct3)
	  {
	  case 0:
	    {
	      uint32_t func7 = iform.top7();
	      if (func7 == 0)
		{
		  if (rs1 != 0 or rd != 0)  stream << "illegal";
		  else if (csrNum == 0)     stream << "ecall";
		  else if (csrNum == 1)     stream << "ebreak";
		  else if (csrNum == 2)     stream << "uret";
		  else                      stream << "illegal";
		}
	      else if (func7 == 9)
		{
		  uint32_t rs2 = iform.rs2();
		  if (rd != 0) stream << "illegal";
		  else         stream << "SFENCE.VMA " << rs1 << ", " << rs2;
		}
	      else if (csrNum == 0x102) stream << "sret";
	      else if (csrNum == 0x302) stream << "mret";
	      else if (csrNum == 0x105) stream << "wfi";
	      else                      stream << "illegal";
	    }
	    break;
	  case 1:
	    stream << "csrrw    " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 2:
	    stream << "csrrs    " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 3:
	    stream << "csrrc    " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 5:
	    stream << "csrrwi   " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 6:
	    stream << "csrrsi   " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  case 7:
	    stream << "csrrci   " << rdn << ", " << csrn << ", " << rs1n;
	    break;
	  default: 
	    stream << "illegal";
	    break;
	  }
      }
      break;

    default:
      stream << "illegal";
      break;
    }
}


template <typename URV>
void
Core<URV>::disassembleInst16(uint16_t inst, std::ostream& stream)
{
  if (not isRvc())
    {
      stream << "illegal";
      return;
    }

  uint16_t quadrant = inst & 0x3;
  uint16_t funct3 =  inst >> 13;    // Bits 15 14 and 13

  switch (quadrant)
    {
    case 0:    // quadrant 0
      switch (funct3) 
	{
	case 0:   // illegal, c.addi4spn
	  {
	    if (inst == 0)
	      stream << "illegal";
	    else
	      {
		CiwFormInst ciwf(inst);
		unsigned immed = ciwf.immed();
		if (immed == 0)
		  stream << "illegal";
		else
		  printInstRegImm(stream, "c.addi4spn", 8+ciwf.bits.rdp,
				  immed >> 2);
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  stream << "illegal";
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    unsigned rd = 8+clf.bits.rdp, rs1 = (8+clf.bits.rs1p);
	    printInstLdSt(stream, "c.lw", rd, rs1, clf.lwImmed());
	  }
	  break;
	case 3:  // c.flw, c.ld
	  {
	    ClFormInst clf(inst);
	    unsigned rd = 8+clf.bits.rdp, rs1 = (8+clf.bits.rs1p);
	    if (isRv64())
	      printInstLdSt(stream, "c.ld", rd, rs1, clf.ldImmed()); 
	    else
	      {
		if (isRvf())
		  printInstFpLdSt(stream, "c.flw", rd, rs1, clf.lwImmed());
		else
		  stream << "illegal";
	      }
	  }
	  break;
	case 4:  // reserved
	  stream << "illegal";
	  break;
	case 5:  // c.fsd, c.sq
	  if (isRvd())
	    {
	      ClFormInst clf(inst);
	      unsigned rd = 8+clf.bits.rdp, rs1 = (8+clf.bits.rs1p);
	      printInstFpLdSt(stream, "c.fsd", rd, rs1, clf.ldImmed());
	    }
	  else
	    stream << "illegal";
	  break;
	case 6:  // c.sw
	  {
	    CsFormInst cs(inst);
	    unsigned rd = (8+cs.bits.rs2p), rs1 = 8+cs.bits.rs1p;
	    printInstLdSt(stream, "c.sw", rd, rs1, cs.swImmed());
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  {
	    CsFormInst cs(inst);
	    unsigned rd = (8+cs.bits.rs2p), rs1 = (8+cs.bits.rs1p);
	    if (isRv64())
	      printInstLdSt(stream, "c.sd", rd, rs1, cs.sdImmed());
	    else
	      {
		if (isRvf())
		  printInstFpLdSt(stream, "c.fsw", rd, rs1, cs.swImmed());
		else
		  stream << "illegal"; // c.fsw
	      }
	  }
	  break;
	}
      break;

    case 1:    // quadrant 1
      switch (funct3)
	{
	case 0:  // c.nop, c.addi
	  {
	    CiFormInst cif(inst);
	    if (cif.bits.rd == 0)
	      stream << "c.nop";
	    else
	      printInstRegImm(stream, "c.addi", cif.bits.rd, cif.addiImmed());
	  }
	  break;
	  
	case 1:  // c.jal, in rv64 and rv128 this is c.addiw
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      if (cif.bits.rd == 0)
		stream << "illegal";
	      else
		printInstRegImm(stream, "c.addiw", cif.bits.rd, cif.addiImmed());
	    }
	  else
	    {
	      CjFormInst cjf(inst);
	      int32_t imm = cjf.immed();
	      stream << "c.jal    ";
	      if (imm < 0) { stream << "-"; imm = -imm; }
	      stream << "0x" << std::hex << imm;
	    }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    printInstRegImm(stream, "c.li", cif.bits.rd, cif.addiImmed());
	  }
	  break;

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      stream << "illegal";
	    else if (cif.bits.rd == RegSp)
	      stream << "c.addi16sp 0x" << std::hex << (immed16 >> 4);
	    else
	      printInstRegImm(stream, "c.lui", cif.bits.rd, cif.luiImmed()>>12);
	  }
	  break;

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
	case 4:
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    unsigned rd = 8 + caf.bits.rdp;
	    std::string rdName = intRegs_.regName(rd, abiNames_);
	    int immed = caf.andiImmed();
	    switch (caf.bits.funct2)
	      {
	      case 0:
		if (caf.bits.ic5 != 0 and not isRv64())
		  stream << "illegal";
		else
		  printInstRegImm(stream, "c.srli", rd, caf.shiftImmed());
		break;
	      case 1:
		if (caf.bits.ic5 != 0 and not isRv64())
		  stream << "illegal";
		else
		  printInstRegImm(stream, "c.srai", rd, caf.shiftImmed());
		break;
	      case 2:
		printInstRegImm(stream, "c.andi", rd, immed);
		break;
	      case 3:
		{
		  unsigned rs2 = 8+(immed & 0x7); // Lowest 3 bits of immed
		  std::string rs2n = intRegs_.regName(rs2, abiNames_);
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0:
			  stream << "c.sub    " << rdName << ", " << rs2n; break;
			case 1:
			  stream << "c.xor    " << rdName << ", " << rs2n; break;
			case 2:
			  stream << "c.or     " << rdName << ", " << rs2n; break;
			case 3:
			  stream << "c.and    " << rdName << ", " << rs2n; break;
			}
		    }
		  else
		    {
		      if (not isRv64())
			stream << "illegal";
		      else
			switch ((immed >> 3) & 3)
			  {
			  case 0: stream << "c.subw " << rdName << ", " << rs2n;
			    break;
			  case 1: stream << "c.addw " << rdName << ", " << rs2n;
			    break;
			  case 3: stream << "illegal";
			    break; // reserved
			  case 4: stream << "illegal";
			    break; // reserved
			}
		    }
		}
		break;
	      }
	  }
	  break;

	case 5:  // c.j
	  {
	    CjFormInst cjf(inst);
	    int32_t imm = cjf.immed();
	    stream << "c.j      ";
	    if (imm < 0) { stream << "-"; imm = -imm; }
	    stream << "0x" << std::hex << imm;
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    printInstRegImm(stream, "c.beqz", 8+cbf.bits.rs1p, cbf.immed());
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    printInstRegImm(stream, "c.bnez", 8+cbf.bits.rs1p, cbf.immed());
	  }
	  break;
	}
      break;

    case 2:    // quadrant 2
      switch (funct3)
	{
	case 0:  // c.slli c.slli64
	  {
	    CiFormInst cif(inst);
	    unsigned immed = unsigned(cif.slliImmed()), rd = cif.bits.rd;
	    if (cif.bits.ic5 != 0 and not isRv64())
	      stream << "illegal";
	    else
	      stream << "c.slli   " << intRegs_.regName(rd, abiNames_) << ", "
		     << immed;
	  }
	  break;

	case 1:   // c.fldsp, c.lqsp
	  stream << "illegal";
	  break;

	case 2:  // c.lwsp
	  {
	    CiFormInst cif(inst);
	    unsigned rd = cif.bits.rd;
	    // rd == 0 is legal per Andrew Watterman
	    stream << "c.lwsp   " << intRegs_.regName(rd, abiNames_) << ", 0x"
		   << std::hex << cif.lwspImmed();
	  }
	break;

	case 3:  // c.flwsp c.ldsp
	  if (isRv64())
	    {
	      CiFormInst cif(inst);
	      unsigned rd = cif.bits.rd;
	      stream << "c.ldsp   " << intRegs_.regName(rd, abiNames_) << ", 0x"
		     << std::hex << cif.ldspImmed();
	    }
	  else
	    {
	      stream << "illegal";  // c.flwsp
	    }
	  break;

	case 4:  // c.jr c.mv c.ebreak c.jalr c.add
	  {
	    CiFormInst cif(inst);
	    unsigned immed = cif.addiImmed();
	    unsigned rd = cif.bits.rd;
	    unsigned rs2 = immed & 0x1f;
	    std::string rdName = intRegs_.regName(rd, abiNames_);
	    std::string rs2Name = intRegs_.regName(rs2, abiNames_);
	    if ((immed & 0x20) == 0)
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.jr     " << rdName;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.mv     " << rdName << ", " << rs2Name;
		  }
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "c.ebreak";
		    else
		      stream << "c.jalr   " << rdName;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.add    " << rdName << ", " << rs2Name;
		  }
	      }
	  }
	  break;

	case 5:  // c.fsdsp  c.sqsp
	  stream << "illegal";
	  break;

	case 6:  // c.swsp
	  {
	    CswspFormInst csw(inst);
	    stream << "c.swsp   " << intRegs_.regName(csw.bits.rs2, abiNames_)
		   << ", 0x" << std::hex << csw.swImmed();
	  }
	  break;

	case 7:  // c.fswsp c.sdsp
	  {
	    if (isRv64())  // c.sdsp
	      {
		CswspFormInst csw(inst);
		stream << "c.sdsp   " << intRegs_.regName(csw.bits.rs2, abiNames_)
		       << ", 0x" << std::hex << csw.sdImmed();
	      }
	  else
	    {
	      stream << "illegal";    // c.fwsp
	    }
	  }
	  break;
	}
      break;

    case 3:  // quadrant 3
      stream << "illegal";
      break;

    default:
      stream << "illegal";
      break;
    }
}


template <typename URV>
void
Core<URV>::disassembleInst32(uint32_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst32(inst, oss);
  str = oss.str();
}


template <typename URV>
void
Core<URV>::disassembleInst16(uint16_t inst, std::string& str)
{
  str.clear();

  std::ostringstream oss;
  disassembleInst16(inst, oss);
  str = oss.str();
}


template <typename URV>
void
Core<URV>::enableInstructionFrequency(bool b)
{
  instFreq_ = b;
  if (b)
    {
      instProfileVec_.resize(size_t(InstId::maxId) + 1);

      unsigned regCount = intRegCount();
      for (auto& inst : instProfileVec_)
	{
	  inst.rd_.resize(regCount);
	  inst.rs1_.resize(regCount);
	  inst.rs2_.resize(regCount);
	  inst.rs1Histo_.resize(13);  // FIX: avoid magic 13
	  inst.rs2Histo_.resize(13);  // FIX: avoid magic 13
	  inst.immHisto_.resize(13);  // FIX: avoid magic 13
	}
    }
}


template <typename URV>
void
Core<URV>::enterDebugMode(DebugModeCause cause, URV pc)
{
  debugMode_ = true;

  URV value = 0;
  if (csRegs_.read(CsrNumber::DCSR, PrivilegeMode::Machine, debugMode_, value))
    {
      value &= ~(URV(7) << 6);   // Clear cause field (starts at bit 6).
      value |= URV(cause) << 6;  // Set cause field
      if (nmiPending_)
	value |= URV(1) << 3;    // Set nmip bit.
      csRegs_.poke(CsrNumber::DCSR, value);

      csRegs_.poke(CsrNumber::DPC, pc);

      // Once test-bench is fixed, enable this.
      // recordCsrWrite(CsrNumber::DCSR);
    }
}


template <typename URV>
void
Core<URV>::enterDebugMode(URV pc)
{
  // This method is used by the test-bench to make the simulator follow it
  // into debug mode.  Do nothing if the simulator has already entered debug
  // mode on its own.
  if (debugMode_)
    return;

  debugMode_ = true;

  URV value = 0;
  if (csRegs_.read(CsrNumber::DCSR, PrivilegeMode::Machine, debugMode_, value))
    {
      if ((value >> 2) & 1)  // Step bit set?
	enterDebugMode(DebugModeCause::STEP, pc);
      else
	enterDebugMode(DebugModeCause::DEBUGGER, pc);
    }
}


template <typename URV>
void
Core<URV>::exitDebugMode()
{
  csRegs_.peek(CsrNumber::DPC, pc_);
  debugMode_ = false;

  // If pending nmi bit is set in dcsr, set pending nmi in core
  URV dcsrVal = 0;
  if (peekCsr(CsrNumber::DCSR, dcsrVal) and ((dcsrVal >> 3) & 1))
    setPendingNmi(nmiCause_);
}


template <typename URV>
void
Core<URV>::execBlt(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  SRV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 < v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
      lastBranchTaken_ = true;
    }
}


template <typename URV>
void
Core<URV>::execBltu(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  URV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 < v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
      lastBranchTaken_ = true;
    }
}


template <typename URV>
void
Core<URV>::execBge(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  SRV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
      lastBranchTaken_ = true;
    }
}


template <typename URV>
void
Core<URV>::execBgeu(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  URV v1 = intRegs_.read(rs1),  v2 = intRegs_.read(rs2);
  if (v1 >= v2)
    {
      pc_ = currPc_ + SRV(offset);
      pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
      lastBranchTaken_ = true;
    }
}


template <typename URV>
void
Core<URV>::execJalr(uint32_t rd, uint32_t rs1, int32_t offset)
{
  URV temp = pc_;  // pc has the address of the instruction after jalr
  pc_ = (intRegs_.read(rs1) + SRV(offset));
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  intRegs_.write(rd, temp);
  lastBranchTaken_ = true;
}


template <typename URV>
void
Core<URV>::execJal(uint32_t rd, uint32_t offset, int32_t)
{
  intRegs_.write(rd, pc_);
  pc_ = currPc_ + SRV(int32_t(offset));
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  lastBranchTaken_ = true;
}


template <typename URV>
void
Core<URV>::execLui(uint32_t rd, uint32_t imm, int32_t)
{
  intRegs_.write(rd, SRV(int32_t(imm)));
}


template <typename URV>
void
Core<URV>::execAuipc(uint32_t rd, uint32_t imm, int32_t)
{
  intRegs_.write(rd, currPc_ + SRV(int32_t(imm)));
}


template <typename URV>
void
Core<URV>::execSlli(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if ((amount & 0x20) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = intRegs_.read(rs1) << amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSlti(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = SRV(intRegs_.read(rs1)) < imm ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSltiu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) < URV(SRV(imm)) ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execXori(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) ^ SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrli(uint32_t rd, uint32_t rs1, int32_t amount)
{
  uint32_t uamount(amount);

  if ((uamount > 31) and not isRv64())
    {
      illegalInst();
      return;
    }

  URV v = intRegs_.read(rs1) >> uamount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrai(uint32_t rd, uint32_t rs1, int32_t amount)
{
  uint32_t uamount(amount);

  if ((uamount > 31) and not isRv64())
    {
      illegalInst();
      return;
    }

  URV v = SRV(intRegs_.read(rs1)) >> uamount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execOri(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) | SRV(imm);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSub(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) - intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSll(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) << (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSlt(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  SRV v1 = intRegs_.read(rs1);
  SRV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSltu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v1 = intRegs_.read(rs1);
  URV v2 = intRegs_.read(rs2);
  URV v = v1 < v2 ? 1 : 0;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execXor(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) ^ intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrl(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = intRegs_.read(rs1) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSra(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV mask = intRegs_.shiftMask();
  URV v = SRV(intRegs_.read(rs1)) >> (intRegs_.read(rs2) & mask);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execOr(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) | intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execAnd(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV v = intRegs_.read(rs1) & intRegs_.read(rs2);
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execFence(uint32_t pred, uint32_t succ, int32_t)
{
  return;  // Currently a no-op.
}


template <typename URV>
void
Core<URV>::execFencei(uint32_t, uint32_t, int32_t)
{
  return;  // Currently a no-op.
}


template <typename URV>
URV
Core<URV>::emulateLinuxSystemCall()
{
  // Preliminary. Need to avoid using syscall numbers.

  URV a0 = intRegs_.read(RegA0);
  URV a1 = intRegs_.read(RegA1);
  URV a2 = intRegs_.read(RegA2);
  URV a3 = intRegs_.read(RegA3);
  URV a4 = intRegs_.read(RegA3);
  URV a5 = intRegs_.read(RegA5);
  URV a6 = intRegs_.read(RegA6);
  URV num = intRegs_.read(RegA7);

  if (num == 80)
    {
      // fstat
      int fd = a0;
      size_t rvBuff = 0;
      if (not memory_.getSimMemAddr(a1, rvBuff))
	return SRV(-1);
      struct stat buff;
      SRV rv = fstat(fd, &buff);
      if (rv < 0)
	return rv;
      if (sizeof(URV) == 4)
	{
	  // Copy x86 stat buffer to riscv stat buffer.
	  char* ptr = (char*) rvBuff;
	  *((uint16_t*) ptr) = buff.st_dev;             ptr += 2;
	  *((uint16_t*) ptr) = buff.st_ino;             ptr += 2;
	  *((uint32_t*) ptr) = buff.st_mode;            ptr += 4;
	  *((uint16_t*) ptr) = buff.st_nlink;           ptr += 2;
	  *((uint16_t*) ptr) = buff.st_uid;             ptr += 2;
	  *((uint16_t*) ptr) = buff.st_gid;             ptr += 2;
	  *((uint16_t*) ptr) = buff.st_rdev;            ptr += 2;
	  *((uint32_t*) ptr) = buff.st_size;            ptr += 4;
	  /* st_spare1 */                               ptr += 4;
	  *((uint32_t*) ptr) = buff.st_mtim.tv_sec;     ptr += 4;
	  /* st_spare2 */                               ptr += 4;
	  *((uint32_t*) ptr) = buff.st_ctim.tv_sec;     ptr += 4;
	  /* st_spare3 */                               ptr += 4;
	  *((uint32_t*) ptr) = buff.st_blksize;         ptr += 4;
	  /* st_spare4 */                               ptr += 8;
	  return rv;
	}

      // Copy x86 stat buffer to riscv stat buffer.
      *((struct stat*) rvBuff) = buff;
      return rv;
    }

  if (num == 214)
    {
      // brk
      if (a0 < progBreak_)
	return progBreak_;
      progBreak_ = a0;
      return a0;
    }

  if (num == 57)
    {
      // close
      int fd = a0;
      SRV rv = 0;
      if (fd > 2)
	rv = close(fd);
      return rv;
    }

  if (num == 63)
    {
      // read
      int fd = a0;
      size_t buffAddr = 0;
      if (not memory_.getSimMemAddr(a1, buffAddr))
	return SRV(-1);
      size_t count = a2;
      SRV rv = read(fd, (void*) buffAddr, count);
      return rv;
    }

  if (num == 64)
    {
      // write
      int fd = a0;
      size_t buffAddr = 0;
      if (not memory_.getSimMemAddr(a1, buffAddr))
	return SRV(-1);
      size_t count = a2;
      SRV rv = write(fd, (void*) buffAddr, count);
      return rv;
    }

  if (num == 93)
    {
      // exit
      throw CoreException(CoreException::Exit, "", 0, a0);
      return 0;
    }

  if (num == 1024)
    {
      // open
      size_t pathAddr = 0;
      if (not memory_.getSimMemAddr(a0, pathAddr))
	return SRV(-1);
      int flags = a1;
      int x86Flags = 0;
      if (flags & 1) x86Flags |= O_WRONLY;
      if (flags & 0x200) x86Flags |= O_CREAT;
      int mode = a2;
      SRV fd = open((const char*) pathAddr, x86Flags, mode);
      return fd;
    }

  std::cerr << "syscall " << num << ' ' << a0 << ' ' << a1 << ' ' << a2 << ' '
	    << a3 << ' ' << a4 << ' ' << a5 << ' ' << a6 <<  '\n';

  return a0;
}


template <typename URV>
void
Core<URV>::execEcall(uint32_t, uint32_t, int32_t)
{
  if (triggerTripped_)
    return;

  if (emulateLinux_)
    {
      URV a0 = emulateLinuxSystemCall();
      intRegs_.write(RegA0, a0);
      return;
    }

  if (privMode_ == PrivilegeMode::Machine)
    initiateException(ExceptionCause::M_ENV_CALL, currPc_, 0);
  else if (privMode_ == PrivilegeMode::Supervisor)
    initiateException(ExceptionCause::S_ENV_CALL, currPc_, 0);
  else if (privMode_ == PrivilegeMode::User)
    initiateException(ExceptionCause::U_ENV_CALL, currPc_, 0);
  else
    assert(0 and "Invalid privilege mode in execEcall");
}


template <typename URV>
void
Core<URV>::execEbreak(uint32_t, uint32_t, int32_t)
{
  if (triggerTripped_)
    return;

  // If in machine mode and DCSR bit ebreakm is set, then enter debug mode.
  if (privMode_ == PrivilegeMode::Machine)
    {
      URV dcsrVal = 0;
      if (peekCsr(CsrNumber::DCSR, dcsrVal))
	{
	  if (dcsrVal & (URV(1) << 15))   // Bit ebreakm is on?
	    {
	      // The documentation (RISCV external debug support) does
	      // not say whether or not we set EPC and MTVAL.
	      enterDebugMode(DebugModeCause::EBREAK, currPc_);
	      ebreakInst_ = true;
	      recordCsrWrite(CsrNumber::DCSR);
	      return;
	    }
	}
    }

  URV savedPc = currPc_;  // Goes into MEPC.
  URV trapInfo = currPc_;  // Goes into MTVAL.

  initiateException(ExceptionCause::BREAKP, savedPc, trapInfo);

  if (enableGdb_)
    {
      pc_ = currPc_;
      handleExceptionForGdb(*this);
      return;
    }
}


template <typename URV>
void
Core<URV>::execMret(uint32_t, uint32_t, int32_t)
{
  if (privMode_ < PrivilegeMode::Machine)
    {
      illegalInst();
      return;
    }

  if (triggerTripped_)
    return;

  // Restore privilege mode and interrupt enable by getting
  // current value of MSTATUS, ...
  URV value = 0;
  if (not csRegs_.read(CsrNumber::MSTATUS, privMode_, debugMode_, value))
    {
      illegalInst();
      return;
    }

  // ... updating/unpacking its fields,
  MstatusFields<URV> fields(value);
  PrivilegeMode savedMode = PrivilegeMode(fields.bits_.MPP);
  fields.bits_.MIE = fields.bits_.MPIE;
  fields.bits_.MPP = 0;
  fields.bits_.MPIE = 1;

  // ... and putting it back
  if (not csRegs_.write(CsrNumber::MSTATUS, privMode_, debugMode_,
			fields.value_))
    assert(0 and "Failed to write MSTATUS register\n");

  // TBD: Handle MPV.

  // Restore program counter from MEPC.
  URV epc;
  if (not csRegs_.read(CsrNumber::MEPC, privMode_, debugMode_, epc))
    illegalInst();
  pc_ = (epc >> 1) << 1;  // Restore pc clearing least sig bit.
      
  // Update privilege mode.
  privMode_ = savedMode;
}


template <typename URV>
void
Core<URV>::execSret(uint32_t, uint32_t, int32_t)
{
  if (not isRvs())
    {
      illegalInst();
      return;
    }

  if (privMode_ < PrivilegeMode::Supervisor)
    {
      illegalInst();
      return;
    }

  if (triggerTripped_)
    return;

  // Restore privilege mode and interrupt enable by getting
  // current value of MSTATUS, ...
  URV value = 0;
  if (not csRegs_.read(CsrNumber::SSTATUS, privMode_, debugMode_, value))
    {
      illegalInst();
      return;
    }

  // ... updating/unpacking its fields,
  MstatusFields<URV> fields(value);
  
  PrivilegeMode savedMode = fields.bits_.SPP? PrivilegeMode::Supervisor :
    PrivilegeMode::User;
  fields.bits_.SIE = fields.bits_.SPIE;
  fields.bits_.SPP = 0;
  fields.bits_.SPIE = 1;

  // ... and putting it back
  if (not csRegs_.write(CsrNumber::SSTATUS, privMode_, debugMode_,
			fields.value_))
    {
      illegalInst();
      return;
    }

  // Restore program counter from UEPC.
  URV epc;
  if (not csRegs_.read(CsrNumber::SEPC, privMode_, debugMode_, epc))
    {
      illegalInst();
      return;
    }
  pc_ = (epc >> 1) << 1;  // Restore pc clearing least sig bit.

  // Update privilege mode.
  privMode_ = savedMode;
}


template <typename URV>
void
Core<URV>::execUret(uint32_t, uint32_t, int32_t)
{
  if (not isRvu())
    {
      illegalInst();
      return;
    }

  if (privMode_ != PrivilegeMode::User)
    {
      illegalInst();
      return;
    }

  if (triggerTripped_)
    return;

  // Restore privilege mode and interrupt enable by getting
  // current value of MSTATUS, ...
  URV value = 0;
  if (not csRegs_.read(CsrNumber::USTATUS, privMode_, debugMode_, value))
    {
      illegalInst();
      return;
    }

  // ... updating/unpacking its fields,
  MstatusFields<URV> fields(value);
  fields.bits_.UIE = fields.bits_.UPIE;
  fields.bits_.UPIE = 1;

  // ... and putting it back
  if (not csRegs_.write(CsrNumber::USTATUS, privMode_, debugMode_,
			fields.value_))
    {
      illegalInst();
      return;
    }

  // Restore program counter from UEPC.
  URV epc;
  if (not csRegs_.read(CsrNumber::UEPC, privMode_, debugMode_, epc))
    {
      illegalInst();
      return;
    }
  pc_ = (epc >> 1) << 1;  // Restore pc clearing least sig bit.
}


template <typename URV>
void
Core<URV>::execWfi(uint32_t, uint32_t, int32_t)
{
  return;   // Currently implemented as a no-op.
}


template <typename URV>
void
Core<URV>::commitCsrWrite(CsrNumber csr, URV csrVal, unsigned intReg,
			  URV intRegVal)
{
  // Make auto-increment happen before write for minstret and cycle.
  if (csr == CsrNumber::MINSTRET or csr == CsrNumber::MINSTRETH)
    retiredInsts_++;
  if (csr == CsrNumber::MCYCLE or csr == CsrNumber::MCYCLEH)
    cycleCount_++;

  // Update CSR and integer register.
  csRegs_.write(csr, privMode_, debugMode_, csrVal);
  intRegs_.write(intReg, intRegVal);

  if (csr == CsrNumber::DCSR)
    {
      debugStep_ = (csrVal >> 2) & 1;
      debugStepIe_ = (csrVal >> 11) & 1;
    }
  else if (csr == CsrNumber::MGPMC)
    {
      prevCountersCsrOn_ = countersCsrOn_;
      countersCsrOn_ = (csrVal & 1) == 1;
    }

  // Csr was written. If it was minstret, compensate for
  // auto-increment that will be done by run, runUntilAddress or
  // singleStep method.
  if (csr == CsrNumber::MINSTRET or csr == CsrNumber::MINSTRETH)
    retiredInsts_--;

  // Same for mcycle.
  if (csr == CsrNumber::MCYCLE or csr == CsrNumber::MCYCLEH)
    cycleCount_--;
}


// Set control and status register csr to value of register rs1 and
// save its original value in register rd.
template <typename URV>
void
Core<URV>::execCsrrw(uint32_t rd, uint32_t rs1, int32_t c)
{
  if (triggerTripped_)
    return;

  CsrNumber csr = CsrNumber(c);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  URV next = intRegs_.read(rs1);

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrs(uint32_t rd, uint32_t rs1, int32_t c)
{
  if (triggerTripped_)
    return;

  CsrNumber csr = CsrNumber(c);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  URV next = prev | intRegs_.read(rs1);
  if (rs1 == 0)
    {
      intRegs_.write(rd, prev);
      return;
    }

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrc(uint32_t rd, uint32_t rs1, int32_t c)
{
  if (triggerTripped_)
    return;

  CsrNumber csr = CsrNumber(c);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  URV next = prev & (~ intRegs_.read(rs1));
  if (rs1 == 0)
    {
      intRegs_.write(rd, prev);
      return;
    }

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrwi(uint32_t rd, uint32_t imm, int32_t c)
{
  if (triggerTripped_)
    return;

  CsrNumber csr = CsrNumber(c);

  URV prev = 0;
  if (rd != 0 and not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  commitCsrWrite(csr, imm, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrsi(uint32_t rd, uint32_t imm, int32_t c)
{
  if (triggerTripped_)
    return;

  CsrNumber csr = CsrNumber(c);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  URV next = prev | imm;
  if (imm == 0)
    {
      intRegs_.write(rd, prev);
      return;
    }

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrci(uint32_t rd, uint32_t imm, int32_t c)
{
  if (triggerTripped_)
    return;

  CsrNumber csr = CsrNumber(c);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  URV next = prev & (~ imm);
  if (imm == 0)
    {
      intRegs_.write(rd, prev);
      return;
    }

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      csrException_ = true;
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execLb(uint32_t rd, uint32_t rs1, int32_t imm)
{
  load<int8_t>(rd, rs1, imm);
}


template <typename URV>
void
Core<URV>::execLbu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  load<uint8_t>(rd, rs1, imm);
}


template <typename URV>
void
Core<URV>::execLhu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  load<uint16_t>(rd, rs1, imm);
}


template <typename URV>
template <typename STORE_TYPE>
void
Core<URV>::store(URV addr, STORE_TYPE storeVal)
{
  // ld/st-address or instruction-address triggers have priority over
  // ld/st access or misaligned exceptions.
  bool hasTrig = hasActiveTrigger();
  TriggerTiming timing = TriggerTiming::Before;
  bool isLoad = false;
  if (hasTrig)
    if (ldStAddrTriggerHit(addr, timing, isLoad, isInterruptEnabled()))
      triggerTripped_ = true;

  // Misaligned store to io section causes an exception. Crossing dccm
  // to non-dccm causes an exception.
  constexpr unsigned alignMask = sizeof(STORE_TYPE) - 1;
  bool misaligned = addr & alignMask;
  misalignedLdSt_ = misaligned;
  if (misaligned)
    {
      if (misalignedAccessCausesException(addr, sizeof(STORE_TYPE)))
	{
	  if (triggerTripped_)
	    return;  // No exception if earlier trig. Suppress store data trig.
	  forceAccessFail_ = false;
	  ldStException_ = true;
	  initiateException(ExceptionCause::STORE_ADDR_MISAL, currPc_, addr);
	  return;
	}
    }

  if (hasTrig and not forceAccessFail_ and memory_.checkWrite(addr, storeVal))
    {
      // No exception: consider store-data  trigger
      if (ldStDataTriggerHit(storeVal, timing, isLoad, isInterruptEnabled()))
	triggerTripped_ = true;
    }
  if (triggerTripped_)
    return;

  if (memory_.write(addr, storeVal) and not forceAccessFail_)
    {
      if (hasLr_ and lrAddr_ == addr)
	hasLr_ = false;

      // If we write to special location, end the simulation.
      if (toHostValid_ and addr == toHost_ and storeVal != 0)
	{
	  throw CoreException(CoreException::Stop, "write to to-host",
			      toHost_, storeVal);
	}

      // If addr is special location, then write to console.
      if constexpr (sizeof(STORE_TYPE) == 1)
        {
	  if (conIoValid_ and addr == conIo_)
	    {
	      if (consoleOut_)
		fputc(storeVal, consoleOut_);
	      return;
	    }
	}
      if (maxStoreQueueSize_)
	{
	  uint64_t prevVal = 0;
	  memory_.getLastWriteOldValue(prevVal);
	  putInStoreQueue(sizeof(STORE_TYPE), addr, storeVal, prevVal);
	}
    }
  else
    {
      forceAccessFail_ = false;
      ldStException_ = true;
      initiateException(ExceptionCause::STORE_ACC_FAULT, currPc_, addr);
    }
}


template <typename URV>
void
Core<URV>::execSb(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV addr = intRegs_.read(rs1) + SRV(imm);
  URV value = intRegs_.read(rs2);

  store<uint8_t>(addr, value);
}


template <typename URV>
void
Core<URV>::execSh(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV addr = intRegs_.read(rs1) + SRV(imm);
  URV value = intRegs_.read(rs2);

  store<uint16_t>(addr, value);
}


namespace WdRiscv
{

  template<>
  void
  Core<uint32_t>::execMul(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    int32_t a = intRegs_.read(rs1);
    int32_t b = intRegs_.read(rs2);

    int32_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint32_t>::execMulh(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));  // sign extend.
    int64_t b = int32_t(intRegs_.read(rs2));
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhsu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    int64_t a = int32_t(intRegs_.read(rs1));
    uint64_t b = intRegs_.read(rs2);
    int64_t c = a * b;
    int32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint32_t>::execMulhu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    uint64_t a = intRegs_.read(rs1);
    uint64_t b = intRegs_.read(rs2);
    uint64_t c = a * b;
    uint32_t high = c >> 32;

    intRegs_.write(rd, high);
  }


  template<>
  void
  Core<uint64_t>::execMul(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend to 64-bit
    __int128_t b = int64_t(intRegs_.read(rs2));

    int64_t c = a * b;
    intRegs_.write(rd, c);
  }


  template<>
  void
  Core<uint64_t>::execMulh(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));  // sign extend.
    __int128_t b = int64_t(intRegs_.read(rs2));
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhsu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __int128_t a = int64_t(intRegs_.read(rs1));
    __uint128_t b = intRegs_.read(rs2);
    __int128_t c = a * b;
    int64_t high = c >> 64;

    intRegs_.write(rd, high);
  }


  template <>
  void
  Core<uint64_t>::execMulhu(uint32_t rd, uint32_t rs1, int32_t rs2)
  {
    __uint128_t a = intRegs_.read(rs1);
    __uint128_t b = intRegs_.read(rs2);
    __uint128_t c = a * b;
    uint64_t high = c >> 64;

    intRegs_.write(rd, high);
  }

}


template <typename URV>
void
Core<URV>::execDiv(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = -1;   // Divide by zero result
  if (b != 0)
    {
      SRV minInt = SRV(1) << (intRegs_.regWidth() - 1);
      if (a == minInt and b == -1)
	c = a;
      else
	c = a / b;  // Per spec: User-Level ISA, Version 2.3, Section 6.2
    }
  intRegs_.write(rd, c);
}


template <typename URV>
void
Core<URV>::execDivu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV a = intRegs_.read(rs1);
  URV b = intRegs_.read(rs2);
  URV c = ~ URV(0);  // Divide by zero result.
  if (b != 0)
    c = a / b;
  intRegs_.write(rd, c);
}


// Remainder instruction.
template <typename URV>
void
Core<URV>::execRem(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  SRV a = intRegs_.read(rs1);
  SRV b = intRegs_.read(rs2);
  SRV c = a;  // Divide by zero remainder.
  if (b != 0)
    {
      SRV minInt = SRV(1) << (intRegs_.regWidth() - 1);
      if (a == minInt and b == -1)
	c = 0;   // Per spec: User-Level ISA, Version 2.3, Section 6.2
      else
	c = a % b;
    }
  intRegs_.write(rd, c);
}


// Unsigned remainder instruction.
template <typename URV>
void
Core<URV>::execRemu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV a = intRegs_.read(rs1);
  URV b = intRegs_.read(rs2);
  URV c = a;  // Divide by zero remainder.
  if (b != 0)
    c = a % b;
  intRegs_.write(rd, c);
}


template <typename URV>
void
Core<URV>::execLwu(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }
  load<uint32_t>(rd, rs1, imm);
}


template <typename URV>
void
Core<URV>::execLd(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }
  load<uint64_t>(rd, rs1, imm);
}


template <typename URV>
void
Core<URV>::execSd(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  URV addr = intRegs_.read(rs1) + SRV(imm);
  URV value = intRegs_.read(rs2);

  store<uint64_t>(addr, value);
}


template <typename URV>
void
Core<URV>::execSlliw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  if (amount > 0x1f)
    {
      illegalInst();   // Bit 5 is 1 or higher values.
      return;
    }

  int32_t word = intRegs_.read(rs1);
  word <<= amount;

  SRV value = word; // Sign extend to 64-bit.
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSrliw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  if (amount > 0x1f)
    {
      illegalInst();   // Bit 5 is 1 or higher values.
      return;
    }

  uint32_t word = intRegs_.read(rs1);
  word >>= amount;

  SRV value = int32_t(word); // Sign extend to 64-bit.
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSraiw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  if (amount > 0x1f)
    {
      illegalInst();   // Bit 5 is 1 or higher values.
      return;
    }

  int32_t word = intRegs_.read(rs1);
  word >>= amount;

  SRV value = word; // Sign extend to 64-bit.
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execAddiw(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1);
  word += imm;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execAddw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1) + intRegs_.read(rs2);
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSubw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1) - intRegs_.read(rs2);
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}



template <typename URV>
void
Core<URV>::execSllw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  uint32_t shift = intRegs_.read(rs2) & 0x1f;
  int32_t word = intRegs_.read(rs1) << shift;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execSrlw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  uint32_t word = intRegs_.read(rs1);
  uint32_t shift = intRegs_.read(rs2) & 0x1f;
  word >>= shift;
  SRV value = int32_t(word);  // sign extend to 64-bits
  intRegs_.write(rd, value);
}

template <typename URV>
void
Core<URV>::execSraw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word = intRegs_.read(rs1);
  uint32_t shift = intRegs_.read(rs2) & 0x1f;
  word >>= shift;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execMulw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);
  int32_t word = word1 * word2;
  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execDivw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);

  int32_t word = -1;  // Divide by zero result
  if (word2 != 0)
    word = word1 / word2;

  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execDivuw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  uint32_t word1 = intRegs_.read(rs1);
  uint32_t word2 = intRegs_.read(rs2);

  uint32_t word = ~uint32_t(0);  // Divide by zero result.
  if (word2 != 0)
    word = word1 / word2;

  URV value = word;  // zero extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execRemw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);

  int32_t word = word1;  // Divide by zero remainder
  if (word2 != 0)
    word = word1 % word2;

  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execRemuw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64())
    {
      illegalInst();
      return;
    }

  uint32_t word1 = intRegs_.read(rs1);
  uint32_t word2 = intRegs_.read(rs1);

  uint32_t word = word1;  // Divide by zero remainder
  if (word1 != 0)
    word = word1 % word2;

  URV value = word;  // zero extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
RoundingMode
Core<URV>::effectiveRoundingMode()
{
  if (instRoundingMode_ != RoundingMode::Dynamic)
    return instRoundingMode_;

  URV fcsrVal = 0;
  if (csRegs_.read(CsrNumber::FCSR, PrivilegeMode::Machine, debugMode_,
		   fcsrVal))
    {
      RoundingMode mode = RoundingMode((fcsrVal >> 5) & 0x7);
      return mode;
    }

  return instRoundingMode_;
}


template <typename URV>
void
Core<URV>::updateAccruedFpBits()
{
  URV val = 0;
  if (csRegs_.read(CsrNumber::FCSR, PrivilegeMode::Machine, debugMode_, val))
    {
      URV prev = val;

      int flags = fetestexcept(FE_ALL_EXCEPT);
      
      if (flags & FE_INEXACT)
	val |= URV(FpFlags::Inexact);

      if (flags & FE_UNDERFLOW)
	val |= URV(FpFlags::Underflow);

      if (flags & FE_OVERFLOW)
	val |= URV(FpFlags::Overflow);
      
      if (flags & FE_DIVBYZERO)
	val |= URV(FpFlags::DivByZero);

      if (flags & FE_INVALID)
	val |= URV(FpFlags::Invalid);

      if (val != prev)
	csRegs_.write(CsrNumber::FCSR, PrivilegeMode::Machine, debugMode_, val);
    }
}


int
setSimulatorRoundingMode(RoundingMode mode)
{
  int previous = std::fegetround();
  switch(mode)
    {
    case RoundingMode::NearestEven: std::fesetround(FE_TONEAREST);  break;
    case RoundingMode::Zero:        std::fesetround(FE_TOWARDZERO); break;
    case RoundingMode::Down:        std::fesetround(FE_DOWNWARD);   break;
    case RoundingMode::Up:          std::fesetround(FE_UPWARD);     break;
    case RoundingMode::NearestMax:  std::fesetround(FE_TONEAREST);  break; //FIX
    default: break;
    }
  return previous;
}


template <typename URV>
void
Core<URV>::execFlw(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + SRV(imm);
  bool hasTrigger = hasActiveTrigger();

  typedef TriggerTiming Timing;

  bool isLoad = true;
  if (hasTrigger and ldStAddrTriggerHit(address, Timing::Before, isLoad,
					isInterruptEnabled()))
    {
      triggerTripped_ = true;
      return;
    }

  loadAddr_ = address;    // For reporting load addr in trace-mode.
  loadAddrValid_ = true;  // For reporting load addr in trace-mode.

  // Misaligned load from io section triggers an exception.
  if ((address & 3) and not isIdempotentRegion(address))
    {
      initiateException(ExceptionCause::LOAD_ADDR_MISAL, currPc_, address);
      ldStException_ = true;
      return;
    }

  union UFU  // Unsigned float union: reinterpret bits as unsigned or float
  {
    uint32_t u;
    float f;
  };

  uint32_t word = 0;
  if (memory_.read(address, word) and not forceAccessFail_)
    {
      UFU ufu;
      ufu.u = word;
      fpRegs_.writeSingle(rd, ufu.f);
    }
  else
    {
      forceAccessFail_ = false;
      initiateException(ExceptionCause::LOAD_ACC_FAULT, currPc_, address);
      ldStException_ = true;
    }
}


template <typename URV>
void
Core<URV>::execFsw(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  URV addr = intRegs_.read(rs1) + SRV(imm);
  float val = fpRegs_.readSingle(rs2);

  union UFU  // Unsigned float union: reinterpret bits as unsigned or float
  {
    uint32_t u;
    float f;
  };

  UFU ufu;
  ufu.f = val;

  store<uint32_t>(addr, ufu.u);
}


inline
void
feClearAllExceptions()
{
  asm("fnclex");  // std::feclearexcept(FE_ALL_EXCEPT);
}


template <typename URV>
void
Core<URV>::execFmadd_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float f3 = fpRegs_.readSingle(instRs3_);
  float res = std::fma(f1, f2, f3);
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmsub_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float f3 = fpRegs_.readSingle(instRs3_);
  float res = std::fma(f1, f2, -f3);
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFnmsub_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float f3 = fpRegs_.readSingle(instRs3_);
  float res = std::fma(f1, f2, -f3);
  fpRegs_.writeSingle(rd, -res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFnmadd_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float f3 = fpRegs_.readSingle(instRs3_);
  float res = std::fma(f1, f2, f3);
  fpRegs_.writeSingle(rd, -res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFadd_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float res = f1 + f2;
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFsub_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float res = f1 - f2;
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmul_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  std::feclearexcept(FE_ALL_EXCEPT);
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float res = f1 * f2;
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFdiv_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float res = f1 / f2;
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFsqrt_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  std::feclearexcept(FE_ALL_EXCEPT);
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  float res = std::sqrt(f1);
  fpRegs_.writeSingle(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFsgnj_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float res = copysign(f1, f2);  // Magnitude of rs1 and sign of rs2
  fpRegs_.writeSingle(rd, res);
}


template <typename URV>
void
Core<URV>::execFsgnjn_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())   {
      illegalInst();
      return;
    }

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);
  float res = copysign(f1, f2);  // Magnitude of rs1 and sign of rs2
  res = -res;  // Magnitude of rs1 and negative the sign of rs2
  fpRegs_.writeSingle(rd, res);
}


template <typename URV>
void
Core<URV>::execFsgnjx_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);

  int sign1 = (std::signbit(f1) == 0) ? 0 : 1;
  int sign2 = (std::signbit(f2) == 0) ? 0 : 1;
  int sign = sign1 ^ sign2;

  float x = sign? -1 : 1;

  float res = copysign(f1, x);  // Magnitude of rs1 and sign of x
  fpRegs_.writeSingle(rd, res);
}


template <typename URV>
void
Core<URV>::execFmin_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  float in1 = fpRegs_.readSingle(rs1);
  float in2 = fpRegs_.readSingle(rs2);
  float res = fmin(in1, in2);
  fpRegs_.writeSingle(rd, res);
}


template <typename URV>
void
Core<URV>::execFmax_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  float in1 = fpRegs_.readSingle(rs1);
  float in2 = fpRegs_.readSingle(rs2);
  float res = fmax(in1, in2);
  fpRegs_.writeSingle(rd, res);
}


template <typename URV>
void
Core<URV>::execFcvt_w_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  SRV result = int32_t(f1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_wu_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  URV result = uint32_t(f1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmv_x_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  float f1 = fpRegs_.readSingle(rs1);

  union IFU  // Int float union: reinterpret bits as int or float
  {
    int32_t i;
    float f;
  };

  IFU ifu;
  ifu.f = f1;

  SRV value = SRV(ifu.i); // Sign extend.

  intRegs_.write(rd, value);
}

 
template <typename URV>
void
Core<URV>::execFeq_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);

  URV res = (f1 == f2)? 1 : 0;
  intRegs_.write(rd, res);

  updateAccruedFpBits();
}


template <typename URV>
void
Core<URV>::execFlt_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);

  URV res = (f1 < f2)? 1 : 0;
  intRegs_.write(rd, res);

  updateAccruedFpBits();
}


template <typename URV>
void
Core<URV>::execFle_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();

  float f1 = fpRegs_.readSingle(rs1);
  float f2 = fpRegs_.readSingle(rs2);

  URV res = (f1 <= f2)? 1 : 0;
  intRegs_.write(rd, res);

  updateAccruedFpBits();
}


bool
mostSignificantFractionBit(float x)
{
  union UFU
  {
    uint32_t u;
    float f;
  };

  UFU ufu;
  ufu.f = x;

  return (ufu.u >> 22) & 1;
}


bool
mostSignificantFractionBit(double x)
{
  union UDU
  {
    uint64_t u;
    double d;
  };

  UDU udu;
  udu.d = x;

  return (udu.u >> 51) & 1;
}



template <typename URV>
void
Core<URV>::execFclass_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  float f1 = fpRegs_.readSingle(rs1);
  URV result = 0;

  bool pos = not std::signbit(f1);
  int type = std::fpclassify(f1);

  if (type == FP_INFINITE)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosInfinity);
      else
	result |= URV(FpClassifyMasks::NegInfinity);
    }
  else if (type == FP_NORMAL)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosNormal);
      else
	result |= URV(FpClassifyMasks::NegNormal);
    }
  else if (type == FP_SUBNORMAL)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosSubnormal);
      else
	result |= URV(FpClassifyMasks::NegSubnormal);
    }
  else if (type == FP_ZERO)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosZero);
      else
	result |= URV(FpClassifyMasks::NegZero);
    }
  else if(type == FP_NAN)
    {
      bool quiet = mostSignificantFractionBit(f1);
      if (quiet)
	result |= URV(FpClassifyMasks::QuietNan);
      else
	result |= URV(FpClassifyMasks::SignalingNan);
    }

  intRegs_.write(rd, result);
}


template <typename URV>
void
Core<URV>::execFcvt_s_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  int32_t i1 = intRegs_.read(rs1);
  float result = i1;
  fpRegs_.writeSingle(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_s_wu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  uint32_t u1 = intRegs_.read(rs1);
  float result = u1;
  fpRegs_.writeSingle(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmv_w_x(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvf())
    {
      illegalInst();
      return;
    }

  uint32_t u1 = intRegs_.read(rs1);

  union UFU  // Unsigned float union: reinterpret bits as unsigned or float
  {
    uint32_t u;
    float f;
  };

  UFU ufu;
  ufu.u = u1;

  fpRegs_.writeSingle(rd, ufu.f);
}


template <typename URV>
void
Core<URV>::execFcvt_l_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  SRV result = int64_t(f1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_lu_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  URV result = uint64_t(f1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_s_l(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  int64_t i1 = intRegs_.read(rs1);
  float result = i1;
  fpRegs_.writeSingle(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_s_lu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvf())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  uint64_t i1 = intRegs_.read(rs1);
  float result = i1;
  fpRegs_.writeSingle(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFld(uint32_t rd, uint32_t rs1, int32_t imm)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  URV address = intRegs_.read(rs1) + SRV(imm);
  bool hasTrigger = hasActiveTrigger();

  typedef TriggerTiming Timing;

  bool isLoad = true;
  if (hasTrigger and ldStAddrTriggerHit(address, Timing::Before, isLoad,
					isInterruptEnabled()))
    {
      triggerTripped_ = true;
      return;
    }

  loadAddr_ = address;    // For reporting load addr in trace-mode.
  loadAddrValid_ = true;  // For reporting load addr in trace-mode.

  // Misaligned load from io section triggers an exception.
  if ((address & 0xf) and not isIdempotentRegion(address))
    {
      initiateException(ExceptionCause::LOAD_ADDR_MISAL, currPc_, address);
      ldStException_ = true;
      return;
    }

  union UDU  // Unsigned double union: reinterpret bits as unsigned or double
  {
    uint64_t u;
    double d;
  };

  uint64_t val64 = 0;
  if (memory_.read(address, val64) and not forceAccessFail_)
    {
      UDU udu;
      udu.u = val64;
      fpRegs_.write(rd, udu.d);
    }
  else
    {
      forceAccessFail_ = false;
      initiateException(ExceptionCause::LOAD_ACC_FAULT, currPc_, address);
      ldStException_ = true;
    }
}


template <typename URV>
void
Core<URV>::execFsd(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  URV addr = intRegs_.read(rs1) + SRV(imm);
  double val = fpRegs_.read(rs2);

  union UDU  // Unsigned double union: reinterpret bits as unsigned or double
  {
    uint64_t u;
    double d;
  };

  UDU udu;
  udu.d = val;

  store<uint64_t>(addr, udu.u);
}


template <typename URV>
void
Core<URV>::execFmadd_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double f1 = fpRegs_.read(rs1);
  double f2 = fpRegs_.read(rs2);
  double f3 = fpRegs_.read(instRs3_);
  double res = std::fma(f1, f2, f3);
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmsub_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double f1 = fpRegs_.read(rs1);
  double f2 = fpRegs_.read(rs2);
  double f3 = fpRegs_.read(instRs3_);
  double res = std::fma(f1, f2, -f3);
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFnmsub_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double f1 = fpRegs_.read(rs1);
  double f2 = fpRegs_.read(rs2);
  double f3 = fpRegs_.read(instRs3_);
  double res = std::fma(f1, f2, -f3);
  fpRegs_.write(rd, -res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFnmadd_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double f1 = fpRegs_.read(rs1);
  double f2 = fpRegs_.read(rs2);
  double f3 = fpRegs_.read(instRs3_);
  double res = std::fma(f1, f2, f3);
  fpRegs_.write(rd, -res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFadd_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);
  double res = d1 + d2;
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFsub_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);
  double res = d1 - d2;
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmul_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);
  double res = d1 * d2;
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFdiv_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }


  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);
  double res = d1 / d2;
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFsgnj_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);
  double res = copysign(d1, d2);  // Magnitude of rs1 and sign of rs2
  fpRegs_.write(rd, res);
}


template <typename URV>
void
Core<URV>::execFsgnjn_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);
  double res = copysign(d1, d2);  // Magnitude of rs1 and sign of rs2
  res = -res;  // Magnitude of rs1 and negative the sign of rs2
  fpRegs_.write(rd, res);
}


template <typename URV>
void
Core<URV>::execFsgnjx_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);

  int sign1 = (std::signbit(d1) == 0) ? 0 : 1;
  int sign2 = (std::signbit(d2) == 0) ? 0 : 1;
  int sign = sign1 ^ sign2;

  double x = sign? -1 : 1;

  double res = copysign(d1, x);  // Magnitude of rs1 and sign of x
  fpRegs_.write(rd, res);
}


template <typename URV>
void
Core<URV>::execFmin_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double in1 = fpRegs_.read(rs1);
  double in2 = fpRegs_.read(rs2);
  double res = fmin(in1, in2);
  fpRegs_.write(rd, res);
}


template <typename URV>
void
Core<URV>::execFmax_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double in1 = fpRegs_.read(rs1);
  double in2 = fpRegs_.read(rs2);
  double res = fmax(in1, in2);
  fpRegs_.write(rd, res);
}


template <typename URV>
void
Core<URV>::execFcvt_d_s(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  float f1 = fpRegs_.readSingle(rs1);
  double result = f1;
  fpRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_s_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  float result = d1;
  fpRegs_.writeSingle(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFsqrt_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  double res = std::sqrt(d1);
  fpRegs_.write(rd, res);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFle_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);

  URV res = (d1 <= d2)? 1 : 0;
  intRegs_.write(rd, res);

  updateAccruedFpBits();
}


template <typename URV>
void
Core<URV>::execFlt_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);

  URV res = (d1 < d2)? 1 : 0;
  intRegs_.write(rd, res);

  updateAccruedFpBits();
}


template <typename URV>
void
Core<URV>::execFeq_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  double d2 = fpRegs_.read(rs2);

  URV res = (d1 == d2)? 1 : 0;
  intRegs_.write(rd, res);

  updateAccruedFpBits();
}


template <typename URV>
void
Core<URV>::execFcvt_w_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  SRV result = int32_t(d1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_wu_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double d1 = fpRegs_.read(rs1);
  URV result = uint32_t(d1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_d_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  int32_t i1 = intRegs_.read(rs1);
  double result = i1;
  fpRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_d_wu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  uint32_t i1 = intRegs_.read(rs1);
  double result = i1;
  fpRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFclass_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);
  URV result = 0;

  bool pos = not std::signbit(d1);
  int type = std::fpclassify(d1);

  if (type == FP_INFINITE)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosInfinity);
      else
	result |= URV(FpClassifyMasks::NegInfinity);
    }
  else if (type == FP_NORMAL)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosNormal);
      else
	result |= URV(FpClassifyMasks::NegNormal);
    }
  else if (type == FP_SUBNORMAL)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosSubnormal);
      else
	result |= URV(FpClassifyMasks::NegSubnormal);
    }
  else if (type == FP_ZERO)
    {
      if (pos)
	result |= URV(FpClassifyMasks::PosZero);
      else
	result |= URV(FpClassifyMasks::NegZero);
    }
  else if(type == FP_NAN)
    {
      bool quiet = mostSignificantFractionBit(d1);
      if (quiet)
	result |= URV(FpClassifyMasks::QuietNan);
      else
	result |= URV(FpClassifyMasks::SignalingNan);
    }

  intRegs_.write(rd, result);
}


template <typename URV>
void
Core<URV>::execFcvt_l_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double f1 = fpRegs_.read(rs1);
  SRV result = int64_t(f1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_lu_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  double f1 = fpRegs_.read(rs1);
  URV result = uint64_t(f1);
  intRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_d_l(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  int64_t i1 = intRegs_.read(rs1);
  double result = i1;
  fpRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFcvt_d_lu(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvd())
    {
      illegalInst();
      return;
    }

  RoundingMode riscvMode = effectiveRoundingMode();
  if (riscvMode >= RoundingMode::Invalid1)
    {
      illegalInst();
      return;
    }

  feClearAllExceptions();
  int prevMode = setSimulatorRoundingMode(riscvMode);

  uint64_t i1 = intRegs_.read(rs1);
  double result = i1;
  fpRegs_.write(rd, result);

  updateAccruedFpBits();
  std::fesetround(prevMode);
}


template <typename URV>
void
Core<URV>::execFmv_d_x(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvd())
    {
      illegalInst();
      return;
    }

  uint64_t u1 = intRegs_.read(rs1);

  union UDU  // Unsigned double union: reinterpret bits as unsigned or double
  {
    uint64_t u;
    double d;
  };

  UDU udu;
  udu.u = u1;

  fpRegs_.write(rd, udu.d);
}


template <typename URV>
void
Core<URV>::execFmv_x_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not isRv64() or not isRvd())
    {
      illegalInst();
      return;
    }

  double d1 = fpRegs_.read(rs1);

  union UDU  // Unsigned double union: reinterpret bits as unsigned or double
  {
    uint64_t u;
    double d;
  };

  UDU udu;
  udu.d = d1;

  intRegs_.write(rd, udu.u);
}


template <typename URV>
void
Core<URV>::execAmoadd_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val + rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmoswap_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
template <typename LOAD_TYPE>
void
Core<URV>::loadReserve(uint32_t rd, uint32_t rs1)
{
  URV addr = intRegs_.read(rs1);

  loadAddr_ = addr;    // For reporting load addr in trace-mode.
  loadAddrValid_ = true;  // For reporting load addr in trace-mode.

  if (hasActiveTrigger())
    {
      typedef TriggerTiming Timing;

      bool isLoad = true;
      if (ldStAddrTriggerHit(addr, Timing::Before, isLoad, isInterruptEnabled()))
	triggerTripped_ = true;
      if (triggerTripped_)
	return;
    }

  // Unsigned version of LOAD_TYPE
  typedef typename std::make_unsigned<LOAD_TYPE>::type ULT;

  // Misaligned load triggers an exception.
  constexpr unsigned alignMask = sizeof(LOAD_TYPE) - 1;
  bool misaligned = addr & alignMask;
  misalignedLdSt_ = misaligned;
  if (misaligned)
    {
      forceAccessFail_ = false;
      ldStException_ = true;
      initiateException(ExceptionCause::LOAD_ADDR_MISAL, currPc_, addr);
      return;
    }

  ULT uval = 0;
  if (memory_.read(addr, uval) and not forceAccessFail_)
    {
      URV value;
      if constexpr (std::is_same<ULT, LOAD_TYPE>::value)
        value = uval;
      else
        value = SRV(LOAD_TYPE(uval)); // Sign extend.

      if (loadQueueEnabled_)
	{
	  URV prev = 0;
	  peekIntReg(rd, prev);
	  putInLoadQueue(sizeof(LOAD_TYPE), addr, rd, prev);
	}

      intRegs_.write(rd, value);
    }
  else
    {
      forceAccessFail_ = false;
      ldStException_ = true;
      initiateException(ExceptionCause::LOAD_ACC_FAULT, currPc_, addr);
    }
}


template <typename URV>
void
Core<URV>::execLr_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  loadReserve<int32_t>(rd, rs1);
  if (ldStException_ or triggerTripped_)
    return;

  hasLr_ = true;
  lrAddr_ = loadAddr_;
}


/// STORE_TYPE is either uint32_t or uint64_t.
template <typename URV>
template <typename STORE_TYPE>
bool
Core<URV>::storeConditional(URV addr, STORE_TYPE storeVal)
{
  // ld/st-address or instruction-address triggers have priority over
  // ld/st access or misaligned exceptions.
  bool hasTrig = hasActiveTrigger();
  TriggerTiming timing = TriggerTiming::Before;
  bool isLoad = false;
  if (hasTrig)
    if (ldStAddrTriggerHit(addr, timing, isLoad, isInterruptEnabled()))
      triggerTripped_ = true;

  // Misaligned store causes an exception.
  constexpr unsigned alignMask = sizeof(STORE_TYPE) - 1;
  bool misaligned = addr & alignMask;
  misalignedLdSt_ = misaligned;
  if (misaligned)
    {
      if (triggerTripped_)
	return false; // No exception if earlier trig. Suppress store data trig.
      forceAccessFail_ = false;
      ldStException_ = true;
      initiateException(ExceptionCause::STORE_ADDR_MISAL, currPc_, addr);
      return false;
    }

  if (hasTrig and not forceAccessFail_ and memory_.checkWrite(addr, storeVal))
    {
      // No exception: consider store-data  trigger
      if (ldStDataTriggerHit(storeVal, timing, isLoad, isInterruptEnabled()))
	triggerTripped_ = true;
    }
  if (triggerTripped_)
    return false;

  if (not hasLr_ or addr != lrAddr_)
    return false;

  if (memory_.write(addr, storeVal) and not forceAccessFail_)
    {
      // If we write to special location, end the simulation.
      if (toHostValid_ and addr == toHost_ and storeVal != 0)
	{
	  throw CoreException(CoreException::Stop, "write to to-host",
			      toHost_, storeVal);
	}

      if (maxStoreQueueSize_)
	{
	  uint64_t prevVal = 0;
	  memory_.getLastWriteOldValue(prevVal);
	  putInStoreQueue(sizeof(STORE_TYPE), addr, storeVal, prevVal);
	}
      return true;
    }
  else
    {
      forceAccessFail_ = false;
      ldStException_ = true;
      initiateException(ExceptionCause::STORE_ACC_FAULT, currPc_, addr);
    }

  return false;
}


template <typename URV>
void
Core<URV>::execSc_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV value = intRegs_.read(rs2);
  URV addr = intRegs_.read(rs1);

  if (storeConditional(addr, uint32_t(value)))
    {
      intRegs_.write(rd, 0); // success
      return;
    }

  if (ldStException_ or triggerTripped_)
    return;

  intRegs_.write(rd, 1);  // fail
}


template <typename URV>
void
Core<URV>::execAmoxor_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val ^ rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmoor_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val | rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmomin_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = (SRV(rs2Val) < SRV(rdVal))? rs2Val : rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmominu_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = (rs2Val < rdVal)? rs2Val : rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmomax_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = (SRV(rs2Val) > SRV(rdVal))? rs2Val : rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmomaxu_w(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);
  int32_t x = rdVal;
  rdVal = SRV(x);

  URV addr = intRegs_.read(rs1);

  URV result = (rs2Val > rdVal)? rs2Val : rdVal;
  store<uint32_t>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmoadd_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLw(rd, rs1, 0);
  if (ldStException_)
    return;

  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val + rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmoswap_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execLr_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  loadReserve<int64_t>(rd, rs1);
  if (ldStException_ or triggerTripped_)
    return;

  hasLr_ = true;
  lrAddr_ = loadAddr_;
}


template <typename URV>
void
Core<URV>::execSc_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV value = intRegs_.read(rs2);
  URV addr = intRegs_.read(rs1);

  if (storeConditional(addr, uint64_t(value)))
    {
      intRegs_.write(rd, 0); // success
      return;
    }

  if (ldStException_ or triggerTripped_)
    return;

  intRegs_.write(rd, 1);  // fail
}


template <typename URV>
void
Core<URV>::execAmoxor_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val ^ rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmoor_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = rs2Val | rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmomin_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = (SRV(rs2Val) < SRV(rdVal))? rs2Val : rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmominu_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = (rs2Val < rdVal)? rs2Val : rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmomax_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = (SRV(rs2Val) > SRV(rdVal))? rs2Val : rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template <typename URV>
void
Core<URV>::execAmomaxu_d(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  URV rs2Val = intRegs_.read(rs2);

  execLd(rd, rs1, 0);
  if (ldStException_)
    return;

  // Sign extend loaded word.
  URV rdVal = intRegs_.read(rd);

  URV addr = intRegs_.read(rs1);

  URV result = (rs2Val > rdVal)? rs2Val : rdVal;
  store<URV>(addr, result);

  if (not ldStException_)
    intRegs_.write(rd, rdVal);
}


template class WdRiscv::Core<uint32_t>;
template class WdRiscv::Core<uint64_t>;
