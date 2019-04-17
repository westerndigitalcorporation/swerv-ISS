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
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include "Interactive.hpp"
#include "linenoise.hpp"

using namespace WdRiscv;


/// Return format string suitable for printing an integer of type URV
/// in hexadecimal form.
template <typename URV>
static
const char*
getHexForm()
{
  if (sizeof(URV) == 4)
    return "0x%08x";
  if (sizeof(URV) == 8)
    return "0x%016x";
  if (sizeof(URV) == 16)
    return "0x%032x";
  return "0x%x";
}


/// Convert the command line string numberStr to a number using
/// strotull and a base of zero (prefixes 0 and 0x are
/// honored). Return true on success and false on failure (string does
/// not represent a number). TYPE is an integer type (e.g
/// uint32_t). Option is the command line option associated with the
/// string and is used for diagnostic messages.
template <typename TYPE>
static
bool
parseCmdLineNumber(const std::string& option,
		   const std::string& numberStr,
		   TYPE& number)
{
  bool good = not numberStr.empty();

  if (good)
    {
      char* end = nullptr;
      uint64_t value = strtoull(numberStr.c_str(), &end, 0);
      number = static_cast<TYPE>(value);
      if (number != value)
	{
	  std::cerr << "parseCmdLineNumber: Number too large: " << numberStr
		    << '\n';
	  return false;
	}
      if (end and *end)
	good = false;  // Part of the string are non parseable.
    }

  if (not good)
    std::cerr << "Invalid command line " << option << " value: " << numberStr
	      << '\n';
  return good;
}


template <typename URV>
Interactive<URV>::Interactive(std::vector< Core<URV>* >& coreVec)
  : cores_(coreVec)
{
}


template <typename URV>
bool
Interactive<URV>::untilCommand(Core<URV>& core, const std::string& line,
			       const std::vector<std::string>& tokens,
			       FILE* traceFile)
{
  if (tokens.size() != 2)
    {
      std::cerr << "Invalid until command: " << line << '\n';
      std::cerr << "Expecting: until address\n";
      return false;
    }

  URV addr = 0;
  if (not parseCmdLineNumber("address", tokens.at(1), addr))
    return false;

  return core.untilAddress(addr, traceFile);
}


template <typename URV>
bool
Interactive<URV>::stepCommand(Core<URV>& core, const std::string& /*line*/,
			      const std::vector<std::string>& tokens,
			      FILE* traceFile)
{
  if (tokens.size() == 1)
    {
      core.singleStep(traceFile);
      core.clearTraceData();
      return true;
    }

  uint64_t count;
  if (not parseCmdLineNumber("instruction-count", tokens.at(1), count))
    return false;

  if (count == 0)
    return true;

  for (uint64_t i = 0; i < count; ++i)
    {
      core.singleStep(traceFile);
      core.clearTraceData();
    }

  return true;
}


template <typename URV>
static
void
peekAllFpRegs(Core<URV>& core)
{
  for (unsigned i = 0; i < core.fpRegCount(); ++i)
    {
      uint64_t val = 0;
      if (core.peekFpReg(i, val))
	{
	  std::cout << "f" << i << ": "
		    << (boost::format("0x%016x") % val) << '\n';
	}
    }
}


template <typename URV>
static
void
peekAllIntRegs(Core<URV>& core)
{
  bool abiNames = core.abiNames();
  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val

  for (unsigned i = 0; i < core.intRegCount(); ++i)
    {
      std::string name;
      URV val = 0;
      if (core.peekIntReg(i, val, name))
	{
	  std::string tag = name;
	  if (abiNames)
	    tag += "(" + std::to_string(i) + ")";
	  tag += ":";

          std::cout << (boost::format("%-9s") % tag)
		    << (boost::format(hexForm) % val) << '\n';
	}
    }
}


template <typename URV>
static
void
peekAllCsrs(Core<URV>& core)
{
  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val

  std::cout << (boost::format("%-23s") % "csr");
  if (sizeof(URV) == 4)
    std::cout << (boost::format("%-10s %-10s %-10s %-10s\n") % "value" %
		  "reset" % "mask" % "pokemask");
  else
    std::cout << (boost::format("%-18s %-18s %-18s %-10s\n") % "value" %
		  "reset" % "mask" % "pokemask");

  for (size_t i = 0; i <= size_t(CsrNumber::MAX_CSR_); ++i)
    {
      CsrNumber csr = CsrNumber(i);
      std::string name;
      URV val = 0;
      if (core.peekCsr(csr, val, name))
	{
	  std::ostringstream oss;
	  oss << name << "(0x" << std::hex << i << "):";

	  std::cout << (boost::format("%-23s") % oss.str())
		    << (boost::format(hexForm) % val);

	  URV reset = 0, writeMask = 0, pokeMask = 0;
	  if (core.peekCsr(csr, val, reset, writeMask, pokeMask))
	    {
	      std::cout << ' ' << (boost::format(hexForm) % reset);
	      std::cout << ' ' << (boost::format(hexForm) % writeMask);
	      std::cout << ' ' << (boost::format(hexForm) % pokeMask);
	    }
	  std::cout << '\n';
	}
    }
}


