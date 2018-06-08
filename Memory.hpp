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
    /// otherwise, it is truncated to a multiple of 4. The memory
    /// is partitioned into regions according ot the region size which
    /// must be a power of 2.
    Memory(size_t size, size_t regionSize = 256*1024*1024);

    /// Destructor.
    ~Memory();

    /// Return memory size in bytes.
    size_t size() const
    { return size_; }

    /// Read an unsigned integer value of type T from memory at the
    /// given address into value. Return true on sucess. Return false
    /// if any of the requested bytes is out of memory bounds or fall
    /// in unmapped memory or if the read corsses memory regions of
    /// different attributes.
    template <typename T>
    bool read(size_t address, T& value) const
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMappedData(attrib))
	return false;

      size_t sectionEnd = getSectionStartAddr(address) + sectionSize_;
      if (address + sizeof(T) > sectionEnd)
	{
	  // Read crosses section boundary: Check next section.
	  unsigned attrib2 = getAttrib(address + sizeof(T));
	  if (not isAttribMappedData(attrib2))
	    return false;
	  if (isAttribDccm(attrib) != isAttribDccm(attrib2))
	    return false;  // Cannot cross a DCCM boundary.
	}

      // Memory mapped region accessible only with read-word.
      if constexpr (sizeof(T) == 4)
        {
	  if (isAttribRegister(attrib))
	    return readRegister(address, value);
	}
      else if (isAttribRegister(attrib))
	return false;

      value = *(reinterpret_cast<const T*>(data_ + address));
      return true;
    }

    /// Read byte from given address into value. Return true on
    /// success.  Return false if address is out of bounds.
    bool readByte(size_t address, uint8_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMappedData(attrib))
	return false;

      if (isAttribRegister(attrib))
	return false; // Only word access allowed to memory mapped regs.

      value = data_[address];
      return true;
    }

    /// Read half-word (2 bytes) from given address into value. See
    /// read method.
    bool readHalfWord(size_t address, uint16_t& value) const
    { return read(address, value); }

    /// Read word (4 bytes) from given address into value. See read
    /// method.
    bool readWord(size_t address, uint32_t& value) const
    { return read(address, value); }

    /// Read a double-word (8 bytes) from given address into
    /// value. See read method.
    bool readDoubleWord(size_t address, uint64_t& value) const
    { return read(address, value); }

    /// On a unified memory model, this is the same as readHalfWord.
    /// On a split memory model, this will taken an exception if the
    /// target address is not in instruction memory.
    bool readInstHalfWord(size_t address, uint16_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (isAttribMappedInst(attrib))
	{
	  size_t sectionEnd = getSectionStartAddr(address) + sectionSize_;
	  if (address + 1 >= sectionEnd)
	    {
	      // Instruction crosses section boundary: Check next section.
	      unsigned attrib2 = getAttrib(address + 1);
	      if (not isAttribMappedInst(attrib2))
		return false;
	      if (isAttribIccm(attrib) != isAttribIccm(attrib2))
		return false;  // Cannot cross an ICCM boundary.
	    }
	  value = *(reinterpret_cast<const uint16_t*>(data_ + address));
	  return true;
	}
      return false;
    }

    /// On a unified memory model, this is the same as readWord.
    /// On a split memory model, this will taken an exception if the
    /// target address is not in instruction memory.
    bool readInstWord(size_t address, uint32_t& value) const
    {
      unsigned attrib = getAttrib(address);
      if (isAttribMappedInst(attrib))
	{
	  size_t sectionEnd = getSectionStartAddr(address) + sectionSize_;
	  if (address + 3 >= sectionEnd)
	    {
	      // Instruction crosses section boundary: Check next section.
	      unsigned attrib2 = getAttrib(address + 3);
	      if (not isAttribMappedInst(attrib2))
		return false;
	      if (isAttribIccm(attrib) != isAttribIccm(attrib2))
		return false;  // Cannot cross a ICCM boundary.
	    }

	  value = *(reinterpret_cast<const uint32_t*>(data_ + address));
	  return true;
	}
	return false;
    }

    /// Write given unsigned integer value of type T into memory
    /// starting at the given address. Return true on success. Return
    /// false if any of the target memory bytes are out of bounds or
    /// fall in inaccessible regions or if the write corsses memory
    /// region of different attributes.
    template <typename T>
    bool write(size_t address, T value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMappedDataWrite(attrib))
	return false;

      size_t sectionEnd = getSectionStartAddr(address) + sectionSize_;
      if (address + sizeof(T) > sectionEnd)
	{
	  // Write crosses section boundary: Check next section.
	  unsigned attrib2 = getAttrib(address + sizeof(T));
	  if (not isAttribMappedDataWrite(attrib2))
	    return false;
	  if (isAttribDccm(attrib) != isAttribDccm(attrib2))
	    return false;  // Cannot cross a DCCM boundary.
	}

      // Memory mapped region accessible only with write-word.
      if constexpr (sizeof(T) == 4)
        {
	  if (isAttribRegister(attrib))
	    return writeRegister(address, value);
	}
      else if (isAttribRegister(attrib))
	return false;

      *(reinterpret_cast<T*>(data_ + address)) = value;
      lastWriteSize_ = sizeof(T);
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      lastWriteIsDccm_ = isAttribDccm(attrib);
      return true;
    }

    /// Write byte to given address. Return true on success. Return
    /// false if address is out of bounds or is not writeable.
    bool writeByte(size_t address, uint8_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMappedDataWrite(attrib))
	return false;

      if (isAttribRegister(attrib))
	return false;  // Only word access allowed to memory mapped regs.

      data_[address] = value;
      lastWriteSize_ = 1;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      lastWriteIsDccm_ = isAttribDccm(attrib);
      return true;
    }

    /// Write half-word (2 bytes) to given address. Return true on
    /// success. Return false if address is out of bounds or is not
    /// writeable.
    bool writeHalfWord(size_t address, uint16_t value)
    { return write(address, value); }

    /// Read word (4 bytes) from given address into value. Return true
    /// on success.  Return false if address is out of bounds or is
    /// not writeable.
    bool writeWord(size_t address, uint32_t value)
    { return write(address, value); }

    /// Read a double-word (8 bytes) from given address into
    /// value. Return true on success. Return false if address is out
    /// of bounds.
    bool writeDoubleWord(size_t address, uint64_t value)
    { return write(address, value); }

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

    /// Same as write but effects not recorded in last-write info.
    template <typename T>
    bool poke(size_t address, T value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMappedDataWrite(attrib))
	return false;

      size_t sectionEnd = getSectionStartAddr(address) + sectionSize_;
      if (address + sizeof(T) > sectionEnd)
	{
	  // Write crosses section boundary: Check next section.
	  unsigned attrib2 = getAttrib(address + sizeof(T));
	  if (not isAttribMappedDataWrite(attrib2))
	    return false;
	  if (isAttribDccm(attrib) != isAttribDccm(attrib2))
	    return false;  // Cannot cross a DCCM boundary.
	}

      // Memory mapped region accessible only with write-word.
      if constexpr (sizeof(T) == 4)
        {
	  if (isAttribRegister(attrib))
	    return writeRegister(address, value);
	}
      else if (isAttribRegister(attrib))
	return false;

      *(reinterpret_cast<T*>(data_ + address)) = value;
      return true;
    }

    /// Same as writeByte but effects are not record in last-write info.
    bool pokeByte(size_t address, uint8_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMappedDataWrite(attrib))
	return false;

      if (isAttribRegister(attrib))
	return false;  // Only word access allowed to memory mapped regs.

      data_[address] = value;
      return true;
    }

    /// Write byte to given address. Return true on success. Return
    /// false if address is not mapped.
    bool writeByteNoAccessCheck(size_t address, uint8_t value)
    {
      unsigned attrib = getAttrib(address);
      if (not isAttribMapped(attrib))
	return false;

      data_[address] = value;
      lastWriteSize_ = 1;
      lastWriteAddr_ = address;
      lastWriteValue_ = value;
      lastWriteIsDccm_ = isAttribDccm(attrib);
      return true;
    }

    /// Poke a word without masking. This is used so that external
    /// agents can modify memory-mapped register bits that are
    /// read-only to this core (e.g. interrupt pending bits).
    bool pokeWordNoMask(size_t addr, uint32_t value)
    {
      unsigned attrib = getAttrib(addr);
      if (not isAttribMappedDataWrite(attrib))
	return false;
      if ((addr & 3) != 0)
	return false; // Address must be word aligned.
      *(reinterpret_cast<uint32_t*>(data_ + addr)) = value;
      return true;
    }

    /// Set addr to the address of the last write and value to the
    /// corresponding value and return the size of that write. Return
    /// 0 if no write since the most recent clearLastWriteInfo in
    /// which case addr and value are not modified.
    unsigned getLastWriteInfo(size_t& addr, uint64_t& value) const
    {
      if (lastWriteSize_)
	{
	  addr = lastWriteAddr_;
	  value = lastWriteValue_;
	}
      return lastWriteSize_;
    }

    /// Clear the information associated with last write.
    void clearLastWriteInfo()
    { lastWriteSize_ = 0; }

    /// Return true if last write was to closed coupled memory.
    bool isLastWriteToDccm() const
    { return lastWriteIsDccm_; }

    // Attribute byte of a section is encoded as follows:
    // Bits 0 and 1 denote size: 0 -> 32k, 1 -> 64k, 2 -> 128k, 3 -> 256k
    // Bit 2: 1 if section is mapped (usable), 0 otherwise.
    // Bit 3: 1 if section is writeable, 0 if read only.
    // Bit 4: 1 if section contains instructions.
    // Bit 5: 1 if section contains data.
    // Bit 6: 1 if section is for memory-mapped registers
    // Bit 7: 1 if section is pristine (this is used to check for if
    //             a section is mapped multiple times)
    // Bit 8: 1 if iccm
    // Bit 9: 1 if dccm
    enum AttribMasks { SizeMask = 0x3, MappedMask = 0x4, WriteMask = 0x8,
		       InstMask = 0x10, DataMask = 0x20, RegisterMask = 0x40,
		       PristineMask = 0x80, IccmMask = 0x100, DccmMask = 0x200,
		       MappedDataMask = MappedMask | DataMask,
		       MappedDataWriteMask = MappedMask | DataMask | WriteMask,
		       MappedInstMask = MappedMask | InstMask };

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

    bool isAttribIccm(unsigned attrib) const
    { return attrib & IccmMask; }

    bool isAttribDccm(unsigned attrib) const
    { return attrib & DccmMask; }

    bool isAttribRegister(unsigned attrib) const
    { return attrib & RegisterMask; }

    size_t getAttribIx(size_t addr) const
    { return addr >> sectionShift_; }

    /// Return true if attribute is that of a mapped data region.
    bool isAttribMappedData(unsigned attrib) const
    { return (attrib & MappedDataMask) == MappedDataMask; }

    /// Return true if attribute is that of a mapped writeable data region.
    bool isAttribMappedDataWrite(unsigned attrib) const
    { return (attrib & MappedDataWriteMask) == MappedDataWriteMask; }

    /// Return true if attribute is that of a mapped instruction region.
    bool isAttribMappedInst(unsigned attrib) const
    { return (attrib & MappedInstMask) == MappedInstMask; }

    /// Return the attribute of the section containing given address.
    unsigned getAttrib(size_t addr) const
    {
      size_t ix = getAttribIx(addr);
      //if (ix < sectionCount_)
	return attribs_[ix];
      return 0; // Unmapped, read-only, not inst, not data.
    }

    /// Retun index of memory section (typically section size is 32k)
    /// containing given address.
    size_t getSectionStartAddr(size_t addr) const
    { return (addr >> sectionShift_) << sectionShift_; }

    /// Define instruction closed coupled memory (in core instruction memory).
    bool defineIccm(size_t region, size_t offset, size_t size);

    /// Define data closed coupled memory (in core data memory).
    bool defineDccm(size_t region, size_t offset, size_t size);

    /// Define region for memory mapped registers. Return true on
    /// success and flase if offset or size are not properly aligned
    /// or sized.
    bool defineMemoryMappedRegisterRegion(size_t region, size_t size,
					  size_t regionOffset);

    /// Define write mask for a memory-mapped register with given
    /// index and register-offset within the given region and region-offset.
    /// Address of memory associated with register is:
    ///   region*256M + regionOffset + registerBlockOffset + registerIx*4.
    /// Return true on success and false if the region (index) is not
    /// valid or if the region is not mapped of if the region was not
    /// defined for memory mapped registers or if the register address
    /// is out of bounds.
    bool defineMemoryMappedRegisterWriteMask(size_t region,
					     size_t regionOffset,
					     size_t registerBlockOffset,
					     size_t registerIx,
					     uint32_t mask);

    /// Read a memory mapped register.
    bool readRegister(size_t addr, uint32_t& value) const
    {
      if ((addr & 3) != 0)
	return false;  // Address must be workd-aligned.
      value = *(reinterpret_cast<const uint32_t*>(data_ + addr));
      return true;
    }

    /// Write a memory mapped register.
    bool writeRegister(size_t addr, uint32_t value)
    {
      if ((addr & 3) != 0)
	return false;  // Address must be workd-aligned.

      if (not masks_.empty())
	{
	  unsigned sectionIx = getAttribIx(addr);
	  auto& sectionMasks = masks_.at(sectionIx);
	  if (not sectionMasks.empty())
	    {
	      size_t ix = (addr - getSectionStartAddr(addr)) / 4;
	      uint32_t mask = sectionMasks.at(ix);
	      value = value & mask;
	    }
	}

      unsigned attrib = getAttrib(addr);

      *(reinterpret_cast<uint32_t*>(data_ + addr)) = value;
      lastWriteSize_ = 4;
      lastWriteAddr_ = addr;
      lastWriteValue_ = value;
      lastWriteIsDccm_ = isAttribDccm(attrib);
      return true;
    }

  private:

    size_t size_;        // Size of memory in bytes.
    uint8_t* data_;      // Pointer to memory data.

    // Memory is organized in regions (e.g. 256 Mb). Each region is
    // orgnized in sections (e.g 32kb). Each section is associated
    // with access attributes. Memory mapped register sections are
    // also associated with write-masks (one 4-byte mask per word).
    unsigned regionCount_    = 16;
    unsigned regionSize_     = 256*1024*1024;
    std::vector<bool> regionConfigured_; // One per region.

    unsigned sectionCount_   = 128*1024; // Should be derived from section size.
    unsigned sectionSize_    = 32*1024;  // Must be a power of 2.
    unsigned sectionShift_   = 15;       // Shift address by this to get section index.

    // Attributes are assigned to sections.
    std::vector<uint16_t> attribs_;      // One attrib per section.
    std::vector<std::vector<uint32_t> > masks_;  // One vector per section.

    unsigned lastWriteSize_ = 0;    // Size of last write.
    size_t lastWriteAddr_ = 0;      // Location of most recent write.
    uint64_t lastWriteValue_ = 0;   // Value of most recent write.
    bool lastWriteIsDccm_ = false;  // Last write was to the DCCM region.
  };
}
