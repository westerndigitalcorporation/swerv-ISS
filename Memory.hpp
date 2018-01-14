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

    /// Return memory size in bytes.
    size_t size() const
    { return size_; }

    /// Read byte from given address into value. Return true on
    /// success.  Return false if address is out of bounds.
    bool readByte(size_t address, uint8_t& value) const
    {
      if (address < size_)
	{
	  value = data_[address];
	  return true;
	}
      return false;
    }

    /// Read half-word (2 bytes) from given address into value. Return
    /// true on success.  Return false if address is out of bounds.
    bool readHalfWord(size_t address, uint16_t& value) const
    {
      if (address < endHalfAddr_)
	{
	  value = *(reinterpret_cast<const uint16_t*>(data_ + address));
	  return true;
	}
      return false;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds.
    bool readWord(size_t address, uint32_t& value) const
    {
      if (address < endWordAddr_)
	{
	  value = *(reinterpret_cast<const uint32_t*>(data_ + address));
	  return true;
	}
      return false;
    }

    /// Write byte to given address. Return true on success.  Return
    /// false if address is out of bounds.
    bool writeByte(size_t address, uint8_t value)
    {
      if (address < size_)
	{
	  data_[address] = value;
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
      if (address < endHalfAddr_)
	{
	  *(reinterpret_cast<uint16_t*>(data_ + address)) = value;
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
      if (address < endWordAddr_)
	{
	  lastWriteSize_ = 4;
	  lastWriteAddr_ = address;
	  *(reinterpret_cast<uint32_t*>(data_ + address)) = value;
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
    /// set entryPoint to the entry point of the loaded file.  If
    /// successful and file contains the symbol tohost, then set
    /// toHost to the corresponding value and set toHostValid to true.
    bool loadElfFile(const std::string& file, size_t& entryPoint,
		     size_t& exitPoint, size_t& toHost,
		     bool& toHostValid);

    /// Reurn the min and max addresses corresponding to the segments
    /// in the given ELF file. Return true on success and false if
    /// the ELF file does not exist or cannot be read (in which
    /// case min and max address are left unmodified).
    static bool getElfFileAddressBounds(const std::string& file,
					size_t& minAddr, size_t& maxAddr);

    /// Copy data from the given memory into this memory. If the two
    /// memories have different sizes then copy data from location
    /// zero up to n-1 where n is the minimum of the sizes.
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

    size_t size_;        // Size of memory in bytes.
    uint8_t* data_;      // Pointer to memory data.

    unsigned lastWriteSize_;    // Size of last write.
    size_t lastWriteAddr_;      // Location of most recent write.

    size_t endHalfAddr_;  // One plus the larest halfword address.
    size_t endWordAddr_;  // One plus the larest word address.
  };
}