template <typename URV>
static
void
peekAllTriggers(Core<URV>& core)
{
  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val

  std::cout << (boost::format("%-12s") % "trigger");
  if (sizeof(URV) == 4)
    std::cout << (boost::format("%-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n") %
		  "value1" % "value2" % "value3" %
		  "mask1" % "mask2" % "mask3" %
		  "poke-mask1" % "poke-mask2"  % "poke-mask3");
  else
    std::cout << (boost::format("%-18s %-18s %-18s %-18s %-18s %-18s %-18s %-18s %-18s\n") %
		  "value1" % "value2" % "value3" %
		  "mask1" % "mask2" % "mask3" %
		  "poke-mask1" % "poke-mask2"  % "poke-mask3");


  // value/reset/write-mask/poke-mask
  URV tselVal = 0, tselReset, tselWm = 0, tselPm = 0;

  if (core.peekCsr(CsrNumber::TSELECT, tselVal, tselReset, tselWm, tselPm))
    {
      URV maxTrigger = tselWm;
      for (URV trigger = 0; trigger <= maxTrigger; ++trigger)
	{
	  URV v1(0), v2(0), v3(0), wm1(0), wm2(0), wm3(0);
	  URV pm1(0), pm2(0), pm3(0);

	  if (core.peekTrigger(trigger, v1, v2, v3, wm1, wm2, wm3,
			       pm1, pm2, pm3))
	    {
	      std::string name = "trigger" + std::to_string(trigger) + ":";
	      std::cout << (boost::format("%-11s") % name);
	      std::cout << ' ' << (boost::format(hexForm) % v1);
	      std::cout << ' ' << (boost::format(hexForm) % v2);
	      std::cout << ' ' << (boost::format(hexForm) % v3);
	      std::cout << ' ' << (boost::format(hexForm) % wm1);
	      std::cout << ' ' << (boost::format(hexForm) % wm2);
	      std::cout << ' ' << (boost::format(hexForm) % wm3);
	      std::cout << ' ' << (boost::format(hexForm) % pm1);
	      std::cout << ' ' << (boost::format(hexForm) % pm2);
	      std::cout << ' ' << (boost::format(hexForm) % pm3);
	      std::cout << '\n';
	    }
	  else
	    break;
	}
    }
}


template <typename URV>
bool
Interactive<URV>::peekCommand(Core<URV>& core, const std::string& line,
			      const std::vector<std::string>& tokens)
{
  if (tokens.size() < 2)
    {
      std::cerr << "Invalid peek command: " << line << '\n';
      std::cerr << "Expecting: peek <item> <addr>  or  peek pc  or  peek all\n";
      std::cerr << "  Item is one of r, f, c, t or m for integer, floating point,\n";
      std::cerr << "  CSR, trigger register or memory location respective\n";

      std::cerr << "  example:  peek r x3\n";
      std::cerr << "  example:  peek c mtval\n";
      std::cerr << "  example:  peek m 0x4096\n";
      std::cerr << "  example:  peek t 0\n";
      std::cerr << "  example:  peek pc\n";
      return false;
    }

  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val
  URV val = 0;

  const std::string& resource = tokens.at(1);

  if (resource == "all")
    {
      std::cout << "pc: " << (boost::format(hexForm) % core.peekPc()) << '\n';
      std::cout << "\n";

      peekAllIntRegs(core);
      std::cout << "\n";

      peekAllCsrs(core);
      std::cout << "\n";

      peekAllTriggers(core);
      return true;
    }

  if (resource == "pc")
    {
      URV pc = core.peekPc();
      std::cout << (boost::format(hexForm) % pc) << std::endl;
      return true;
    }

  if (tokens.size() < 3)
    {
      std::cerr << "Invalid peek command: " << line << '\n';
      std::cerr << "Expecting: peek <resource> <address>\n";
      return false;
    }

  const std::string& addrStr = tokens.at(2);

  if (resource == "m")
    {
      URV addr0 = 0;
      if (not parseCmdLineNumber("memory-address", addrStr, addr0))
	return false;

      URV addr1 = addr0;
      if (tokens.size() == 4)
	if (not parseCmdLineNumber("memory-address", tokens.at(3), addr1))
	  return false;

      uint32_t word = 0;
      for (URV addr = addr0; addr <= addr1; addr += 4)
	{
	  if (not core.peekMemory(addr, word))
	    {
	      std::cerr << "Memory address out of bounds: " << addrStr << '\n';
	      return false;
	    }
	  std::cout << (boost::format(hexForm) % addr) << ": ";
	  std::cout << (boost::format("0x%08x") % word) << std::endl;
	}
      return true;
    }

  if (resource == "r")
    {
      if (addrStr == "all")
	{
	  peekAllIntRegs(core);
	  return true;
	}

      unsigned intReg = 0;
      if (not core.findIntReg(addrStr, intReg))
	{
	  std::cerr << "No such integer register: " << addrStr << '\n';
	  return false;
	}
      if (core.peekIntReg(intReg, val))
	{
	  std::cout << (boost::format(hexForm) % val) << std::endl;
	  return true;
	}
      std::cerr << "Failed to read integer register: " << addrStr << '\n';
      return false;
    }

  if (resource == "f")
    {
      if (not core.isRvf())
	{
	  std::cerr << "Floating point extension is no enabled\n";
	  return false;
	}

      if (addrStr == "all")
	{
	  peekAllFpRegs(core);
	  return true;
	}

      unsigned fpReg = 0;
      if (not core.findFpReg(addrStr, fpReg))
	{
	  std::cerr << "No such integer register: " << addrStr << '\n';
	  return false;
	}
      uint64_t fpVal = 0;
      if (core.peekFpReg(fpReg, fpVal))
	{
	  std::cout << (boost::format("0x%016x") % fpVal) << std::endl;
	  return true;
	}
      std::cerr << "Failed to read fp register: " << addrStr << '\n';
      return false;
    }

  if (resource == "c")
    {
      if (addrStr == "all")
	{
	  peekAllCsrs(core);
	  return true;
	}

      auto csr = core.findCsr(addrStr);
      if (not csr)
	{
	  std::cerr << "No such CSR: " << addrStr << '\n';
	  return false;
	}
      if (core.peekCsr(csr->getNumber(), val))
	{
	  std::cout << (boost::format(hexForm) % val) << std::endl;
	  return true;
	}
      std::cerr << "Failed to read CSR: " << addrStr << '\n';
      return false;
    }

  if (resource == "t")
    {
      if (addrStr == "all")
	{
	  peekAllTriggers(core);
	  return true;
	}

      URV trigger = 0;
      if (not parseCmdLineNumber("trigger-number", addrStr, trigger))
	return false;
      URV v1(0), v2(0), v3(0);
      if (core.peekTrigger(trigger, v1, v2, v3))
	{
	  std::cout << (boost::format(hexForm) % v1) << ' '
		    << (boost::format(hexForm) % v2) << ' '
		    << (boost::format(hexForm) % v3) << std::endl;
	  return true;
	}
      std::cerr << "Trigger number out of bounds: " << addrStr << '\n';
      return false;
    }

  std::cerr << "No such resource: " << resource
	    << " -- expecting r, m, c, t, or pc\n";
  return false;
}


