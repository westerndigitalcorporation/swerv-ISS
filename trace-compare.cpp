#include <iostream>
#include <fstream>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>


/// Read the next trace record from the given input stream and put the
/// read record in line and its numeric fields in fields.
/// Sample record:
///    #1 0 00001000 00000297 r 5 0x00001000 auipc t0, 0x0
/// For the above record, line is set to the whole record. And fields
/// is set ot the numeric values of the 1st 7 tokens with the # dropped
/// and the tag character ("r") replaced by its ascii value.
/// 
/// The first 7 tokens of the line represent the instruction number,
/// program counter, instruction code, resource tag, resource address
/// and resource value respectively. The instruction number and hart
/// are in decimal notation. Thre renaming fields (except tag) are in
/// heaxdecimal notation.
static bool
getNextRecord(const std::string& fileName, std::istream& input,
	      uint64_t& lineNum, std::string& line,
	      std::vector<uint64_t>& fields)
{
  fields.clear();

  while (std::getline(input, line))
    {
      lineNum++;
      // TODO: remove comments.

      if (line.empty())
	continue;

      if (line[0] != '#')
	continue;  // Spurious non-trace record. Ignore.

      uint64_t instNum = 0, hart = 0, pc = 0, opcode = 0, addr = 0, val = 0;
      char tag;
      if (not sscanf(line.c_str(), "#%d %d %llx %llx %c %llx %llxd", 
		     &instNum, &hart, &pc, &opcode, &tag, &addr, &val))
	{
	  std::cerr << "File " << fileName << ", Line " << lineNum
		    << ": Invalid trace record: " << line << '\n';
	  return false;
	}

      fields.push_back(instNum);
      fields.push_back(hart);
      fields.push_back(pc);
      fields.push_back(opcode);
      fields.push_back(tag);
      fields.push_back(addr);
      fields.push_back(val);
      return true;
    }

    return false;
}


int
main(int argc, char* argv[])
{
  std::string file1;
  std::string file2;
  std::string startPcString;
  uint64_t startPc = 0;
  bool verbose = false;

  try
    {
      // Define command line options.
      namespace po = boost::program_options;
      po::options_description desc("options");
      desc.add_options()
	("help,h", "Produce this message.")
	("file1", po::value<std::string>(),
	 "File to compare")
	("file2", po::value<std::string>(),
	 "File to compare")
	("startpc", po::value<std::string>(),
	 "Program counter at which to start comparing the 2 files")
	("verbose,v", "Be verbose");

      // Parse command line options.
      po::variables_map varMap;
      po::store(po::command_line_parser(argc, argv)
		.options(desc)
		.run(), varMap);
      po::notify(varMap);

      if (varMap.count("help"))
	{
	  std::cout << "Compare 2 instruction trace files. Skip to program "
		    << "counter before\n"
		    << "starting to compare if one is given\n";
	  std::cout << desc;
	  return 0;
	}

      // Collect command line values.
      verbose = varMap.count("verbose") > 0;
      if (varMap.count("file1"))
	file1 = varMap["file1"].as<std::string>();
      if (varMap.count("file2"))
	file2 = varMap["file2"].as<std::string>();
      if (varMap.count("startpc"))
	startPcString = varMap["startpc"].as<std::string>();
      if (not startPcString.empty())
	{
	  std::stringstream ss(startPcString);
	  if (startPcString.size() > 2 and startPcString[0] =='0' and
	      startPcString[1] == 'x')
	    ss >> std::hex;
	  ss >> startPc;
	}
    }
  catch (std::exception& exp)
    {
      std::cerr << "Failed to parse command line args: " << exp.what() << '\n';
      return 1;
    }

  std::string line1, line2;
  uint64_t lineNum1 = 0, lineNum2 = 0;
  char tag1, tag2;

  std::vector<uint64_t> fields1, fields2;

  std::ifstream input1(file1);
  if (not input1)
    {
      std::cerr << "Failed to open file " << file1 << " for reading\n";
      return 1;
    }

  std::ifstream input2(file2);
  if (not input2)
    {
      std::cerr << "Failed to open file " << file2 << " for reading\n";
      return 1;
    }

  /// First 7 items in each record.
  enum TagEnum { InstNum, Hart, Pc, Opcode, Resource, Addr, Value };
  std::vector<std::string> tags = { "inst-num", "hart", "pc", "opcode",
				    "resource", "address", "value" };

  if (not startPcString.empty())
    {
      bool found = false;  // True if start pc found.

      while (not found)
	{
	  if (not getNextRecord(file1, input1, lineNum1, line1, fields1))
	    break;
	  found = fields1.size() >= Pc and fields1[Pc] == startPc;
	}
      if (not found)
	{
	  std::cerr << "Failed to find start address (" << startPcString << ")"
		    << " in file " << file1 << '\n';
	  return 1;
	}

      found = false;
      while (not found)
	{
	  if (not getNextRecord(file2, input2, lineNum2, line2, fields2))
	    break;
	  found = fields2.size() >= Pc and fields2[Pc] == startPc;
	}
      if (not found)
	{
	  std::cerr << "Failed to find start address (" << startPcString << ")"
		    << " in file " << file2 << '\n';
	  return 1;
	}
    }
  else
    {
      getNextRecord(file1, input1, lineNum1, line1, fields2);
      getNextRecord(file2, input2, lineNum2, line2, fields2);
    }


  while (1)
    {
      if (fields1.size() < 7 or fields2.size() < 7)
	break;

      // We do not compare the instruction number.
      for (unsigned i = Hart; i <= Value; ++i)
	{
	  if (fields1[i] == fields2[i])
	    continue;

	  std::string item1, item2;
	  if (i == Resource)
	    {
	      item1.append(1, fields1[i]);
	      item2.append(1, fields2[i]);
	    }
	  else if (i == InstNum or i == Hart)
	    {
	      item1 = boost::lexical_cast<std::string>(fields1[i]);
	      item2 = boost::lexical_cast<std::string>(fields2[i]);
	    }
	  else
	    {
	      std::ostringstream oss1;
	      oss1 << std::hex << fields1[i];
	      item1 = oss1.str();

	      std::ostringstream oss2;
	      oss2 << std::hex << fields2[i];
	      item2 = oss2.str();
	    }

	  std::string tag = tags[i];

	  std::cerr << "Difference found in $tag field:\n"
		    << "  File " << file1 << ", " << tag << ": "
		    << item1 << '\n'
		    << "  File " << file2 << ", " << tag << ": "
		    << item2 << '\n'
		    << "  File " << file1 << ", Line " << lineNum1
		    << ": " << line1 << '\n'
		    << "  File " << file2 << ", Line " << lineNum2
		    << ": " << line2 << '\n';
	  return 1;
	}

      getNextRecord(file1, input1, lineNum1, line1, fields1);
      getNextRecord(file2, input2, lineNum2, line2, fields2);
    }


  if (fields1.size() != fields2.size())
    {
      if (fields1.size() == 0)
	std::cerr << "File " << file1 << " ends too early\n";
      if (fields2.size() == 0)
	std::cerr << "File " << file2 << " ends too early\n";
      return 1;
    }

  return 0;
}

