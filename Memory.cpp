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

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <boost/algorithm/string.hpp>
#ifndef __MINGW64__
#include <sys/mman.h>
#endif
#include <elfio/elfio.hpp>
#include <zlib.h>
#include "Memory.hpp"

using namespace WdRiscv;


Memory::Memory(size_t size, size_t pageSize, size_t regionSize)
  : size_(size), data_(nullptr), pageSize_(pageSize), reservations_(1),
    lastWriteData_(1)
{ 
  if ((size & 4) != 0)
    {
      size_ = (size >> 2) << 2;
      std::cerr << "Memory size (" << size << ") is not a multiple of 4. Using "
		<< size_ << '\n';
    }

  unsigned logPageSize = static_cast<unsigned>(std::log2(pageSize_));
  unsigned p2PageSize = unsigned(1) << logPageSize;
  if (p2PageSize != pageSize_)
    {
      std::cerr << "Memory page size (0x" << std::hex << pageSize_ << ") "
		<< "is not a power of 2 -- using 0x" << p2PageSize << '\n'
		<< std::dec;
      pageSize_ = p2PageSize;
    }
  pageShift_ = logPageSize;

  if (size_ < pageSize_)
    {
      std::cerr << "Unreasonably small memory size (less than 0x "
		<< std::hex << pageSize_ << ") -- using 0x" << pageSize_
		<< '\n' << std::dec;
      size_ = pageSize_;
    }

  pageCount_ = size_ / pageSize_;
  if (size_t(pageCount_) * pageSize_ != size_)
    {
      pageCount_++;
      size_t newSize = pageCount_ * pageSize_;
      std::cerr << "Memory size (0x" << std::hex << size_ << ") is not a "
		<< "multiple of page size (0x" << pageSize_ << ") -- "
		<< "using 0x" << newSize << '\n' << std::dec;

      size_ = newSize;
    }

  size_t logRegionSize = static_cast<size_t>(std::log2(regionSize));
  size_t p2RegionSize = size_t(1) << logRegionSize;
  if (p2RegionSize != regionSize)
    {
      std::cerr << "Memory region size (0x" << std::hex << regionSize << ") "
		<< "is not a power of 2 -- using 0x" << p2RegionSize << '\n'
		<< std::dec;
      regionSize = p2RegionSize;
    }

  regionSize_ = regionSize;
  if (regionSize_ < pageSize_)
    {
      std::cerr << "Memory region size (0x" << std::hex << regionSize_ << ") "
		<< "smaller than page size (0x" << pageSize_ << ") -- "
		<< "using page size\n" << std::dec;
      regionSize_ = pageSize_;
    }

  size_t pagesInRegion = regionSize_ / pageSize_;
  size_t multiple = pagesInRegion * pageSize_;
  if (multiple != regionSize_)
    {
      std::cerr << "Memory region size (0x" << std::hex << regionSize_ << ") "
		<< "is not a multiple of page size (0x" << pageSize_ << ") -- "
		<< "using " << multiple << " as region size\n" << std::dec;
      regionSize_ = multiple;
    }

  regionCount_ = size_ / regionSize_;
  if (regionCount_ * regionSize_ < size_)
    regionCount_++;

#ifndef __MINGW64__
  void* mem = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (mem == (void*) -1)
    {
      std::cerr << "Failed to map " << size_ << " bytes using mmap.\n";
#else
  void* mem = malloc(size_);
  if (mem == nullptr)
    {
      std::cerr << "Failed to alloc " << size_ << " bytes using malloc.\n";
#endif
      throw std::runtime_error("Out of memory");
    }

  data_ = reinterpret_cast<uint8_t*>(mem);

  // Mark all regions as non-configured.
  regionConfigured_.resize(regionCount_);

  attribs_.resize(pageCount_);

  // Make whole memory as mapped, writable, allowing data and inst.
  // Some of the pages will be later reconfigured when the user
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
#ifndef __MINGW64__
      munmap(data_, size_);
#else
      free(data_);
#endif
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

  size_t address = 0, errors = 0, overwrites = 0;

  std::string line;

  for (unsigned lineNum = 0; std::getline(input, line); ++lineNum)
    {
      boost::algorithm::trim(line);
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
	  if (end and *end and not isspace(*end))
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
			<< "Invalid value: " << std::hex << value << '\n'
			<< std::dec;
	      errors++;
	    }
	  if (address < size_)
	    {
	      if (not errors)
		{
		  if (data_[address] != 0)
		    overwrites++;
		  data_[address++] = value & 0xff;
		}
	    }
	  else
	    {
	      std::cerr << "File " << fileName << ", Line " << lineNum << ": "
			<< "Address out of bounds: " << std::hex << address
			<< '\n' << std::dec;
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

  if (overwrites)
    std::cerr << "File " << fileName << ": Overwrote previously loaded data "
	      << "changing " << overwrites << " or more bytes\n";

  return errors == 0;
}


bool
Memory::loadElfFile(const std::string& fileName, unsigned regWidth,
		    size_t& entryPoint, size_t& end)
{
  entryPoint = 0;
  end = 0;

  ELFIO::elfio reader;

  if (regWidth != 32 and regWidth != 64)
    {
      std::cerr << "Error: Memory::loadElfFile called with a unsupported "
		<< "register width: " << regWidth << '\n';
      return false;
    }

  if (not reader.load(fileName))
    {
      std::cerr << "Error: Failed to load ELF file " << fileName << '\n';
      return false;
    }

  bool is32 = reader.get_class() == ELFCLASS32;
  bool is64 = reader.get_class() == ELFCLASS64;
  if (not (is32 or is64))
    {
      std::cerr << "Error: ELF file is neither 32 nor 64-bit. Only 32/64-bit ELFs are currently supported\n";
      return false;
    }

  if (regWidth == 32 and not is32)
    {
      if (is64)
	std::cerr << "Error: Loading a 64-bit ELF file in 32-bit mode.\n";
      else
	std::cerr << "Error: Loading non-32-bit ELF file in 32-bit mode.\n";
      return false;
    }

  if (regWidth == 64 and not is64)
    {
      std::cerr << "Error: Loading non-64-bit ELF file in 64-bit mode.\n";
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
  size_t errors = 0, overwrites = 0;

  unsigned loadedSegs = 0;
  for (int segIx = 0; segIx < reader.segments.size(); ++segIx)
    {
      const ELFIO::segment* seg = reader.segments[segIx];
      ELFIO::Elf64_Addr vaddr = seg->get_virtual_address();
      ELFIO::Elf_Xword segSize = seg->get_file_size(); // Size in file.
      const char* segData = seg->get_data();
      if (seg->get_type() != PT_LOAD)
	continue;

      if (vaddr + segSize > size_)
	{
	  std::cerr << "End of ELF segment " << segIx << " ("
		    << (vaddr+segSize)
		    << ") is beyond end of simulated memory ("
		    << size_ << ")\n";
	  if (checkUnmappedElf_)
	    {
	      errors++;
	      continue;
	    }
	}

      size_t unmappedCount = 0;
      for (size_t i = 0; i < segSize; ++i)
	{
	  if (data_[vaddr + i] != 0)
	    overwrites++;
	  if (not writeByteNoAccessCheck(vaddr + i, segData[i]))
	    {
	      if (unmappedCount == 0)
		std::cerr << "Failed to copy ELF byte at address 0x"
			  << std::hex << (vaddr + i) << std::dec
			  << ": corresponding location is not mapped\n";
	      unmappedCount++;
	      if (checkUnmappedElf_)
		{
		  errors++;
		  break;
		}
	    }
	}

      loadedSegs++;
      maxEnd = std::max(maxEnd, size_t(vaddr) + size_t(segSize));
    }

  if (loadedSegs == 0)
    {
      std::cerr << "No loadable segment in ELF file\n";
      errors++;
    }

  // In case writing ELF data modified last-written-data associated
  // with each hart.
  for (unsigned hartId = 0; hartId < reservations_.size(); ++hartId)
    clearLastWriteInfo(hartId);

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
	    {
	      if (name.empty())
		continue;
	      if (type == STT_NOTYPE or type == STT_FUNC or type == STT_OBJECT)
		symbols_[name] = ElfSymbol(address, size);
	    }
	}
    }

  // Get the program entry point.
  if (not errors)
    {
      entryPoint = reader.get_entry();
      end = maxEnd;
    }

  if (overwrites)
    std::cerr << "File " << fileName << ": Overwrote previously loaded data "
	      << "changing " << overwrites << " or more bytes\n";

  return errors == 0;
}


bool
Memory::findElfSymbol(const std::string& symbol, ElfSymbol& value) const
{
  if (not symbols_.count(symbol))
    return false;

  value = symbols_.at(symbol);
  return true;
}


bool
Memory::findElfFunction(size_t addr, std::string& name, ElfSymbol& value) const
{
  for (const auto& kv : symbols_)
    {
      auto& sym = kv.second;
      size_t start = sym.addr_, end = sym.addr_ + sym.size_;
      if (addr >= start and addr < end)
	{
	  name = kv.first;
	  value = sym;
	  return true;
	}
    }

  return false;
}


void
Memory::printElfSymbols(std::ostream& out) const
{
  out << std::hex;
  for (const auto& kv : symbols_)
    out << kv.first << ' ' << "0x" << kv.second.addr_ << '\n';
  out << std::dec;
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


bool
Memory::checkElfFile(const std::string& path, bool& is32bit,
		     bool& is64bit, bool& isRiscv)
{
  ELFIO::elfio reader;

  if (not reader.load(path))
    return false;

  is32bit = reader.get_class() == ELFCLASS32;
  is64bit = reader.get_class() == ELFCLASS64;
  isRiscv = reader.get_machine() == EM_RISCV;

  return true;
}


bool
Memory::isSymbolInElfFile(const std::string& path, const std::string& target)
{
  ELFIO::elfio reader;

  if (not reader.load(path))
    return false;

  auto secCount = reader.sections.size();
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
	    {
	      if (name.empty())
		continue;
	      if (type == STT_NOTYPE or type == STT_FUNC or type == STT_OBJECT)
		if (name == target)
		  return true;
	    }
	}
    }
  return false;
}