template <typename URV>
bool
Interactive<URV>::pokeCommand(Core<URV>& core, const std::string& line,
			      const std::vector<std::string>& tokens)
{
  if (tokens.size() < 3)
    {
      std::cerr << "Invalid poke command: " << line << '\n';
      std::cerr << "  Expecting: poke pc <value>\n";
      std::cerr << "    or       poke <resource> <address> <value>\n";
      std::cerr << "    or       poke t <number> <value1> <value2> <value3>\n";
      std::cerr << "  where <resource> is one of r, f, c, t or m\n";
      return false;
    }

  const std::string& resource = tokens.at(1);
  URV value = 0;

  if (resource == "pc")
    {
      if (not parseCmdLineNumber("pc", tokens.at(2), value))
	return false;
      core.pokePc(value);
      return true;
    }

  size_t count = tokens.size();
  if ((resource == "t" and count != 6) or (resource != "t" and count != 4))
    {
      std::cerr << "Invalid poke command: " << line << '\n';
      std::cerr << "  Expecting: poke <resource> <address> <value>\n";
      std::cerr << "    or       poke t <number> <value1> <value2> <value3>\n";
      std::cerr << "  where <resource> is one of r, c, or m\n";
      return false;
    }

  const std::string& addrStr = tokens.at(2);
  const std::string& valueStr = tokens.at(3);

  if (not parseCmdLineNumber("poke", valueStr, value))
    return false;

  if (resource == "r")
    {
      unsigned intReg = 0;
      if (core.findIntReg(addrStr, intReg))
	{
	  if (core.pokeIntReg(intReg, value))
	    return true;
	  std::cerr << "Failed to write integer register " << addrStr << '\n';
	  return false;
	}

      std::cerr << "No such integer register " << addrStr << '\n';
      return false;
    }

  if (resource == "f")
    {
      unsigned fpReg = 0;
      if (core.findFpReg(addrStr, fpReg))
	{
	  if (core.pokeFpReg(fpReg, value))
	    return true;
	  std::cerr << "Failed to write FP register " << addrStr << '\n';
	  return false;
	}

      std::cerr << "No such FP register " << addrStr << '\n';
      return false;
    }

  if (resource == "c")
    {
      auto csr = core.findCsr(addrStr);
      if (csr)
	{
	  if (core.pokeCsr(csr->getNumber(), value))
	    return true;
	  std::cerr << "Failed to write CSR " << addrStr << '\n';
	  return false;
	}

      std::cerr << "No such CSR " << addrStr << '\n';
      return false;
    }

  if (resource == "t")
    {
      URV trigger = 0, v1 = 0, v2 = 0, v3 = 0;
      if (not parseCmdLineNumber("trigger", addrStr, trigger))
	return false;
      if (not parseCmdLineNumber("value1", tokens.at(3), v1))
	return false;
      if (not parseCmdLineNumber("value2", tokens.at(4), v2))
	return false;
      if (not parseCmdLineNumber("value3", tokens.at(5), v3))
	return false;
      if (core.pokeTrigger(trigger, v1, v2, v3))
	return true;
      std::cerr << "Trigger out of bounds: " << addrStr << '\n';
      return false;
    }

  if (resource == "m")
    {
      URV addr = 0;
      if (not parseCmdLineNumber("address", addrStr, addr))
	return false;
      if (core.pokeMemory(addr, value))
	return true;
      std::cerr << "Address out of bounds: " << addrStr << '\n';
      return false;
    }

  std::cerr << "No such resource: " << resource <<
    " -- expecting r, c, m or pc\n";
  return false;
}


