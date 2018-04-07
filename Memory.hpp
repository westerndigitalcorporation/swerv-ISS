// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <type_traits>
#include <assert.h>

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
    ~Memory();

    /// Return memory size in bytes.
    size_t size() const
    { return size_; }

    /// Read byte from given address into value. Return true on
    /// success.  Return false if address is out of bounds.
    bool readByte(size_t address, uint8_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (__builtin_expect(not isAttribMapped(attrib) or
			   not isAttribData(attrib), 1))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address >= chunkEnd, 0))
	return false;
      
      value = data_[address];
      return true;
    }

    /// Read half-word (2 bytes) from given address into value. Return
    /// true on success.  Return false if address is out of bounds.
    bool readHalfWord(size_t address, uint16_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (__builtin_expect(not isAttribMapped(attrib) or
			   not isAttribData(attrib), 1))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 1 >= chunkEnd, 0))
	return false;

      value = *(reinterpret_cast<const uint16_t*>(data_ + address));
      return true;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds.
    bool readWord(size_t address, uint32_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (__builtin_expect(not isAttribMapped(attrib) or
			   not isAttribData(attrib), 1))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 3 >= chunkEnd, 0))
	return false;

      value = *(reinterpret_cast<const uint32_t*>(data_ + address));
      return true;
    }

    /// Read a double-word (8 bytes) from given address into
    /// value. Return true on success. Return false if address is out
    /// of bounds.
    bool readDoubleWord(size_t address, uint64_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib) or not isAttribData(attrib))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 7 >= chunkEnd, 0))
	return false;

      value = *(reinterpret_cast<const uint64_t*>(data_ + address));
      return true;
    }

    /// On a unified memory model, this is the same as readHalfWord.
    /// On a split memory model, this will taken an exception if the
    /// target address is not in instruction memory.
    bool readInstHalfWord(size_t address, uint16_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (__builtin_expect(not isAttribMapped(attrib) or
			   not isAttribInst(attrib), 0))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 1 >= chunkEnd, 0))
	return false;

      value = *(reinterpret_cast<const uint16_t*>(data_ + address));
      return true;
    }

    /// On a unified memory model, this is the same as readWord.
    /// On a split memory model, this will taken an exception if the
    /// target address is not in instruction memory.
    bool readInstWord(size_t address, uint32_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (__builtin_expect(not isAttribMapped(attrib) or
			   not isAttribInst(attrib), 0))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 3 >= chunkEnd, 0))
	return false;

      value = *(reinterpret_cast<const uint32_t*>(data_ + address));
      return true;
    }

    /// Write byte to given address. Return true on success. Return
    /// false if address is out of bounds or is not writeable.
    bool writeByte(size_t address, uint8_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib) or not isAttribData(attrib) or
	  not isAttribWrite(attrib))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address >= chunkEnd, 0))
	return false;

      data_[address] = value;
      lastWriteSize_ = 1;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      return true;
    }

    /// Write half-word (2 bytes) to given address. Return true on
    /// success. Return false if address is out of bounds or is not
    /// writeable.
    bool writeHalfWord(size_t address, uint16_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib) or not isAttribData(attrib) or
	  not isAttribWrite(attrib))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 1 >= chunkEnd, 0))
	return false;

      *(reinterpret_cast<uint16_t*>(data_ + address)) = value;
      lastWriteSize_ = 2;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      return true;
    }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds or is
    /// not writeable.
    bool writeWord(size_t address, uint32_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib) or not isAttribData(attrib) or
	  not isAttribWrite(attrib))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 3 >= chunkEnd, 0))
	return false;

      *(reinterpret_cast<uint32_t*>(data_ + address)) = value;
      lastWriteSize_ = 4;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      return true;
    }

    /// Read a double-word (8 bytes) from given address into
    /// value. Return true on success. Return false if address is out
    /// of bounds.
    bool writeDoubleWord(size_t address, uint64_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib) or not isAttribData(attrib) or
	  not isAttribWrite(attrib))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address + 7 >= chunkEnd, 0))
	return false;

      *(reinterpret_cast<uint64_t*>(data_ + address)) = value;
      lastWriteSize_ = 8;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      return true;
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

    /// Write byte to given address. Return true on success. Return
    /// false if address is out of bounds.
    bool writeByteNoAccessCheck(size_t address, uint8_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib))
	return false;

      size_t chunkEnd = getChunkStartAddr(address) + attribSize(attrib);
      if (__builtin_expect(address >= chunkEnd, 0))
	return false;

      data_[address] = value;
      lastWriteSize_ = 1;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      return true;
    }

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

    // Attribute byte of a chunk is encoded as follows:
    // Bits 0 and 1 denote size: 0 -> 32k, 1 -> 64k, 2 -> 128k, 3 -> 256k
    // Bit 2: 1 if chunk is mapped (usable), 0 otherwise.
    // Bit 3: 1 if chunk is writeable, 0 if read only.
    // Bit 4: 1 if chunk contains instructions.
    // Bit 5: 1 if chunk contains data.
    enum AttribMasks { SizeMask = 0x3, MappedMask = 0x4, WriteMask = 0x8,
		       InstMask = 0x10, DataMask = 0x20 };

    bool isAttribMapped(unsigned attrib) const
    { return attrib & MappedMask; }

    size_t attribSize(unsigned attrib) const
    {
      if (not isAttribMapped(attrib))
	return 0;
      unsigned sizeCode = attrib & SizeMask;
      return size_t(32*1024) << sizeCode;
    }

    bool isAttribWrite(unsigned attrib) const
    { return attrib & WriteMask; }
      
    bool isAttribInst(unsigned attrib) const
    { return attrib & InstMask; }

    bool isAttribData(unsigned attrib) const
    { return attrib & DataMask; }

    size_t getAttribIx(size_t addr) const
    { return addr >> chunkShift_; }

    unsigned getAttrib(size_t addr) const
    {
      size_t ix = getAttribIx(addr);
      //if (__builtin_expect(ix < chunkCount_, 1))
	return attribs_[ix];
      return 0; // Unmapped, read-only, not inst, not data.
    }

    size_t getChunkStartAddr(size_t addr) const
    { return (addr >> chunkShift_) << chunkShift_; }

    /// Define instruction closed coupled memory (in core instruction memory).
    bool defineIccm(size_t region, size_t offset, size_t size);

    /// Define data closed coupled memory (in core data memory).
    bool defineDccm(size_t region, size_t offset, size_t size);

  private:

    size_t size_;        // Size of memory in bytes.
    uint8_t* data_;      // Pointer to memory data.

    // Memory is organized in 256kb chunk within 256Mb regions. Each
    // chunk has access attributes.
    uint8_t* attribs_;
    unsigned regionSize_ = 256*1024*1024;
    unsigned chunkCount_ = 16*1024;  // Should be derived from chunk size.
    unsigned chunkSize_  = 256*1024; // Must be a power of 2.
    unsigned chunkShift_ = 18;       // Shift address by this to get chunk index.

    unsigned lastWriteSize_;    // Size of last write.
    size_t lastWriteAddr_;      // Location of most recent write.
    uint64_t lastWriteValue_;   // Value of most recent write.
  };
}
