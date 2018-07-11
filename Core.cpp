#include <iomanip>
#include <iostream>
#include <sstream>
#include <boost/format.hpp>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
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
	  std::cerr << "Only 32 and 64-bit numbers supported in "
		    << "parseCmdLineNumber\n";
	  return false;
	}
      if (end and *end)
	good = false;  // Part of the string are non parseable.
    }
  return good;
}


template <typename URV>
Core<URV>::Core(unsigned hartId, size_t memorySize, unsigned intRegCount)
  : hartId_(hartId), memory_(memorySize), intRegs_(intRegCount)
{
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
  clearTraceData();

  storeQueue_.clear();

  pc_ = resetPc_;
  currPc_ = resetPc_;

  // Enable M (multiply/divide) and C (compressed-instruction)
  // extensions if corresponding bits are set in the MISA CSR..
  rvm_ = false;
  rvc_ = false;

  URV misaVal = 0;
  if (peekCsr(CsrNumber::MISA, misaVal))
    {
      if (misaVal & (URV(1) << ('m' - 'a')))
	rvm_ = true;
      if (misaVal & (URV(1) << ('c' - 'a')))
	rvc_ = true;
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
		       std::unordered_map<std::string,size_t>& symbols)
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
  return memory_.readHalfWord(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint32_t& val) const
{
  return memory_.readWord(address, val);
}


template <typename URV>
bool
Core<URV>::peekMemory(size_t address, uint64_t& val) const
{
  uint32_t high = 0, low = 0;

  if (not memory_.readWord(address, low))
    return false;

  if (not memory_.readWord(address + 4, high))
    return false;

  val = (uint64_t(high) << 32) | low;
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
  // We allow poke to bypasss masking for memory mapped registers
  // otherwise, there is no way for external driver to clear bits that
  // are read-only to this core.
  return memory_.pokeWordNoMask(address, val);
}


template <typename URV>
bool
Core<URV>::pokeMemory(size_t address, uint64_t val)
{
  return memory_.poke(address, val);
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
Core<URV>::putInStoreQueue(unsigned size, size_t addr, uint64_t data)
{
  if (maxStoreQueueSize_ == 0 or memory_.isLastWriteToDccm())
    return;

  if (storeQueue_.size() >= maxStoreQueueSize_)
    {
      for (size_t i = 1; i < maxStoreQueueSize_; ++i)
	storeQueue_[i-1] = storeQueue_[i];
      storeQueue_[maxStoreQueueSize_-1] = StoreInfo(size, addr, data);
    }
  else
    storeQueue_.push_back(StoreInfo(size, addr, data));
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
bool
Core<URV>::isIdempotentRegion(size_t addr) const
{
  unsigned region = addr >> (sizeof(URV)*8 - 4);
  URV mracVal = 0;
  if (csRegs_.read(CsrNumber::MRAC, PrivilegeMode::Machine, debugMode_,
		   mracVal))
    {
      unsigned bit = (mracVal >> (region*2 + 1)) & 1;
      return bit == 0;
    }
  return true;
}


template <typename URV>
bool
Core<URV>::applyStoreException(URV addr, unsigned& matches)
{
  URV mdsealVal = 0;
  if (peekCsr(CsrNumber::MDSEAL, mdsealVal) and mdsealVal == 0)
    {
      // MDSEAL can only accept a write of zero: poke it.
      pokeCsr(CsrNumber::MDSEAL, 1);
      recordCsrWrite(CsrNumber::MDSEAL);

      // MDSEAC is read only and will be not modified by the
      // write method: poke it.
      pokeCsr(CsrNumber::MDSEAC, addr);
      recordCsrWrite(CsrNumber::MDSEAC);
    }

  matches = 0;

  if (storeQueue_.empty())
    {
      std::cerr << "Error: Store exception at 0x" << std::hex << addr
		<< ": empty store queue\n";
      return false;
    }

  for (const auto& entry : storeQueue_)
    {
      if (entry.size_ > 0 and addr >= entry.addr_ and
	  addr < entry.addr_ + entry.size_)
	matches++;
    }

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
      uint64_t data = entry.data_;

      size_t entryEnd = entry.addr_ + entry.size_;
      if (hit)
	{
	  // Re-play portions of subsequent (to one with exception)
	  // transactions covering undone bytes.
	  for (size_t ba = entry.addr_; ba < entryEnd; ++ba, data >>= 8)
	    if (ba >= undoBegin and ba < undoEnd)
	      pokeMemory(ba, uint8_t(data));
	}
      else if (addr >= entry.addr_ and addr < entryEnd)
	{
	  hit = true;
	  removeIx = ix;
	  size_t offset = addr - entry.addr_;
	  data = data >> (offset*8);
	  for (size_t i = offset; i < entry.size_; ++i)
	    {
	      pokeMemory(addr++, uint8_t(data));
	      data = data >> 8;
	      undoEnd = addr;
	      if ((addr & 7) == 0)
		{ // Crossing double-word boundary
		  if (i + 1 < entry.size_)
		    {
		      entry = StoreInfo(entry.size_ - i - 1, addr, data);
		      removeIx = storeQueue_.size();
		      break;
		    }
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

  return hit;
}


template <typename URV>
inline
void
Core<URV>::reportInstructionFrequency(FILE* file) const
{
  struct CompareFreq
  {
    CompareFreq(const std::vector<uint32_t>& freq)
      : freq(freq)
    { }

    bool operator()(size_t a, size_t b) const
    {
      return freq.at(a) < freq.at(b);
    }

    const std::vector<uint32_t>& freq;
  };

  std::vector<size_t> indices(instFreqVec_.size());
  for (size_t i = 0; i < indices.size(); ++i)
    indices.at(i) = i;
  std::sort(indices.begin(), indices.end(), CompareFreq(instFreqVec_));

  for (size_t i = 0; i < indices.size(); ++i)
    {
      size_t ix = indices.at(i);
      InstId id = InstId(ix);
      uint32_t freq = instFreqVec_.at(ix);
      if (freq)
	{
	  const InstInfo& info = instTable_.getInstInfo(id);
	  fprintf(file, "%s %d\n", info.name().c_str(), freq);
	}
    }
}


template <typename URV>
template <typename LOAD_TYPE>
void
Core<URV>::load(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  bool hasTrigger = hasActiveTrigger();

  typedef TriggerTiming Timing;

  bool isLoad = true;
  if (hasTrigger and ldStAddrTriggerHit(address, Timing::Before, isLoad))
    throw CoreException(CoreException::TriggerHit, "", address, 0, true);

  loadAddr_ = address;    // For reporting load addr in trace-mode.
  loadAddrValid_ = true;  // For reporting load addr in trace-mode.

  // Misaligned load from io section triggers an exception.
  unsigned alignMask = sizeof(LOAD_TYPE) - 1;
  if ((address & alignMask) and not isIdempotentRegion(address))
    {
      initiateException(ExceptionCause::LOAD_ADDR_MISAL, currPc_, address);
      return;
    }

  // Unsigned version of LOAD_TYPE
  typedef typename std::make_unsigned<LOAD_TYPE>::type ULT;

  if constexpr (std::is_same<ULT, uint8_t>::value)
    {
      if (conIoValid_ and address == conIo_)
	{
	  int c = fgetc(stdin);
	  SRV val = c;
	  intRegs_.write(rd, val);
	  return;
	}
    }

  ULT uval = 0;
  if (memory_.read(address, uval) and not forceAccessFail_)
    {
      URV value;
      if constexpr (std::is_same<ULT, LOAD_TYPE>::value)
        value = uval;
      else
        value = SRV(LOAD_TYPE(uval)); // Sign extend.

      if (hasTrigger and ldStDataTriggerHit(value, Timing::Before, isLoad))
	throw CoreException(CoreException::TriggerHit, "", address, value,
			    true);

      intRegs_.write(rd, value);

      if (hasTrigger)
	{
	  bool addrHit = ldStAddrTriggerHit(address, Timing::After, isLoad);
	  bool valueHit = ldStDataTriggerHit(value, Timing::After, isLoad);
	  if (addrHit or valueHit)
	    throw CoreException(CoreException::TriggerHit, "", address, value,
				false);
	}
    }
  else
    {
      forceAccessFail_ = false;
      retiredInsts_--;
      initiateException(ExceptionCause::LOAD_ACC_FAULT, currPc_, address);
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
  return memory_.defineIccm(region, offset, size);
}
    

template <typename URV>
bool
Core<URV>::defineDccm(size_t region, size_t offset, size_t size)
{
  return memory_.defineDccm(region, offset, size);
}


template <typename URV>
bool
Core<URV>::defineMemoryMappedRegisterRegion(size_t region, size_t size,
					  size_t regionOffset)
{
  return memory_.defineMemoryMappedRegisterRegion(region, size, regionOffset);
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
  if (__builtin_expect(addr & 1, 0))
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
void
Core<URV>::illegalInst()
{
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
}


// Start a synchronous exception.
template <typename URV>
void
Core<URV>::initiateException(ExceptionCause cause, URV pc, URV info)
{
  bool interrupt = false;
  exceptionCount_++;
  initiateTrap(interrupt, URV(cause), pc, info);
}


template <typename URV>
void
Core<URV>::initiateTrap(bool interrupt, URV cause, URV pcToSave, URV info)
{
  lastTrapValid_ = true;
  lastTrapInterrupt_ = interrupt;
  lastTrapCause_ = cause;

  // TBD: support cores with S and U privilege modes.
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

  // Save addres of instruction that caused the exception or address
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

  // Update status register saving xIE in xPIE and prevoius privilege
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
Core<URV>::pokeIntReg(unsigned ix, URV val)
{ 
  if (ix < intRegs_.size())
    {
      intRegs_.write(ix, val);
      return true;
    }
  return false;
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val) const
{ 
  return csRegs_.read(csrn, PrivilegeMode::Machine, debugMode_, val);
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val, URV& writeMask,
		   URV& pokeMask) const
{ 
  Csr<URV> csr;
  if (not csRegs_.findCsr(csrn, csr))
    return false;

  if (not csr.isImplemented())
    return false;

  if (csRegs_.read(csrn, PrivilegeMode::Machine, debugMode_, val))
    {
      writeMask = csr.getWriteMask();
      pokeMask = csr.getPokeMask();
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::peekCsr(CsrNumber csrn, URV& val, std::string& name) const
{ 
  Csr<URV> csr;
  if (not csRegs_.findCsr(csrn, csr))
    return false;

  if (not csr.isImplemented())
    return false;

  if (csRegs_.read(csrn, PrivilegeMode::Machine, debugMode_, val))
    {
      name = csr.getName();
      return true;
    }

  return false;
}


template <typename URV>
bool
Core<URV>::pokeCsr(CsrNumber csr, URV val)
{ 
  // Direct write will not affect claimid. Set indirectly changing
  // only claim id.
  if (csr == CsrNumber::MEIHAP)
    {
      URV claimIdMask = 0x3fc;
      URV prev = 0;
      if (not csRegs_.read(CsrNumber::MEIHAP, PrivilegeMode::Machine,
			   debugMode_, prev))
	return false;
      URV newVal = (prev & ~claimIdMask) | (val & claimIdMask);
      csRegs_.poke(CsrNumber::MEIHAP, PrivilegeMode::Machine, newVal);
      return true;
    }

  // Some/all bits of some CSRs are read only to CSR instructions but
  // are modifiable. Use the poke method (instead of write) to make
  // sure modifiable value are changed.
  return csRegs_.poke(csr, PrivilegeMode::Machine, val);
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
Core<URV>::findCsr(const std::string& name, CsrNumber& num) const
{
  Csr<URV> csr;
  if (csRegs_.findCsr(name, csr))
    {
      num = csr.getNumber();
      return true;
    }

  unsigned n = 0;
  if (parseNumber<unsigned>(name, n))
    {
      CsrNumber csrn = CsrNumber(n);
      if (csRegs_.findCsr(csrn, csr))
	{
	  num = csr.getNumber();
	  return true;
	}
    }

  return false;
}


template <typename URV>
bool
Core<URV>::configCsr(const std::string& name, bool implemented,
		     URV resetValue, URV mask, URV pokeMask)
{
  return csRegs_.configCsr(name, implemented, resetValue, mask, pokeMask);
}


template <typename URV>
void
printInstTrace(FILE* out, uint64_t tag, unsigned hartId, URV currPc,
	       const char* opcode, char resource, URV addr,
	       URV value, const char* assembly)
{
  if constexpr (sizeof(URV) == 4)
     fprintf(out, "#%ld %d %08x %8s %c %08x %08x  %s",
	     tag, hartId, currPc, opcode, resource, addr, value, assembly);

  else
    fprintf(out, "#%ld %d %016lx %8s %c %016lx %016lx  %s",
	    tag, hartId, currPc, opcode, resource, addr, value, assembly);
}


template <typename URV>
void
Core<URV>::traceInst(uint32_t inst, uint64_t tag, std::string& tmp,
		     FILE* out, bool interrupt)
{
  // TBD: Change format when using 64-bit.
  disassembleInst(inst, tmp);
  if (interrupt)
    tmp += " (interrupted)";

  if (traceLoad_ and loadAddrValid_)
    {
      std::ostringstream oss;
      oss << "0x" << std::hex << loadAddr_;
      tmp += " [" + oss.str() + "]";
      loadAddrValid_ = false;
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
      printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'r', reg, value,
			  tmp.c_str());
      pending = true;
    }

  // Process CSR diff.
  std::vector<CsrNumber> csrs;
  std::vector<unsigned> triggers;
  csRegs_.getLastWrittenRegs(csrs, triggers);
  std::sort(csrs.begin(), csrs.end());
  std::sort(triggers.begin(), triggers.end());

  std::vector<bool> tdataChanged(3);

  if (not csrs.empty())
    {
      // Sort to avoid printing duplicate records.
      std::sort(csrs.begin(), csrs.end());

      // Invalid CSR num.
      CsrNumber prev = CsrNumber(unsigned(CsrNumber::MAX_CSR_) + 1);

      for (CsrNumber csr : csrs)
	{
	  if (csr == prev)
	    continue;

	  prev = csr;
	  if (not csRegs_.read(csr, PrivilegeMode::Machine, debugMode_, value))
	    continue;

	  if (csr >= CsrNumber::TDATA1 and csr <= CsrNumber::TDATA3)
	    {
	      size_t ix = size_t(csr) - size_t(CsrNumber::TDATA1);
	      tdataChanged.at(ix) = true;
	      continue; // Debug triggers printed separately below
	    }

	  if (pending) fprintf(out, "  +\n");
	  printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'c',
			      URV(csr), value, tmp.c_str());
	  pending = true;
	}
    }

  if (not triggers.empty())
    {
      unsigned prevTrigger = triggers.back() + 1;
      for (unsigned trigger : triggers)
	{
	  if (trigger == prevTrigger)
	    continue;
	  prevTrigger = trigger;
	  URV data1(0), data2(0), data3(0);
	  if (not peekTrigger(trigger, data1, data2, data3))
	    continue;
	  if (tdataChanged.at(0))
	    {
	      if (pending) fprintf(out, "  +\n");
	      URV ecsr = (trigger << 16) | URV(CsrNumber::TDATA1);
	      printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'c',
				  ecsr, data1, tmp.c_str());
	      pending = true;
	    }
	  if (tdataChanged.at(1))
	    {
	      if (pending) fprintf(out, "  +\n");
	      URV ecsr = (trigger << 16) | URV(CsrNumber::TDATA2);
	      printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'c',
				  ecsr, data1, tmp.c_str());
	      pending = true;
	    }
	  if (tdataChanged.at(2))
	    {
	      if (pending) fprintf(out, "  +\n");
	      URV ecsr = (trigger << 16) | URV(CsrNumber::TDATA3);
	      printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'c',
				  ecsr, data1, tmp.c_str());
	      pending = true;
	    }
	}
    }

  // Process memory diff.
  size_t address = 0;
  uint64_t memValue = 0;
  unsigned writeSize = memory_.getLastWriteInfo(address, memValue);
  if (writeSize > 0)
    {
      if (pending)
	fprintf(out, "  +\n");

      uint32_t word = memValue;
      printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'm',
			  address, word, tmp.c_str());
      pending = true;
    }

  if (pending) 
    fprintf(out, "\n");
  else
    {
      // No diffs: Generate an x0 record.
      printInstTrace<URV>(out, tag, hartId_, currPc_, instBuff, 'r', 0, 0,
			  tmp.c_str());
      fprintf(out, "\n");
    }
}


template <typename URV>
void
Core<URV>::accumulateInstructionFrequency(uint32_t inst)
{
  uint32_t op0 = 0, op1 = 0; int32_t op2 = 0;
  const InstInfo& info = decode(inst, op0, op1, op2);
  InstId id = info.instId();
  instFreqVec_.at(size_t(id))++;
}


template <typename URV>
inline
void
Core<URV>::clearTraceData()
{
  intRegs_.clearLastWrittenReg();
  csRegs_.clearLastWrittenRegs();
  memory_.clearLastWriteInfo();
}


template <typename URV>
bool
Core<URV>::ldStAddrTriggerHit(URV address, TriggerTiming timing, bool isLoad)
{
  bool hit = csRegs_.ldStAddrTriggerHit(address, timing, isLoad);
  if (hit)
    {
      if (timing == TriggerTiming::Before)
	triggerBeforeCount_++;
      else if (timing == TriggerTiming::After)
	triggerAfterCount_++;
    }
  return hit;
}


template <typename URV>
bool
Core<URV>::ldStDataTriggerHit(URV value, TriggerTiming timing, bool isLoad)
{
  bool hit = csRegs_.ldStDataTriggerHit(value, timing, isLoad);
  if (hit)
    {
      if (timing == TriggerTiming::Before)
	triggerBeforeCount_++;
      else if (timing == TriggerTiming::After)
	triggerAfterCount_++;
    }
  return hit;
}


template <typename URV>
bool
Core<URV>::instAddrTriggerHit(URV address, TriggerTiming timing)
{
  bool hit = csRegs_.instAddrTriggerHit(address, timing);
  if (hit)
    {
      if (timing == TriggerTiming::Before)
	triggerBeforeCount_++;
      else if (timing == TriggerTiming::After)
	triggerAfterCount_++;
    }
  return hit;
}


template <typename URV>
bool
Core<URV>::instOpcodeTriggerHit(URV opcode, TriggerTiming timing)
{
  bool hit = csRegs_.instOpcodeTriggerHit(opcode, timing);
  if (hit)
    {
      if (timing == TriggerTiming::Before)
	triggerBeforeCount_++;
      else if (timing == TriggerTiming::After)
	triggerAfterCount_++;
    }
  return hit;
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
  unsigned writeSize = memory_.getLastWriteInfo(address, value);

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



template <typename URV>
bool
Core<URV>::runUntilAddress(URV address, FILE* traceFile)
{
  struct timeval t0;
  gettimeofday(&t0, nullptr);

  std::string instStr;
  instStr.reserve(128);

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  retiredInsts_ = csRegs_.getRetiredInstCount();
  cycleCount_ = csRegs_.getCycleCount();

  bool trace = traceFile != nullptr;
  csRegs_.traceWrites(true);

  uint64_t counter = counter_;
  uint64_t limit = instCountLim_;
  bool success = true;
  bool instFreq = instFreq_;

  clearTraceData();

  if (enableGdb_)
    handleExceptionForGdb(*this);

  uint32_t inst = 0;

  while (pc_ != address and counter < limit)
    {
      inst = 0;

      try
	{
	  // Fetch instruction incrementing program counter. A two-byte
	  // value is first loaded. If its least significant bits are
	  // 00, 01, or 10 then we have a 2-byte instruction and the fetch
	  // is complete. If the least sig bits are 11 then we have a 4-byte
	  // instruction and two additional bytes are loaded.
	  currPc_ = pc_;

	  // Process pre-execute address trigger.
	  bool hasTrig = hasActiveInstTrigger();
	  if (hasTrig and instAddrTriggerHit(currPc_, TriggerTiming::Before))
	    {
	      readInst(currPc_, inst);
	      initiateException(ExceptionCause::BREAKP, currPc_, currPc_);
	      ++cycleCount_; ++counter;
	      if (traceFile)
		traceInst(inst, counter, instStr, traceFile);
	      clearTraceData();
	      continue;  // Next instruction in trap handler.
	    }

	  if (fetchInst(pc_, inst))
	    {
	      // Process pre-execute opcode trigger.
	      if (hasTrig and instOpcodeTriggerHit(inst, TriggerTiming::Before))
		{
		  initiateException(ExceptionCause::BREAKP, currPc_, currPc_);
		  ++cycleCount_; ++counter;
		  if (traceFile)
		    traceInst(inst, counter, instStr, traceFile);
		  clearTraceData();
		  continue;  // Next instruction in trap handler.
		}

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
	      ++retiredInsts_;
	    }

	  ++cycleCount_;
	  ++counter;

	  bool icountHit = false;
	  if (enableTriggers_ and isInterruptEnabled())
	    icountHit = icountTriggerHit();

	  if (instFreq)
	    accumulateInstructionFrequency(inst);

	  if (trace)
	    traceInst(inst, counter, instStr, traceFile);
	  clearTraceData();

	  if (icountHit or hasTrig)
	    {
	      bool ah = instAddrTriggerHit(currPc_, TriggerTiming::After);
	      bool oh = instOpcodeTriggerHit(currPc_, TriggerTiming::After);
	      if (ah or oh or icountHit)
		initiateException(ExceptionCause::BREAKP, pc_, pc_);
	    }
	}
      catch (const CoreException& ce)
	{
	  if (ce.type() == CoreException::Stop)
	    {
	      if (trace)
		{
		  uint32_t inst = 0;
		  readInst(currPc_, inst);
		  ++counter;
		  traceInst(inst, counter, instStr, traceFile);
		  clearTraceData();
		}
	      std::cout.flush();
	      success = ce.value() == 1; // Anything besides 1 is a fail.
	      std::cerr << (success? "Successful " : "Error: Failed ")
			<< "stop: " << std::dec << ce.value() << " written to "
			<< "tohost\n";
	      break;
	    }
	  else if (ce.type() == CoreException::TriggerHit)
	    {
	      URV epc = ce.isTriggerBefore() ? currPc_ : pc_;
	      initiateException(ExceptionCause::BREAKP, epc, epc);
	      if (traceFile)
		traceInst(inst, counter, instStr, traceFile);
	      clearTraceData();
	      continue;
	    }
	  else
	    {
	      std::cout.flush();
	      std::cerr << "Stopped -- unexpected exception\n";
	    }
	}
    }

  if (counter == limit)
    std::cerr << "Stopped -- Reached instruction limit\n";
  else if (pc_ == address)
    std::cerr << "Stopped -- Reached end address\n";

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);
  counter_ = counter;

  // Simulator stats.
  struct timeval t1;
  gettimeofday(&t1, nullptr);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)*1e-6;

  std::cout.flush();
  std::cerr << "Retired " << counter << " instruction"
	    << (counter > 1? "s" : "") << " in "
	    << (boost::format("%.2fs") % elapsed);
  if (elapsed > 0)
    std::cerr << "  " << size_t(counter/elapsed) << " inst/s";
  std::cerr << '\n';

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
  // address given us about an 10 percent boost in speed.
  if (stopAddrValid_ and not toHostValid_)
    return runUntilAddress(stopAddr_, file);

  // To run fast, this method does not do much besides straigh-forward
  // execution. If any option is turned on, we switch to
  // runUntilAdress which runs slower but is full-featured.
  if (file or instCountLim_ < ~uint64_t(0) or instFreq_ or enableTriggers_ or
      enableGdb_)
    {
      URV address = ~URV(0);  // Invalid stop PC.
      return runUntilAddress(address, file);
    }

  struct timeval t0;
  gettimeofday(&t0, nullptr);

  csRegs_.traceWrites(false);

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  retiredInsts_ = csRegs_.getRetiredInstCount();
  cycleCount_ = csRegs_.getCycleCount();
  bool success = true;

  try
    {
      while (true) 
	{
	  // Fetch instruction incrementing program counter. A two-byte
	  // value is first loaded. If its least significant bits are
	  // 00, 01, or 10 then we have a 2-byte instruction and the fetch
	  // is complete. If the least sig bits are 11 then we have a 4-byte
	  // instruction and two additional bytes are loaded.
	  currPc_ = pc_;

	  uint32_t inst;
	  if (not fetchInst(pc_, inst))
	    {
	      ++cycleCount_;
	      continue; // Next instruction in trap handler.
	    }

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
	  ++retiredInsts_;
	}
    }
  catch (const CoreException& ce)
    {
      if (ce.type() == CoreException::Stop)
	{
	  std::cout.flush();
	  success = ce.value() == 1; // Anything besides 1 is a fail.
	  std::cerr << (success? "Successful " : "Error: Failed ")
		    << "stop: " << std::dec << ce.value() << " written to "
		    << "tohost\n";
	}
      else
	{
	  std::cout.flush();
	  std::cerr << "Stopped -- unexpected exception\n";
	}
    }

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);

  // Simulator stats.
  struct timeval t1;
  gettimeofday(&t1, nullptr);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)*1e-6;

  std::cout.flush();
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
      // Order of priority: machine, supervisor, user and then
      //  external, software, timer
      if (mie & (1 << unsigned(InterruptCause::M_EXTERNAL)) & mip)
	{
	  cause = InterruptCause::M_EXTERNAL;
	  return true;
	}
      if (mie & (1 << unsigned(InterruptCause::M_STORE_BUS)) & mip)
	{
	  cause = InterruptCause::M_STORE_BUS;
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
    }

  return false;
}


template <typename URV>
void
Core<URV>::singleStep(FILE* traceFile)
{
  std::string instStr;

  // Get retired instruction and cycle count from the CSR register(s)
  // so that we can count in a local variable and avoid the overhead
  // of accessing CSRs after each instruction.
  retiredInsts_ = csRegs_.getRetiredInstCount();
  cycleCount_ = csRegs_.getCycleCount();

  // Single step is mostly used for follow-me mode where we want to
  // know the changes after the execution of each instruction.
  csRegs_.traceWrites(true);

  try
    {
      uint32_t inst = 0;
      currPc_ = pc_;

      // Check if there is a pending interrupt and interrupts are enabled.
      // If so, take interrupt.
      InterruptCause cause;
      if (isInterruptPossible(cause))
	{
	  // Attach changes to interrupted instruction.
	  initiateInterrupt(cause, pc_);
	  ++cycleCount_;
	  ++counter_;
	  uint32_t inst = 0; // Load interrupted inst.
	  readInst(currPc_, inst);
	  if (traceFile)  // Trace interrupted instruction.
	    traceInst(inst, counter_, instStr, traceFile, true);
	  return; // Next instruction in trap handler
	}

      // Process pre-execute address trigger.
      bool hasTrigger = hasActiveInstTrigger();
      if (hasTrigger and instAddrTriggerHit(currPc_, TriggerTiming::Before))
	{
	  readInst(currPc_, inst);
	  initiateException(ExceptionCause::BREAKP, currPc_, currPc_);
	  ++cycleCount_; ++counter_;
	  if (traceFile)
	    traceInst(inst, counter_, instStr, traceFile);
	  return;  // Next instruction in trap handler.
	}

      // Fetch instruction incrementing program counter. A two-byte
      // value is first loaded. If its least significant bits are
      // 00, 01, or 10 then we have a 2-byte instruction and the fetch
      // is complete. If the least sig bits are 11 then we have a 4-byte
      // instruction and two additional bytes are loaded.
      bool fetchFail = not fetchInst(pc_, inst);
      if (forceFetchFail_ and not fetchFail)
	initiateException(ExceptionCause::INST_ACC_FAULT, pc_, pc_);
      if (fetchFail or forceFetchFail_)
	{
	  forceFetchFail_ = false;
	  ++cycleCount_; ++counter_;
	  if (traceFile)
	    traceInst(inst, counter_, instStr, traceFile);
	  return; // Next instruction in trap handler.
	}

      // Process pre-execute opcode trigger.
      if (hasTrigger and instOpcodeTriggerHit(inst, TriggerTiming::Before))
	{
	  initiateException(ExceptionCause::BREAKP, currPc_, currPc_);
	  ++cycleCount_; ++counter_;
	  if (traceFile)
	    traceInst(inst, counter_, instStr, traceFile);
	  return;  // Next instruction in trap handler.
	}

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
      ++retiredInsts_;
      ++counter_;

      bool icountHit = false;
      if (enableTriggers_ and isInterruptEnabled())
	icountHit = icountTriggerHit();

      if (instFreq_)
	accumulateInstructionFrequency(inst);

      if (traceFile)
	traceInst(inst, counter_, instStr, traceFile);

      if (icountHit or hasActiveInstTrigger())
	{
	  bool addrHit = instAddrTriggerHit(currPc_, TriggerTiming::After);
	  bool opcodeHit = instOpcodeTriggerHit(currPc_, TriggerTiming::After);
	  if (addrHit or opcodeHit or icountHit)
	    initiateException(ExceptionCause::BREAKP, pc_, pc_);
	}
    }
  catch (const CoreException& ce)
    {
      uint32_t inst = 0;
      readInst(currPc_, inst);
      ++cycleCount_;
      ++counter_;
      if (ce.type() == CoreException::Stop)
	{
	  if (traceFile)
	    traceInst(inst, counter_, instStr, traceFile);
	  std::cout.flush();
	  std::cerr << "Stopped...\n";
	}
      else if (ce.type() == CoreException::TriggerHit)
	{
	  URV epc = ce.isTriggerBefore() ? currPc_ : pc_;
	  initiateException(ExceptionCause::BREAKP, epc, epc);
	  if (traceFile)
	    traceInst(inst, counter_, instStr, traceFile);
	  return;  // Next instruction in trap handler.
	}
    }

  // Update retired-instruction and cycle count registers.
  csRegs_.setRetiredInstCount(retiredInsts_);
  csRegs_.setCycleCount(cycleCount_);
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
    switch (iform.fields.funct3)
      {
      case 0: execLb(rd, rs1, imm);  break;
      case 1: execLh(rd, rs1, imm);  break;
      case 2: execLw(rd, rs1, imm);  break;
      case 3: execLd(rd, rs1, imm);  break;
      case 4: execLbu(rd, rs1, imm); break;
      case 5: execLhu(rd, rs1, imm); break;
      case 6: execLwu(rd, rs1, imm); break;
      default: illegalInst();        break;
      }
  }
  return;

 l1:
 l2:
 l7:
 l9:
 l10:
 l15:
 l16:
 l17:
 l18:
 l19:
 l20:
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
	if (iform.fields2.top7 == 0)
	  execSlli(rd, rs1, iform.fields2.shamt);
	else
	  illegalInst();
      }
    else if (funct3 == 2)  execSlti(rd, rs1, imm);
    else if (funct3 == 3)  execSltiu(rd, rs1, imm);
    else if (funct3 == 4)  execXori(rd, rs1, imm);
    else if (funct3 == 5)
      {
	if (iform.fields2.top7 == 0)
	  execSrli(rd, rs1, iform.fields2.shamt);
	else if (iform.fields2.top7 == 0x20)
	  execSrai(rd, rs1, iform.fields2.shamt);
	else
	  illegalInst();
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
  }
  return;

 l8:  // 01000  S-form
  {
    SFormInst sform(inst);
    unsigned rs1 = sform.bits.rs1, rs2 = sform.bits.rs2;
    unsigned funct3 = sform.bits.funct3;
    int32_t imm = sform.immed();
    if      (funct3 == 0)  execSb(rs1, rs2, imm);
    else if (funct3 == 1)  execSh(rs1, rs2, imm);
    else if (funct3 == 2)  execSw(rs1, rs2, imm);
    else if (funct3 == 3)  execSd(rs2, rs2, imm);
  }
  return;

 l11:  // 01011  R-form atomics
  {
    RFormInst rf(inst);
    uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
    // uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
    // bool r1 = rf.r1(), aq = rf.aq();
    if (f3 == 2)
      {
	if      (top5 == 0)     unimplemented();  // amoadd.w 
	else if (top5 == 1)     unimplemented();  // amoswap.w
	else if (top5 == 2)     unimplemented();  // lr.w     
	else if (top5 == 3)     unimplemented();  // sc.w     
	else if (top5 == 4)     unimplemented();  // amoxor.w 
	else if (top5 == 8)     unimplemented();  // amoor.w  
	else if (top5 == 0x10)  unimplemented();  // amomin.w 
	else if (top5 == 0x14)  unimplemented();  // amomax.w 
	else if (top5 == 0x18)  unimplemented();  // maominu.w
	else if (top5 == 0x1c)  unimplemented();  // maomaxu.w
      }
    else if (f3 == 3)
      {
	if      (top5 == 0)     unimplemented();  // amoadd.d
	else if (top5 == 1)     unimplemented();  // amoswap.d
	else if (top5 == 2)     unimplemented();  // lr.d
	else if (top5 == 3)     unimplemented();  // sc.d
	else if (top5 == 4)     unimplemented();  // amoxor.d
	else if (top5 == 8)     unimplemented();  // amoor.d
	else if (top5 == 0x10)  unimplemented();  // amomin.d
	else if (top5 == 0x14)  unimplemented();  // amomax.d
	else if (top5 == 0x18)  unimplemented();  // maominu.d
	else if (top5 == 0x1c)  unimplemented();  // maomaxu.d
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
	if      (not rvm_)    illegalInst();
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
  if (not rvc_)
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
	}

      else if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  execLw(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed());
	}

      else if (funct3 == 3)  // c.flw, c.ld
	{
	  if (not rv64_)
	    illegalInst();  // c.flw
	  else
	    {
	      ClFormInst clf(inst);
	      execLd(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed());
	    }
	}

      else if (funct3 == 6)  // c.sw
	{
	  CsFormInst cs(inst);
	  execSw(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.swImmed());
	}

      else if (funct3 == 7) // c.fsw, c.sd
	{
	  if (not rv64_)
	    illegalInst(); // c.fsw
	  else
	    {
	      CsFormInst cs(inst);
	      execSd(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.sdImmed());
	    }
	}

      else // funct3 is 1 (c_fld c_lq), or 4 (reserverd) or 5 (c.fsd c.sq).
	illegalInst();
      return;
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  execAddi(cif.bits.rd, cif.bits.rd, cif.addiImmed());
	}
	  
      else if (funct3 == 1)  // c.jal   TBD: in rv64 and rv128 this is c.addiw
	{
	  CjFormInst cjf(inst);
	  execJal(RegRa, cjf.immed());
	}

      else if (funct3 == 2)  // c.li
	{
	  CiFormInst cif(inst);
	  execAddi(cif.bits.rd, RegX0, cif.addiImmed());
	}

      else if (funct3 == 3)  // c.addi16sp, c.lui
	{
	  CiFormInst cif(inst);
	  int immed16 = cif.addi16spImmed();
	  if (immed16 == 0)
	    illegalInst();
	  else if (cif.bits.rd == RegSp)  // c.addi16sp
	    execAddi(cif.bits.rd, cif.bits.rd, immed16);
	  else
	    execLui(cif.bits.rd, cif.luiImmed());
	}

      // c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
      // c.subw c.addw
      else if (funct3 == 4)
	{
	  CaiFormInst caf(inst);  // compressed and immediate form
	  int immed = caf.andiImmed();
	  unsigned rd = 8 + caf.bits.rdp;
	  unsigned f2 = caf.bits.funct2;
	  if (f2 == 0) // srli64, srli
	    {
	      if (caf.bits.ic5 != 0 and not rv64_)
		illegalInst(); // As of v2.3 of User-Level ISA (Dec 2107).
	      else
		execSrli(rd, rd, caf.shiftImmed());
	    }
	  else if (f2 == 1) // srai64, srai
	    {
	      if (caf.bits.ic5 != 0 and not rv64_)
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
	}

      else if (funct3 == 5)  // c.j
	{
	  CjFormInst cjf(inst);
	  execJal(RegX0, cjf.immed());
	}
	  
      else if (funct3 == 6)  // c.beqz
	{
	  CbFormInst cbf(inst);
	  execBeq(8+cbf.bits.rs1p, RegX0, cbf.immed());
	}

      else // (funct3 == 7)  // c.bnez
	{
	  CbFormInst cbf(inst);
	  execBne(8+cbf.bits.rs1p, RegX0, cbf.immed());
	}

      return;
    }

  if (quadrant == 2)
    {
      if (funct3 == 0)  // c.slli, c.slli64
	{
	  CiFormInst cif(inst);
	  unsigned immed = unsigned(cif.slliImmed());
	  if (cif.bits.ic5 != 0 and not rv64_)
	    illegalInst();
	  else
	    execSlli(cif.bits.rd, cif.bits.rd, immed);
	}

      else if (funct3 == 1)  // c.fldsp c.lqsp
	{
	  unimplemented();
	}

      else if (funct3 == 2)  // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  // rd == 0 is legal per Andrew Watterman
	  execLw(rd, RegSp, cif.lwspImmed());
	}

      else  if (funct3 == 3)  // c.ldsp  c.flwsp
	{
	  if (rv64_)
	    {
	      CiFormInst cif(inst);
	      unsigned rd = cif.bits.rd;
	      // rd == 0 is legal per Andrew Watterman
	      execLd(rd, RegSp, cif.ldspImmed());
	    }
	  else
	    {
	      unimplemented();	      // c.flwsp
	    }
	}

      else if (funct3 == 4)   // c.jr c.mv c.ebreak c.jalr c.add
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
	}

      else if (funct3 == 5)  // c.fsfsp c.sqsp
	{
	  unimplemented();
	}

      else if (funct3 == 6)  // c.swsp
	{
	  CswspFormInst csw(inst);
	  execSw(RegSp, csw.bits.rs2, csw.swImmed());  // imm(sp) <- rs2
	}

      else  if (funct3 == 7)  // c.sdsp  c.fswsp
	{
	  if (rv64_)  // c.sdsp
	    {
	      CswspFormInst csw(inst);
	      execSd(RegSp, csw.bits.rs2, csw.sdImmed());
	    }
	  else
	    {
	      unimplemented();	      // c.fswsp
	    }
	}

      return;
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

      if (funct3 == 2) // c.lw
	{
	  ClFormInst clf(inst);
	  return encodeLw(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed(),
			  code32);
	}

      if (funct3 == 3) // c.flw, c.ld
	{
	  if (not rv64_)
	    return false;  // c.flw
	  ClFormInst clf(inst);
	  return encodeLd(8+clf.bits.rdp, 8+clf.bits.rs1p, clf.lwImmed(),
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
	  if (not rv64_)
	    return false;
	  CsFormInst cs(inst);
	  return encodeSd(8+cs.bits.rs1p, 8+cs.bits.rs2p, cs.sdImmed(), code32);
	}

      // funct3 is 1 (c.fld c.lq), or 4 (reserved), or 5 (c.fsd c.sq)
      return false;
    }

  if (quadrant == 1)
    {
      if (funct3 == 0)  // c.nop, c.addi
	{
	  CiFormInst cif(inst);
	  return encodeAddi(cif.bits.rd, cif.bits.rd, cif.addiImmed(), code32);
	}
	  
      if (funct3 == 1)  // c.jal   TBD: in rv64 and rv128 this is c.addiw
	{
	  CjFormInst cjf(inst);
	  return encodeJal(RegRa, cjf.immed(), 0, code32);
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
	      if (caf.bits.ic5 != 0 and not rv64_)
		return false;  // As of v2.3 of User-Level ISA (Dec 2107).
	      return encodeSrli(rd, rd, caf.shiftImmed(), code32);
	    }
	  if (f2 == 1)  // srai64, srai
	    {
	      if (caf.bits.ic5 != 0 and not rv64_)
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
	  if (not rv64_)
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
	  if (cif.bits.ic5 != 0 and not rv64_)
	    return false;
	  return encodeSlli(cif.bits.rd, cif.bits.rd, immed, code32);
	}

      if (funct3 == 2) // c.lwsp
	{
	  CiFormInst cif(inst);
	  unsigned rd = cif.bits.rd;
	  // rd == 0 is legal per Andrew Watterman
	  return encodeLw(rd, RegSp, cif.lwspImmed(), code32);
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

      if (funct3 == 6) // c.swsp
	{
	  CswspFormInst csw(inst);
	  return encodeSw(RegSp, csw.bits.rs2, csw.swImmed(), code32);
	}

      // funct3 is 1 (c.fldsp c.lqsp), or 3 (c.flwsp c.ldsp),
      // or 5 (c.fsfsp c.sqsp) or 7 (c.fswsp, c.sdsp)
      return false;
    }

  return false; // quadrant 3
}


