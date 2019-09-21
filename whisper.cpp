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

#include <csignal>
#include "HartConfig.hpp"
#include "WhisperMessage.h"
#include "Hart.hpp"
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


/// Aapter for the parseCmdLineNumber for optionals.
template <typename TYPE>
static
bool
parseCmdLineNumber(const std::string& option,
		   const std::string& numberStr,
		   std::optional<TYPE>& number)
{
  TYPE n;
  if (not parseCmdLineNumber(option, numberStr, n))
    return false;
  number = n;
  return true;
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
  StringVec   zisa;
  StringVec   regInits;        // Initial values of regs
  StringVec   targets;         // Target (ELF file) programs and associated
                               // program options to be loaded into simulator
                               // memory. Each target plus args is one string.
  std::string targetSep = " "; // Target program argument separator.

  std::optional<std::string> toHostSym;

  // Ith item is a vector of strings representing ith target and its args.
  std::vector<StringVec> expandedTargets;

  std::optional<uint64_t> startPc;
  std::optional<uint64_t> endPc;
  std::optional<uint64_t> toHost;
  std::optional<uint64_t> consoleIo;
  std::optional<uint64_t> instCountLim;
  
  unsigned regWidth = 32;
  unsigned harts = 1;
  unsigned pageSize = 4*1024;

  bool help = false;
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
  bool linux = false;      // True if target program linked with Linux C-lib.
  bool raw = false;       // True if bare-metal program (no linux no newlib).
  bool fastExt = false;    // True if fast external interrupt dispatch enabled.
  bool unmappedElfOk = false;

  // Expand each target program string into program name and args.
  void expandTargets();
};


void
Args::expandTargets()
{
  this->expandedTargets.clear();
  for (const auto& target : this->targets)
    {
      StringVec tokens;
      boost::split(tokens, target, boost::is_any_of(this->targetSep),
		   boost::token_compress_on);
      this->expandedTargets.push_back(tokens);
    }
}


static
void
printVersion()
{
  unsigned version = 1;
  unsigned subversion = 409;
  std::cout << "Version " << version << "." << subversion << " compiled on "
	    << __DATE__ << " at " << __TIME__ << '\n';
}


static
bool
collectCommandLineValues(const boost::program_options::variables_map& varMap,
			 Args& args)
{
  bool ok = true;

  if (varMap.count("startpc"))
    {
      auto numStr = varMap["startpc"].as<std::string>();
      if (not parseCmdLineNumber("startpc", numStr, args.startPc))
	ok = false;
    }

  if (varMap.count("endpc"))
    {
      auto numStr = varMap["endpc"].as<std::string>();
      if (not parseCmdLineNumber("endpc", numStr, args.endPc))
	ok = false;
    }

  if (varMap.count("tohost"))
    {
      auto numStr = varMap["tohost"].as<std::string>();
      if (not parseCmdLineNumber("tohost", numStr, args.toHost))
	ok = false;
    }

  if (varMap.count("consoleio"))
    {
      auto numStr = varMap["consoleio"].as<std::string>();
      if (not parseCmdLineNumber("consoleio", numStr, args.consoleIo))
	ok = false;
    }

  if (varMap.count("maxinst"))
    {
      auto numStr = varMap["maxinst"].as<std::string>();
      if (not parseCmdLineNumber("maxinst", numStr, args.instCountLim))
	ok = false;
    }

  if (varMap.count("tohostsymbol"))
    args.toHostSym = varMap["tohostsymbol"].as<std::string>();

  if (varMap.count("xlen"))
    args.hasRegWidth = true;

  if (args.interactive)
    args.trace = true;  // Enable instruction tracing in interactive mode.

  return ok;
}


