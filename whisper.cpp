#include <iostream>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include "Core.hpp"
#include "linenoise.h"


using namespace WdRiscv;


/// Convert command line number-string to a number using strotull and
/// a base of zero (prefixes 0 and 0x are honored). Return true on success
/// and false on failure.  TYPE is an integer type (e.g uint32_t).
template <typename TYPE>
static
bool
parseCmdLineNumber(const std::string& optionName,
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
	  std::cerr << "Only 32 and 64-bit numbers supported in "
		    << "parseCmdLineNumber\n";
	  return false;
	}
      if (end and *end)
	good = false;  // Part of the string are non parseable.
    }

  if (not good)
    std::cerr << "Invalid " << optionName << " value: " << numberStr << '\n';
  return good;
}


/// Hold values provided on the command line.
struct Args
{
  std::string elfFile;
  std::string hexFile;
  std::string traceFile;
  std::string isa;
  std::vector<std::string> regInits;  // Initial values of regs

  uint64_t startPc = 0;
  uint64_t endPc = 0;
  uint64_t toHost = 0;
  
  bool hasStartPc = false;
  bool hasEndPc = false;
  bool hasToHost = false;
  bool trace = false;
  bool interactive = false;
  bool verbose = false;
};


/// Pocess command line arguments. Place option values in args.  Set
/// help to true if --help is used. Return true on success and false
/// on failure.
static
bool
parseCmdLineArgs(int argc, char* argv[], Args& args, bool& help)
{
  help = false;

  std::string toHostStr, startPcStr, endPcStr;

  unsigned errors = 0;

  try
    {
      // Define command line options.
      namespace po = boost::program_options;
      po::options_description desc("options");
      desc.add_options()
	("help,h", po::bool_switch(&help),
	 "Produce this message.")
	("log,l", po::bool_switch(&args.trace),
	 "Enable tracing of instructions to standard output")
	("isa", po::value(&args.isa),
	 "Specify instruction set architecture options")
	("target,t", po::value(&args.elfFile),
	 "ELF file to load into simulator memory")
	("hex,x", po::value(&args.hexFile),
	 "HEX file to load into simulator memory")
	("logfile,f", po::value(&args.traceFile),
	 "Enable tracing of instructions to given file")
	("startpc,s", po::value<std::string>(),
	 "Set program entry point (in hex notation with a 0x prefix). "
	 "If not specified address of start_ symbol found in the ELF file "
	 "(if any) is used.")
	("endpc,e", po::value<std::string>(),
	 "Set stop program counter (in hex notation with a 0x prefix). "
	 "Simulator will stop once instruction at the stop program counter "
	 "is executed. If not specified address of finish_ symbol "
	 "found in the ELF file (if any) is used.")
	("tohost,s", po::value<std::string>(),
	 "Memory address in which a write stops simulator (in hex with "
	 "0x prefix)")
	("interactive,i", po::bool_switch(&args.interactive),
	 "Enable interacive mode")
	("setreg", po::value(&args.regInits),
	 "Initialize registers. Exampple --setreg x1=4")
	("verbose,v", po::bool_switch(&args.verbose),
	 "Be verbose");

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

      if (help)
	{
	  std::cout << "Run riscv simulator on program specified by the given ";
	  std::cout << "ELF and/or HEX file.\n";
	  std::cout << desc;
	  help = true;
	  return true;
	}

      // Collect command line values.
      if (not args.isa.empty())
	std::cerr << "Warning: --isa option currently ignored\n";
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
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return false;
    }

  return errors == 0;
}


/// Apply command line arguments: Load ELF and HEX files, set
/// start/end/tohost. Return true on success and false on failure.
template<typename URV>
static
bool
applyCmdLineArgs(const Args& args, Core<URV>& core)
{
  size_t entryPoint = 0, exitPoint = 0, elfToHost = 0;
  unsigned errors = 0;

  if (not args.elfFile.empty())
    {
      bool elfHasToHost = false;
      if (args.verbose)
	std::cerr << "Loading ELF file " << args.elfFile << '\n';
      if (not core.loadElfFile(args.elfFile, entryPoint, exitPoint, elfToHost,
			       elfHasToHost))
	errors++;
      else
	{
	  core.pokePc(entryPoint);
	  if (elfHasToHost)
	    core.setToHostAddress(elfToHost);
	  if (exitPoint)
	    core.setStopAddress(exitPoint);
	}
    }

  if (not args.hexFile.empty())
    {
      if (args.verbose)
	std::cerr << "Loading HEX file " << args.hexFile << '\n';
      if (not core.loadHexFile(args.hexFile))
	errors++;
    }

  // Command line to-host overrides that of ELF.
  if (args.hasToHost)
    core.setToHostAddress(args.toHost);

  // Command-line entry point overrides that of ELF.
  if (args.hasStartPc)
    core.pokePc(args.startPc);

  // Command-lne exit point overrides that of ELF.
  if (args.hasEndPc)
    core.setStopAddress(args.endPc);

  return errors == 0;
}