bool
Memory::saveSnapshot(const std::string& filename,
                     const std::vector<std::pair<uint64_t,uint64_t>>& used_blocks)
{
  constexpr size_t max_chunk = size_t(1) << 30;

  // Open binary file for write (compressed) and check success.
  std::cout << "saveSnapshot starts..\n";
  gzFile gzout = gzopen(filename.c_str(), "wb");
  if (not gzout)
    {
      std::cerr << "Memory::saveSnapshot failed - cannot open " << filename
                << " for write\n";
      return false;
    }

  // write the simulated memory into the file and check success
  // loop over blocks
  uint64_t prev_addr = 0;
  bool success = true;
  for (auto& blk: used_blocks)
    {
      uint8_t* buffer = data_+blk.first;
      size_t remainingSize = blk.second;
      assert(prev_addr<=blk.first);
      prev_addr = blk.first+blk.second;
      std::cout << "*";
      while (remainingSize)  // write in chunk due to limitation of gzwrite
        {
          std::cout << "-";
          fflush(stdout);
          size_t current_chunk = std::min(remainingSize, max_chunk);
          int resp = gzwrite(gzout, buffer, current_chunk);
          success = resp > 0 and size_t(resp) == current_chunk;
          if (not success)
            break;
          remainingSize -= current_chunk;
          buffer += current_chunk;
        }
      if (not success)
        break;
    }

  if (not success)
    std::cerr << "Memory::saveSnapshot failed - write into " << filename
              << " failed with errno " << strerror(errno) << "\n";
  gzclose(gzout);
  std::cout << "\nsaveSnapshot finished\n";
  return success;
}