/// Parse command line arguments. Place option values in args.
/// Return true on success and false on failure. Exists program
/// if --help is used.
static
bool
parseCmdLineArgs(int argc, char* argv[], Args& args)
{
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
	("zisa", po::value(&args.zisa)->multitoken(),
	 "Specify instruction set z-extension to enable. Only z-extensions "
	 "currently supported are zbb and zbs (Exammple --zisa zbb)")
	("xlen", po::value(&args.regWidth),
	 "Specify register width (32 or 64), defaults to 32")
	("harts", po::value(&args.harts),
	 "Specify number of hardware threads.")
	("pagesize", po::value(&args.pageSize),
	 "Specify memory page size.")
	("target,t", po::value(&args.targets)->multitoken(),
	 "Target program (ELF file) to load into simulator memory. In newlib/linux "
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
	 "Set program entry point. If not specified, use entry point of the "
	 "most recently loaded ELF file.")
	("endpc,e", po::value<std::string>(),
	 "Set stop program counter. Simulator will stop once instruction at "
	 "the stop program counter is executed.")
	("tohost", po::value<std::string>(),
	 "Memory address to which a write stops simulator.")
	("tohostsymbol", po::value<std::string>(),
	 "ELF symbol to use for setting tohost from ELF file (in the case "
	 "where tohost is not specified on the command line). Default: "
	 "\"tohost\".")
	("consoleio", po::value<std::string>(),
	 "Memory address corresponding to console io. Reading/writing a byte "
	 "(lb/sb) from given address reads/writes a byte from the console.")
	("maxinst,m", po::value<std::string>(),
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
	("configfile", po::value(&args.configFile),
	 "Configuration file (JSON file defining system features).")
	("abinames", po::bool_switch(&args.abiNames),
	 "Use ABI register names (e.g. sp instead of x2) in instruction disassembly.")
	("newlib", po::bool_switch(&args.newlib),
	 "Emulate (some) newlib system calls.")
	("linux", po::bool_switch(&args.linux),
	 "Emulate (some) Linux system calls.")
	("raw", po::bool_switch(&args.raw),
	 "Bare metal mode (no linux/newlib system call emulation).")
	("fastext", po::bool_switch(&args.fastExt),
	 "Enable fast external interrupt dispatch.")
	("unmappedelfok", po::bool_switch(&args.unmappedElfOk),
	 "Enable checking fast external interrupt dispatch.")
	("verbose,v", po::bool_switch(&args.verbose),
	 "Be verbose.")
	("version", po::bool_switch(&args.version),
	 "Print version.");

      // Define positional options.
      po::positional_options_description pdesc;
      pdesc.add("target", -1);

      // Parse command line options.
      po::variables_map varMap;
      po::command_line_parser parser(argc, argv);
      auto parsed = parser.options(desc).positional(pdesc).allow_unregistered().run();
      po::store(parsed, varMap);
      po::notify(varMap);

      // auto unparsed = po::collect_unrecognized(parsed.options, po::include_positional);

      if (args.help)
	{
	  std::cout <<
	    "Simulate a RISCV system running the program specified by the given ELF\n"
	    "and/or HEX file. With --newlib/--linux, the ELF file is a newlib/linux linked\n"
	    "program and may be followed by corresponding command line arguments.\n"
	    "All numeric arguments are interpreted as hexadecimal numbers when prefixed"
	    " with 0x."
	    "Examples:\n"
	    "  whisper --target prog --log\n"
	    "  whisper --target prog --setreg sp=0xffffff00\n"
	    "  whisper --newlib --log --target \"prog -x -y\"\n"
	    "  whisper --linux --log --targetsep ':' --target \"prog:-x:-y\"\n\n";
	  std::cout << desc;
	  return true;
	}

      if (not collectCommandLineValues(varMap, args))
	return false;
    }

  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return false;
    }

  return true;
}


/// Apply register initializations specified on the command line.
template<typename URV>
static
bool
applyCmdLineRegInit(const Args& args, Hart<URV>& hart)
{
  bool ok = true;

  URV hartId = hart.localHartId();

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
      unsigned id = 0;
      size_t colonIx = regName.find(':');
      if (colonIx != std::string::npos)
	{
	  std::string hartStr = regName.substr(0, colonIx);
	  regName = regName.substr(colonIx + 1);
	  if (not parseCmdLineNumber("hart", hartStr, id))
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

      if (specificHart and id != hartId)
	continue;

      if (unsigned reg = 0; hart.findIntReg(regName, reg))
	{
	  if (args.verbose)
	    std::cerr << "Setting register " << regName << " to command line "
		      << "value 0x" << std::hex << val << std::dec << '\n';
	  hart.pokeIntReg(reg, val);
	  continue;
	}

      if (unsigned reg = 0; hart.findFpReg(regName, reg))
	{
	  if (args.verbose)
	    std::cerr << "Setting register " << regName << " to command line "
		      << "value 0x" << std::hex << val << std::dec << '\n';
	  hart.pokeFpReg(reg, val);
	  continue;
	}

      auto csr = hart.findCsr(regName);
      if (csr)
	{
	  if (args.verbose)
	    std::cerr << "Setting register " << regName << " to command line "
		      << "value 0x" << std::hex << val << std::dec << '\n';
	  hart.pokeCsr(csr->getNumber(), val);
	  continue;
	}

      std::cerr << "No such RISCV register: " << regName << '\n';
      ok = false;
    }

  return ok;
}


