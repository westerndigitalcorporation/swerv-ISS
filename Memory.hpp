// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
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

    /// Read a double-word (8 bytes) from given address into
    /// value. Return true on success. Return false if address is out
    /// of bounds.
    bool readDoubleWord(size_t address, uint64_t& value) const
    {
      if (address < endWordAddr_)
	{
	  value = *(reinterpret_cast<const uint64_t*>(data_ + address));
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
	  lastWriteValue_ = value;
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
	  lastWriteValue_ = value;
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
	  *(reinterpret_cast<uint32_t*>(data_ + address)) = value;
	  lastWriteSize_ = 4;
	  lastWriteAddr_ = address;
	  lastWriteValue_ = value;
	  return true;
	}
      return false;
    }

    /// Read a double-word (8 bytes) from given address into
    /// value. Return true on success. Return false if address is out
    /// of bounds.
    bool writeDoubleWord(size_t address, uint64_t value)
    {
      if (address < endDoubleAddr_)
	{
	  *(reinterpret_cast<uint64_t*>(data_ + address)) = value;
	  lastWriteSize_ = 8;
	  lastWriteAddr_ = address;
	  lastWriteValue_ = value;
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
    /// set entryPoint to the entry point of the loaded file and
    /// exitPoint to the value of the _finish symbol or to the end
    /// address of the last loaded ELF file segment if the _finish
    /// symbol is not found. Extract symbol names and corresponding
    /// values into the symbols map.
    bool loadElfFile(const std::string& file, size_t& entryPoint,
		     size_t& exitPoint,
		     std::unordered_map<std::string, size_t>& symbols);

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

    /// Set addr to the address of the last write and value to the
    /// corresponding value and return the size of that write. Return
    /// 0 if no write since the most recent clearLastWriteInfo in
    /// which case addr and value are not modified.
    unsigned getLastWriteInfo(size_t& addr, uint64_t& value) const
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
    uint64_t lastWriteValue_;   // Value of most recent write.

    size_t endHalfAddr_;   // One plus the largest half-word address.
    size_t endWordAddr_;   // One plus the largest word address.
    size_t endDoubleAddr_; // One plus the largest double-word address.
  };
}