template <typename URV>
bool
Interactive<URV>::disassCommand(Core<URV>& core, const std::string& line,
				const std::vector<std::string>& tokens)
{
  if (tokens.size() >= 2 and tokens.at(1) == "opcode")
    {
      for (size_t i = 2; i < tokens.size(); ++i)
	{
	  uint32_t code = 0;
	  if (not parseCmdLineNumber("opcode", tokens[i], code))
	    return false;
	  std::string str;
	  core.disassembleInst(code, str);
	  std::cout << "  " << tokens[i] << ":  " << str << '\n';
	}
      return true;
    }

  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val

  if (tokens.size() == 3 and (tokens.at(1) == "func" or
			      tokens.at(1) == "function"))
    {
      std::string item = tokens.at(2);
      std::string name;  // item (if a symbol) or function name containing item
      ElfSymbol symbol;
      if (core.findElfSymbol(item, symbol))
	name = item;
      else
	{
	  // See if address falls in a function, then disassemble function.
	  URV addr = 0;
	  if (not parseCmdLineNumber("address", item, addr))
	    return false;

	  // Find function containing address.
	  core.findElfFunction(addr, name, symbol);
	}

      if (name.empty())
	{
	  std::cerr << "Not a function or an address withing a function: " << item
		    << '\n';
	  return false;
	}

      std::cout << "disassemble function " << name << ":\n";

      size_t start = symbol.addr_, end = symbol.addr_ + symbol.size_;
      for (size_t addr = start; addr < end; )
	{
	  uint32_t inst = 0;
	  if (not core.peekMemory(addr, inst))
	    {
	      std::cerr << "Address out of bounds: 0x" << std::hex << addr << '\n';
	      return false;
	    }

	  unsigned instSize = instructionSize(inst);
	  if (instSize == 2)
	    inst = (inst << 16) >> 16; // Clear top 16 bits.

	  std::string str;
	  core.disassembleInst(inst, str);
	  std::cout << "  " << (boost::format(hexForm) % addr) << ' '
		    << (boost::format(hexForm) % inst) << ' ' << str << '\n';

	  addr += instSize;
	}
      return true;
    }

  if (tokens.size() != 3)
    {
      std::cerr << "Invalid disass command: " << line << '\n';
      std::cerr << "Expecting: disass opcode <number> ...\n";
      std::cerr << "       or: disass function <name>\n";
      std::cerr << "       or: disass function <addr>\n";
      std::cerr << "       or: disass <addr1> <addr2>\n";
      return false;
    }

  URV addr1, addr2;
  if (not parseCmdLineNumber("address", tokens[1], addr1))
    return false;

  if (not parseCmdLineNumber("address", tokens[2], addr2))
    return false;

  for (URV addr = addr1; addr <= addr2; )
    {
      uint32_t inst = 0;
      if (not core.peekMemory(addr, inst))
	{
	  std::cerr << "Address out of bounds: 0x" << std::hex << addr << '\n';
	  return false;
	}

      unsigned instSize = instructionSize(inst);
      if (instSize == 2)
	inst = (inst << 16) >> 16; // Clear top 16 bits.

      std::string str;
      core.disassembleInst(inst, str);
      std::cout << (boost::format(hexForm) % addr) << ' '
		<< (boost::format(hexForm) % inst) << ' '
		<< str << '\n';

      addr += instSize;
    }

  return true;
}


template <typename URV>
bool
Interactive<URV>::elfCommand(Core<URV>& core, const std::string& line,
			     const std::vector<std::string>& tokens)
{
  if (tokens.size() != 2)
    {
      std::cerr << "Invalid elf command: " << line << '\n';
      std::cerr << "Expecting: elf <file-name>\n";
      return false;
    }

  std::string filePath = tokens.at(1);

  size_t entryPoint = 0, exitPoint = 0;

  if (not core.loadElfFile(filePath, entryPoint, exitPoint))
    return false;

  core.pokePc(URV(entryPoint));

  if (exitPoint)
    core.setStopAddress(URV(exitPoint));

  ElfSymbol sym;
  if (core.findElfSymbol("tohost", sym))
    core.setToHostAddress(sym.addr_);

  if (core.findElfSymbol("__whisper_console_io", sym))
    core.setConsoleIo(URV(sym.addr_));

  if (core.findElfSymbol("__global_pointer$", sym))
    core.pokeIntReg(RegGp, URV(sym.addr_));

  if (core.findElfSymbol("_end", sym))   // For newlib emulation.
    core.setTargetProgramBreak(URV(sym.addr_));
  else
    core.setTargetProgramBreak(URV(exitPoint));

  return true;
}


