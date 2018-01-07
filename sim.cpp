#include <iostream>
#include <boost/program_options.hpp>
#include "Core.hpp"


using namespace WdRiscv;


int
main(int argc, char* argv[])
{
  bool verbose = false;
  bool trace = false;
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
	("trace,t", "Enable tracing of instructions to standard output")
	("elf,e", po::value<std::string>(),
	 "ELF file to load into simulator memory")
	("hex,x", po::value<std::string>(),
	 "HEX file to load into simulator memory")
	("trace-file,f", po::value<std::string>(),
	 "Enable tracing of instructions to given file")
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
      trace = varMap.count("trace") > 0;
      verbose = varMap.count("verbose") > 0;
      if (varMap.count("elf"))
	elfFile = varMap["elf"].as<std::string>();
      if (varMap.count("hex"))
	elfFile = varMap["hex"].as<std::string>();
      if (varMap.count("trace-file"))
	traceFile = varMap["trace-file"].as<std::string>();
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return 1;
    }


  size_t memorySize = 10*1024;
  unsigned registerCount = 32;
  unsigned hartId = 0;

  Core<uint32_t> core(hartId, memorySize, registerCount);
  core.initialize();

  size_t entryPoint = 0, exitPoint = 0;
  if (not elfFile.empty())
    {
      size_t minAddr = 0, maxAddr = 0;
      if (not elfFile.empty())
	if (not Memory::getElfFileAddressBounds(elfFile, minAddr, maxAddr))
	  return 1;
      if (not core.changeMemoryBounds(minAddr, maxAddr))
	{
	  std::cerr << "Failed to change memory bounds to match those of ELF file: "
		    << std::hex << minAddr << " to " << std::hex << maxAddr << '\n';
	  return 1;
	}
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
  if (trace and file == NULL)
    file = stdout;
  core.runUntilAddress(exitPoint, file);

  if (file and file != stdout)
    fclose(file);

  return 0;
}
