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
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include "CoreConfig.hpp"
#include "WhisperMessage.h"
#include "Core.hpp"
#include "linenoise.h"


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
      if (sizeof(TYPE) == 4)
	number = strtoul(numberStr.c_str(), &end, 0);
      else if (sizeof(TYPE) == 8)
	number = strtoull(numberStr.c_str(), &end, 0);
      else
	{
	  std::cerr << "parseCmdLineNumber: Only 32/64-bit RISCV cores supported\n";
	  return false;
	}
      if (end and *end)
	good = false;  // Part of the string are non parseable.
    }

  if (not good)
    std::cerr << "Invalid command line " << option << " value: " << numberStr << '\n';
  return good;
}


/// Hold values provided on the command line.
struct Args
{
  std::string hexFile;         // Hex file to be loaded into simulator memory.
  std::string traceFile;       // Log of state change after each instruction.
  std::string commandLogFile;  // Log of interactive or socket commands.
  std::string consoleOutFile;  // Console io output file.
  std::string serverFile;      // File in which to write server host and port.
  std::string instFreqFile;    // Instruction frequency file.
  std::string configFile;      // Configuration (JSON) file.
  std::string isa;
  std::vector<std::string> regInits; // Initial values of regs
  std::vector<std::string> codes;    // Instruction codes to disassemble
  std::vector<std::string> target;   // Target (ELF file) program and associated
                                     // program options to be loaded into simulator
                                     // memeory.

  uint64_t startPc = 0;
  uint64_t endPc = 0;
  uint64_t toHost = 0;
  uint64_t consoleIo = 0;
  uint64_t instCountLim = ~uint64_t(0);
  
  unsigned regWidth = 32;

  bool help = false;
  bool hasStartPc = false;
  bool hasEndPc = false;
  bool hasToHost = false;
  bool hasConsoleIo = false;
  bool hasRegWidth = false;
  bool trace = false;
  bool interactive = false;
  bool verbose = false;
  bool version = false;
  bool traceLoad = false;  // Trace load address if true.
  bool triggers = false;   // Enable debug triggers when true.
  bool counters = false;   // Enable peformance counters when true.
  bool gdb = false;        // Enable gdb mode when true.
  bool abiNames = false;   // Use ABI register names in inst disassembly.
  bool emulateLinux = false;
};


/// Pocess command line arguments. Place option values in args.  Set
/// help to true if "--help" is used. Return true on success and false
/// on failure.
static
bool
parseCmdLineArgs(int argc, char* argv[], Args& args)
{
  std::string toHostStr, startPcStr, endPcStr;

  unsigned errors = 0;

  try
    {
      // Define command line options.
      namespace po = boost::program_options;
      po::options_description desc("options");
      desc.add_options()
	("help,h", po::bool_switch(&args.help),
	 "Produce this message.")
	("log,l", po::bool_switch(&args.trace),
	 "Enable tracing to standard output of executed instructions.")
	("isa", po::value(&args.isa),
	 "Specify instruction set architecture options (currently no-op).")
	("xlen", po::value(&args.regWidth),
	 "Specify register width (32 or 64), defaults to 32")
	("target,t", po::value(&args.target)->multitoken(),
	 "Target program (ELF file) to load into simulator memory. In linux emulations mode, program options may follow prgram name.")
	("hex,x", po::value(&args.hexFile),
	 "HEX file to load into simulator memory.")
	("logfile,f", po::value(&args.traceFile),
	 "Enable tracing to given file of executed instructions.")
	("consoleoutfile", po::value(&args.consoleOutFile),
	 "Redirect console output to given file.")
	("commandlog", po::value(&args.commandLogFile),
	 "Enable logging of interactive/socket commands to the given file.")
	("server", po::value(&args.serverFile),
	 "Interactive server mode. Put server hostname and port in file.")
	("startpc,s", po::value<std::string>(),
	 "Set program entry point (in hex notation with a 0x prefix). "
	 "If not specified, use the ELF file entry point.")
	("endpc,e", po::value<std::string>(),
	 "Set stop program counter (in hex notation with a 0x prefix). "
	 "Simulator will stop once instruction at the stop program counter "
	 "is executed. If not specified, use the ELF file _finish symbol.")
	("tohost,o", po::value<std::string>(),
	 "Memory address to which a write stops simulator (in hex with "
	 "0x prefix).")
	("consoleio", po::value<std::string>(),
	 "Memory address corresponding to console io (in hex with "
	 "0x prefix). Reading/writing a byte (lb/sb) from given address "
	 "reads/writes a byte from the console.")
	("maxinst,m", po::value(&args.instCountLim),
	 "Limit executed instruction count to limit.")
	("interactive,i", po::bool_switch(&args.interactive),
	 "Enable interacive mode.")
	("traceload", po::bool_switch(&args.traceLoad),
	 "Enable tracing of load instruction data address.")
	("triggers", po::bool_switch(&args.triggers),
	 "Enable debug triggers (triggers are on in interactive and server modes)")
	("counters", po::bool_switch(&args.counters),
	 "Enable performance counters")
	("gdb", po::bool_switch(&args.gdb),
	 "Run in gdb mode enabling remote debugging from gdb.")
	("profileinst", po::value(&args.instFreqFile),
	 "Report instruction frequency to file.")
	("setreg", po::value(&args.regInits)->multitoken(),
	 "Initialize registers. Exampple --setreg x1=4 x2=0xff")
	("disass,d", po::value(&args.codes)->multitoken(),
	 "Disassemble instruction code(s). Example --disass 0x93 0x33")
	("configfile", po::value(&args.configFile),
	 "Configuration file (JSON file defining system features).")
	("abinames,v", po::bool_switch(&args.abiNames),
	 "Use ABI register names (e.g. sp instead of x2) in instruction disassembly.")
	("emulatelinux", po::bool_switch(&args.emulateLinux),
	 "Emulate (some) linux system calls when true.")
	("verbose,v", po::bool_switch(&args.verbose),
	 "Be verbose.")
	("version", po::bool_switch(&args.version),
	 "Print version.");

      // Define positional options.
      po::positional_options_description pdesc;
      pdesc.add("target", -1);

      // Parse command line options.
      po::variables_map varMap;
      po::store(po::command_line_parser(argc, argv)
		.options(desc)
		.positional(pdesc)
		.run(), varMap);
      po::notify(varMap);

      if (args.help)
	{
	  std::cout <<
	    "Simulate a RISCV system running the program specified by the given ELF\n"
	      "and/or HEX file. With --emulatelinux, the ELF file is a Linux program\n"
	      "and may be followed by corresponding command line arguments, in which\n"
	      "case it is best to put program and arguments following a double dash.\n"
	      "Examples:\n"
	      "  whisper --target prog --log\n"
	      "  whisper --emulatelinux --log -- prog -x -y\n\n";
	  std::cout << desc;
	  return true;
	}

      // Collect command line values.
      if (not args.isa.empty())
	std::cerr << "Warning: --isa command line option currently ignored\n";
      if (varMap.count("startpc"))
	{
	  auto startStr = varMap["startpc"].as<std::string>();
	  args.hasStartPc = parseCmdLineNumber("startpc", startStr, args.startPc);
	  if (not args.hasStartPc)
	    errors++;
	}
      if (varMap.count("endpc"))
	{
	  auto endStr = varMap["endpc"].as<std::string>();
	  args.hasEndPc = parseCmdLineNumber("endpc", endStr, args.endPc);
	  if (not args.hasEndPc)
	    errors++;
	}
      if (varMap.count("tohost"))
	{
	  auto addrStr = varMap["tohost"].as<std::string>();
	  args.hasToHost = parseCmdLineNumber("tohost", addrStr, args.toHost);
	  if (not args.hasToHost)
	    errors++;
	}
      if (varMap.count("consoleio"))
	{
	  auto consoleIoStr = varMap["consoleio"].as<std::string>();
	  args.hasConsoleIo = parseCmdLineNumber("consoleio", consoleIoStr,
						 args.consoleIo);
	  if (not args.hasConsoleIo)
	    errors++;
	}
      if (varMap.count("xlen"))
	args.hasRegWidth = true;
      if (args.interactive)
	args.trace = true;  // Enable instruction tracing in interactive mode.
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return false;
    }

  return errors == 0;
}


