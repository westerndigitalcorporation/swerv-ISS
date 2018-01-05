#include <iostream>
#include <boost/program_options.hpp>
#include "Core.hpp"


using namespace WdRiscv;


int
main(int argc, char* argv[])
{
  bool verbose = false;
  std::string elfFile;
  std::string hexFile;
  std::string traceFile;

  try
    {
      // Define command line options.
      namespace po = boost::program_options;
      po::options_description desc("options");
      desc.add_options()
	("help,h", "Produce this message.")
	("elf,e", po::value<std::string>(),
	 "ELF file to load into simulator memory")
	("hex,x", po::value<std::string>(),
	 "HEX file to load into simulator memory")
	("trace,t", po::value<std::string>(),
	 "Enable tracing of instructions to given output file")
	("verbose,v", "Be verbose");

      // Parse command line options.
      po::variables_map varMap;
      po::store(po::parse_command_line(argc, argv, desc), varMap);
      po::notify(varMap);

      if (varMap.count("help"))
	{
	  std::cout << "Run riscv simulator on progam specified by the given";
	  std::cout << "ELF or HEX file.\n";
	  std::cout << desc;
	  return 0;
	}

      // Collect command line values.
      verbose = varMap.count("verbose") > 0;
      if (varMap.count("elf"))
	elfFile = varMap["elf"].as<std::string>();
      if (varMap.count("hex"))
	elfFile = varMap["hex"].as<std::string>();
      if (varMap.count("trace"))
	traceFile = varMap["trace"].as<std::string>();
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return 1;
    }


  size_t memorySize = 3*1024*1024*size_t(1024);
  unsigned registerCount = 32;
  unsigned hartId = 0;

  Core<uint32_t> core(hartId, memorySize, registerCount);
  core.initialize();

  size_t entryPoint = 0, exitPoint = 0;
  if (not elfFile.empty())
    {
      if (not core.loadElfFile(elfFile, entryPoint, exitPoint))
	return 1;
      core.pokePc(entryPoint);
    }

  FILE* file = nullptr;
  if (not traceFile.empty())
    {
      file = fopen(traceFile.c_str(), "w");
      if (not file)
	{
	  std::cerr << "Faield to open trace file '" << traceFile << "' for writing\n";
	  return 1;
	}
    }
  core.runUntilAddress(exitPoint, file);

  return 0;
}
