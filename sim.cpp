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
	("log,l", "Enable tracing of instructions to standard output")
	("target,t", po::value<std::string>(),
	 "ELF file to load into simulator memory")
	("hex,x", po::value<std::string>(),
	 "HEX file to load into simulator memory")
	("log-file,f", po::value<std::string>(),
	 "Enable tracing of instructions to given file")
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
	  std::cout << "Run riscv simulator on progam specified by the given";
	  std::cout << "ELF or HEX file.\n";
	  std::cout << desc;
	  return 0;
	}

      // Collect command line values.
      trace = varMap.count("log") > 0;
      verbose = varMap.count("verbose") > 0;
      if (varMap.count("target"))
	elfFile = varMap["target"].as<std::string>();
      if (varMap.count("hex"))
	elfFile = varMap["hex"].as<std::string>();
      if (varMap.count("log-file"))
	traceFile = varMap["log-file"].as<std::string>();
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return 1;
    }

  size_t memorySize = size_t(3) << 30;  // 3 gigs
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

  if (trace and file == NULL)
    file = stdout;
  core.runUntilAddress(exitPoint, file);

  if (file and file != stdout)
    fclose(file);

  return 0;
}