template <typename URV>
static
bool
peekCommand(Core<URV>& core, const std::string& line)
{
  std::stringstream ss(line);
  std::string cmd, resource, addrStr;
  ss >> cmd >> resource;
  if (ss.fail())
    {
      std::cerr << "Invalid peek command: " << line << '\n';
      std::cerr << "Expecting: peek <resource> [<number>]\n";
      return false;
    }
  ss >> addrStr;

  const char* hexForm = "%x"; // Formatting string for printing a hex vals
  if (sizeof(URV) == 4)
    hexForm = "0x%08x";
  else if (sizeof(URV) == 8)
    hexForm = "0x%016x";
  else if (sizeof(URV) == 16)
    hexForm = "0x%032x";

  URV val;

  if (resource == "r")
    {
      if (addrStr.empty())
	{
	  for (unsigned i = 0; i < core.intRegCount(); ++i)
	    if (core.peekIntReg(i, val))
	      std::cout << "x" << i << '='
			<< (boost::format(hexForm) % val) << '\n';
	  return true;
	}
      URV addr;
      if (not parseCmdLineNumber("register-number", addrStr, addr))
	return false; // TBD: accept symbolic reg numbers
      if (core.peekIntReg(addr, val))
	{
	  std::cout << (boost::format(hexForm) % val) << std::endl;
	  return true;
	}
      std::cerr << "Register number out of bounds: " << addr << '\n';
      return false;
    }

  if (resource == "c")
    {
      if (addrStr.empty())
	{
	  Csr<URV> csr;
	  std::string name;
	  for (unsigned i = 0; i <= MAX_CSR_; ++i)
	    if (core.peekCsr(CsrNumber(i), val, name))
	      std::cout << name << '=' << (boost::format(hexForm) % val)
			<< '\n';
	  return true;
	}
      URV addr;
      if (not parseCmdLineNumber("csr-number", addrStr, addr))
	return false; // TBD: accept symbolic reg numbers
      std::string name;
      if (core.peekCsr(CsrNumber(addr), val, name))
	{
	  std::cout << (boost::format(hexForm) % val) << std::endl;
	  return true;
	}
      std::cerr << "Csr number out of bounds: " << addr << '\n';
      return false;
    }

  if (resource == "m")
    {
      URV addr;
      if (not parseCmdLineNumber("address", addrStr, addr))
	return false; // TBD: accept symbolic addresses
      URV v;
      if (core.peekMemory(addr, v))
	{
	  std::cout << (boost::format(hexForm) % v) << std::endl;
	  return true;
	}
      std::cerr << "Csr number out of bounds: " << addr << '\n';
      return false;
    }

  std::cerr << "No such resource: " << resource << " -- expecting r, c, or m\n";
  return false;
}