bool
Memory::loadSnapshot(const std::string & filename,
                     const std::vector<std::pair<uint64_t,uint64_t>>& used_blocks)
{
  constexpr size_t max_chunk = size_t(1) << 30;
  std::cout << "loadSnapshot starts..\n";

  // open binary file for read (decompress) and check success
  gzFile gzin = gzopen(filename.c_str(), "rb");
  if (not gzin or gzeof(gzin))
    {
      std::cerr << "Memory::loadSnapshot failed - cannot open "
                << filename << " for read\n";
      return false;
    }

  // read (decompress) file into simulated memory and check success
  bool success = true;
  uint64_t prev_addr = 0;
  size_t remainingSize = 0;
  for (auto& blk: used_blocks)
    {
      uint8_t * buffer = data_+blk.first;
      remainingSize = blk.second;
      assert(prev_addr<=blk.first);
      prev_addr = blk.first+blk.second;
      std::cout << "*";
      while (remainingSize) // read in chunk due to gzread limitation
        {
          std::cout << "-";
          fflush(stdout);
          size_t current_chunk = std::min(remainingSize, max_chunk);
          int resp = gzread(gzin, buffer, current_chunk);
          if (resp == 0)
            {
              success = gzeof(gzin);
              break;
            }
          remainingSize -= resp;
          buffer += resp;
        }
      if(not success)
        break;
    }

  if (not success)
    std::cerr << "Memory::loadSnapshot failed - read from " << filename
              << " failed: " << gzerror(gzin, nullptr) << "\n";
  else if (remainingSize > 0)
    std::cerr << "Memory::loadSnapshot: Warning: Snapshot data size smaller than memory size\n";
  else if (not gzeof(gzin))
    std::cerr << "Memory::loadSnapshot: Warning: Snapshot data size larger than memory size\n";

  gzclose(gzin);
  std::cout << "\nloadSnapshot finished\n";
  return success;
}