template <typename URV>
bool
Interactive<URV>::hexCommand(Core<URV>& core, const std::string& line,
			     const std::vector<std::string>& tokens)
{
  if (tokens.size() != 2)
    {
      std::cerr << "Invalid hex command: " << line << '\n';
      std::cerr << "Expecting: hex <file-name>\n";
      return false;
    }

  std::string fileName = tokens.at(1);

  if (not core.loadHexFile(fileName))
    return false;

  return true;
}


template <typename URV>
bool
Interactive<URV>::resetCommand(Core<URV>& core, const std::string& /*line*/,
			       const std::vector<std::string>& tokens)
{
  if (tokens.size() == 1)
    {
      core.reset(resetMemoryMappedRegs_);
      return true;
    }

  if (tokens.size() == 2)
    {
      URV resetPc = 0;
      if (not parseCmdLineNumber("reset-pc", tokens[1], resetPc))
	return false;

      core.defineResetPc(resetPc);
      core.reset(resetMemoryMappedRegs_);
      return true;
    }

  std::cerr << "Invalid reset command (extra arguments)\n";
  return false;
}


template <typename URV>
bool
Interactive<URV>::replayFileCommand(const std::string& line,
				    const std::vector<std::string>& tokens,
				    std::ifstream& stream)
{
  if (tokens.size() != 2)
    {
      std::cerr << "Invalid replay_file command: " << line << '\n';
      std::cerr << "Expecting: replay_file <file-name>\n";
      return false;
    }

  std::string fileName = tokens.at(1);

  stream.close();
  stream.open(fileName.c_str());
  if (not stream.good())
    {
      std::cerr << "Failed to open replay-file '" << fileName << "'\n";
      return false;
    }

  return true;
}


template <typename URV>
bool
Interactive<URV>::exceptionCommand(Core<URV>& core, const std::string& line,
				   const std::vector<std::string>& tokens)
{
  bool bad = false;

  URV addr = 0;

  if (tokens.size() < 2)
    bad = true;
  else
    {
      const std::string& tag = tokens.at(1);
      if (tag == "inst")
	{
	  if (tokens.size() == 2)
	    core.postInstAccessFault(0);
	  else if (tokens.size() == 3)
	    {
	      if (parseCmdLineNumber("exception inst offset", tokens.at(2), addr))
		core.postInstAccessFault(addr);
	      else
		bad = true;
	    }
	  else
	    bad = true;
	}

      else if (tag == "data")
	{
	  if (tokens.size() == 2)
	    core.postDataAccessFault(0);
	  else if (tokens.size() == 3)
	    {
	      if (parseCmdLineNumber("exception data offset", tokens.at(2), addr))
		core.postDataAccessFault(addr);
	      else
		bad = true;
	    }
	  else
	    bad = true;
	}

      else if (tag == "store")
	{
	  bad = tokens.size() != 3;
	  if (not bad)
	    {
	      bad = not parseCmdLineNumber("exception store address",
					   tokens.at(2), addr);
	      if (not bad)
		{
		  unsigned matchCount = 0;
		  if (core.applyStoreException(addr, matchCount))
		    return true;
		  std::cerr << "Invalid exception store command: " << line << '\n';
		  if (matchCount == 0)
		    std::cerr << "  No pending store or invalid address\n";
		  else
		    std::cerr << "  Multiple matching addresses (unsupported)\n";
		  return false;
		}
	    }
	}

      else if (tag == "load")
	{
	  bad = tokens.size() != 3;
	  if (not bad)
	    {
	      bad = not parseCmdLineNumber("exception load address",
					   tokens.at(2), addr);
	      if (not bad)
		{
		  unsigned matchCount = 0;
		  if (core.applyLoadException(addr, matchCount))
		    return true;
		  std::cerr << "Invalid exception load command: " << line << '\n';
		  if (matchCount == 0)
		    std::cerr << "  No pending load or invalid address\n";
		  else
		    std::cerr << "  Multiple matching addresses (unsupported)\n";
		  return false;
		}
	    }
	}

      else if (tag == "nmi")
	{
	  bad = tokens.size() != 3;
	  if (not bad)
	    {
	      bad = not parseCmdLineNumber("nmi", tokens.at(2), addr);
	      if (not bad)
		{
		  core.setPendingNmi(NmiCause(addr));
		  return true;
		}
	    }
	}

      else if (tag == "memory_data")
	{
	  if (parseCmdLineNumber("memory_data", tokens.at(2), addr))
	    {
	      return true;
	    }
	}

      else if (tag == "memory_inst")
	{
	  if (parseCmdLineNumber("memory_inst", tokens.at(2), addr))
	    {
	      return true;
	    }
	}

      else
	bad = true;
    }

  if (bad)
    {
      std::cerr << "Invalid exception command: " << line << '\n';
      std::cerr << "  Expecting: exception inst [<offset>]\n";
      std::cerr << "   or:       exception data [<offset>]\n";
      std::cerr << "   or:       exception load <address>\n";
      std::cerr << "   or:       exception store <address>\n";
      std::cerr << "   or:       exception nmi <cause>\n";
      return false;
    }

  return true;
}


