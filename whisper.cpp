#include <iostream>
#include <boost/program_options.hpp>
#include "Core.hpp"
#include "linenoise.h"


using namespace WdRiscv;


/// Convert command line number-string to a number using strotull and
/// a base of zero (prefixes 0 and 0x are honored). Return true on success
/// and false on failure.
static
bool
parseCmdLineNumber(const std::string& optionName,
		   const std::string& numberStr,
		   uint64_t& number)
{
  bool good = not numberStr.empty();

  if (good)
    {
      char* end = nullptr;
      number = strtoull(numberStr.c_str(), &end, 0);
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
	("help,h", "Produce this message.")
	("log,l", "Enable tracing of instructions to standard output")
	("isa", po::value<std::string>(),
	 "Specify instruction set architecture options")
	("target,t", po::value<std::string>(),
	 "ELF file to load into simulator memory")
	("hex,x", po::value<std::string>(),
	 "HEX file to load into simulator memory")
	("logfile,f", po::value<std::string>(),
	 "Enable tracing of instructions to given file")
	("startpc,s", po::value<std::string>(),
	 "Set program entry point (in hex notation with a 0x prefix). "
	 "If not specified address of start_ symbol found in the ELF file "
	 "(if any) is used.")
	("endpc,s", po::value<std::string>(),
	 "Set stop program counter (in hex notation with a 0x prefix). "
	 "Simulator will stop once instruction at the stop program counter "
	 "is executed. If not specified address of finish_ symbol "
	 "found in the ELF file (if any) is used.")
	("tohost,s", po::value<std::string>(),
	 "Memory address in which a write stops simulator (in hex with "
	 "0x prefix)")
	("interactive,i", "Enable interacive mode")
	("verbose,v", "Be verbose");

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

      if (varMap.count("help"))
	{
	  std::cout << "Run riscv simulator on program specified by the given ";
	  std::cout << "ELF and/or HEX file.\n";
	  std::cout << desc;
	  help = true;
	  return true;
	}

      // Collect command line values.
      args.trace = varMap.count("log") > 0;
      args.verbose = varMap.count("verbose") > 0;
      if (varMap.count("target"))
	args.elfFile = varMap["target"].as<std::string>();
      if (varMap.count("hex"))
	args.hexFile = varMap["hex"].as<std::string>();
      if (varMap.count("log-file"))
	args.traceFile = varMap["log-file"].as<std::string>();
      if (varMap.count("isa"))
	{
	  args.isa = varMap["isa"].as<std::string>();
	  std::cerr << "Warning: --isa option currently ignored\n";
	}
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
interact(Core<URV>& core, FILE* file)
{
  return false;
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
