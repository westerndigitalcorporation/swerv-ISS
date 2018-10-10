#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <math.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <elfio/elfio.hpp>
#include "Memory.hpp"

using namespace WdRiscv;


Memory::Memory(size_t size, size_t regionSize)
  : size_(size), data_(nullptr)
{ 
  if ((size & 4) != 0)
    {
      size_ = (size >> 2) << 2;
      std::cerr << "Memory size (" << size << ") is not a multiple of 4. Using "
		<< size_ << '\n';
    }

  size_t logPageSize = std::log2(pageSize_);
  size_t p2PageSize = size_t(1) << logPageSize;
  if (p2PageSize != pageSize_)
    {
      std::cerr << "Memory page size (0x" << std::hex << pageSize_ << ") "
		<< "is not a power of 2 -- using 0x" << p2PageSize << '\n';
      pageSize_ = p2PageSize;
    }
  pageShift_ = logPageSize;

  if (size_ < pageSize_)
    {
      std::cerr << "Unreasonably small memory size (less than 0x "
		<< std::hex << pageSize_ << ") -- using 0x" << pageSize_
		<< '\n';
      size_ = pageSize_;
    }

  pageCount_ = size_ / pageSize_;
  if (size_t(pageCount_) * pageSize_ != size_)
    {
      pageCount_++;
      size_t newSize = pageCount_ * pageSize_;
      std::cerr << "Memory size (0x" << std::hex << size_ << ") is not a "
		<< "multiple of page size (0x" << pageSize_ << ") -- "
		<< "using 0x" << newSize << '\n';

      size_ = newSize;
    }

  unsigned logRegionSize = std::log2(regionSize);
  size_t p2RegionSize = size_t(1) << logRegionSize;
  if (p2RegionSize != regionSize)
    {
      std::cerr << "Memory region size (0x" << std::hex << regionSize << ") "
		<< "is not a power of 2 -- using 0x" << p2RegionSize << '\n';
      regionSize = p2RegionSize;
    }

  regionSize_ = regionSize;
  if (regionSize_ < pageSize_)
    {
      std::cerr << "Memory region size (0x" << std::hex << regionSize_ << ") "
		<< "smaller than page size (0x" << pageSize_ << ") -- "
		<< "using page size\n";
      regionSize_ = pageSize_;
    }

  size_t pagesInRegion = regionSize_ / pageSize_;
  size_t multiple = size_t(pagesInRegion) * pageSize_;
  if (multiple != regionSize_)
    {
      std::cerr << "Memory region size (0x" << std::hex << regionSize_ << ") "
		<< "is not a multiple of page size (0x" << pageSize_ << ") -- "
		<< "using " << multiple << " as region size\n";
      regionSize_ = multiple;
    }

  regionCount_ = size_ / regionSize_;
  if (size_t(regionCount_) * size_t(regionSize_) < size_)
    regionCount_++;

  void* mem = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (not mem)
    {
      std::cerr << "Failed to map " << size_ << " bytes using mmap.\n";
      throw std::runtime_error("Out of memory");
    }

  data_ = reinterpret_cast<uint8_t*>(mem);

  // Mark all regions as non-configured.
  regionConfigured_.resize(regionCount_);

  attribs_.resize(pageCount_);

  // Make whole memory as mapped, writeable, allowing data and inst.
  // Some of the sections will be later reconfigured when the user
  // supplied configuration file is processed.
  for (size_t i = 0; i < pageCount_; ++i)
    {
      attribs_.at(i).setAll(true);
      attribs_.at(i).setIccm(false);
      attribs_.at(i).setDccm(false);
      attribs_.at(i).setMemMappedReg(false);
    }
}


Memory::~Memory()
{
  if (data_)
    {
      munmap(data_, size_);
      data_ = nullptr;
    }
}


