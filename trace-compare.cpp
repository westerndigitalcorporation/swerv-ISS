#include <iostream>
#include <fstream>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

/// Sample trace record:
///    #1 0 00001000 00000297 r 5 0x00001000 auipc t0, 0x0
/// The first 7 tokens of the line represent the instruction number,
/// program counter, instruction code, resource tag, resource address
/// and resource value respectively. The instruction number and hart
/// are in decimal notation. Thre renaming fields (except tag) are in
/// heaxdecimal notation.

enum class Resource { IntReg, CsReg, Mem };
std::vector<std::string> resourceTags = { "r", "c", "m" };

struct Record
{
  uint64_t instNum;
  uint64_t hart;
  uint64_t pc;
  uint64_t opcode;
  Resource resource;
  uint64_t addr;
  uint64_t value;

  uint64_t lineNum = 0;
  std::string line;
  bool valid = false;

  // Order records for sorting:
  //  1. Order using resource tag with r < c < m
  //  2. If same resource tag order using addr
  bool operator< (const Record& other)
  {
    if (resource < other.resource)
      return true;

    if (resource > other.resource)
      return false;

    return addr < other.addr;
  }
};


/// Trace file reader
struct Reader
{
  Reader(const std::string& fileName, std::istream& stream)
    : fileName_(fileName), stream_(stream), lineNum_(0)
  { }

  /// Read next record. Return true on success and false if stream is
  /// exhausted before the next record can be read.
  bool read(Record& record);

  /// Skip till the record having the given program counter.  Return
  /// true if record is found. False if stream is exhausted before
  /// finding the target pc.
  bool skipTillPc(uint64_t pc);

  /// Read the next sequence of one or more records sharing the same
  /// instruction number. Put read recod in the block vector (cleared
  /// on entyr). Return false if no more records in file.
  bool readBlock(std::vector<Record>& block);

  const std::string& fileName_;
  std::istream& stream_;
  uint64_t lineNum_;
  Record pending_; // Pending record
};


bool
Reader::read(Record& record)
{
  if (pending_.valid)
    {
      record = pending_;
      pending_.valid = false;
      return true;
    }

  record.valid = false;

  std::string line;
  while (std::getline(stream_, line))
    {
      lineNum_++;
      // TODO: remove comments.

      if (line.empty())
	continue;

      if (line[0] != '#')
	continue;  // Spurious non-trace record. Ignore.

      uint64_t instNum = 0, hart = 0, pc = 0, opcode = 0, addr = 0, val = 0;
      char tag;
      if (sscanf(line.c_str(), "#%lld %lld %llx %llx %c %llx %llx",
		 &record.instNum, &record.hart, &record.pc,
		 &record.opcode, &tag, &record.addr,
		 &record.value) != 7)
	{
	  std::cerr << "File " << fileName_ << ", Line " << lineNum_
		    << ": Invalid trace record: " << line << '\n';
	  return false;
	}

      if (tag == 'r')
	record.resource = Resource::IntReg;
      else if (tag == 'c')
	record.resource = Resource::CsReg;
      else if (tag == 'm')
	record.resource = Resource::Mem;
      else
	{
	  std::cerr << "File "<< fileName_ << ", Line " << lineNum_
		    << ": Invalid resource char: " << tag << '\n';
	  return false;
	}

      record.line = line;
      record.lineNum = lineNum_;
      record.valid = true;
      return true;
    }

    return false;
}


bool
Reader::skipTillPc(uint64_t pc)
{
  Record record;

  while (read(record))
    {
      if (record.pc == pc)
	{
	  pending_ = record;
	  return true;
	}
    }

  return false;
}


bool
Reader::readBlock(std::vector<Record>& block)
{
  block.clear();

  Record rec;
  if (not read(rec))
    return false;

  uint64_t instNum = rec.instNum;

  block.push_back(rec);

  while (read(rec))
    {
      if (rec.instNum == instNum)
	block.push_back(rec);
      else
	{
	  pending_ = rec;
	  break;
	}
    }

  // Source block records by resource tag (r < c < m) and by address if
  // same tag.
  if (block.size() > 1)
    std::sort(block.begin(), block.end());

  return true;
}


std::string
hexString(uint64_t num)
{
  std::ostringstream oss;
  oss << std::hex << num;
  return oss.str();
}