template<typename URV>
static
bool
applyZisaStrings(const std::vector<std::string>& zisa, Hart<URV>& hart)
{
  unsigned errors = 0;

  for (const auto& ext : zisa)
    {
      if (ext == "zbb" or ext == "bb")
	hart.enableRvzbb(true);
      else if (ext == "zbs" or ext == "bs")
	hart.enableRvzbs(true);
      else if (ext == "zbmini" or ext == "bmini")
	{
	  hart.enableRvzbb(true);
	  hart.enableRvzbs(true);
	  std::cerr << "ISA option zbmini is deprecated. Using zbb and zbs.\n";
	}
      else
	{
	  std::cerr << "No such Z extension: " << ext << '\n';
	  errors++;
	}
    }

  return errors == 0;
}


template<typename URV>
static
bool
applyIsaString(const std::string& isaStr, Hart<URV>& hart)
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
  bool implemented = true, isDebug = false, shared = true;
  if (not hart.configCsr("misa", implemented, isa, mask, pokeMask, isDebug,
                         shared))
    {
      std::cerr << "Failed to configure MISA CSR\n";
      errors++;
    }
  else
    hart.reset(resetMemoryMappedRegs); // Apply effects of new misa value.

  return errors == 0;
}


/// Enable linux or newlib based on the symbols in the ELF files.
/// Return true if either is enabled.
template<typename URV>
static
bool
enableNewlibOrLinuxFromElf(const Args& args, Hart<URV>& hart, std::string& isa)
{
  bool newlib = args.newlib, linux = args.linux;
  if (args.raw)
    {
      if (newlib or linux)
	std::cerr << "Raw mode not comptible with newlib/linux. Sticking"
		  << " with raw mode.\n";
      return false;
    }

  if (linux or newlib)
    ;  // Emulation preference already set by user.
  else
    {
      // At this point ELF files have not been loaded: Cannot use
      // hart.findElfSymbol.
      for (auto target : args.expandedTargets)
	{
	  auto elfPath = target.at(0);
	  if (not linux)
	    linux = Memory::isSymbolInElfFile(elfPath, "__libc_csu_init");

	  if (not newlib)
	    newlib = Memory::isSymbolInElfFile(elfPath, "__call_exitprocs");
	}

      if (args.verbose and linux)
	std::cerr << "Deteced linux symbol in ELF\n";

      if (args.verbose and newlib)
	std::cerr << "Deteced newlib symbol in ELF\n";

      if (newlib and linux)
	{
	  std::cerr << "Fishy: Both newlib and linux symbols present in "
		    << "ELF file(s). Doing linux emulation.\n";
	  newlib = false;
	}
    }

  hart.enableNewlib(newlib);
  hart.enableLinux(linux);

  if (newlib or linux)
    {
      // Enable c, a, f, and d extensions for newlib/linux
      if (isa.empty())
	{
	  if (args.verbose)
	    std::cerr << "Enabling a/f/d ISA extensions for newlib/linux\n";
	  isa = "icmafd";
	}
      return true;
    }

  return false;
}


/// Set stack pointer to a reasonable value for linux/newlib.
template<typename URV>
static
void
sanitizeStackPointer(Hart<URV>& hart, bool verbose)
{
  // Set stack pointer to the 8 bytes below end of memory.
  size_t memSize = hart.getMemorySize();
  if (memSize > 128)
    {
      size_t spValue = memSize - 128;
      if (verbose)
	std::cerr << "Setting stack pointer to 0x" << std::hex << spValue
		  << std::dec << " for newlib/linux\n";
      hart.pokeIntReg(IntRegNumber::RegSp, spValue);
    }
}