bool
Memory::loadHexFile(const std::string& fileName)
{
  std::ifstream input(fileName);

  if (not input.good())
    {
      std::cerr << "Failed to open hex-file '" << fileName << "' for input\n";
      return false;
    }

  size_t address = 0, errors = 0;

  std::string line;

  for (unsigned lineNum = 0; std::getline(input, line); ++lineNum)
    {
      if (line.empty())
	continue;

      if (line[0] == '@')
	{
	  if (line.size() == 1)
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid hexadecimal address: " << line << '\n';
	      errors++;
	      continue;
	    }
	  char* end = nullptr;
	  address = std::strtoull(line.c_str() + 1, &end, 16);
	  if (end and *end and *end != ' ')
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid hexadecimal address: " << line << '\n';
	      errors++;
	    }
	  continue;
	}

      std::istringstream iss(line);
      uint32_t value;
      while (iss)
	{
	  iss >> std::hex >> value;
	  if (iss.fail())
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid data: " << line << '\n';
	      errors++;
	      break;
	    }
	  if (value > 0xff)
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Invalid value: " << std::hex << value << '\n';
	      errors++;
	    }
	  if (address < size_)
	    {
	      if (not errors)
		data_[address++] = value;
	    }
	  else
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Address out of bounds: " << std::hex << address
			<< '\n';
	      errors++;
	      break;
	    }
	  if (iss.eof())
	    break;
	}

      if (iss.bad())
	{
	  std::cerr << "File " << fileName << ", Line " << lineNum << ": "
		    << "Failed to parse data line: " << line << '\n';
	  errors++;
	}
    }

  return errors == 0;
}


bool
Memory::loadElfFile(const std::string& fileName, size_t& entryPoint,
		    size_t& exitPoint,
		    std::unordered_map<std::string, size_t>& symbols)
{
  entryPoint = 0;

  ELFIO::elfio reader;

  if (not reader.load(fileName))
    {
      std::cerr << "Failed to load ELF file " << fileName << '\n';
      return false;
    }

  if (reader.get_class() != ELFCLASS32 and reader.get_class() != ELFCLASS64)
    {
      std::cerr << "Ony 32/64-bit ELFs are currently supported\n";
      return false;
    }

  if (reader.get_encoding() != ELFDATA2LSB)
    {
      std::cerr << "Only little-endian ELF is currently supported\n";
      return false;
    }

  if (reader.get_machine() != EM_RISCV)
    {
      std::cerr << "Warning: non-riscv ELF file\n";
    }

  auto secCount = reader.sections.size();

  // Copy loadable ELF segments into memory.
  size_t maxEnd = 0;  // Largest end address of a segment.
  unsigned errors = 0;

  unsigned loadedSegs = 0;
  for (int segIx = 0; segIx < reader.segments.size(); ++segIx)
    {
      const ELFIO::segment* seg = reader.segments[segIx];
      ELFIO::Elf64_Addr vaddr = seg->get_virtual_address();
      ELFIO::Elf_Xword segSize = seg->get_file_size(); // Size in file.
      const char* segData = seg->get_data();
      if (seg->get_type() == PT_LOAD)
	{
	  if (vaddr + segSize > size_)
	    {
	      std::cerr << "End of ELF segment " << segIx << " ("
			<< (vaddr+segSize)
			<< ") is beyond end of simulated meomry ("
			<< size_ << ")\n";
	      errors++;
	    }
	  else
	    {
	      for (size_t i = 0; i < segSize; ++i)
		if (not writeByteNoAccessCheck(vaddr + i, segData[i]))
		  {
		    std::cerr << "Failed to copy ELF byte at address 0x"
			      << std::hex << (vaddr + i) << '\n';
		    errors++;
		    break;
		  }
	      loadedSegs++;
	      maxEnd = std::max(maxEnd, size_t(vaddr) + size_t(segSize));
	    }
	}
    }
  if (loadedSegs == 0)
    {
      std::cerr << "No loadable segment in ELF file\n";
      errors++;
    }

  clearLastWriteInfo();

  // Collect symbols.
  for (int secIx = 0; secIx < secCount; ++secIx)
    {
      auto sec = reader.sections[secIx];
      if (sec->get_type() != SHT_SYMTAB)
	continue;

      const ELFIO::symbol_section_accessor symAccesor(reader, sec);
      ELFIO::Elf64_Addr address = 0;
      ELFIO::Elf_Xword size = 0;
      unsigned char bind, type, other;
      ELFIO::Elf_Half index = 0;

      // Finding symbol by name does not work. Walk all the symbols.
      ELFIO::Elf_Xword symCount = symAccesor.get_symbols_num();
      for (ELFIO::Elf_Xword symIx = 0; symIx < symCount; ++symIx)
	{
	  std::string name;
	  if (symAccesor.get_symbol(symIx, name, address, size, bind, type,
				    index, other))
	    symbols[name] = address;
	}
    }

  // Get the program entry point.
  if (not errors)
    {
      entryPoint = reader.get_entry();
      exitPoint = maxEnd;
      if (symbols.count("_finish"))
	exitPoint = symbols.at("_finish");
    }

  return errors == 0;
}


