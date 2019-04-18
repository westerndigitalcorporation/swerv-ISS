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
#include <thread>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#ifdef __MINGW64__
#include <winsock2.h>
typedef int socklen_t;
#define close(s)          closesocket((s))
#define setlinebuf(f)     setvbuf((f),NULL,_IOLBF,0)
#define strerror_r(a,b,c) strerror((a))
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <signal.h>
#include "CoreConfig.hpp"
#include "WhisperMessage.h"
#include "Core.hpp"
#include "Server.hpp"
#include "Interactive.hpp"


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


typedef std::vector<std::string> StringVec;


/// Hold values provided on the command line.
struct Args
{
  StringVec   hexFiles;        // Hex files to be loaded into simulator memory.
  std::string traceFile;       // Log of state change after each instruction.
  std::string commandLogFile;  // Log of interactive or socket commands.
  std::string consoleOutFile;  // Console io output file.
  std::string serverFile;      // File in which to write server host and port.
  std::string instFreqFile;    // Instruction frequency file.
  std::string configFile;      // Configuration (JSON) file.
  std::string isa;
  StringVec   regInits;        // Initial values of regs
  StringVec   codes;           // Instruction codes to disassemble
  StringVec   targets;         // Target (ELF file) programs and associated
                               // program options to be loaded into simulator
                               // memory. Each target plus args is one string.
  std::string targetSep = " "; // Target program argument separator.

  // Ith item is a vector of strings representing ith target and its args.
  std::vector<StringVec> expandedTargets;

  uint64_t startPc = 0;
  uint64_t endPc = 0;
  uint64_t toHost = 0;
  uint64_t consoleIo = 0;
  uint64_t instCountLim = ~uint64_t(0);
  
  unsigned regWidth = 32;
  unsigned harts = 1;

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
  bool counters = false;   // Enable performance counters when true.
  bool gdb = false;        // Enable gdb mode when true.
  bool abiNames = false;   // Use ABI register names in inst disassembly.
  bool newlib = false;     // True if target program linked with newlib.
};


/// Poses command line arguments. Place option values in args.  Set
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
	 "Specify instruction set extensions to enable. Supported extensions "
	 "are a, c, d, f, i, m, s and u. Default is imc.")
	("xlen", po::value(&args.regWidth),
	 "Specify register width (32 or 64), defaults to 32")
	("harts", po::value(&args.harts),
	 "Specify number of hardware threads.")
	("target,t", po::value(&args.targets)->multitoken(),
	 "Target program (ELF file) to load into simulator memory. In newlib "
	 "emulations mode, program options may follow program name.")
	("targetsep", po::value(&args.targetSep),
	 "Target program argument separator.")
	("hex,x", po::value(&args.hexFiles)->multitoken(),
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
	 "Enable interactive mode.")
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
	 "Initialize registers. Apply to all harts unless specific prefix "
	 "present (hart is 1 in 1:x3=0xabc). Example: --setreg x1=4 x2=0xff "
	 "1:x3=0xabc")
	("disass,d", po::value(&args.codes)->multitoken(),
	 "Disassemble instruction code(s). Example --disass 0x93 0x33")
	("configfile", po::value(&args.configFile),
	 "Configuration file (JSON file defining system features).")
	("abinames", po::bool_switch(&args.abiNames),
	 "Use ABI register names (e.g. sp instead of x2) in instruction disassembly.")
	("newlib", po::bool_switch(&args.newlib),
	 "Emulate (some) newlib system calls when true.")
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
	    "and/or HEX file. With --newlib, the ELF file is a newlib-linked program\n"
	    "and may be followed by corresponding command line arguments.\n"
	    "Examples:\n"
	    "  whisper --target prog --log\n"
	    "  whisper --newlib --log --target \"prog -x -y\"\n"
	    "  whisper --newlib --log --targetsep ':' --target \"prog:-x:-y\"\n\n";
	  std::cout << desc;
	  return true;
	}

      // Collect command line values.
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

  URV hartId = 0;
  core.peekCsr(CsrNumber::MHARTID, hartId);

  for (const auto& regInit : args.regInits)
    {
      // Each register initialization is a string of the form reg=val
      // or hart:reg=val
      std::vector<std::string> tokens;
      boost::split(tokens, regInit, boost::is_any_of("="),
		   boost::token_compress_on);
      if (tokens.size() != 2)
	{
	  std::cerr << "Invalid command line register initialization: "
		    << regInit << '\n';
	  ok = false;
	  continue;
	}

      std::string regName = tokens.at(0);
      const std::string& regVal = tokens.at(1);

      bool specificHart = false;
      unsigned hart = 0;
      size_t colonIx = regName.find(':');
      if (colonIx != std::string::npos)
	{
	  std::string hartStr = regName.substr(0, colonIx);
	  regName = regName.substr(colonIx + 1);
	  if (not parseCmdLineNumber("hart", hartStr, hart))
	    {
	      std::cerr << "Invalid command line register initialization: "
			<< regInit << '\n';
	      ok = false;
	      continue;
	    }
	  specificHart = true;
	}

      URV val = 0;
      if (not parseCmdLineNumber("register", regVal, val))
	{
	  ok = false;
	  continue;
	}

      if (specificHart and hart != hartId)
	continue;

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

      std::cerr << "No such RISCV register: " << regName << '\n';
      ok = false;
    }

  return ok;
}