bool
compareRecords(const Record& r1, const Record& r2,
	       std::string& fieldName, std::string& val1,
	       std::string& val2)
{
  if (r1.hart == r2.hart and r1.pc == r2.pc and r1.opcode == r2.opcode and
      r1.resource == r2.resource and r1.addr == r2.addr and
      r1.value == r2.value)
    return true;
      
  if (r1.hart != r2.hart)
    {
      fieldName = "hart";
      val1 = boost::lexical_cast<std::string>(r1.hart);
      val2 = boost::lexical_cast<std::string>(r2.hart);
      return false;
    }

  if (r1.pc != r2.pc)
    {
      fieldName = "pc";
      val1 = hexString(r1.pc);
      val2 = hexString(r2.pc);
      return false;
    }

  if (r1.opcode != r2.opcode)
    {
      fieldName = "opcode";
      val1 = hexString(r1.opcode);
      val2 = hexString(r2.opcode);
      return false;
    }

  if (r1.resource != r2.resource)
    {
      val1.clear(); val2.clear();
      fieldName = "resource";
      val1 = resourceTags.at(size_t(r1.resource));
      val2 = resourceTags.at(size_t(r2.resource));
      return false;
    }

  if (r1.addr != r2.addr)
    {
      fieldName = "address";
      val1 = hexString(r1.addr);
      val2 = hexString(r2.addr);
      return false;
    }

  if (r1.value != r2.value)
    {
      fieldName = "value";
      val1 = hexString(r1.value);
      val2 = hexString(r2.value);
      return false;
    }

  return false;
}


void
printDiffs(const std::string& file1, const std::string& file2,
	   const Record& rec1, const Record& rec2,
	   const std::string& fieldName, const std::string& val1,
	   const std::string& val2)
{
  std::cerr << "Difference found in " << fieldName << " field:\n"
	    << "  File " << file1 << ", " << fieldName << ": "
	    << val1 << '\n';

  std::cerr << "  File " << file2 << ", " << fieldName << ": "
	    << val2 << '\n';

  std::cerr << "  File " << file1 << ", Line " << rec1.lineNum
	    << ": " << rec1.line << '\n';

  std::cerr << "  File " << file2 << ", Line " << rec2.lineNum
	    << ": " << rec2.line << '\n';
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
	("file1,a", po::value<std::string>(),
	 "File to compare")
	("file2,b", po::value<std::string>(),
	 "File to compare")
	("startpc,s", po::value<std::string>(),
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

  Reader reader1(file1, input1);
  Reader reader2(file2, input2);

  if (not startPcString.empty())
    {
      if (not reader1.skipTillPc(startPc))
	{
	  std::cerr << "Failed to find start address (" << startPcString << ")"
		    << " in file " << file1 << '\n';
	  return 1;
	}

      if (not reader2.skipTillPc(startPc))
	{
	  std::cerr << "Failed to find start address (" << startPcString << ")"
		    << " in file " << file2 << '\n';
	  return 1;
	}
    }

  std::vector<Record> block1;
  std::vector<Record> block2;

  uint64_t errors = 0;
  std::string val1, val2, fieldName;

  while (not errors)
    {
      reader1.readBlock(block1);
      reader2.readBlock(block2);
      if (block1.empty() or block2.empty())
	break;

      size_t ix1 = 0, ix2 = 0;
      while (ix1 < block1.size() and ix2 < block2.size() and not errors)
	{
	  const Record& rec1 = block1[ix1];
	  const Record& rec2 = block2[ix2];
	  // The blocks should match record by record; however, spike
	  // currently drops some/all records related to CSRs. We
	  // compensate by ignording missing records.
	  if (rec1.resource == rec2.resource)
	    {
	      if (not compareRecords(rec1, rec2, fieldName, val1, val2))
		{
		  printDiffs(file1, file2, rec1, rec2, fieldName, val1, val2);
		  errors++;
		}
	      ix1++; ix2++;
	      continue;
	    }

	  if (block1[ix1].resource == Resource::CsReg)
	    ix1++;
	  else if (block2[ix2].resource == Resource::CsReg)
	    ix2++;
	  else
	    {
	      if (not compareRecords(rec1, rec2, fieldName, val1, val2))
		{
		  printDiffs(file1, file2, rec1, rec2, fieldName, val1, val2);
		  errors++;
		}
	      ix1++; ix2++;
	    }
	}
    }


  if (block1.size() != block2.size() and not errors)
    {
      if (block1.empty())
	std::cerr << "File " << file1 << " ends too early\n";
      else if (block2.empty() == 0)
	std::cerr << "File " << file2 << " ends too early\n";
      errors++;
    }

  return errors == 0 ? 0 : 1;
}