void
Memory::copy(const Memory& other)
{
  size_t n = std::min(size_, other.size_);
  memcpy(data_, other.data_, n);
}


bool
Memory::writeByteNoAccessCheck(size_t addr, uint8_t value)
{
  PageAttribs attrib = getAttrib(addr);
  if (not attrib.isMapped())
    return false;

  // Perform masking for memory mapped registers.
  uint32_t mask = getMemoryMappedMask(addr);
  unsigned byteIx = addr & 3;
  value = value & uint8_t((mask >> (byteIx*8)));

  data_[addr] = value;

  return true;
}


bool
Memory::checkCcmConfig(const std::string& tag, size_t region, size_t offset,
		       size_t size) const
{
  if (region >= regionCount_)
    {
      std::cerr << "Invalid " << tag << " region (" << region
		<< "). Expecting number between 0 and "
		<< (regionCount_ - 1) << "\n";
      return false;
    }

  if (size < pageSize_)
    {
      std::cerr << "Invalid " << tag << " size (" << size << "). Expecting a\n"
		<< "  multiple of page size (" << pageSize_ << ")\n";
      return false;
    }

  // CCM area must be page aligned.
  size_t addr = region*regionSize_ + offset;
  if ((addr % pageSize_) != 0)
    {
      std::cerr << "Invalid " << tag << " start address (" << addr
		<< "): not page (" << pageSize_ << ") aligned\n";
      return false;
    }

  // CCM area must be aligned to the nearest power of 2 larger than or
  // equal to its size.
  size_t log2Size = static_cast<size_t>(log2(size));
  size_t powerOf2 = size_t(1) << log2Size;
  if (powerOf2 != size)
    powerOf2 *= 2;

  if ((addr % powerOf2) != 0)
    {
      std::cerr << "Invalid " << tag << " start address (" << addr
		<< "): not aligned to size (" << powerOf2 << ")\n";
      return false;
    }

  return true;
}
    

