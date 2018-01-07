// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>

namespace WdRiscv
{

  template <typename URV>
  class Core;

  /// Model physical memory of system.
  class Memory
  {
  public:

    friend class Core<uint32_t>;
    friend class Core<uint64_t>;

    /// Constructor: define a memory of the given size intialized to
    /// zero. Given memory size (byte count) must be a multiple of 4
    /// otherwise, it is truncated to a multiple of 4.
    Memory(size_t size);

    /// Destructor.
    ~Memory()
    { }

    /// Reset the meomry (all contests are cleared) and change its
    /// bounds to the given min and max addresses. Return true on success
    /// and false if bounds are not valid:
    /// 1. beginAddr < endAddr + 4
    /// 2. beginAddr not a multiple of 4
    /// 3. endAddr not a multiple of 4
    /// Valid byte addresses are beginAddr ot endAddr-1 inclusive.
    bool changeBounds(size_t beginAddr, size_t endAddr);

    /// Return memory size in bytes.
    size_t size() const
    { return endAddr_ - beginAddr_ + 1; }

    /// Return smallest valid memory byte address.
    size_t beginAddr() const
    { return beginAddr_; }

    /// Return largest valid memory byte address.
    size_t endAddr() const
    { return endAddr_; }

    /// Read byte from given address into value. Return true on
    /// success.  Return false if address is out of bounds.
    bool readByte(size_t address, uint8_t& value) const
    {
      size_t ix = address - beginAddr_;
      if (ix < endByteIx_)
	{
	  value = mem_.at(ix);
	  return true;
	}
      return false;
    }

    /// Read half-word (2 bytes) from given address into value. Return
    /// true on success.  Return false if address is out of bounds.
    bool readHalfWord(size_t address, uint16_t& value) const
    {
      size_t ix = address - beginAddr_;
      if (ix < endHalfIx_)
	{
	  value = *(reinterpret_cast<const uint16_t*>(mem_.data() + ix));
	  return true;
	}
      return false;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds.
    bool readWord(size_t address, uint32_t& value) const
    {
      size_t ix = address - beginAddr_;
      if (ix < endWordIx_)
	{
	  value = *(reinterpret_cast<const uint32_t*>(mem_.data() + ix));
	  return true;
	}
      return false;
    }

    /// Write byte to given address. Return true on success.  Return
    /// false if address is out of bounds.
    bool writeByte(size_t address, uint8_t value)
    {
      size_t ix = address - beginAddr_;
      if (ix < endByteIx_) {
	mem_.at(ix) = value;
	lastWriteSize_ = 1;
	lastWriteAddr_ = address;
	return true;
      }
      return false;
    }

    /// Write half-word (2 bytes) to given address. Return true on
    /// success. Return false if address is out of bounds.
    bool writeHalfWord(size_t address, uint16_t value)
    {
      size_t ix = address - beginAddr_;
      if (ix < endHalfIx_) {
	*(reinterpret_cast<uint16_t*>(mem_.data() + ix)) = value;
	lastWriteSize_ = 2;
	lastWriteAddr_ = address;
	return true;
      }
      return false;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds.
    bool writeWord(size_t address, uint32_t value)
    {
      size_t ix = address - beginAddr_;
      if (ix < endWordIx_) {
	lastWriteSize_ = 4;
	lastWriteAddr_ = address;
	*(reinterpret_cast<uint32_t*>(mem_.data() + ix)) = value;
	return true;
      }
      return false;
    }

    /// Load the given hex file and set memory locations accordingly.
    /// Return true on success. Return false if file does not exists,
    /// cannot be opened or contains malformed data.
    /// File format: A line either contains @address where address
    /// is a hexadecimal memory address or one or more space separated
    /// tokens each consisting of two hexadecimal digits.
    bool loadHexFile(const std::string& file);

    /// Load the given ELF file and set memory locations accordingly.
    /// Return true on success. Return false if file does not exists,
    /// cannot be opened or contains malformed data. If successful,
    /// set entryPoint to the entry point of the loaded file.
    bool loadElfFile(const std::string& file, size_t& entryPoint,
		     size_t& exitPoint);

    /// Reurn the min and max addresses corresponding to the segments
    /// in the given ELF file. Return true on success and false if
    /// the ELF file does not exist or cannot be read (in which
    /// case min and max address are left unmodified).
    static bool getElfFileAddressBounds(const std::string& file,
					size_t& minAddr, size_t& maxAddr);

    /// Change the memory size to the given size. Fill new space (if 
    /// new size is larger than old) with given value.
    void resize(size_t newSize, uint8_t value = 0);

    /// Copy data from the given memory into this memory. If the two
    /// memories have different sizes then cop data from location zero
    /// up to n-1 where n is the minimum of the sizes.
    void copy(const Memory& other);

  protected:

    /// Set addr to the address of the last write and return the size
    /// of that write. Return 0 if no write since the most recent
    /// clearLastWriteInfo.
    unsigned getLastWriteInfo(size_t& addr)
    {
      if (lastWriteSize_)
	addr = lastWriteAddr_;
      return lastWriteSize_;
    }

    /// Clear the information associated with last write.
    void clearLastWriteInfo()
    { lastWriteSize_ = 0; }

  private:

    std::vector<uint8_t> mem_;
    unsigned lastWriteSize_;    // Size of last write.
    size_t lastWriteAddr_;      // Location of most recent write.

    size_t beginAddr_;  // Smallest valid memory address.
    size_t endAddr_;    // One plus largest valid memory address.
    size_t endByteIx_;  // One plus the larget byte index.
    size_t endHalfIx_;  // One plus the larest halfword index.
    size_t endWordIx_;  // One plus the larest word index.
  };
}