/// Apply register initializations specified on the command line.
template<typename URV>
static
bool
applyCmdLineRegInit(const Args& args, Core<URV>& core)
{
  bool ok = true;

  for (const auto& regInit : args.regInits)
    {
      // Each register initialization is a string of the form reg=val
      std::vector<std::string> tokens;
      boost::split(tokens, regInit, boost::is_any_of("="),
		   boost::token_compress_on);
      if (tokens.size() != 2)
	{
	  std::cerr << "Invalid command line register intialization: "
		    << regInit << '\n';
	  ok = false;
	  continue;
	}

      const std::string& regName = tokens.at(0);
      const std::string& regVal = tokens.at(1);

      URV val = 0;
      if (not parseCmdLineNumber("register", regVal, val))
	{
	  ok = false;
	  continue;
	}

      if (unsigned reg = 0; core.findIntReg(regName, reg))
	{
	  core.pokeIntReg(reg, val);
	  continue;
	}

      auto csr = core.findCsr(regName);
      if (csr)
	{
	  core.pokeCsr(csr->getNumber(), val);
	  continue;
	}

      std::cerr << "No such RISCV register: " << regVal << '\n';
      ok = false;
    }

  return ok;
}


std::unordered_map<std::string, ElfSymbol> elfSymbols;


template<typename URV>
static
bool
loadElfFile(Core<URV>& core, const std::string& filePath)
{
  size_t entryPoint = 0, exitPoint = 0;

  if (not core.loadElfFile(filePath, entryPoint, exitPoint, elfSymbols))
    return false;

  core.pokePc(entryPoint);

  if (exitPoint)
    core.setStopAddress(exitPoint);

  if (elfSymbols.count("tohost"))
    core.setToHostAddress(elfSymbols.at("tohost").addr_);

  if (elfSymbols.count("__whisper_console_io"))
    core.setConsoleIo(elfSymbols.at("__whisper_console_io").addr_);

  if (elfSymbols.count("__global_pointer$"))
    core.pokeIntReg(RegGp, elfSymbols.at("__global_pointer$").addr_);

  if (elfSymbols.count("_end"))   // For linux emulation.
    core.setTargetProgramBreak(elfSymbols.at("_end").addr_);
  else
    core.setTargetProgramBreak(exitPoint);

  return true;
}


/// Apply command line arguments: Load ELF and HEX files, set
/// start/end/tohost. Return true on success and false on failure.
template<typename URV>
static
bool
applyCmdLineArgs(const Args& args, Core<URV>& core)
{
  unsigned errors = 0;

  if (not args.target.empty())
    {
      std::string elfFile = args.target.front();
      if (args.verbose)
	std::cerr << "Loading ELF file " << elfFile << '\n';
      if (not loadElfFile(core, elfFile))
	errors++;
    }

  if (not args.hexFile.empty())
    {
      if (args.verbose)
	std::cerr << "Loading HEX file " << args.hexFile << '\n';
      if (not core.loadHexFile(args.hexFile))
	errors++;
    }

  if (not args.instFreqFile.empty())
    core.enableInstructionFrequency(true);

  // Command line to-host overrides that of ELF and config file.
  if (args.hasToHost)
    core.setToHostAddress(args.toHost);

  // Command-line entry point overrides that of ELF.
  if (args.hasStartPc)
    core.pokePc(args.startPc);

  // Command-line exit point overrides that of ELF.
  if (args.hasEndPc)
    core.setStopAddress(args.endPc);

  // Command-line console io address overrides config file.
  if (args.hasConsoleIo)
    core.setConsoleIo(args.consoleIo);

  // Set instruction count limit.
  core.setInstructionCountLimit(args.instCountLim);

  // Print load-instruction data-address when tracing instructions.
  core.setTraceLoad(args.traceLoad);

  core.enableTriggers(args.triggers);
  core.enableGdb(args.gdb);
  core.enablePerformanceCounters(args.counters);
  core.enableAbiNames(args.abiNames);
  core.enableLinuxEmulation(args.emulateLinux);

  // Apply register intialization.
  if (not applyCmdLineRegInit(args, core))
    errors++;

  if (args.emulateLinux)
    {
      if (not core.setTargetProgramArgs(args.target))
	{
	  std::cerr << "Failed to setup target program arguments -- stack"
		    << " is not writeable\n";
	  errors++;
	}
    }
  else if (args.target.size() > 1)
    {
      std::cerr << "Warning: Target program options present but that requires\n"
		<< "         --emulatelinux. Options ignored.\n";
    }

  return errors == 0;
}