bool
Memory::checkCcmOverlap(const std::string& tag, size_t region, size_t offset,
			size_t size, bool iccm, bool dccm, bool pic)
{
  // If a region is ever configured, then only the configured parts
  // are available (accessible).
  if (not regionConfigured_.at(region))
    {
      // Region never configured. Make it all inaccessible.
      regionConfigured_.at(region) = true;
      size_t ix0 = getPageIx(regionSize_*size_t(region));
      size_t ix1 = ix0 + getPageIx(regionSize_);
      for (size_t ix = ix0; ix < ix1; ++ix)
	{
	  auto& attrib = attribs_.at(ix);
	  attrib.setAll(false);
	}
      return true;  // No overlap.
    }

  // Check area overlap.
  size_t addr = region * regionSize_ + offset;
  size_t ix0 = getPageIx(addr);
  size_t ix1 = getPageIx(addr + size);
  for (size_t ix = ix0; ix < ix1; ++ix)
    {
      auto& attrib = attribs_.at(ix);
      if (attrib.isMapped())
	{
	  if ((iccm and not attrib.isIccm()) or
	      (dccm and not attrib.isDccm()) or
	      (pic  and not attrib.isMemMappedReg()))
	    {
	      std::cerr << tag << " area at address " << addr << " overlaps"
			<< " a previously defined area.\n";
	      return false;
	    }
	}
    }

  return true;
}


bool
Memory::defineIccm(size_t region, size_t offset, size_t size)
{
  if (not checkCcmConfig("ICCM", region, offset, size))
    return false;

  checkCcmOverlap("ICCM", region, offset, size, true, false, false);

  size_t addr = region * regionSize_ + offset;
  size_t ix = getPageIx(addr);

  // Set attributes of pages in iccm
  size_t count = size/pageSize_;  // Count of pages in iccm
  for (size_t i = 0; i < count; ++i)
    {
      auto& attrib = attribs_.at(ix + i);
      attrib.setExec(true);
      // attrib.setRead(true);
      attrib.setIccm(true);
    }
  return true;
}


bool
Memory::defineDccm(size_t region, size_t offset, size_t size)
{
  if (not checkCcmConfig("DCCM", region, offset, size))
    return false;

  checkCcmOverlap("DCCM", region, offset, size, false, true, false);

  size_t addr = region * regionSize_ + offset;
  size_t ix = getPageIx(addr);

  // Set attributes of pages in dccm
  size_t count = size/pageSize_;  // Count of pages in iccm
  for (size_t i = 0; i < count; ++i)
    {
      auto& attrib = attribs_.at(ix + i);
      attrib.setWrite(true);
      attrib.setRead(true);
      attrib.setDccm(true);
    }
  return true;
}


bool
Memory::defineMemoryMappedRegisterRegion(size_t region, size_t offset,
					 size_t size)
{
  if (not checkCcmConfig("PIC memory", region, offset, size))
    return false;

  checkCcmOverlap("PIC memory", region, offset, size, false, false, true);

  size_t addr = region * regionSize_ + offset;
  size_t pageIx = getPageIx(addr);

  // Set attributes of memory-mapped-register pages
  size_t count = size / pageSize_;  // page count
  for (size_t i = 0; i < count; ++i)
    {
      mmrPages_.push_back(pageIx);

      auto& attrib = attribs_.at(pageIx++);
      attrib.setRead(true);
      attrib.setWrite(true);
      attrib.setMemMappedReg(true);
    }
  return true;
}


void
Memory::resetMemoryMappedRegisters()
{
  for (auto pageIx : mmrPages_)
    {
      size_t addr0 = pageIx * pageSize_;  // page start address
      size_t addr1 = addr0 + pageSize_ - 1; // last byte in page.
      size_t hostAddr0 = 0, hostAddr1 = 0;
      if (getSimMemAddr(addr0, hostAddr0) and getSimMemAddr(addr1, hostAddr1))
	memset(reinterpret_cast<void*>(hostAddr0), 0, pageSize_);
    }
}


static void
printPicRegisterError(const std::string& error, size_t region, size_t picOffset,
		      size_t regAreaOffset, size_t regIx)
{
  std::cerr << std::hex;
  std::cerr << error << ":\n"
	    << "  region:          0x" << region << '\n'
	    << "  pic-base-offset: 0x" << picOffset << '\n'
	    << "  register-offset: 0x" << regAreaOffset << '\n'
	    << "  register-index:  0x" << regIx << '\n';
  std::cerr << std::dec;
}