template <typename URV>
bool
Interactive<URV>::loadFinishedCommand(Core<URV>& core, const std::string& line,
				      const std::vector<std::string>& tokens)
{
  if (tokens.size() < 2 or tokens.size() > 3)
    {
      std::cerr << "Invalid load_finished command: " << line << '\n';
      std::cerr << "  Expecting: load_finished address [flag]\n";
      return false;
    }

  URV addr = 0;
  if (not parseCmdLineNumber("address", tokens.at(1), addr))
    return false;

  unsigned matchOldest = true;
  if (tokens.size() == 3)
    if (not parseCmdLineNumber("flag", tokens.at(2), matchOldest))
      return false;

  unsigned matches = 0;
  core.applyLoadFinished(addr, matchOldest, matches);

  return true;
}


/// If tokens contain a string of the form hart=<id> then remove that
/// token from tokens and set hartId to <id> returning true. Return
/// false if no hart=<id> token is found or if there is an error (<id>
/// is not an integer value) in which case error is set to true.
static
bool
getCommandHartId(std::vector<std::string>& tokens, unsigned& hartId,
		 bool& error)
{
  error = false;
  if (tokens.empty())
    return false;

  bool hasHart = false;

  // Remaining tokens after removal of hart=<id> tokens.
  std::vector<std::string> rest;

  for (const auto& token : tokens)
    {
      if (boost::starts_with(token, "hart="))
	{
	  std::string value = token.substr(strlen("hart="));
	  try
	    {
	      hartId = boost::lexical_cast<unsigned>(value);
	      hasHart = true;
	    }
	  catch(...)
	    {
	      std::cerr << "Bad hart id: " << value << '\n';
	      error = true;
	      return false;
	    }
	}
      else
	rest.push_back(token);
    }

  tokens = rest;
  return hasHart;
}


/// Interactive "help" command.
static
void
printInteractiveHelp()
{
  using std::cout;
  cout << "The argument hart=<id> may be used with any command.\n";
  cout << "help [<command>]\n";
  cout << "  Print help for given command or for all commands if no command given.\n\n";
  cout << "run\n";
  cout << "  Run till interrupted.\n\n";
  cout << "until <address>\n";
  cout << "  Run until address or interrupted.\n\n";
  cout << "step [<n>]\n";
  cout << "  Execute n instructions (1 if n is missing).\n\n";
  cout << "peek <res> <addr>\n";
  cout << "  Print value of resource res (one of r, f, c, m) and address addr.\n";
  cout << "  For memory (m) up to 2 addresses may be provided to define a range\n";
  cout << "  of memory locations to be printed.\n";
  cout << "  examples: peek r x1   peek c mtval   peek m 0x4096\n\n";
  cout << "peek pc\n";
  cout << "  Print value of the program counter.\n\n";
  cout << "peek all\n";
  cout << "  Print value of all non-memory resources\n\n";
  cout << "poke res addr value\n";
  cout << "  Set value of resource res (one of r, c or m) and address addr\n";
  cout << "  Examples: poke r x1 0xff  poke c 0x4096 0xabcd\n\n";
  cout << "disass opcode <code> <code> ...\n";
  cout << "  Disassemble opcodes. Example: disass opcode 0x3b 0x8082\n\n";
  cout << "disass function <name>\n";
  cout << "  Disassemble function with given name. Example: disas func main\n\n";
  cout << "disass <addr1> <addr2>>\n";
  cout << "  Disassemble memory locations between addr1 and addr2.\n\n";
  cout << "elf file\n";
  cout << "  Load elf file into simulated memory.\n\n";
  cout << "hex file\n";
  cout << "  Load hex file into simulated memory.\n\n";
  cout << "replay_file file\n";
  cout << "  Open command file for replay.\n\n";
  cout << "replay n\n";
  cout << "  Execute the next n commands in the replay file or all the\n";
  cout << "  remaining commands if n is missing.\n\n";
  cout << "replay step n\n";
  cout << "  Execute consecutive commands from the replay file until n\n";
  cout << "  step commands are executed or the file is exhausted\n\n";
  cout << "reset [<reset_pc>]\n";
  cout << "  Reset hart.  If reset_pc is given, then change the reset program\n";
  cout << "  counter to the given reset_pc before resetting the hart.\n\n";
  cout << "quit\n";
  cout << "  Terminate the simulator\n\n";
}