template <typename URV>
static
bool
pokeCommand(Core<URV>& core, const std::string& line)
{
  std::stringstream ss(line);
  std::string cmd, resource, addrStr, valueStr;
  ss >> cmd >> resource >> addrStr >> valueStr;
  if (ss.fail())
    {
      std::cerr << "Invalid peek command: " << line << '\n';
      return false;
    }

  const char* hexForm = "%x"; // Formatting string for printing a hex vals
  if (sizeof(URV) == 4)
    hexForm = "%08x";
  else if (sizeof(URV) == 8)
    hexForm = "%016x";
  else if (sizeof(URV) == 16)
    hexForm = "%032x";

  URV value;
  if (not parseCmdLineNumber("value", valueStr, value))
    {
      std::cerr << "Invalid value: " << valueStr << '\n';
      return false;
    }

  if (resource == "r")
    {
      URV addr;
      if (not parseCmdLineNumber("register-number", addrStr, addr))
	{
	  std::cerr << "Invalid register number: " << addrStr << '\n';
	  return false; // TBD: accept symbolic reg numbers
	}
      if (core.pokeIntReg(addr, value))
	return true;
      std::cerr << "Register number out of bounds: " << addr << '\n';
      return false;
    }

  if (resource == "c")
    {
      URV addr;
      if (not parseCmdLineNumber("csr-number", addrStr, addr))
	{
	  std::cerr << "Invalid csr number: " << addrStr << '\n';
	  return false; // TBD: accept symbolic reg numbers
	}
      if (core.pokeCsr(CsrNumber(addr), value))
	return true;
      std::cerr << "Csr number out of bounds: " << addr << '\n';
      return false;
    }

  if (resource == "m")
    {
      URV addr;
      if (not parseCmdLineNumber("address", addrStr, addr))
	{
	  std::cerr << "Invalid address: " << addrStr << '\n';
	  return false; // TBD: accept symbolic addresses
	}
      if (core.peekMemory(addr, value))
	return true;
      std::cerr << "Csr number out of bounds: " << addr << '\n';
      return false;
    }

  std::cerr << "No such resource: " << resource << " -- expecting r, c, or m\n";
  return false;
}


template <typename URV>
static
bool
interact(Core<URV>& core, FILE* file)
{
  linenoiseHistorySetMaxLen(1024);

  uint64_t errors = 0;

  while (true)
    {
      char* cline = linenoise("whisper> ");
      if (cline == nullptr)
	return true;

      std::string line = cline;
      linenoiseHistoryAdd(cline);
      free(cline);

      boost::algorithm::trim_left(line);  // Remove leading white space
      

      if (boost::starts_with(line, "r"))
	{
	  core.run();
	  continue;
	}

      if (boost::starts_with(line, "peek"))
	{
	  if (not peekCommand(core, line))
	    errors++;
	  continue;
	}

      if (boost::starts_with(line, "poke"))
	{
	  if (not pokeCommand(core, line))
	    errors++;
	  continue;
	}

      if (boost::starts_with(line, "q"))
	return true;

      if (boost::starts_with(line, "h"))
	{
	  std::cout << "help               print help\n";
	  std::cout << "run                run till interrupted\n";
	  std::cout << "run until addr     run untill address or interrupted\n";
	  std::cout << "peek res addr      print content of resource\n";
	  std::cout << "poke res addr val  set value of resource\n";
	  std::cout << "elf file           load elf file\n";
	  std::cout << "hex file           load hex file\n";
	  std::cout << "quit               exit\n";
	  continue;
	}
    }

  return true;
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
      boost::split(tokens, regInit, boost::is_any_of("="));
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

      unsigned reg = 0;
      if (core.findIntReg(regName, reg))
	{
	  core.pokeIntReg(reg, val);
	  continue;
	}

      CsrNumber csr;
      if (core.findCsr(regName, csr))
	{
	  core.pokeCsr(csr, val);
	  continue;
	}

      std::cerr << "No such register: " << regVal << '\n';
      ok = false;
    }

  return ok;
}


int
main(int argc, char* argv[])
{
  bool help = false;  // True if --help used on command line.
  Args args;
  if (not parseCmdLineArgs(argc, argv, args, help))
    return 1;

  if (help)
    return 0;

  size_t memorySize = size_t(1) << 32;  // 4 gigs
  unsigned registerCount = 32;
  unsigned hartId = 0;

  Core<uint32_t> core(hartId, memorySize, registerCount);
  core.initialize();

  if (not applyCmdLineRegInit(args, core))
    return 1;


  if (not applyCmdLineArgs(args, core))
    {
      if (not args.interactive)
	return 1;
    }

  if (args.hexFile.empty() and args.elfFile.empty() and not args.interactive)
    {
      std::cerr << "No program file specified.\n";
      return 1;
    }

  FILE* file = nullptr;
  if (not args.traceFile.empty())
    {
      file = fopen(args.traceFile.c_str(), "w");
      if (not file)
	{
	  std::cerr << "Faield to open trace file '" << args.traceFile
		    << "' for writing\n";
	  return 1;
	}
    }

  if (args.trace and file == NULL)
    file = stdout;

  if (args.interactive)
    interact(core, file);
  else
    core.run(file);

  if (file and file != stdout)
    fclose(file);

  return 0;
}