/// Apply command line arguments: Load ELF and HEX files, set
/// start/end/tohost. Return true on success and false on failure.
template<typename URV>
static
bool
applyCmdLineArgs(const Args& args, Hart<URV>& hart)
{
  unsigned errors = 0;

  std::string isa = args.isa;

  // Handle linux/newlib adjusting stack if needed.
  bool clib = enableNewlibOrLinuxFromElf(args, hart, isa);

  if (not isa.empty())
    {
      if (not applyIsaString(isa, hart))
	errors++;
    }

  if (not applyZisaStrings(args.zisa, hart))
    errors++;

  if (clib)  // Linux or newlib enabled.
    sanitizeStackPointer(hart, args.verbose);

  if (args.toHostSym)
    hart.setTohostSymbol(*args.toHostSym);

  // Load ELF files.
  for (const auto& target : args.expandedTargets)
    {
      const auto& elfFile = target.front();
      if (args.verbose)
	std::cerr << "Loading ELF file " << elfFile << '\n';
      size_t entryPoint = 0;
      if (hart.loadElfFile(elfFile, entryPoint))
	hart.pokePc(URV(entryPoint));
      else
	errors++;
    }

  // Load HEX files.
  for (const auto& hexFile : args.hexFiles)
    {
      if (args.verbose)
	std::cerr << "Loading HEX file " << hexFile << '\n';
      if (not hart.loadHexFile(hexFile))
	errors++;
    }

  if (not args.instFreqFile.empty())
    hart.enableInstructionFrequency(true);

  // Command line to-host overrides that of ELF and config file.
  if (args.toHost)
    hart.setToHostAddress(*args.toHost);

  // Command-line entry point overrides that of ELF.
  if (args.startPc)
    hart.pokePc(URV(*args.startPc));

  // Command-line exit point overrides that of ELF.
  if (args.endPc)
    hart.setStopAddress(URV(*args.endPc));

  // Command-line console io address overrides config file.
  if (args.consoleIo)
    hart.setConsoleIo(URV(*args.consoleIo));

  // Set instruction count limit.
  if (args.instCountLim)
    hart.setInstructionCountLimit(*args.instCountLim);

  // Print load-instruction data-address when tracing instructions.
  hart.setTraceLoad(args.traceLoad);

  hart.enableTriggers(args.triggers);
  hart.enableGdb(args.gdb);
  hart.enablePerformanceCounters(args.counters);
  hart.enableAbiNames(args.abiNames);

  if (args.fastExt)
    hart.enableFastInterrupts(args.fastExt);

  // Apply register initialization.
  if (not applyCmdLineRegInit(args, hart))
    errors++;

  if (args.expandedTargets.empty())
    return errors == 0;

  // Setup target program arguments.
  if (clib)
    {
      if (not hart.setTargetProgramArgs(args.expandedTargets.front()))
	{
	  size_t memSize = hart.memorySize();
	  size_t suggestedStack = memSize - 4;

	  std::cerr << "Failed to setup target program arguments -- stack "
		    << "is not writable\n"
		    << "Try using --setreg sp=<val> to set the stack pointer "
		    << "to a\nwritable region of memory (e.g. --setreg "
		    << "sp=0x" << std::hex << suggestedStack << '\n'
		    << std::dec;
	  errors++;
	}
    }
  else if (args.expandedTargets.front().size() > 1)
    {
      std::cerr << "Warning: Target program options present which requires\n"
		<< "         the use of --newlib/--linux. Options ignored.\n";
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
runServer(std::vector<Hart<URV>*>& harts, const std::string& serverFile,
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
      Server<URV> server(harts);
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
reportInstructionFrequency(Hart<URV>& hart, const std::string& outPath)
{
  FILE* outFile = fopen(outPath.c_str(), "w");
  if (not outFile)
    {
      std::cerr << "Failed to open instruction frequency file '" << outPath
		<< "' for output.\n";
      return false;
    }
  hart.reportInstructionFrequency(outFile);
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
batchRun(std::vector<Hart<URV>*>& harts, FILE* traceFile)
{
  if (harts.empty())
    return true;

  if (harts.size() == 1)
    return harts.front()->run(traceFile);

  // Run each hart in its own thread.

  std::vector<std::thread> threadVec;

  bool result = true;

  auto threadFunc = [&traceFile, &result] (Hart<URV>* hart) {
		      bool r = hart->run(traceFile);
		      result = result and r;
		    };

  for (auto hartPtr : harts)
    threadVec.emplace_back(std::thread(threadFunc, hartPtr));

  for (auto& t : threadVec)
    t.join();

  return result;
}


/// Depending on command line args, start a server, run in interactive
/// mode, or initiate a batch run.
template <typename URV>
static
bool
sessionRun(std::vector<Hart<URV>*>& harts, const Args& args, FILE* traceFile,
	   FILE* commandLog)
{
  for (auto hartPtr : harts)
    if (not applyCmdLineArgs(args, *hartPtr))
      if (not args.interactive)
	return false;

  bool serverMode = not args.serverFile.empty();
  if (serverMode or args.interactive)
    for (auto hartPtr : harts)
      {
	hartPtr->enableTriggers(true);
	hartPtr->enablePerformanceCounters(true);
      }

  if (serverMode)
    return runServer(harts, args.serverFile, traceFile, commandLog);

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

      Interactive interactive(harts);
      return interactive.interact(traceFile, commandLog);
    }

  return batchRun(harts, traceFile);
}


template <typename URV>
static
bool
session(const Args& args, const HartConfig& config)
{
  unsigned registerCount = 32;
  unsigned hartCount = args.harts;
  if (hartCount == 0 or hartCount > 64)
    {
      std::cerr << "Unreasonable hart count: " << hartCount << '\n';
      return false;
    }

  // Determine simulated memory size. Default to 4 gigs.
  // If running a 32-bit machine (pointer size = 32 bits), try 2 gigs.
  size_t memorySize = size_t(1) << 32;  // 4 gigs
  if (memorySize == 0)
    memorySize = size_t(1) << 31;  // 2 gigs
  config.getMemorySize(memorySize);

  size_t pageSize = 4*1024;
  if (not config.getPageSize(pageSize))
    pageSize = args.pageSize;

  Memory memory(memorySize, pageSize);
  memory.setHartCount(hartCount);
  memory.checkUnmappedElf(not args.unmappedElfOk);

  // Make sure harts get deleted on exit of this scope.
  std::vector<std::unique_ptr<Hart<URV>>> autoDeleteHarts;

  // Create and configure harts.
  std::vector<Hart<URV>*> harts;
  for (unsigned i = 0; i < hartCount; ++i)
    {
      auto hart = new Hart<URV>(i, memory, registerCount);
      harts.push_back(hart);
      autoDeleteHarts.push_back(std::unique_ptr<Hart<URV>>(hart));
    }

  // Configure harts.
  for (auto hartPtr : harts)
    if (not config.applyConfig(*hartPtr, args.verbose))
      if (not args.interactive)
	return false;
  config.finalizeCsrConfig(harts);

  // Configure memory.
  if (not config.applyMemoryConfig(*(harts.at(0)), args.verbose))
    return false;
  for (unsigned i = 1; i < hartCount; ++i)
    harts.at(i)->copyMemRegionConfig(*harts.at(0));

  if (args.hexFiles.empty() and args.expandedTargets.empty()
      and not args.interactive)
    {
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

  for (auto hartPtr : harts)
    {
      hartPtr->setConsoleOutput(consoleOut);
      hartPtr->enableLoadExceptions(storeExceptions);
      hartPtr->reset();
    }

  bool result = sessionRun(harts, args, traceFile, commandLog);

  if (not args.instFreqFile.empty())
    {
      Hart<URV>& hart0 = *harts.front();
      result = reportInstructionFrequency(hart0, args.instFreqFile) and result;
    }

  closeUserFiles(traceFile, commandLog, consoleOut);

  return result;
}


/// Determine regiser width (xlen) from ELF file.  Return true if
/// successful and false otherwise (xlen is left unmodified).
static
bool
getXlenFromElfFile(const Args& args, unsigned& xlen)
{
  if (args.expandedTargets.empty())
    return false;

  // Get the length from the first target.
  auto& elfPath = args.expandedTargets.front().front();
  bool is32 = false, is64 = false, isRiscv = false;
  if (not Memory::checkElfFile(elfPath, is32, is64, isRiscv))
    return false;  // ELF does not exist.

  if (not is32 and not is64)
    return false;

  if (is32 and is64)
    {
      std::cerr << "Error: ELF file '" << elfPath << "' has both"
		<< " 32  and 64-bit calss\n";
      return false;
    }

  if (is32)
    xlen = 32;
  else
    xlen = 64;

  if (args.verbose)
    std::cerr << "Setting xlen to " << xlen << " based on ELF file "
	      <<  elfPath << '\n';
  return true;
}


/// Obtain integer-register width (xlen). Command line has top
/// priority, then config file, then ELF file.
static
unsigned
determineRegisterWidth(const Args& args, const HartConfig& config)
{
  unsigned width = 32;
  if (args.hasRegWidth)
    width = args.regWidth;
  else if (not config.getXlen(width))
    getXlenFromElfFile(args, width);
  return width;
}


int
main(int argc, char* argv[])
{
  Args args;
  if (not parseCmdLineArgs(argc, argv, args))
    return 1;

  if (args.version)
    printVersion();

  if (args.help)
    return 0;

  // Expand each target program string into program name and args.
  args.expandTargets();

  // Load configuration file.
  HartConfig config;
  if (not args.configFile.empty())
    if (not config.loadConfigFile(args.configFile))
      return 1;

  unsigned regWidth = determineRegisterWidth(args, config);

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
