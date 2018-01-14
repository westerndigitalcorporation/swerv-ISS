#include <iostream>
#include <boost/program_options.hpp>
#include "Core.hpp"


using namespace WdRiscv;


/// Convert given string to an unsigned number honoring its prefix. If
/// prefix is "0x" then treat the string as a hexadecimal number. If
/// the prefix is "0" then treat it as an octal number.  Otherwise,
/// use decimal. Return true on success and false if string does not
/// contain a number.
static
bool
parseNumber(const std::string& str, uint64_t& num)
{
  if (str.empty())
    return false;

  unsigned sign = 1;
  std::string str2 = str;

  if (str[0] == '+')
    str2 = str.substr(1);  // Drop leading sign.
  if (str[0] == '-')
    {
      sign = -1;
      str2 = str.substr(1);  // Drop leading sign.
    }
  
  if (str2.empty())
    return false;

  std::stringstream ss(str2);
  if (str2.size() > 2 and str2.substr(0, 2) == "0x")
    ss >> std::hex;
  else if (str2[0] == '0')
    ss >> std::oct;

  ss >> num;

  if (ss.fail())
    return false;

  std::string extra;
  if (std::getline(ss, extra) and extra.size() > 0)
    return false;

  num *= sign;

  return true;
}


static
bool
parseCmdLineNumber(const std::string& optionName,
		   const std::string& numberStr,
		   uint64_t& number)
{
  bool result = parseNumber(numberStr, number);
  if (not result)
    std::cerr << "Invalid " << optionName << " value: " << numberStr << '\n';
  return result;
}


int
main(int argc, char* argv[])
{
  bool verbose = false;
  bool trace = false;
  std::string elfFile;
  std::string hexFile;
  std::string traceFile;
  std::string isa;
  std::string startPcStr, endPcStr; // Command line start/end pc.
  uint64_t startPc = 0, endPc = 0;  // Command line start/end pc.
  bool hasStartPc = false, hasEndPc = false;

  std::string toHostStr;
  uint64_t toHost;
  bool hasToHost;

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
	("log-file,f", po::value<std::string>(),
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
	("log-file,f", po::value<std::string>(),
	 "Enable tracing of instructions to given file")
	("tohost,s", po::value<std::string>(),
	 "Memory address in which a write stops simulator (in hex with "
	 "0x prefix)")
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
	  return 0;
	}

      // Collect command line values.
      trace = varMap.count("log") > 0;
      verbose = varMap.count("verbose") > 0;
      if (varMap.count("target"))
	elfFile = varMap["target"].as<std::string>();
      if (varMap.count("hex"))
	hexFile = varMap["hex"].as<std::string>();
      if (varMap.count("log-file"))
	traceFile = varMap["log-file"].as<std::string>();
      if (varMap.count("isa"))
	{
	  isa = varMap["isa"].as<std::string>();
	  std::cerr << "Warning: --isa option currently ignored\n";
	}
      if (varMap.count("startpc"))
	{
	  auto startStr = varMap["startpc"].as<std::string>();
	  hasStartPc = parseCmdLineNumber("startpc", startStr, startPc);
	  if (not hasStartPc)
	    errors++;
	}
      if (varMap.count("endpc"))
	{
	  auto endStr = varMap["endpc"].as<std::string>();
	  hasEndPc = parseCmdLineNumber("endpc", endStr, endPc);
	  if (not hasEndPc)
	    errors++;
	}
      if (varMap.count("tohost"))
	{
	  auto addrStr = varMap["tohost"].as<std::string>();
	  hasToHost = parseCmdLineNumber("tohost", addrStr, toHost);
	  if (not hasToHost)
	    errors++;
	}
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return 1;
    }

  if (errors)
    return 1;

  size_t memorySize = size_t(1) << 32;  // 4 gigs
  unsigned registerCount = 32;
  unsigned hartId = 0;

  Core<uint32_t> core(hartId, memorySize, registerCount);
  core.initialize();

  size_t entryPoint = 0, exitPoint = 0, elfToHost = 0;
  if (not elfFile.empty())
    {
      bool elfHasToHost = false;
      if (not core.loadElfFile(elfFile, entryPoint, exitPoint, elfToHost,
			       elfHasToHost))
	return 1;
      core.pokePc(entryPoint);
      if (elfHasToHost)
	core.setToHostAddress(elfToHost);
    }

  if (not hexFile.empty())
    {
      if (not elfFile.empty())
	std::cerr << "Warning: Loading HEX files on top of an ELF file\n";
      if (not core.loadHexFile(hexFile))
	return 1;
    }

  if (hexFile.empty() and elfFile.empty())
    {
      std::cerr << "No program file specified.\n";
      exit(1);
    }

  // Command line to-host overrides that of ELF.
  if (hasToHost)
    core.setToHostAddress(toHost);

  // Command-line entry point overrides that of ELF.
  if (hasStartPc)
    core.pokePc(startPc);

  // Command-lne exit point overrides that of ELF.
  if (hasEndPc)
    exitPoint = endPc;

  FILE* file = nullptr;
  if (not traceFile.empty())
    {
      file = fopen(traceFile.c_str(), "w");
      if (not file)
	{
	  std::cerr << "Faield to open trace file '" << traceFile
		    << "' for writing\n";
	  return 1;
	}
    }

  if (trace and file == NULL)
    file = stdout;
  core.runUntilAddress(exitPoint, file);

  if (file and file != stdout)
    fclose(file);

  return 0;
}