bool
Memory::getElfFileAddressBounds(const std::string& fileName, size_t& minAddr,
				size_t& maxAddr)

{
  ELFIO::elfio reader;

  if (not reader.load(fileName))
    {
      std::cerr << "Failed to load ELF file " << fileName << '\n';
      return false;
    }

  // Get min max bounds of the segments.
  size_t minBound = ~ size_t(0);
  size_t maxBound = 0;
  unsigned validSegs = 0;
  for (int segIx = 0; segIx < reader.segments.size(); ++segIx)
    {
      const ELFIO::segment* seg = reader.segments[segIx];
      if (seg->get_type() != PT_LOAD)
	continue;

      ELFIO::Elf64_Addr vaddr = seg->get_virtual_address();
      ELFIO::Elf_Xword size = seg->get_file_size(); // Size in file.

      minBound = std::min(minBound, size_t(vaddr));
      maxBound = std::max(maxBound, size_t(vaddr + size));
      validSegs++;
    }

  if (validSegs == 0)
    {
      std::cerr << "No loadable segment in ELF file\n";
      return false;
    }

  minAddr = minBound;
  maxAddr = maxBound;
  return true;
}


void
Memory::copy(const Memory& other)
{
  size_t n = std::min(size_, other.size_);
  memcpy(data_, other.data_, n);
}


static
bool
checkCcmConfig(const std::string& tag, size_t region, size_t offset,
	       size_t size, size_t regionCount)
{
  if (region >= regionCount)
    {
      std::cerr << "Invalid " << tag << " region (" << region << "). Expecting "
		<< "number betwen 0 and " << (regionCount - 1) << "\n";
      return false;
    }

  // TBD: pass page size and limits

  size_t pageSize = 4*1024;

  if (size < pageSize or size > 1024*pageSize)
    {
      std::cerr << "Invalid " << tag << " size (" << size << "). Expecting a\n"
		<< "  multiple of page size (" << pageSize << ") between\n"
		<< "  " << pageSize << " and " << (1024*pageSize) << '\n';
      return false;
    }

  if ((offset & 0x3ffff) != 0)  // Must be a multiple of 256k
    {
      std::cerr << "Invalid " << tag << " offset (" << offset;
      return false;
    }

  return true;
}
    

bool
Memory::defineIccm(size_t region, size_t offset, size_t size)
{
  if (not checkCcmConfig("ICCM", region, offset, size, regionCount_))
    return false;

  // If a region is ever configured, then only the configured parts
  // are available (accessible).
  if (not regionConfigured_.at(region))
    {
      // Region never configured. Make it all inacessible and mark it pristine.
      regionConfigured_.at(region) = true;
      size_t ix0 = getPageIx(size_t(regionSize_)*size_t(region));
      size_t ix1 = ix0 + getPageIx(regionSize_);
      for (size_t ix = ix0; ix < ix1; ++ix)
	{
	  auto& attrib = attribs_.at(ix);
	  attrib.setAll(false);
	  attrib.setPristine(true);
	}
    }

  size_t addr = region * regionSize_ + offset;
  size_t ix = getPageIx(addr);
  if (not attribs_.at(ix).isPristine())
    {
      std::cerr << "Region 0x" << std::hex << region << " offset 0x"
		<< std::hex << offset << " already mapped\n";
    }

  // Set attributes of sections in iccm
  size_t count = size/pageSize_;  // Count of pages in iccm
  for (size_t i = 0; i < count; ++i)
    {
      auto& attrib = attribs_.at(ix + i);
      attrib.sectionSize_ = count;
      attrib.setMapped(true);
      attrib.setInst(true);
      attrib.setIccm(true);
      attrib.setPristine(false);
    }
  return true;
}