template <typename URV>
const InstInfo&
Core<URV>::decode(uint32_t inst, uint32_t& op0, uint32_t& op1,
		  int32_t& op2) const
{
  static void *opcodeLabels[] = { &&l0, &&l1, &&l2, &&l3, &&l4, &&l5,
				  &&l6, &&l7, &&l8, &&l9, &&l10, &&l11,
				  &&l12, &&l13, &&l14, &&l15, &&l16, &&l17,
				  &&l18, &&l19, &&l20, &&l21, &&l22, &&l23,
				  &&l24, &&l25, &&l26, &&l27, &&l28, &&l29,
				  &&l30, &&l31 };

  // Expand 16-bit instructions to 32.
  if (isCompressedInst(inst))
    {
      if (not rvc_)
	inst = ~0; // All ones: illegal 32-bit instruction.
      else if (not expandInst(inst, inst))
	inst = ~0; // All ones: illegal 32-bit instruction.
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
    l2:
    l7:
    l9:
    l10:
    l15:
    l16:
    l17:
    l18:
    l19:
    l20:
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
	    if (iform.fields2.top7 == 0)
	      {
		op2 = iform.fields2.shamt;
		return instTable_.getInstInfo(InstId::slli);
	      }
	  }
	else if (funct3 == 2)  return instTable_.getInstInfo(InstId::slti);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::sltiu);
	else if (funct3 == 4)  return instTable_.getInstInfo(InstId::xori);
	else if (funct3 == 5)
	  {
	    op2 = iform.fields2.shamt;
	    if (iform.fields2.top7 == 0)
	      return instTable_.getInstInfo(InstId::srli);
	    else if (iform.fields2.top7 == 0x20)
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
	if      (funct3 == 0)  return instTable_.getInstInfo(InstId::sb);
	else if (funct3 == 1)  return instTable_.getInstInfo(InstId::sh);
	else if (funct3 == 2)  return instTable_.getInstInfo(InstId::sw);
	else if (funct3 == 3)  return instTable_.getInstInfo(InstId::sd);
      }
      return instTable_.getInstInfo(InstId::illegal);

    l11:  // 01011  R-form atomics
      if (false)  // Not implemented
      {
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
	// uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	// bool r1 = rf.r1(), aq = rf.aq();
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
	    if      (funct3 == 0) return instTable_.getInstInfo(InstId::mul);
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
	  case 0:
	    stream << "lb     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 1:
	    stream << "lh     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 2:
	    stream << "lw     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 3:
	    stream << "ld     x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 4:
	    stream << "lbu    x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 5:
	    stream << "lhu    x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 6:
	    stream << "lwu    x" << rd << ", " << imm << "(x" << rs1 << ")";
	    break;
	  default: stream << "illegal";
	    break;
	  }
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
	    stream << "addi   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 1: 
	    if (iform.top7() == 0)
	      stream << "slli   x" << rd << ", x" << rs1 << ", "
		     << iform.fields2.shamt;
	    else
	      stream << "illegal";
	    break;
	  case 2:
	    stream << "slti   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 3:
	    stream << "sltiu  x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 4:
	    stream << "xori   x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 5:
	    if (iform.top7() == 0)
	      stream << "srli   x" << rd << ", x" << rs1 << ", "
		     << iform.fields2.shamt;
	    else if (iform.top7() == 0x20)
	      stream << "srai   x" << rd << ", x" << rs1 << ", "
		     << iform.fields2.shamt;
	    else
	      stream << "illegal";
	    break;
	  case 6:
	    stream << "ori    x" << rd << ", x" << rs1 << ", " << imm;
	    break;
	  case 7:
	    stream << "andi   x" << rd << ", x" << rs1 << ", " << imm;
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
	stream << "auipc x" << uform.bits.rd << ", 0x"
	       << std::hex << ((uform.immed() >> 12) & 0xfffff);
      }
      break;

    case 8:  // 01000  S-form
      {
	SFormInst sform(inst);
	unsigned rs1 = sform.bits.rs1, rs2 = sform.bits.rs2;
	int32_t imm = sform.immed();
	switch (sform.bits.funct3)
	  {
	  case 0:
	    stream << "sb     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 1:
	    stream << "sh     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  case 2:
	    stream << "sw     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  default:
	    stream << "sd     x" << rs2 << ", " << imm << "(x" << rs1 << ")";
	    break;
	  }
      }
      break;

    case 11:  // 01011  R-form  atomics
      {
	RFormInst rf(inst);
	uint32_t top5 = rf.top5(), f3 = rf.bits.funct3;
	// uint32_t rd = rf.rd, rs1 = rf.rs1, rs2 = rf.rs2;
	// bool r1 = rf.r1(), aq = rf.aq();
	if (f3 == 2)
	  {
	    if      (top5 == 0)     stream << "illegal";  // amoadd.w
	    else if (top5 == 1)     stream << "illegal";  // amoswap.w
	    else if (top5 == 2)     stream << "illegal";  // lr.w
	    else if (top5 == 3)     stream << "illegal";  // sc.w
	    else if (top5 == 4)     stream << "illegal";  // amoxor.w
	    else if (top5 == 8)     stream << "illegal";  // amoor.w
	    else if (top5 == 0x0c)  stream << "illegal";  // amoand.w
	    else if (top5 == 0x10)  stream << "illegal";  // amomin.w
	    else if (top5 == 0x14)  stream << "illegal";  // amomax.w
	    else if (top5 == 0x18)  stream << "illegal";  // maominu.w
	    else if (top5 == 0x1c)  stream << "illegal";  // maomaxu.w
	  }
	else if (f3 == 3)
	  {
	    if      (top5 == 0)     stream << "illegal";  // amoadd.d
	    else if (top5 == 1)     stream << "illegal";  // amoswap.d
	    else if (top5 == 2)     stream << "illegal";  // lr.d
	    else if (top5 == 3)     stream << "illegal";  // sc.d
	    else if (top5 == 4)     stream << "illegal";  // amoxor.d
	    else if (top5 == 8)     stream << "illegal";  // amoor.d
	    else if (top5 == 0x0c)  stream << "illegal";  // amoand.d
	    else if (top5 == 0x10)  stream << "illegal";  // amomin.d
	    else if (top5 == 0x14)  stream << "illegal";  // amomax.d
	    else if (top5 == 0x18)  stream << "illegal";  // maominu.d
	    else if (top5 == 0x1c)  stream << "illegal";  // maomaxu.d
	  }
	else stream << "illegal";
      }
      break;

    case 12:  // 01100  R-form
      {
	RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	if (funct7 == 0)
	  {
	    if (funct3 == 0)
	      stream << "add    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 1)
	      stream << "sll    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 2)
	      stream << "slt    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 3)
	      stream << "sltu   x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 4)
	      stream << "xor    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 5)
	      stream << "srl    x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 6)
	      stream << "or     x" << rd << ", x" << rs1 << ", x" << rs2;
	    if (funct3 == 7)
	      stream << "and    x" << rd << ", x" << rs1 << ", x" << rs2;
	  }
	else if (funct7 == 1)
	  {
	    if (not rvm_)
	      stream << "illegal";
	    else if (funct3 == 0)
	      stream << "mul    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 1)
	      stream << "mulh   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 2)
	      stream << "mulhsu x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 3)
	      stream << "mulhu  x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 4)
	      stream << "div    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "divu   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 6)
	      stream << "rem    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 7)
	      stream << "remu   x" << rd << ", x" << rs1 << ", x" << rs2;
	  }
	else if (funct7 == 0x20)
	  {
	    if (funct3 == 0)
	      stream << "sub    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "sra    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
	else
	  stream << "illegal";
      }
      break;

    case 13:  // 01101  U-form
      {
	UFormInst uform(inst);
	stream << "lui    x" << uform.bits.rd << ", " << uform.immed();
      }
      break;

    case 14:  // 01110  R-Form
      {
	const RFormInst rform(inst);
	unsigned rd = rform.bits.rd, rs1 = rform.bits.rs1, rs2 = rform.bits.rs2;
	unsigned funct7 = rform.bits.funct7, funct3 = rform.bits.funct3;
	if (funct7 == 0)
	  {
	    if (funct3 == 0)
	      stream << "addw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 1)
	      stream << "sllw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "srlw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
	else if (funct7 == 1)
	  {
	    if (funct3 == 0)
	      stream << "mulw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 4)
	      stream << "divw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "divuw   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 6)
	      stream << "remw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 7)
	      stream << "remuw   x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
	else if (funct7 == 0x20)
	  {
	    if (funct3 == 0)
	      stream << "subw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else if (funct3 == 5)
	      stream << "sraw    x" << rd << ", x" << rs1 << ", x" << rs2;
	    else
	      stream << "illegal";
	  }
      }
      break;

    case 24:  // 11000   B-form
      {
	BFormInst bform(inst);
	unsigned rs1 = bform.bits.rs1, rs2 = bform.bits.rs2;
	int32_t imm = bform.immed();
	switch (bform.bits.funct3)
	  {
	  case 0:
	    stream << "beq    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 1:
	    stream << "bne    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 4:
	    stream << "blt    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 5:
	    stream << "bge    x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 6:
	    stream << "bltu   x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  case 7:
	    stream << "bgeu   x" << rs1 << ", x" << rs2 << ", " << imm;
	    break;
	  default:
	    stream << "illegal";
	    break;
	  }
      }
      break;

    case 25:  // 11001  I-form
      {
	IFormInst iform(inst);
	if (iform.fields.funct3 == 0)
	  stream << "jalr   x" << iform.fields.rd << ", x" << iform.fields.rs1
		 << ", " << iform.immed();
	else
	  stream << "illegal";
      }
      break;

    case 27:  // 11011  J-form
      {
	JFormInst jform(inst);
	stream << "jal    x" << jform.bits.rd << ", " << jform.immed();
      }
      break;

    case 28:  // 11100  I-form
      {
	IFormInst iform(inst);
	unsigned rd = iform.fields.rd, rs1 = iform.fields.rs1;
	unsigned csrNum = iform.uimmed();
	std::string csrName;
	Csr<URV> csr;
	if (csRegs_.findCsr(CsrNumber(csrNum), csr))
	  csrName = csr.getName();
	else
	  csrName = "illegal";
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
	    stream << "csrrw  x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 2:
	    stream << "csrrs  x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 3:
	    stream << "csrrc  x" << rd << ", " << csrName << ", x" << rs1;
	    break;
	  case 5:
	    stream << "csrrwi x" << rd << ", " << csrName << ", " << rs1;
	    break;
	  case 6:
	    stream << "csrrsi x" << rd << ", " << csrName << ", " << rs1;
	    break;
	  case 7:
	    stream << "csrrci x" << rd << ", " << csrName << ", " << rs1;
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
  if (not rvc_)
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
		  stream << "c.addi4spn x" << (8+ciwf.bits.rdp) << ", "
			 << (immed >> 2);
	      }
	  }
	  break;
	case 1: // c_fld, c_lq  
	  stream << "illegal";
	  break;
	case 2: // c.lw
	  {
	    ClFormInst clf(inst);
	    stream << "c.lw   x" << (8+clf.bits.rdp) << ", " << clf.lwImmed()
		   << "(x" << (8+clf.bits.rs1p) << ")";
	  }
	  break;
	case 3:  // c.flw, c.ld
	  {
	    ClFormInst clf(inst);
	    if (rv64_)
	      stream << "c.ld   x" << (8+clf.bits.rdp) << ", " << clf.ldImmed()
		     << "(x" << (8+clf.bits.rs1p) << ")";
	    else
	      stream << "illegal"; // c.flw
	  }
	  break;
	case 4:  // reserved
	  stream << "illegal";
	  break;
	case 5:  // c.fsd, c.sq
	  stream << "illegal";
	  break;
	case 6:  // c.sw
	  {
	    CsFormInst cs(inst);
	    stream << "c.sw   x" << (8+cs.bits.rs2p) << ", " << cs.swImmed()
		   << "(x" << (8+cs.bits.rs1p) << ")";
	  }
	  break;
	case 7:  // c.fsw, c.sd
	  {
	    CsFormInst cs(inst);
	    if (rv64_)
	      stream << "c.sd  x" << (8+cs.bits.rs2p) << ", " << cs.sdImmed()
		     << "(x" << (8+cs.bits.rs1p) << ")";
	    else
	      stream << "illegal"; // c.fsw
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
	      stream << "c.addi x" << cif.bits.rd << ", " << cif.addiImmed();
	  }
	  break;
	  
	case 1:  // c.jal   TBD: in rv64 and rv128 this is c.addiw
	  {
	    CjFormInst cjf(inst);
	    stream << "c.jal   " << cjf.immed();
	  }
	  break;

	case 2:  // c.li
	  {
	    CiFormInst cif(inst);
	    stream << "c.li   x" << cif.bits.rd << ", " << cif.addiImmed();
	  }
	  break;

	case 3:  // c.addi16sp, c.lui
	  {
	    CiFormInst cif(inst);
	    int immed16 = cif.addi16spImmed();
	    if (immed16 == 0)
	      stream << "illegal";
	    else if (cif.bits.rd == RegSp)
	      stream << "c.addi16sp" << ' ' << (immed16 >> 4);
	    else
	      stream << "c.lui  x" << cif.bits.rd << ", " << cif.luiImmed();
	  }
	  break;

	// c.srli c.srli64 c.srai c.srai64 c.andi c.sub c.xor c.and
	// c.subw c.addw
	case 4:
	  {
	    CaiFormInst caf(inst);  // compressed and immediate form
	    int immed = caf.andiImmed();
	    switch (caf.bits.funct2)
	      {
	      case 0:
		if (caf.bits.ic5 != 0 and not rv64_)
		  stream << "illegal";
		else
		  stream << "c.srli x" << (8+caf.bits.rdp) << ", "
			 << caf.shiftImmed();
		break;
	      case 1:
		if (caf.bits.ic5 != 0 and not rv64_)
		  stream << "illegal";
		else
		  stream << "c.srai x" << (8+caf.bits.rdp) << ", "
			 << caf.shiftImmed();
		break;
	      case 2:
		stream << "c.andi x" << (8+caf.bits.rdp) << ", " << immed;
		break;
	      case 3:
		{
		  unsigned rs2 = 8+(immed & 0x7); // Lowest 3 bits of immed
		  unsigned rd = 8+caf.bits.rdp;
		  if ((immed & 0x20) == 0)  // Bit 5 of immed
		    {
		      switch ((immed >> 3) & 3) // Bits 3 and 4 of immed
			{
			case 0:
			  stream << "c.sub  x" << rd << ", x" << rs2; break;
			case 1:
			  stream << "c.xor  x" << rd << ", x" << rs2; break;
			case 2:
			  stream << "c.or   x" << rd << ", x" << rs2; break;
			case 3:
			  stream << "c.and  x" << rd << ", x" << rs2; break;
			}
		    }
		  else
		    {
		      if (not rv64_)
			stream << "illegal";
		      else
			switch ((immed >> 3) & 3)
			  {
			  case 0: stream << "c.subw x" << rd << ", x" << rs2;
			    break;
			  case 1: stream << "c.addw x" << rd << ", x" << rs2;
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
	    stream << "c.j " << cjf.immed();
	  }
	  break;
	  
	case 6:  // c.beqz
	  {
	    CbFormInst cbf(inst);
	    stream << "c.beqz x" << (8+cbf.bits.rs1p) << ", " << cbf.immed();
	  }
	  break;

	case 7:  // c.bnez
	  {
	    CbFormInst cbf(inst);
	    stream << "c.bnez x" << (8+cbf.bits.rs1p) << ", " << cbf.immed();
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
	    unsigned immed = unsigned(cif.slliImmed());
	    if (cif.bits.ic5 != 0 and not rv64_)
	      stream << "illegal";  // TBD: ok for RV64
	    else
	      stream << "c.slli x" << cif.bits.rd << ", " << immed;
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
	    stream << "c.lwsp x" << rd << ", " << (cif.lwspImmed() >> 2);
	  }
	break;

	case 3:  // c.flwsp c.ldsp
	  if (rv64_)
	    {
	      CiFormInst cif(inst);
	      unsigned rd = cif.bits.rd;
	      stream << "c.ldsp x" << rd << ", " << (cif.ldspImmed() >> 3);
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
	    if ((immed & 0x20) == 0)
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.jr   x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.mv   x" << rd << ", x" << rs2;
		  }
	      }
	    else
	      {
		if (rs2 == 0)
		  {
		    if (rd == 0)
		      stream << "c.ebreak";
		    else
		      stream << "c.jalr x" << rd;
		  }
		else
		  {
		    if (rd == 0)
		      stream << "illegal";
		    else
		      stream << "c.add  x" << rd << ", x" << rs2;
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
	    stream << "c.swsp x" << csw.bits.rs2 << ", "
		   << (csw.swImmed() >> 2);
	  }
	  break;

	case 7:  // c.fswsp c.sdsp
	  {
	    if (rv64_)  // c.sdsp
	      {
		CswspFormInst csw(inst);
		stream << "c.sdsp x" << csw.bits.rs2 << ", "
		       << (csw.sdImmed() >> 3);
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
Core<URV>::enterDebugMode(DebugModeCause cause)
{
  debugMode_ = true;

  URV value = 0;
  if (csRegs_.read(CsrNumber::DCSR, PrivilegeMode::Machine, debugMode_, value))
    {
      value |= (URV(cause) << 6);  // Cause field starts at bit 6
      csRegs_.poke(CsrNumber::DCSR, PrivilegeMode::Machine, value);

      // Once test-bench is fixed, enable this.
      // recordCsrWrite(CsrNumber::DCSR);
    }
}


template <typename URV>
void
Core<URV>::execBeq(uint32_t rs1, uint32_t rs2, int32_t offset)
{
  if (intRegs_.read(rs1) != intRegs_.read(rs2))
    return;
  pc_ = currPc_ + SRV(offset);
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
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
    }
}


template <typename URV>
void
Core<URV>::execJalr(uint32_t rd, uint32_t rs1, int32_t offset)
{
  URV temp = pc_;  // pc has the address of the instruction adter jalr
  pc_ = (intRegs_.read(rs1) + SRV(offset));
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
  intRegs_.write(rd, temp);
}


template <typename URV>
void
Core<URV>::execJal(uint32_t rd, uint32_t offset, int32_t)
{
  intRegs_.write(rd, pc_);
  pc_ = currPc_ + SRV(int32_t(offset));
  pc_ = (pc_ >> 1) << 1;  // Clear least sig bit.
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
  if (amount < 0)
    {
      illegalInst();
      return;
    }
  if ((amount > 31) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = intRegs_.read(rs1) >> amount;
  intRegs_.write(rd, v);
}


template <typename URV>
void
Core<URV>::execSrai(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if ((amount & 0x20) and not rv64_)
    {
      illegalInst();  // Bit 5 of shift amount cannot be zero in 32-bit.
      return;
    }

  URV v = SRV(intRegs_.read(rs1)) >> amount;
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
Core<URV>::execAndi(uint32_t rd, uint32_t rs1, int32_t imm)
{
  URV v = intRegs_.read(rs1) & SRV(imm);
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
void
Core<URV>::execEcall(uint32_t, uint32_t, int32_t)
{
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
  URV savedPc = currPc_;  // Goes into MEPC.

  // Goes into MTVAL: Sec 3.1.21 of RISCV privileged arch (version 1.11).
  URV trapInfo = currPc_;

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
    illegalInst();
  else
    {
      // Restore privilege mode and interrupt enable by getting
      // current value of MSTATUS, ...
      URV value = 0;
      if (not csRegs_.read(CsrNumber::MSTATUS, privMode_, debugMode_, value))
	assert(0 and "Failed to write MSTATUS register\n");

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
}


template <typename URV>
void
Core<URV>::execSret(uint32_t, uint32_t, int32_t)
{
  unimplemented();  // Not yet implemented.
}


template <typename URV>
void
Core<URV>::execUret(uint32_t, uint32_t, int32_t)
{
  illegalInst();  // Not yet implemented.
}


template <typename URV>
void
Core<URV>::execWfi(uint32_t, uint32_t, int32_t)
{
  return;   // Currently implemented as a no-op.
}


template <typename URV>
void
Core<URV>::preCsrInstruction(CsrNumber csr)
{
  if (csr == CsrNumber::MINSTRET or csr == CsrNumber::MINSTRETH)
    csRegs_.setRetiredInstCount(retiredInsts_);
  if (csr == CsrNumber::MCYCLE or csr == CsrNumber::MCYCLEH)
    csRegs_.setCycleCount(cycleCount_);
}


template <typename URV>
void
Core<URV>::commitCsrWrite(CsrNumber csr, URV csrVal, unsigned intReg,
			  URV intRegVal)
{
  // Make auto-increment happen before write for minstret and cycle.
  if (csr == CsrNumber::MINSTRET or csr == CsrNumber::MINSTRETH)
    csRegs_.setRetiredInstCount(retiredInsts_ + 1);
  if (csr == CsrNumber::MCYCLE or csr == CsrNumber::MCYCLEH)
    csRegs_.setCycleCount(cycleCount_ + 1);

  // Update CSR and integer register.
  csRegs_.write(csr, privMode_, debugMode_, csrVal);
  intRegs_.write(intReg, intRegVal);

  // Csr was written. If it was minstret, cancel auto-increment done
  // by caller once we return from here.
  if (csr == CsrNumber::MINSTRET or csr == CsrNumber::MINSTRETH)
    retiredInsts_ = csRegs_.getRetiredInstCount() - 1;

  // Same for mcycle.
  if (csr == CsrNumber::MCYCLE or csr == CsrNumber::MCYCLEH)
    cycleCount_ = csRegs_.getCycleCount() - 1;
}


// Set control and status register csr to value of register rs1 and
// save its original value in register rd.
template <typename URV>
void
Core<URV>::execCsrrw(uint32_t rd, uint32_t rs1, int32_t c)
{
  CsrNumber csr = CsrNumber(c);
  preCsrInstruction(csr);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      return;
    }

  URV next = intRegs_.read(rs1);

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrs(uint32_t rd, uint32_t rs1, int32_t c)
{
  CsrNumber csr = CsrNumber(c);
  preCsrInstruction(csr);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
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
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrc(uint32_t rd, uint32_t rs1, int32_t c)
{
  CsrNumber csr = CsrNumber(c);
  preCsrInstruction(csr);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
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
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrwi(uint32_t rd, uint32_t imm, int32_t c)
{
  CsrNumber csr = CsrNumber(c);
  preCsrInstruction(csr);

  URV prev = 0;
  if (rd != 0 and not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
      return;
    }

  if (not csRegs_.isWriteable(csr, privMode_, debugMode_))
    {
      illegalInst();
      return;
    }

  commitCsrWrite(csr, imm, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrsi(uint32_t rd, uint32_t imm, int32_t c)
{
  CsrNumber csr = CsrNumber(c);
  preCsrInstruction(csr);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
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
      return;
    }

  commitCsrWrite(csr, next, rd, prev);
}


template <typename URV>
void
Core<URV>::execCsrrci(uint32_t rd, uint32_t imm, int32_t c)
{
  CsrNumber csr = CsrNumber(c);
  preCsrInstruction(csr);

  URV prev = 0;
  if (not csRegs_.read(csr, privMode_, debugMode_, prev))
    {
      illegalInst();
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
Core<URV>::execLh(uint32_t rd, uint32_t rs1, int32_t imm)
{
  load<int16_t>(rd, rs1, imm);
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
Core<URV>::store(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  URV address = intRegs_.read(rs1) + SRV(imm);
  STORE_TYPE storeVal = intRegs_.read(rs2);

  typedef TriggerTiming Timing;

  bool isLoad = false, hasTrigger = hasActiveTrigger();
  if (hasTrigger)
    {
      bool addrHit = ldStAddrTriggerHit(address, Timing::Before, isLoad);
      bool valueHit = ldStDataTriggerHit(storeVal, Timing::Before, isLoad);
      if (addrHit or valueHit)
	throw CoreException(CoreException::TriggerHit, "", address, 0, true);
    }

  // If we write to special location, end the simulation.
  STORE_TYPE prevVal = 0;  // Memory before write. Useful for restore.
  if (toHostValid_ and address == toHost_ and storeVal != 0)
    {
      memory_.write(address, storeVal, prevVal);
      throw CoreException(CoreException::Stop, "write to to-host",
			  toHost_, storeVal);
    }

  // If we write to special location, then write to console.
  if constexpr (sizeof(STORE_TYPE) == 1)
   {
     if (conIoValid_ and address == conIo_)
       {
	 fputc(storeVal, stdout);
	 return;
       }
   }

  // Misaligned store to io section triggers an exception.
  unsigned alignMask = sizeof(STORE_TYPE) - 1;
  if ((address & alignMask) and not isIdempotentRegion(address))
    {
      initiateException(ExceptionCause::STORE_ADDR_MISAL, currPc_, address);
      return;
    }

  if (memory_.write(address, storeVal, prevVal) and not forceAccessFail_)
    {
      if (maxStoreQueueSize_)
	putInStoreQueue(sizeof(STORE_TYPE), address, prevVal);

      if (hasTrigger)
	{
	  bool addrHit = ldStAddrTriggerHit(address, Timing::After, isLoad);
	  bool valueHit = ldStDataTriggerHit(storeVal, Timing::After, isLoad);
	  if (addrHit or valueHit)
	    throw CoreException(CoreException::TriggerHit, "", address,
				storeVal, false);
	}
    }
  else
    {
      forceAccessFail_ = false;
      retiredInsts_--;
      initiateException(ExceptionCause::STORE_ACC_FAULT, currPc_, address);
    }
}


template <typename URV>
void
Core<URV>::execSb(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  store<uint8_t>(rs1, rs2, imm);
}


template <typename URV>
void
Core<URV>::execSh(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  store<uint16_t>(rs1, rs2, imm);
}


template <typename URV>
void
Core<URV>::execSw(uint32_t rs1, uint32_t rs2, int32_t imm)
{
  store<uint32_t>(rs1, rs2, imm);
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  store<uint64_t>(rs1, rs2, imm);
}


template <typename URV>
void
Core<URV>::execSlliw(uint32_t rd, uint32_t rs1, int32_t amount)
{
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
    {
      illegalInst();
      return;
    }

  int32_t word1 = intRegs_.read(rs1);
  int32_t word2 = intRegs_.read(rs2);

  int32_t word = -1;  // Divide by zero resut
  if (word2 != 0)
    word = word1 / word2;

  SRV value = word;  // sign extend to 64-bits
  intRegs_.write(rd, value);
}


template <typename URV>
void
Core<URV>::execDivuw(uint32_t rd, uint32_t rs1, int32_t rs2)
{
  if (not rv64_)
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
  if (not rv64_)
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
  if (not rv64_)
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


template class WdRiscv::Core<uint32_t>;
template class WdRiscv::Core<uint64_t>;