template <typename URV>
void
Interactive<URV>::helpCommand(const std::vector<std::string>& tokens)
{
  using std::cout;

  if (tokens.size() <= 1)
    {
      printInteractiveHelp();
      return;
    }

  auto& tag = tokens.at(1);
  if (tag == "help")
    {
      cout << "help [<command>]\n"
	   << "  Print information about interactive commands. If a command\n"
	   << "  argument is given, print info about that command.\n";
      return;
    }

  if (tag == "run")
    {
      cout << "run\n"
	   << "  Run the target program until it exits (in newlib emulation mode),\n"
	   << "  it writes into the \"tohost\" location, or the user interrupts\n"
	   << "  it by pressing control-c on the keyboard.\n";
      return;
    }

  if (tag == "until")
    {
      cout << "until <address>\n"
	   << "  Same as run but the target program will also stop when the\n"
	   << "  instruction at the given address is reached (but before it is\n"
	   << "  executed).\n";
      return;
    }

  if (tag == "step")
    {
      cout << "step [<n>]\n"
	   << "  Execute a single instruction. If an integer argument <n> is\n"
	   << "  given, then execute up to n instructions or until a stop\n"
	   << "  condition (see run command) is encountered\n";
      return;
    }

  if (tag == "peek")
    {
      cout << "peek <res> <addr>\n"
	   << "peek pc\n"
	   << "  Show contents of given resource having given address. Possible\n"
	   << "  resources are r, f, c, or m for integer, floating-point,\n"
	   << "  control-and-status register or for memory respectively.\n"
	   << "  Addr stands for a register number, register name or memory\n"
	   << "  address. If resource is memory (m), then an additional address\n"
	   << "  may be provided to define a range of memory locations to be\n"
	   << "  display.  Examples\n"
	   << "    peek pc\n"
	   << "    peek r t0\n"
	   << "    peek r x12\n"
	   << "    peek c mtval\n"
	   << "    peek m 0x80000000\n"
	   << "    peek m 0x80000000 0x80000010\n";
      return;
    }

  if (tag == "poke")
    {
      cout << "poke <res> <addr> <value>\n"
	   << "poke pc <value>\n"
	   << "  Set the contents of given resource having given address to the\n"
	   << "  given value. Possible resources are r, f, c, or m for integer,\n"
	   << "  floating-point, control-and-status register or for memory\n"
	   << "  respectively. Addr stands for a register number, register name\n"
	   << "  or memory address.  Examples:\n"
	   << "    poke r t0 0\n"
	   << "    poke r x12 0x44\n"
	   << "    poke c mtval 0xff\n"
	   << "    poke m 0x80000000 0xabdcffff\n";
      return;
    }

  if (tag == "disas")
    {
      cout << "disas opcode <op0> <op1> ...\n"
	   << "disas func <address>\n"
	   << "disas <addr1> <addr2>\n"
	   << "  The first form will disassemble the given opcodes.\n"
	   << "  The second form will disassemble the instructions of the\n"
	   << "  function containing the given address.\n"
	   << "  The third form will disassemble the memory contents between\n"
	   << "  addresses addr1 and addr2 inclusive.\n";
      return;
    }

  if (tag == "elf")
    {
      cout << "elf <file> ...\n"
	   << "  Load into memory the contents of the given ELF file.\n"
	   << "  Set the program counter to the value of the ELF file entry point.\n"
	   << "  If the file contains the symbol \"tohost\" then subsequent writes\n"
	   << "  to the corresponding address will stop the simulation.\n";
      return;
    }

  if (tag == "replay_file")
    {
      cout << "replay_file <file> ...\n"
	   << "  Define the input replay file to serve as input for the replay\n"
	   << "  command. The user would typically load the commands of a session\n"
	   << "  and replays them in a subsequent session.\n";
      return;
    }

  if (tag == "replay")
    {
      cout << "replay [step] [<n>]\n"
	   << "  Without any arguments, replay all remaining commands in the\n"
	   << "  replay file (defined by the replay_file command).\n"
	   << "  With the keyword step, key-in on step commands in the replay\n"
	   << "  file. With an integer number n, replay n commands (or n step\n"
	   << "  commands if step keyword is present).\n";
      return;
    }

  if (tag == "reset")
    {
      cout << "reset [<reset_pc>]\n"
	   << "  Reset simulated processor. If reset_pc is given, then change\n"
	   << "  reset program counter to the given reset_pc before resetting\n"
	   << "  the processor.\n";
      return;
    }

  if (tag == "reset")
    {
      cout << "quit\n"
	   << "  Terminate the simulator.\n";
      return;
    }

  std::cerr << "No such command: " << tag	<< '\n';
}