bool
Memory::defineDccm(size_t region, size_t offset, size_t size)
{
  if (not checkCcmConfig("DCCM", region, offset, size, regionCount_))
    return false;

  // If a region is ever configured, then only the configured parts
  // are available (accessible).
  if (not regionConfigured_.at(region))
    {
      // Region never configured. Make it all inacessible and mark it pristine.
      regionConfigured_.at(region) = true;
      size_t ix0 = getPageIx(size_t(regionSize_)*size_t(region));
      size_t ix1 = ix0 + getPageIx(regionSize_);
      for (size_t ix = ix0; ix < ix1; ++ix)
	{
	  auto& attrib = attribs_.at(ix);
	  attrib.setAll(false);
	  attrib.setPristine(true);
	}
    }

  // Make defined region acessible.
  size_t addr = region * regionSize_ + offset;
  size_t ix = getPageIx(addr);
  if (not attribs_.at(ix).isPristine())
    {
      std::cerr << "Region 0x" << std::hex << region << " offset 0x"
		<< std::hex << offset << " already mapped\n";
    }
	
  // Set attributes of sections in dccm
  size_t count = size/pageSize_;  // Count of pages in iccm
  for (size_t i = 0; i < count; ++i)
    {
      auto& attrib = attribs_.at(ix + i);
      attrib.sectionSize_ = count;
      attrib.setMapped(true);
      attrib.setWrite(true);
      attrib.setData(true);
      attrib.setDccm(true);
      attrib.setPristine(false);
    }
  return true;
}


bool
Memory::defineMemoryMappedRegisterRegion(size_t region, size_t size,
					 size_t regionOffset)
{
  if (region >= regionCount_)
    {
      std::cerr << "Invalid PIC memory region (" << region << "). Expecting "
		<< "number betwen 0 and " << (regionCount_ - 1) << "\n";
      return false;
    }

  // If a region is ever configured for PIC, then only the configured
  // parts are available (accessible) for data load/store.
  if (not regionConfigured_.at(region))
    {
      // Region never configured. Make it all inacessible and mark it
      // pristine.
      regionConfigured_.at(region) = true;
      size_t ix0 = getPageIx(size_t(regionSize_)*size_t(region));
      size_t ix1 = ix0 + getPageIx(regionSize_);
      for (size_t ix = ix0; ix < ix1; ++ix)
	{
	  auto& attrib = attribs_.at(ix);
	  attrib.setAll(false);
	  attrib.setPristine(true);
	}
    }

  // TBD: check size

  if ((regionOffset & 0x3ffff) != 0)  // Must be a multiple of 256k
    {
      std::cerr << "Invalid PIC memory offset (" << regionOffset
		<< "). Expecting a multiple of 256k\n";
      return false;
    }

  size_t addr = region * regionSize_ + regionOffset;
  size_t ix = getPageIx(addr);
  if (not attribs_.at(ix).isPristine())
    {
      std::cerr << "Region 0x" << std::hex << region << " offset 0x"
		<< std::hex << regionOffset << " already mapped\n";
    }

  // Set attributes of memory-mapped-register pages
  size_t count = size / pageSize_;  // page count
  for (size_t i = 0; i < count; ++i)
    {
      auto& attrib = attribs_.at(ix + i);
      attrib.sectionSize_ = count;
      attrib.setMapped(true);
      attrib.setData(true);
      attrib.setWrite(true);
      attrib.setMemMappedReg(true);
      attrib.setPristine(false);
    }
  return true;
}