// Parameters:
//   region: 256 mb region index
//   pic offset: pic area offset within region
//   register area offset: offset of register file withing pic area
//   register index: index of register with register area
//   mask: mask of register
bool
Memory::defineMemoryMappedRegisterWriteMask(size_t region,
					    size_t picOffset,
					    size_t regAreaOffset,
					    size_t regIx,
					    uint32_t mask)
{
  size_t sectionStart = region * regionSize_ + picOffset;
  size_t ix = getPageIx(sectionStart);
  if (not attribs_.at(ix).isMapped())
    {
      printPicRegisterError("PIC area does not exist", region, picOffset,
			    regAreaOffset, regIx);
      return false;
    }

  if (not attribs_.at(ix).isMemMappedReg())
    {
      printPicRegisterError("Area not defined for PIC registers", region,
			    picOffset, regAreaOffset, regIx);
      return false;
    }

  if (regAreaOffset & 3)
    {
      printPicRegisterError("PIC register offset not a multiple of 4",
			    region, picOffset, regAreaOffset, regIx);
      return false;
    }

  size_t registerAddr = sectionStart + regAreaOffset + regIx*4;
  size_t pageIx = getPageIx(registerAddr);
  if (not attribs_.at(pageIx).isMemMappedReg())
    {
      printPicRegisterError("PIC register out of bounds", region, picOffset,
			    regAreaOffset, regIx);
      return false;
    }

  if (masks_.empty())
    masks_.resize(pageCount_);

  size_t pageStart = getPageStartAddr(registerAddr);
  std::vector<uint32_t>& pageMasks = masks_.at(pageIx);
  if (pageMasks.empty())
    {
      size_t wordCount = pageSize_ / 4;
      pageMasks.resize(wordCount);
    }
  size_t maskIx = (registerAddr - pageStart) / 4;
  pageMasks.at(maskIx) = mask;

  return true;
}


// If a region (256 mb) contains one or more ICCM section but no
// DCCM/PIC, then all pages in that region become accessible for data
// (including those of the ICCM sections).
//
// If a region contains one or more DCCM/PIC section but no ICCM, then
// all pages in that region become accessible for instruction fetch
// (including those of the DCCM/PIC sections).
//
// If a region contains both ICCM and DCCM/PIC sections then no page
// outside the ICCM section(s) is accessible for instruction fetch and
// no page outside the DCCM/PIC section(s) is accessible for data
// access.
//
// This is done to match the echx1 RTL.
void
Memory::finishCcmConfig()
{
  for (size_t region = 0; region < regionCount_; ++region)
    {
      if (not regionConfigured_.at(region))
	continue;   // Region does not have DCCP, PIC, or ICCM.

      bool hasData = false;  // True if region has DCCM/PIC section(s).
      bool hasInst = false;  // True if region has ICCM section(s).

      size_t addr = region * regionSize_;
      size_t pageCount = regionSize_ / pageSize_;

      size_t pageIx = getPageIx(addr);
      for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	{
	  PageAttribs attrib = attribs_.at(pageIx);
	  hasData = hasData or attrib.isWrite();
	  hasInst = hasInst or attrib.isExec();
	}

      if (hasInst and hasData)
	{
	  // Make ICCM pages non-read and non-write. Make DCCM pages
	  // non-exec.
	  size_t pageIx = getPageIx(addr);
	  for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	    {
	      PageAttribs& attrib = attribs_.at(pageIx);
	      if (attrib.isExec())
		{
		  attrib.setWrite(false);
		  attrib.setRead(false);
		}
	      else if (attrib.isWrite())
		attrib.setExec(false);
	    }

	  continue;
	}

      if (hasInst)
	{
	  size_t pageIx = getPageIx(addr);
	  for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	    {
	      auto& attrib = attribs_.at(pageIx);
	      attrib.setWrite(true);
	      attrib.setRead(true);
	    }
	}

      if (hasData)
	{
	  size_t pageIx = getPageIx(addr);
	  for (size_t i = 0; i < pageCount; ++i, ++pageIx)
	    {
	      auto& attrib = attribs_.at(pageIx);
	      attrib.setExec(true);
	    }
	}
    }
}