/// Interactive "until" command.
template <typename URV>
static
bool
untilCommand(Core<URV>& core, const std::string& line,
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


/// Interactive "step" command.
template <typename URV>
static
bool
stepCommand(Core<URV>& core, const std::string& line,
	    const std::vector<std::string>& tokens, FILE* traceFile)
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
  for (size_t i = 0; i < core.fpRegCount(); ++i)
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

  for (size_t i = 0; i < core.intRegCount(); ++i)
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
	      std::cout << "trigger" << std::dec << trigger << ':';
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


/// Interactive "peek" command.
template <typename URV>
static
bool
peekCommand(Core<URV>& core, const std::string& line,
	    const std::vector<std::string>& tokens)
{
  if (tokens.size() < 2)
    {
      std::cerr << "Invalid peek command: " << line << '\n';
      std::cerr << "Expecting: peek <item> <addr>  or  peek pc  or  peek all\n";
      std::cerr << "  Item is one of r, f, c, t or m for integer, floating point,\n";
      std::cerr << "  CSR, trigger register or memory location respectivey\n";

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
      URV addr = 0;
      if (not parseCmdLineNumber("memory-address", addrStr, addr))
	return false;
      if (core.peekMemory(addr, val))
	{
	  std::cout << (boost::format(hexForm) % val) << std::endl;
	  return true;
	}
      std::cerr << "Memory address out of bounds: " << addrStr << '\n';
      return false;
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
      if (core.isRvf())
	{
	  std::cerr << "Floting point extension is no enabled\n";
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
	  std::cout << (boost::format("0x%016x") % val) << std::endl;
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


/// Interactive "poke" command.
template <typename URV>
static
bool
pokeCommand(Core<URV>& core, const std::string& line,
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
      if (not parseCmdLineNumber("value", tokens.at(2), value))
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

  if (not parseCmdLineNumber("value", valueStr, value))
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


/// Interactive "disassemble" command.
template <typename URV>
static
bool
disassCommand(Core<URV>& core, const std::string& line,
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

  if (tokens.size() == 3 and (tokens.at(1) == "func" or tokens.at(1) == "function"))
    {
      std::string item = tokens.at(2);
      std::string name;  // item (if a symbol) or function name containing item
      ElfSymbol symbol;
      if (elfSymbols.count(item))
	{
	  name = item;
	  symbol = elfSymbols.at(item);
	}
      else
	{
	  // See if address falls in a function, then disassemble function.
	  URV addr = 0;
	  if (not parseCmdLineNumber("address", item, addr))
	    return false;

	  for (const auto& kv : elfSymbols)
	    {
	      auto& sym = kv.second;
	      size_t start = sym.addr_, end = sym.addr_ + sym.size_;
	      if (addr >= start and addr < end)
		{
		  name = kv.first;
		  symbol = sym;
		}
	    }
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


/// Interactive "elf" command.
template <typename URV>
static
bool
elfCommand(Core<URV>& core, const std::string& line,
	   const std::vector<std::string>& tokens)
{
  if (tokens.size() != 2)
    {
      std::cerr << "Invalid elf command: " << line << '\n';
      std::cerr << "Expecting: elf <file-name>\n";
      return false;
    }

  std::string fileName = tokens.at(1);
  return loadElfFile(core, fileName);
}


/// Interactive "hex" command.
template <typename URV>
static
bool
hexCommand(Core<URV>& core, const std::string& line,
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


/// Interactive "reset" command.
template <typename URV>
static
bool
resetCommand(Core<URV>& core, const std::string& line,
	     const std::vector<std::string>& tokens)
{
  if (tokens.size() == 1)
    {
      core.reset();
      return true;
    }

  if (tokens.size() == 2)
    {
      uint32_t resetPc = 0;
      if (not parseCmdLineNumber("reset-pc", tokens[1], resetPc))
	return false;

      core.defineResetPc(resetPc);
      core.reset();
      return true;
    }

  std::cerr << "Invalid reset command (extra arguments)\n";
  return false;
}


/// Interactive "replay_file" command.
static
bool
replayFileCommand(const std::string& line,
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


/// Interactive "exception" command.
template <typename URV>
static
bool
exceptionCommand(Core<URV>& core, const std::string& line,
		 const std::vector<std::string>& tokens)
{
  bool bad = false;

  if (tokens.size() == 2)
    {
      const std::string& tag = tokens.at(1);
      if (tag == "inst")
	core.postInstAccessFault();
      else if (tag == "data")
	core.postDataAccessFault();
      else
	bad = true;
    }
  else if (tokens.size() == 3)
    {
      const std::string& tag = tokens.at(1);
      URV addr = 0;
      if (tag == "store")
	{
	  if (parseCmdLineNumber("store", tokens.at(2), addr))
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
      if (tag == "load")
	{
	  if (parseCmdLineNumber("load", tokens.at(2), addr))
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
      else if (tag == "nmi")
	{
	  if (parseCmdLineNumber("nmi", tokens.at(2), addr))
	    {
	      core.setPendingNmi(NmiCause(addr));
	      return true;
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
      bad = true;
    }
  else
    bad = true;

  if (bad)
    {
      std::cerr << "Invalid exception command: " << line << '\n';
      std::cerr << "  Expecting: exception inst|data\n";
      std::cerr << "   or:       exception store <address>\n";
      std::cerr << "   or:       exception nmi <cause>\n";
      return false;
    }

  return true;
}


/// Unpack socket message (recevied in server mode) into the given
/// WhisperMessage object.
void
deserializeMessage(const char buffer[], size_t bufferLen,
		   WhisperMessage& msg)

{
  assert (bufferLen >= sizeof(msg));

  const char* p = buffer;
  uint32_t x = ntohl(*((uint32_t*)p));
  msg.hart = x;
  p += sizeof(x);

  x = ntohl(*((uint32_t*)p));
  msg.type = x;
  p += sizeof(x);

  x = ntohl(*((uint32_t*)p));
  msg.resource = x;
  p += sizeof(x);

  uint32_t part = ntohl(*((uint32_t*)p));
  msg.address = uint64_t(part) << 32;
  p += sizeof(part);

  part = ntohl(*((uint32_t*)p));
  msg.address |= part;
  p += sizeof(part);

  part = ntohl(*((uint32_t*)p));
  msg.value = uint64_t(part) << 32;
  p += sizeof(part);

  part = ntohl(*((uint32_t*)p));
  msg.value |= part;
  p += sizeof(part);

  memcpy(msg.buffer, p, sizeof(msg.buffer));
  p += sizeof(msg.buffer);

  assert(size_t(p - buffer) <= bufferLen);
}


/// Serialize the given WhisperMessage into the given buffer in
/// prepearation for socket send. Return the number of bytes written
/// into buffer.
size_t
serializeMessage(const WhisperMessage& msg, char buffer[],
		 size_t bufferLen)
{
  assert (bufferLen >= sizeof(msg));

  char* p = buffer;
  uint32_t x = htonl(msg.hart);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  x = htonl(msg.type);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  x = htonl(msg.resource);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  uint32_t part = msg.address >> 32;
  x = htonl(part);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  part = msg.address;
  x = htonl(part);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  part = msg.value >> 32;
  x = htonl(part);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  part = msg.value;
  x = htonl(part);
  memcpy(p, &x, sizeof(x));
  p += sizeof(x);

  memcpy(p, msg.buffer, sizeof(msg.buffer));
  p += sizeof(msg.buffer);

  size_t len = p - buffer;
  assert(len < bufferLen);
  assert(len <= sizeof(msg));
  for (size_t i = len; i < sizeof(msg); ++i)
    buffer[i] = 0;

  return sizeof(msg);
}


static bool
receiveMessage(int soc, WhisperMessage& msg)
{
  char buffer[sizeof(msg)];
  char* p = buffer;

  size_t remain = sizeof(msg);

  while (remain > 0)
    {
      ssize_t l = recv(soc, p, remain, 0);
      if (l < 0)
	{
	  if (errno == EINTR)
	    continue;
	  std::cerr << "Failed to receive socket message\n";
	  return false;
	}
      if (l == 0)
	{
	  msg.type = Quit;
	  return true;
	}
      remain -= l;
      p += l;
    }

  deserializeMessage(buffer, sizeof(buffer), msg);

  return true;
}


static bool
sendMessage(int soc, WhisperMessage& msg)
{
  char buffer[sizeof(msg)];

  serializeMessage(msg, buffer, sizeof(buffer));

  // Send command.
  ssize_t remain = sizeof(msg);
  char* p = buffer;
  while (remain > 0)
    {
      ssize_t l = send(soc, p, remain , 0);
      if (l < 0)
	{
	  if (errno == EINTR)
	    continue;
	  std::cerr << "Failed to send socket command\n";
	  return false;
	}
      remain -= l;
      p += l;
    }

  return true;
}


/// Server mode poke command.
template <typename URV>
static
bool
pokeCommand(Core<URV>& core, const WhisperMessage& req, WhisperMessage& reply)
{
  reply = req;

  switch (req.resource)
    {
    case 'r':
      if (core.pokeIntReg(req.address, req.value))
	return true;
      break;
    case 'c':
      if (core.pokeCsr(CsrNumber(req.address), req.value))
	return true;
      break;
    case 'm':
      if (sizeof(URV) == 4)
	{
	  // Poke a word in 32-bit cores.
	  if (core.pokeMemory(req.address, uint32_t(req.value)))
	    return true;
	}
      else if (core.pokeMemory(req.address, req.value))
	return true;
      break;
    }

  reply.type = Invalid;
  return false;
}


/// Server mode peek command.
template <typename URV>
static
bool
peekCommand(Core<URV>& core, const WhisperMessage& req, WhisperMessage& reply)
{
  reply = req;

  URV value;

  switch (req.resource)
    {
    case 'r':
      if (core.peekIntReg(req.address, value))
	{
	  reply.value = value;
	  return true;
	}
      break;
    case 'f':
      {
	uint64_t fpVal = 0;
	if (core.peekFpReg(req.address, fpVal))
	  {
	    reply.value = fpVal;
	    return true;
	  }
      }
      break;
    case 'c':
      if (core.peekCsr(CsrNumber(req.address), value))
	{
	  reply.value = value;
	  return true;
	}
      break;
    case 'm':
      if (core.peekMemory(req.address, value))
	{
	  reply.value = value;
	  return true;
	}
      break;
    }

  reply.type = Invalid;
  return true;
}


/// Server mode disassemble command.
template <typename URV>
static
void
disassembleAnnotateInst(Core<URV>& core, uint32_t inst, bool interrupted,
			bool hasPreTrigger, bool hasPostTrigger,
			std::string& text)
{
  core.disassembleInst(inst, text);
  uint32_t op0 = 0, op1 = 0; int32_t op2 = 0;
  const InstInfo& info = core.decode(inst, op0, op1, op2);
  if (info.isBranch())
    {
      if (core.lastPc() + instructionSize(inst) != core.peekPc())
       text += " (T)";
      else
       text += " (NT)";
    }
  if (info.isLoad())
    {
      URV addr = 0;
      core.peekIntReg(op1, addr);
      addr += op2;
      std::ostringstream oss;
      oss << " [0x" << std::hex << addr << "]";
      text += oss.str();
    }
  if (interrupted)
    text += " (interrupted)";
  else if (hasPreTrigger)
    text += " (pre-trigger)";
  else if (hasPostTrigger)
    text += " (post-trigger)";
}


/// Process changes of a single-step commmand. Put the changes in the
/// pendingChanges vector (which is cleared on entry). Put the number
/// of change record in the reply parameter along with the instruction
/// address, opcode and assembly text. Use hasPre (instruction tripped
/// a "before" trigger), hasPost (tripped an "after" trigger) and
/// interrupted (instruction encoutered an external interrupt) to
/// annotate the assembly text.
template <typename URV>
static
void
processStepCahnges(Core<URV>& core, std::vector<WhisperMessage>& pendingChanges,
		   bool interrupted, bool hasPre, bool hasPost,
		   WhisperMessage& reply, FILE* traceFile)
{
  // Get executed instruction.
  URV pc = core.lastPc();
  uint32_t inst = 0;
  core.readInst(pc, inst);

  // Add pc and instruction to reply.
  reply.type = ChangeCount;
  reply.address = pc;
  reply.resource = inst;

  // Add disassembly of instruction to reply.
  std::string text;
  disassembleAnnotateInst(core, inst, interrupted, hasPre, hasPost, text);

  strncpy(reply.buffer, text.c_str(), sizeof(reply.buffer) - 1);
  reply.buffer[sizeof(reply.buffer) -1] = 0;

  // Collect integer register change caused by execution of instruction.
  pendingChanges.clear();
  int regIx = core.lastIntReg();
  if (regIx > 0)
    {
      URV value;
      if (core.peekIntReg(regIx, value))
	{
	  WhisperMessage msg;
	  msg.type = Change;
	  msg.resource = 'r';
	  msg.address = regIx;
	  msg.value = value;
	  pendingChanges.push_back(msg);
	}
    }

  // Collect CSR and trigger changes.
  std::vector<CsrNumber> csrs;
  std::vector<unsigned> triggers;
  core.lastCsr(csrs, triggers);

  // Map to keep CSRs in order and to drop duplicate entries.
  std::map<URV,URV> csrMap;

  // Components of the triggers that changed (if any).
  std::vector<bool> tdataChanged(3);

  // Collect changed CSRs and their values. Collect components of
  // changed trigger.
  for (CsrNumber csr : csrs)
    {
      URV value;
      if (core.peekCsr(csr, value))
	{
	  if (csr >= CsrNumber::TDATA1 and csr <= CsrNumber::TDATA3)
	    {
	      size_t ix = size_t(csr) - size_t(CsrNumber::TDATA1);
	      tdataChanged.at(ix) = true;
	    }
	  else
	    csrMap[URV(csr)] = value;
	}
    }

  // Collect changes associated with trigger register.
  for (unsigned trigger : triggers)
    {
      URV data1(0), data2(0), data3(0);
      if (not core.peekTrigger(trigger, data1, data2, data3))
	continue;
      if (tdataChanged.at(0))
	{
	  URV addr = (trigger << 16) | unsigned(CsrNumber::TDATA1);
	  csrMap[addr] = data1;
	}
      if (tdataChanged.at(1))
	{
	  URV addr = (trigger << 16) | unsigned(CsrNumber::TDATA2);
	  csrMap[addr] = data2;
	}
      if (tdataChanged.at(2))
	{
	  URV addr = (trigger << 16) | unsigned(CsrNumber::TDATA3);
	  csrMap[addr] = data3;
	}
    }

  for (const auto& [key, val] : csrMap)
    {
      WhisperMessage msg(0, Change, 'c', key, val);
      pendingChanges.push_back(msg);
    }

  std::vector<size_t> addresses;
  std::vector<uint32_t> words;

  core.lastMemory(addresses, words);
  assert(addresses.size() == words.size());

  for (size_t i = 0; i < addresses.size(); ++i)
    {
      WhisperMessage msg(0, Change, 'm', addresses.at(i), words.at(i));
      pendingChanges.push_back(msg);
    }

  // Add count of changes to reply.
  reply.value = pendingChanges.size();

  // The changes will be retreived one at a time from the back of the
  // pendigChanges vector: Put the vector in reverse order. Changes
  // are retrieved using a Change request (see interactUsingSocket).
  std::reverse(pendingChanges.begin(), pendingChanges.end());
}


/// Server mode step command.
template <typename URV>
static
bool
stepCommand(Core<URV>& core, const WhisperMessage& req, 
	    std::vector<WhisperMessage>& pendingChanges,
	    WhisperMessage& reply,
	    FILE* traceFile)
{
  // Execute instruction. Determine if an interrupt was taken or if a
  // trigger got tripped.
  uint64_t interruptCount = core.getInterruptCount();

  core.singleStep(traceFile);

  bool interrupted = core.getInterruptCount() != interruptCount;

  unsigned preCount = 0, postCount = 0;
  core.countTrippedTriggers(preCount, postCount);

  bool hasPre = preCount > 0;
  bool hasPost = postCount > 0;

  processStepCahnges(core, pendingChanges, interrupted, hasPre,
		     hasPost, reply, traceFile);

  core.clearTraceData();
  return true;
}


/// Server mode exception command.
template <typename URV>
static
bool
exceptionCommand(Core<URV>& core, const WhisperMessage& req, 
		 WhisperMessage& reply, FILE* traceFile,
		 std::string& text)
{
  std::ostringstream oss;
  bool ok = true;

  reply = req;

  WhisperExceptionType expType = WhisperExceptionType(req.value);
  switch (expType)
    {
    case InstAccessFault:
      core.postInstAccessFault();
      oss << "exception inst";
      break;

    case DataAccessFault:
      core.postDataAccessFault();
      oss << "exception data";
      break;

    case ImpreciseStoreFault:
      {
	URV addr = req.address;
	unsigned matchCount;
	ok = core.applyStoreException(addr, matchCount);
	reply.value = matchCount;
	oss << "exception store 0x" << std::hex << addr;
      }
      break;

    case ImpreciseLoadFault:
      {
	URV addr = req.address;
	unsigned matchCount;
	ok = core.applyLoadException(addr, matchCount);
	reply.value = matchCount;
	oss << "exception load 0x" << std::hex << addr;
      }
      break;

    case NonMaskableInterrupt:
      {
	URV addr = req.address;
	core.setPendingNmi(NmiCause(addr));
	oss << "exception nmi 0x" << std::hex << addr;
      }
      break;

    case DataMemoryError:
      {
	URV addr = req.address;
	oss << "exception memory_data 0x" << std::hex << addr;
	ok = false;
      }
      break;

    case InstMemoryError:
      {
	URV addr = req.address;
	oss << "exception memory_inst 0x" << std::hex << addr;
	ok = false;
      }
      break;

    default:
      {
	URV addr = req.address;
	oss << "exception ? 0x" << std::hex << addr;
	ok = false;
      }
      break;
    }

  if (not ok)
    reply.type = Invalid;

  text = oss.str();
  return ok;
}


/// Server mode loop: Receive command and send reply till a quit
/// command is received. Return true on successful termination (quit
/// received). Rturn false otherwise.
template <typename URV>
static
bool
interactUsingSocket(Core<URV>& core, int soc, FILE* traceFile, FILE* commandLog)
{
  std::vector<WhisperMessage> pendingChanges;

  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val

  while (true)
    {
      WhisperMessage msg;
      WhisperMessage reply;
      if (not receiveMessage(soc, msg))
	return false;

      switch (msg.type)
	{
	case Quit:
	  if (commandLog)
	    fprintf(commandLog, "quit\n");
	  return true;

	case Poke:
	  pokeCommand(core, msg, reply);
	  if (commandLog)
	    fprintf(commandLog, "poke %c %s %s\n", msg.resource,
		    (boost::format(hexForm) % msg.address).str().c_str(),
		    (boost::format(hexForm) % msg.value).str().c_str());
	  break;

	case Peek:
	  peekCommand(core, msg, reply);
	  if (commandLog)
	    fprintf(commandLog, "peek %c %s\n", msg.resource,
		    (boost::format(hexForm) % msg.address).str().c_str());

	  break;

	case Step:
	  stepCommand(core, msg, pendingChanges, reply, traceFile);
	  if (commandLog)
	    fprintf(commandLog, "step # %ld\n", core.getInstructionCount());
	  break;

	case ChangeCount:
	  reply.type = ChangeCount;
	  reply.value = pendingChanges.size();
	  reply.address = core.lastPc();
	  {
	    uint32_t inst = 0;
	    core.readInst(core.lastPc(), inst);
	    reply.resource = inst;
	    std::string text;
	    core.disassembleInst(inst, text);
	    uint32_t op0 = 0, op1 = 0; int32_t op2 = 0;
	    const InstInfo& info = core.decode(inst, op0, op1, op2);
	    if (info.isBranch())
	      {
		if (core.lastPc() + instructionSize(inst) != core.peekPc())
		  text += " (T)";
		else
		  text += " (NT)";
	      }
	    strncpy(reply.buffer, text.c_str(), sizeof(reply.buffer) - 1);
	    reply.buffer[sizeof(reply.buffer) -1] = 0;
	  }
	  break;

	case Change:
	  if (pendingChanges.empty())
	    reply.type = Invalid;
	  else
	    {
	      reply = pendingChanges.back();
	      pendingChanges.pop_back();
	    }
	  break;

	case Reset:
	  pendingChanges.clear();
	  if (msg.value != 0)
	    core.defineResetPc(msg.address);
	  core.reset();
	  reply = msg;
	  if (commandLog)
	    {
	      if ((msg.address & 1) == 0)
		fprintf(commandLog, "reset 0x%lx\n", msg.address);
	      else
		fprintf(commandLog, "reset\n");
	    }
	  break;

	case Exception:
	  {
	    std::string text;
	    exceptionCommand(core, msg, reply, traceFile, text);
	    if (commandLog)
	      fprintf(commandLog, "%s\n", text.c_str());
	  }
	  break;

	case EnterDebug:
	  core.enterDebugMode(core.peekPc());
	  if (commandLog)
	    fprintf(commandLog, "enter_debug\n");
	  break;

	case ExitDebug:
	  core.exitDebugMode();
	  if (commandLog)
	    fprintf(commandLog, "exit_debug\n");
	  break;

	default:
	  reply.type = Invalid;
	}

      if (not sendMessage(soc, reply))
	return false;
    }

  return false;
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
  cout << "help [<comand>]\n";
  cout << "  print help for given command or for all commands if no command given\n\n";
  cout << "run\n";
  cout << "  run till interrupted\n\n";
  cout << "until <address>\n";
  cout << "  run until address or interrupted\n\n";
  cout << "step [<n>]\n";
  cout << "  execute n instructions (1 if n is missing)\n\n";
  cout << "peek <res> <addr>\n";
  cout << "  print value of resource res (one of r, f, c, m) of address addr\n";
  cout << "  examples: peek r x1   peek c mtval   peek m 0x4096\n\n";
  cout << "peek pc\n";
  cout << "  print value of the program counter\n\n";
  cout << "peek all\n";
  cout << "  print value of all non-memory resources\n\n";
  cout << "poke res addr value\n";
  cout << "  set value of resource res (one of r, c or m) of address addr\n";
  cout << "  examples: poke r x1 0xff  poke c 0x4096 0xabcd\n\n";
  cout << "disass opcode <code> <code> ...\n";
  cout << "  disassemble opcodes -- example: disass opcode 0x3b 0x8082\n\n";
  cout << "disass funtion <name>\n";
  cout << "  disassemble function with given name -- example: disas func main\n\n";
  cout << "disass <addr1> <addr2>>\n";
  cout << "  disassemble memory locations between addr1 and addr2\n\n";
  cout << "elf file\n";
  cout << "  load elf file into simulator meory\n\n";
  cout << "hex file\n";
  cout << "  load hex file into simulator memory\n\n";
  cout << "replay_file file\n";
  cout << "  open command file for replay\n\n";
  cout << "replay n\n";
  cout << "  execute the next n commands in the replay file or all the\n";
  cout << "  remaining commands if n is missing\n\n";
  cout << "replay step n\n";
  cout << "  execute consecutive commands from the replay file until n\n";
  cout << "  step commands are exeuted or the file is exhausted\n\n";
  cout << "reset [<reset_pc>]\n";
  cout << "  reset hart.  If reset_pc is given, then change the reset program\n";
  cout << "  counter to the given reset_pc before resetting the hart.\n\n";
  cout << "quit\n";
  cout << "  terminate the simulator\n\n";
}


static
void
helpCommand(const std::vector<std::string>& tokens)
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
	   << "  argument is given, print info abot that command.\n";
      return;
    }

  if (tag == "run")
    {
      cout << "run\n"
	   << "  Run the target program until it exits (in Linux emulation mode),\n"
	   << "  it writes into the \"tohost\" location, or the user interrupts\n"
	   << "  it by pressing control-c on the beyboard.\n";
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
	   << "  condition (see run command) is encoutered\n";
      return;
    }

  if (tag == "peek")
    {
      cout << "peek <res> <addr>\n"
	   << "peek pc\n"
	   << "  Show contents of given resource having given address. Possible\n"
	   << "  resources are r, f, c, or m for integer, floating-point,\n"
	   << "  contol-and-status register or for memory respectively.\n"
	   << "  Addr stands for a register number, register name or memory\n"
	   << "  address.  Examples\n"
	   << "    peek pc\n"
	   << "    peek r t0\n"
	   << "    peek r x12\n"
	   << "    peek c mtval\n"
	   << "    peek m 0x80000000\n";
      return;
    }

  if (tag == "poke")
    {
      cout << "poke <res> <addr> <value>\n"
	   << "poke pc <value>\n"
	   << "  Set the contents of given resource having given address to the\n"
	   << "  given value. Possible resources are r, f, c, or m for integer,\n"
	   << "  floating-point, contol-and-status register or for memory\n"
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
	   << "  and replays them in a squbsequent session.\n";
      return;
    }

  if (tag == "replay")
    {
      cout << "replay [step] [<n>]\n"
	   << "  Witout any arguments, replay all remaining commands in the\n"
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


template <typename URV>
static
bool
replayCommand(std::vector<Core<URV>*>& cores, unsigned& currentHartId,
	      const std::string& line, const std::vector<std::string>& tokens,
	      FILE* traceFile, FILE* commandLog,
	      std::ifstream& replayStream, bool& done);


/// Command line interpreter: Execute a command line.
template <typename URV>
static
bool
executeLine(std::vector<Core<URV>*>& cores, unsigned& currentHartId,
	    const std::string& inLine, FILE* traceFile, FILE* commandLog,
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

  // Recover hart id (if any) removing hart=<id> token from tokens.
  unsigned hartId = 0;
  bool error = false;
  bool hasHart = getCommandHartId(tokens, hartId, error);
  if (error)
    return false;
  if (not hasHart)
    hartId = currentHartId;

  if (hartId >= cores.size())
    {
      std::cerr << "Hart id out of bounds: " << hartId << '\n';
      return false;
    }

  Core<URV>& core = *(cores.at(hartId));

  const std::string& command = tokens.front();

  if (command == "run")
    {
      bool success = core.run(traceFile);
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return success;
    }

  if (command == "u" or command == "until")
    {
      if (not untilCommand(core, line, tokens, traceFile))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "s" or command == "step")
    {
      if (not stepCommand(core, line, tokens, traceFile))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "peek")
    {
      if (not peekCommand(core, line, tokens))
	return false;
       if (commandLog)
	 fprintf(commandLog, "%s\n", line.c_str());
       return true;
    }

  if (command == "poke")
    {
      if (not pokeCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "d" or command == "disas")
    {
      if (not disassCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "elf")
    {
      if (not elfCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "hex")
    {
      if (not hexCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "q" or command == "quit")
    {
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      done = true;
      return true;
    }

  if (command == "reset")
    {
      core.reset();
      if (commandLog)
	fprintf(commandLog, "%s\n", "reset");
      return true;
    }

  if (command == "exception")
    {
      if (not exceptionCommand(core, line, tokens))
	return false;
      if (commandLog)
	fprintf(commandLog, "%s\n", line.c_str());
      return true;
    }

  if (command == "enter_debug")
    {
      core.enterDebugMode(core.peekPc());
      if (commandLog)
	fprintf(commandLog, "enter_debug\n");
      return true;
    }

  if (command == "exit_debug")
    {
      core.exitDebugMode();
      if (commandLog)
	fprintf(commandLog, "exit_debug\n");
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
      if (not replayCommand(cores, currentHartId,
			    line, tokens, traceFile, commandLog,
			    replayStream, done))
	return false;
      return true;
    }

  if (command == "symbols")
    {
      for (const auto& kv : elfSymbols)
	std::cout << kv.first << ' ' << "0x" << std::hex << kv.second.addr_ << '\n';
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
static
bool
replayCommand(std::vector<Core<URV>*>& cores, unsigned& currentHartId,
	      const std::string& line, const std::vector<std::string>& tokens,
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
	  if (not executeLine(cores, currentHartId, replayLine, traceFile,
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
	  std::cerr << "Expacting: replay <step> <count>\n";
	  return false;
	}

      if (not parseCmdLineNumber("step-count", tokens.at(2), maxCount))
	return false;
      
      uint64_t count = 0;
      while (count < maxCount  and  not done   and
	     std::getline(replayStream, replayLine))
	{
	  if (not executeLine(cores, currentHartId, replayLine, traceFile,
			      commandLog, replayStream, done))
	    return false;

	  std::vector<std::string> tokens;
	  boost::split(tokens, replayLine, boost::is_any_of(" \t"),
		       boost::token_compress_on);
	  if (tokens.size() > 0 and tokens.at(0) == "step")
	    count++;
	}

      return true;
    }

  std::cerr << "Invalid command: " << line << '\n';
  std::cerr << "Expecting: replay, replay <count>, or replay step <count>\n";
  return false;    
}


/// Interactive mode command loop.
template <typename URV>
static
bool
interact(std::vector<Core<URV>*>& cores, FILE* traceFile, FILE* commandLog)
{
  linenoiseHistorySetMaxLen(1024);

  uint64_t errors = 0;
  unsigned currentHartId = 0;
  std::string replayFile;
  std::ifstream replayStream;

  bool done = false;

  while (not done)
    {
      errno = 0;
      char* cline = linenoise("whisper> ");
      if (cline == nullptr)
	{
	  if (errno == EAGAIN)
	    continue;
	  return true;
	}

      std::string line = cline;
      linenoiseHistoryAdd(cline);
      free(cline);

      if (not executeLine(cores, currentHartId, line, traceFile, commandLog,
			  replayStream, done))
	errors++;
    }

  return errors == 0;
}


/// Open a server socket and put opened socket information (hostname
/// and port number) in the given server file. Wait for one
/// connection. Service connection. Return true on success and false
/// on failure.
template <typename URV>
static
bool
runServer(Core<URV>& core, const std::string& serverFile, FILE* traceFile,
	  FILE* commandLog)
{
  char hostName[1024];
  if (gethostname(hostName, sizeof(hostName)) != 0)
    {
      std::cerr << "Failed to obtain name of this computer\n";
      return false;
    }

  int soc = socket(AF_INET, SOCK_STREAM, 0);
  if (soc < 0)
    {
      char buffer[512];
      char* p = strerror_r(errno, buffer, 512);
      std::cerr << "Failed to create socket: " << p << '\n';
      return -1;
    }

  sockaddr_in serverAddr;
  memset(&serverAddr, '0', sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(0);

  if (bind(soc, (sockaddr*) &serverAddr, sizeof(serverAddr)) < 0)
    {
      perror("Socket bind failed");
      return false;
    }

  if (listen(soc, 1) < 0)
    {
      perror("Socket listen failed");
      return false;
    }

  sockaddr_in socAddr;
  socklen_t socAddrSize = sizeof(socAddr);
  socAddr.sin_family = AF_INET;
  socAddr.sin_port = 0;
  if (getsockname(soc, (sockaddr*) &socAddr,  &socAddrSize) == -1)
    {
      perror("Failed to obtain socket information");
      return false;
    }

  {
    std::ofstream out(serverFile);
    if (not out.good())
      {
	std::cerr << "Failed to open file '" << serverFile << "' for output\n";
	return false;
      }
    out << hostName << ' ' << ntohs(socAddr.sin_port) << std::endl;
  }

  sockaddr_in clientAddr;
  socklen_t clientAddrSize = sizeof(clientAddr);
  int newSoc = accept(soc, (sockaddr*) & clientAddr, &clientAddrSize);
  if (newSoc < 0)
    {
      perror("Socket accept failed");
      return false;
    }

  bool ok = interactUsingSocket(core, newSoc, traceFile, commandLog);

  close(newSoc);
  close(soc);

  return ok;
}


template <typename URV>
static
bool
reportInstructionFrequency(Core<URV>& core, const std::string& outPath)
{
  FILE* outFile = fopen(outPath.c_str(), "w");
  if (not outFile)
    {
      std::cerr << "Failed to open instruction frequency file '" << outPath
		<< "' for output.\n";
      return false;
    }
  core.reportInstructionFrequency(outFile);
  fclose(outFile);
  return true;
}


/// Open the trace-file, command-log and console-output files
/// specified on the command line. Return true if successful or false
/// if any specified file fails to open.
static
bool
openUserFiles(const Args& args, FILE*& traceFile, FILE*& commandLog,
	      FILE*& consoleOut)
{
  if (not args.traceFile.empty())
    {
      traceFile = fopen(args.traceFile.c_str(), "w");
      if (not traceFile)
	{
	  std::cerr << "Faield to open trace file '" << args.traceFile
		    << "' for output\n";
	  return false;
	}
    }

  if (args.trace and traceFile == NULL)
    traceFile = stdout;
  if (traceFile)
    setlinebuf(traceFile);  // Make line-buffered.

  if (not args.commandLogFile.empty())
    {
      commandLog = fopen(args.commandLogFile.c_str(), "w");
      if (not commandLog)
	{
	  std::cerr << "Failed to open command log file '"
		    << args.commandLogFile << "' for output\n";
	  return false;
	}
      setlinebuf(commandLog);  // Make line-buffered.
    }

  if (not args.consoleOutFile.empty())
    {
      consoleOut = fopen(args.consoleOutFile.c_str(), "w");
      if (not consoleOut)
	{
	  std::cerr << "Failed to open console output file '"
		    << args.consoleOutFile << "' for output\n";
	  return false;
	}
    }

  return true;
}


/// Counterpart to openUserFiles: Close any open user file.
static
void
closeUserFiles(FILE*& traceFile, FILE*& commandLog, FILE*& consoleOut)
{
  if (consoleOut and consoleOut != stdout)
    fclose(consoleOut);
  consoleOut = nullptr;

  if (traceFile and traceFile != stdout)
    fclose(traceFile);
  traceFile = nullptr;

  if (commandLog and commandLog != stdout)
    fclose(commandLog);
  commandLog = nullptr;
}


// In interactive mode, keboard interrupts (typically control-c) are
// ignored.
static void
kbdInterruptHandler(int)
{
  std::cerr << "keboard interrupt\n";
}


/// Depending on command line args, start a server, run in interactive
/// mode, or initiate a batch run.
template <typename URV>
static
bool
sessionRun(Core<URV>& core, const Args& args, FILE* traceFile, FILE* commandLog)
{
  if (not applyCmdLineArgs(args, core))
    if (not args.interactive)
      return false;

  bool serverMode = not args.serverFile.empty();
  if (serverMode)
    {
      core.enableTriggers(true);
      core.enablePerformanceCounters(true);

      return runServer(core, args.serverFile, traceFile, commandLog);
    }

  if (args.interactive)
    {
      core.enableTriggers(true);
      core.enablePerformanceCounters(true);

      // Ignore keyboard interrupt for most commands. Long running
      // commands will enable keyboard interrupts while they run.
      struct sigaction newAction;
      sigemptyset(&newAction.sa_mask);
      newAction.sa_flags = 0;
      newAction.sa_handler = kbdInterruptHandler;
      sigaction(SIGINT, &newAction, nullptr);

      std::vector<Core<URV>*> cores;
      cores.push_back(&core);
      return interact(cores, traceFile, commandLog);
    }

  return core.run(traceFile);
}


template <typename URV>
static
bool
applyDisassemble(Core<URV>& core, const Args& args)
{
  unsigned errors = 0;
  auto hexForm = getHexForm<URV>(); // Format string for printing a hex val
  for (const auto& codeStr : args.codes)
    {
      uint32_t code = 0;
      if (not parseCmdLineNumber("disassemble-code", codeStr, code))
	errors++;
      else
	{
	  std::string text;
	  core.disassembleInst(code, text);
	  std::cout << (boost::format(hexForm) % code) << ' ' << text << '\n';
	}
    }
  return errors == 0;
}


template <typename URV>
static
bool
session(const Args& args, const CoreConfig& config)
{
  size_t memorySize = size_t(1) << 32;  // 4 gigs
  unsigned registerCount = 32;
  unsigned hartId = 0;

  Core<URV> core(hartId, memorySize, registerCount);

  if (not config.applyConfig(core, args.verbose))
    if (not args.interactive)
      return false;

  bool disasOk = applyDisassemble(core, args);

  if (args.hexFile.empty() and args.target.empty() and not args.interactive)
    {
      if (not args.codes.empty())
	return disasOk;
      std::cerr << "No program file specified.\n";
      return false;
    }

  FILE* traceFile = nullptr;
  FILE* commandLog = nullptr;
  FILE* consoleOut = stdout;
  if (not openUserFiles(args, traceFile, commandLog, consoleOut))
    return false;

  core.setConsoleOutput(consoleOut);

  bool serverMode = not args.serverFile.empty();
  bool storeExceptions = args.interactive or serverMode;
  core.enableStoreExceptions(storeExceptions);
  core.enableLoadExceptions(storeExceptions);

  core.reset();

  bool result = sessionRun(core, args, traceFile, commandLog);

  if (not args.instFreqFile.empty())
    result = reportInstructionFrequency(core, args.instFreqFile) and result;

  closeUserFiles(traceFile, commandLog, consoleOut);

  return result;
}


int
main(int argc, char* argv[])
{
  Args args;
  if (not parseCmdLineArgs(argc, argv, args))
    return 1;

  unsigned version = 1;
  unsigned subversion = 213;
  if (args.version)
    std::cout << "Version " << version << "." << subversion << " compiled on "
	      << __DATE__ << " at " << __TIME__ << '\n';

  if (args.help)
    return 0;

  // Load configuration file.
  CoreConfig config;
  if (not args.configFile.empty())
    {
      if (not config.loadConfigFile(args.configFile))
	return 1;
    }

  // Obtain register width (xlen). First from config file then from
  // command line.
  unsigned regWidth = 32;
  config.getXlen(regWidth);
  if (args.hasRegWidth)
    regWidth = args.regWidth;

  bool ok = true;

  if (regWidth == 32)
    ok = session<uint32_t>(args, config);
  else if (regWidth == 64)
    ok = session<uint64_t>(args, config);
  else
    {
      std::cerr << "Invalid register width: " << regWidth;
      std::cerr << " -- expecting 32 or 64\n";
      ok = false;
    }
	
  return ok? 0 : 1;
}