template<typename URV>
bool
loadElfFile(Core<URV>& core, const std::string& filePath)
{
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


template<typename URV>
static
bool
applyIsaString(const std::string& isaStr, Core<URV>& core)
{
  URV isa = 0;
  unsigned errors = 0;

  for (auto c : isaStr)
    {
      switch(c)
	{
	case 'a':
	case 'c':
	case 'd':
	case 'f':
	case 'i':
	case 'm':
	case 'u':
	case 's':
	  isa |= URV(1) << (c -  'a');
	  break;

	default:
	  std::cerr << "Extension \"" << c << "\" is not supported.\n";
	  errors++;
	  break;
	}
    }

  if (not (isa & (URV(1) << ('i' - 'a'))))
    {
      std::cerr << "Extension \"i\" implicitly enabled\n";
      isa |= URV(1) << ('i' -  'a');
    }

  if (isa & (URV(1) << ('d' - 'a')))
    if (not (isa & (URV(1) << ('f' - 'a'))))
      {
	std::cerr << "Extension \"d\" requires \"f\" -- Enabling \"f\"\n";
	isa |= URV(1) << ('f' -  'a');
      }

  // Set the xlen bits: 1 for 32-bits and 2 for 64.
  URV xlen = sizeof(URV) == 4? 1 : 2;
  isa |= xlen << (8*sizeof(URV) - 2);

  bool resetMemoryMappedRegs = false;

  URV mask = 0, pokeMask = 0;
  bool implemented = true, isDebug = false;
  if (not core.configCsr("misa", implemented, isa, mask, pokeMask, isDebug))
    {
      std::cerr << "Failed to configure MISA CSR\n";
      errors++;
    }
  else
    core.reset(resetMemoryMappedRegs); // Apply effects of new misa value.

  return errors == 0;
}


/// Apply command line arguments: Load ELF and HEX files, set
/// start/end/tohost. Return true on success and false on failure.
template<typename URV>
static
bool
applyCmdLineArgs(const Args& args, Core<URV>& core)
{
  unsigned errors = 0;

  if (not args.isa.empty())
    {
      if (not applyIsaString(args.isa, core))
	errors++;
    }

  // Load ELF files.
  for (const auto& target : args.expandedTargets)
    {
      const auto& elfFile = target.front();
      if (args.verbose)
	std::cerr << "Loading ELF file " << elfFile << '\n';
      if (not loadElfFile(core, elfFile))
	errors++;
    }

  // Load HEX files.
  for (const auto& hexFile : args.hexFiles)
    {
      if (args.verbose)
	std::cerr << "Loading HEX file " << hexFile << '\n';
      if (not core.loadHexFile(hexFile))
	errors++;
    }

  if (not args.instFreqFile.empty())
    core.enableInstructionFrequency(true);

  // Command line to-host overrides that of ELF and config file.
  if (args.hasToHost)
    core.setToHostAddress(args.toHost);

  // Command-line entry point overrides that of ELF.
  if (args.hasStartPc)
    core.pokePc(URV(args.startPc));

  // Command-line exit point overrides that of ELF.
  if (args.hasEndPc)
    core.setStopAddress(URV(args.endPc));

  // Command-line console io address overrides config file.
  if (args.hasConsoleIo)
    core.setConsoleIo(URV(args.consoleIo));

  // Set instruction count limit.
  core.setInstructionCountLimit(args.instCountLim);

  // Print load-instruction data-address when tracing instructions.
  core.setTraceLoad(args.traceLoad);

  core.enableTriggers(args.triggers);
  core.enableGdb(args.gdb);
  core.enablePerformanceCounters(args.counters);
  core.enableAbiNames(args.abiNames);
  core.enableNewlib(args.newlib);

  // Apply register initialization.
  if (not applyCmdLineRegInit(args, core))
    errors++;

  if (args.expandedTargets.empty())
    return errors == 0;

  // Setup target program arguments.
  if (args.newlib)
    {
      if (not core.setTargetProgramArgs(args.expandedTargets.front()))
	{
	  size_t memSize = core.memorySize();
	  size_t suggestedStack = memSize - 4;

	  std::cerr << "Failed to setup target program arguments -- stack "
		    << "is not writable\n"
		    << "Try using --setreg sp=<val> to set the stack pointer "
		    << "to a\nwritable region of memory (e.g. --setreg "
		    << "sp=0x" << std::hex << suggestedStack << '\n';
	  errors++;
	}
    }
  else if (args.expandedTargets.front().size() > 1)
    {
      std::cerr << "Warning: Target program options present, that requires\n"
		<< "         --newlib. Options ignored.\n";
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
runServer(std::vector<Core<URV>*>& cores, const std::string& serverFile,
	  FILE* traceFile, FILE* commandLog)
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
      char* p = buffer;
#ifdef __APPLE__
      strerror_r(errno, buffer, 512);
#else
      p = strerror_r(errno, buffer, 512);
#endif
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

  bool ok = true;

  try
    {
      Server<URV> server(cores);
      ok = server.interact(newSoc, traceFile, commandLog);
    }
  catch(...)
    {
      ok = false;
    }

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
	  std::cerr << "Failed to open trace file '" << args.traceFile
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


// In interactive mode, keyboard interrupts (typically control-c) are
// ignored.
static void
kbdInterruptHandler(int)
{
  std::cerr << "keyboard interrupt\n";
}


template <typename URV>
static bool
batchRun(std::vector<Core<URV>*>& cores, FILE* traceFile)
{
  if (cores.empty())
    return true;

  if (cores.size() == 1)
    return cores.front()->run(traceFile);

  // Run each hart in its own thread.

  std::vector<std::thread> threadVec;

  bool result = true;

  auto threadFunc = [&traceFile, &result] (Core<URV>* core) {
		      bool r = core->run(traceFile);
		      result = result and r;
		    };

  for (auto corePtr : cores)
    threadVec.emplace_back(std::thread(threadFunc, corePtr));

  for (auto& t : threadVec)
    t.join();

  return result;
}


/// Depending on command line args, start a server, run in interactive
/// mode, or initiate a batch run.
template <typename URV>
static
bool
sessionRun(std::vector<Core<URV>*>& cores, const Args& args, FILE* traceFile,
	   FILE* commandLog)
{
  for (auto corePtr : cores)
    if (not applyCmdLineArgs(args, *corePtr))
      if (not args.interactive)
	return false;

  bool serverMode = not args.serverFile.empty();
  if (serverMode or args.interactive)
    for (auto corePtr : cores)
      {
	corePtr->enableTriggers(true);
	corePtr->enablePerformanceCounters(true);
      }

  if (serverMode)
    return runServer(cores, args.serverFile, traceFile, commandLog);

  if (args.interactive)
    {
      // Ignore keyboard interrupt for most commands. Long running
      // commands will enable keyboard interrupts while they run.
#ifdef __MINGW64__
      signal(SIGINT, kbdInterruptHandler);
#else
      struct sigaction newAction;
      sigemptyset(&newAction.sa_mask);
      newAction.sa_flags = 0;
      newAction.sa_handler = kbdInterruptHandler;
      sigaction(SIGINT, &newAction, nullptr);
#endif

      Interactive interactive(cores);
      return interactive.interact(traceFile, commandLog);
    }

  return batchRun(cores, traceFile);
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
  unsigned registerCount = 32;
  unsigned harts = args.harts;
  if (harts == 0 or harts > 64)
    {
      std::cerr << "Unreasonable hart count: " << harts << '\n';
      return false;
    }

  // Determine simulated memory size. Default to 4 gigs.
  // If running a 32-bit machine (pointer siz = 32 bits), try 2 gigs.
  size_t memorySize = size_t(1) << 32;  // 4 gigs
  if (memorySize == 0)
    memorySize = size_t(1) << 31;  // 2 gigs

  Memory memory(memorySize);

  // Make sure cores get deleted on exit of this scope.
  std::vector<std::unique_ptr<Core<URV>>> autoDeleteCores;

  // Create and configure cores.
  std::vector<Core<URV>*> cores;
  for (unsigned i = 0; i < harts; ++i)
    {
      auto core = new Core<URV>(i, memory, registerCount);
      cores.push_back(core);
      autoDeleteCores.push_back(std::unique_ptr<Core<URV>>(core));
    }

  // Configure cores.
  for (auto corePtr : cores)
    if (not config.applyConfig(*corePtr, args.verbose))
      if (not args.interactive)
	return false;

  // Diassemble command line op-codes.
  Core<URV>& core0 = *cores.front();
  bool disasOk = applyDisassemble(core0, args);

  if (args.hexFiles.empty() and args.expandedTargets.empty()
      and not args.interactive)
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

  bool serverMode = not args.serverFile.empty();
  bool storeExceptions = args.interactive or serverMode;

  for (auto corePtr : cores)
    {
      corePtr->setConsoleOutput(consoleOut);
      corePtr->enableStoreExceptions(storeExceptions);
      corePtr->enableLoadExceptions(storeExceptions);
      corePtr->reset();
    }

  bool result = sessionRun(cores, args, traceFile, commandLog);

  if (not args.instFreqFile.empty())
    result = reportInstructionFrequency(core0, args.instFreqFile) and result;

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
  unsigned subversion = 302;
  if (args.version)
    std::cout << "Version " << version << "." << subversion << " compiled on "
	      << __DATE__ << " at " << __TIME__ << '\n';

  if (args.help)
    return 0;

  // Expand each target program string into program name and args.
  for (const auto& target : args.targets)
    {
      StringVec tokens;
      boost::split(tokens, target, boost::is_any_of(args.targetSep),
		   boost::token_compress_on);
      args.expandedTargets.push_back(tokens);
    }

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

  try
    {
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
    }
  catch (std::exception& e)
    {
      std::cerr << e.what() << '\n';
      ok = false;
    }
	
  return ok? 0 : 1;
}
