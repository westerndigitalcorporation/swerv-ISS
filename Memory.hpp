// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>

namespace WdRiscv
{

  /// Model physical memory of system.
  class Memory
  {
  public:

    /// Constructor: define a memory of the given size intialized to
    /// zero.
    Memory(size_t byteCount)
      : mem_(byteCount, 0)
    { }

    /// Destructor.
    ~Memory()
    { }

    /// Return memory size in bytes.
    size_t size() const
    {
      return mem_.size();
    }

    /// Read byte from given address into value. Return true on
    /// success.  Return false if address is out of bounds.
    bool readByte(size_t address, uint8_t& value) const
    {
      if (address < mem_.size()) {
	value = mem_[address];
	return true;
      }
      return false;
    }

    /// Write byte to given address. Return true on success.  Return
    /// false if address is out of bounds.
    bool writeByte(size_t address, uint8_t value)
    {
      if (address < mem_.size()) {
	mem_[address] = value;
	return true;
      }
      return false;
    }

    /// Read half-word (2 bytes) from given address into value. Return
    /// true on success.  Return false if address is out of bounds.
    bool readHalfWord(size_t address, uint16_t& value) const
    {
      if (address + 1 < mem_.size()) {
	value = *(reinterpret_cast<const uint16_t*>(mem_.data() + address));
	return true;
      }
      return false;
    }

    /// Write half-word (2 bytes) to given address. Return true on
    /// success. Return false if address is out of bounds.
    bool writeHalfWord(size_t address, uint16_t value)
    {
      if (address + 1 < mem_.size()) {
	*(reinterpret_cast<uint16_t*>(mem_.data() + address)) = value;
	return true;
      }
      return false;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds.
    bool readWord(size_t address, uint32_t& value) const
    {
      if (address + 3 < mem_.size()) {
	value = *(reinterpret_cast<const uint32_t*>(mem_.data() + address));
	return true;
      }
      return false;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds.
    bool writeWord(size_t address, uint32_t value)
    {
      if (address + 1 < mem_.size()) {
	*(reinterpret_cast<uint32_t*>(mem_.data() + address)) = value;
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

    private:

    std::vector<uint8_t> mem_;
  };
}