/// Command line interpreter: Execute a command line.
template <typename URV>
bool
Interactive<URV>::executeLine(unsigned& currentHartId,
			      const std::string& inLine, FILE* traceFile,
			      FILE* commandLog,
			      std::ifstream& replayStream, bool& done)
{
  // Remove comments (anything starting with #).
  std::string line = inLine;
  auto sharpIx = line.find_first_of('#');
  if (sharpIx != std::string::npos)
    line = line.substr(0, sharpIx);

  // Remove leading/trailing white space
  boost::algorithm::trim_if(line, boost::is_any_of(" \t"));

  if (line.empty())
    return true;

  // Break line into tokens.
  std::vector<std::string> tokens;
  boost::split(tokens, line, boost::is_any_of(" \t"),
	       boost::token_compress_on);
  if (tokens.empty())
    return true;

  std::string outLine;   // Line to print on command log.

  // Recover hart id (if any) removing hart=<id> token from tokens.
  unsigned hartId = 0;
  bool error = false;
  bool hasHart = getCommandHartId(tokens, hartId, error);
  if (error)
    return false;

  if (hasHart)
    outLine = line;
  else
    {
      hartId = currentHartId;
      outLine = std::string("hart=") + std::to_string(hartId) + " " + line;
    }

  if (hartId >= cores_.size())
    {
      std::cerr << "Hart id out of bounds: " << hartId << '\n';
      return false;
    }

  Core<URV>& core = *(cores_.at(hartId));

  const std::string& command = tokens.front();

  if (command != "reset")
    resetMemoryMappedRegs_ = true;

  if (command == "run")
    {
      bool success = core.run(traceFile);
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return success;
    }

  if (command == "u" or command == "until")
    {
      if (not untilCommand(core, line, tokens, traceFile))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "s" or command == "step")
    {
      if (core.inDebugMode() and not core.inDebugStepMode())
	{
	  std::cerr << "Error: Single step while in debug-halt mode\n";
	  return false;
	}
      if (not stepCommand(core, line, tokens, traceFile))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "peek")
    {
      if (not peekCommand(core, line, tokens))
	return false;
       if (commandLog)
	 fprintf(commandLog, "%s\n", outLine.c_str());
       return true;
    }

  if (command == "poke")
    {
      if (not pokeCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "d" or command == "disas")
    {
      if (not disassCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "elf")
    {
      if (not elfCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "hex")
    {
      if (not hexCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "q" or command == "quit")
    {
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      done = true;
      return true;
    }

  if (command == "reset")
    {
      if (not resetCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "exception")
    {
      if (not exceptionCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "enter_debug")
    {
      core.enterDebugMode(core.peekPc());
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "exit_debug")
    {
      core.exitDebugMode();
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "load_finished")
    {
      if (not loadFinishedCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", outLine.c_str());
      return true;
    }

  if (command == "replay_file")
    {
      if (not replayFileCommand(line, tokens, replayStream))
	return false;
      return true;
    }

  if (command == "replay")
    {
      if (not replayStream.is_open())
	{
	  std::cerr << "No replay file defined. Use the replay_file to define one\n";
	  return false;
	}
      if (not replayCommand(currentHartId, line, tokens, traceFile, commandLog,
			    replayStream, done))
	return false;
      return true;
    }

  if (command == "symbols")
    {
      core.printElfSymbols(std::cout);
      return true;
    }

  if (command == "h" or command == "?" or command == "help")
    {
      helpCommand(tokens);
      return true;
    }

  std::cerr << "No such command: " << line << '\n';
  return false;
}


/// Interactive "replay" command.
template <typename URV>
bool
Interactive<URV>::replayCommand(unsigned& currentHartId,
				const std::string& line,
				const std::vector<std::string>& tokens,
				FILE* traceFile, FILE* commandLog,
				std::ifstream& replayStream, bool& done)
{
  std::string replayLine;
  uint64_t maxCount = ~uint64_t(0);  // Unlimited

  if (tokens.size() <= 2)    // Either replay or replay n.
    {
      if (tokens.size() == 2)
	if (not parseCmdLineNumber("command-count", tokens.at(1), maxCount))
	  return false;

      uint64_t count = 0;
      while (count < maxCount  and  not done  and
	     std::getline(replayStream, replayLine))
	{
	  if (not executeLine(currentHartId, replayLine, traceFile,
			      commandLog, replayStream, done))
	    return false;
	  count++;
	}
      return true;
    }

  if (tokens.size() == 3)
    {
      if (tokens.at(1) != "step")
	{
	  std::cerr << "Invalid command: " << line << '\n';
	  std::cerr << "Expecting: replay <step> <count>\n";
	  return false;
	}

      if (not parseCmdLineNumber("step-count", tokens.at(2), maxCount))
	return false;
      
      uint64_t count = 0;
      while (count < maxCount  and  not done   and
	     std::getline(replayStream, replayLine))
	{
	  if (not executeLine(currentHartId, replayLine, traceFile,
			      commandLog, replayStream, done))
	    return false;

	  std::vector<std::string> tokens;
	  boost::split(tokens, replayLine, boost::is_any_of(" \t"),
		       boost::token_compress_on);
	  if (tokens.size() > 0 and tokens.at(0) == "step")
	    count++;
	  else if (tokens.size() > 1 and tokens.at(1) == "step")
	    count++;
	}

      return true;
    }

  std::cerr << "Invalid command: " << line << '\n';
  std::cerr << "Expecting: replay, replay <count>, or replay step <count>\n";
  return false;    
}


template <typename URV>
bool
Interactive<URV>::interact(FILE* traceFile, FILE* commandLog)
{
  linenoise::SetHistoryMaxLen(1024);

  uint64_t errors = 0;
  unsigned currentHartId = 0;
  std::string replayFile;
  std::ifstream replayStream;

  const char* prompt = isatty(0) ? "whisper> " : "";

  bool done = false;
  while (not done)
    {
      errno = 0;
      std::string line = linenoise::Readline(prompt);

      if (line.empty())
	{
	  if (errno == EAGAIN)
	    continue;
	  return true;
	}

      linenoise::AddHistory(line.c_str());

      if (not executeLine(currentHartId, line, traceFile, commandLog,
			  replayStream, done))
	errors++;
    }

  return errors == 0;
}


template class WdRiscv::Interactive<uint32_t>;
template class WdRiscv::Interactive<uint64_t>;