bool
Memory::defineMemoryMappedRegisterWriteMask(size_t region,
					    size_t picBaseOffset,
					    size_t registerBlockOffset,
					    size_t registerIx,
					    uint32_t mask)
{
  size_t sectionStart = region * regionSize_ + picBaseOffset;
  size_t ix = getPageIx(sectionStart);
  if (not attribs_.at(ix).isMapped())
    {
      std::cerr << "Region 0x" << std::hex << region << " offset 0x"
		<< std::hex << picBaseOffset << " is not defined\n";
      return false;
    }

  if (not attribs_.at(ix).isMemMappedReg())
    {
      std::cerr << "Region 0x" << std::hex << region << " offset 0x"
		<< std::hex << picBaseOffset
		<< " is not for memory mapped registers\n";
      return false;
    }

  if (registerBlockOffset & 3)
    {
      std::cerr << "Memory mapped register offset (0x" << std::hex
		<< registerBlockOffset << " is not a multiple of 4\n";
      return false;
    }

#if 0
  size_t expectedStart = getSectionStartAddr(sectionStart);

  if (expectedStart != sectionStart)
    {
      std::cerr << "Region 0x" << std::hex << region << " offset 0x"
		<< std::hex << picBaseOffset << " is invalid\n";
      return false;
    }
#endif

  PageAttribs attrib = getAttrib(sectionStart);
  size_t sectionEnd = sectionStart + size_t(attrib.sectionSize_)*size_t(pageSize_);
  size_t registerEndAddr = sectionStart + registerBlockOffset + registerIx*4 + 3;
  if (registerEndAddr >= sectionEnd)
    {
      std::cerr << "PIC register out of bounds:\n"
		<< "  region:          0x" << std::hex << region << '\n'
		<< "  pic-base-offset: 0x" << std::hex << picBaseOffset << '\n'
		<< "  register-offset: 0x" << std::hex << registerBlockOffset << '\n'
		<< "  register-index:  0x" << std::hex << registerIx << '\n';
      return false;
    }

  if (masks_.empty())
    masks_.resize(pageCount_);

  size_t registerStartAddr = sectionStart + registerBlockOffset + registerIx*4;
  size_t pageIx = getPageIx(registerStartAddr);
  size_t pageStart = getPageStartAddr(registerStartAddr);
  std::vector<uint32_t>& pageMasks = masks_.at(pageIx);
  if (pageMasks.empty())
    {
      size_t wordCount = pageSize_ / 4;
      pageMasks.resize(wordCount);
    }
  size_t maskIx = (registerStartAddr - pageStart) / 4;
  pageMasks.at(maskIx) = mask;

  return true;
}


// If a region (256 mb) contains one or more ICCM section but no
// DCCM/PIC, then all sections in that region are accessible for data
// (including the ICCM sections).
//
// If a region contains one or more DCCM/PIC section but no ICCM, then
// all unmapped sections become accessible for instruction fetch (including
// DCCM/PIC sections).
//
// This is done to match the echx1 RTL.
void
Memory::finishMemoryConfig()
{
  for (size_t region = 0; region < regionCount_; ++region)
    {
      if (not regionConfigured_.at(region))
	continue;   // Region does not have DCCP, PIC, or ICCM.

      bool hasData = false;  // True if region has DCCM/PIC section(s).
      bool hasInst = false;  // True if region has ICCM secion(s).

      size_t addr = region * regionSize_;
      size_t pageCount = regionSize_ / pageSize_;

      size_t pageIx = getPageIx(addr);
      for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	{
	  PageAttribs attrib = attribs_.at(pageIx);
	  hasData = hasData or attrib.isMappedData();
	  hasInst = hasInst or attrib.isMappedInst();
	}

      if (hasInst and hasData)
	continue;

      if (hasInst)
	{
	  size_t pageIx = getPageIx(addr);
	  for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	    {
	      auto& attrib = attribs_.at(pageIx);
	      attrib.setMapped(true);
	      attrib.setWrite(true);
	      attrib.setData(true);
	      attrib.setPristine(false);
	    }
	}

      if (hasData)
	{
	  size_t pageIx = getPageIx(addr);
	  for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	    {
	      auto& attrib = attribs_.at(pageIx);
	      attrib.setMapped(true);
	      attrib.setInst(true);
	      attrib.setPristine(false);
	    }
	}
    }
}
