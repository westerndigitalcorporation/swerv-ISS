#include <iostream>
#include <string>
#include <vector>


// Change spike trace records to whisper format. Input is taken from
// the standard input. Output is placed on the standard output.
//
// A spike trace record starts with a pound sign and has the form:
//   #<n> core <hart>: <pc>   (<opcode>)    <text>
// Example:
//   #7   core 0:      0x0004 (0x6e000297)  auipc   t0, 0x6e000
//
// It is followed by zero or more annotation lines of the form:
//   <mode> <pc>   (<opcode>)   <resource> <address> <value>
// or
//   <mode> <pc>   (<opcode>)   <address> <value>
// Example:
//   3      0x0004 (0x6e000297) x 5 0xee000004
//
// The <resource> is either 'x', 'f', or 'c'.  If it is 'x', sometimes
// the space before address is missing. We compensate.
//
// If we find no more than one annotation line, then we produce
// one whisper ercord for the spike record. If ther are two or
// more annotation lines, then we produce a whisper ercord for
// the spike record and its first annotation line and a repeated
// record (same <n> record number) for each of the remaining
// annotation lines.
//
// Ouptut line format:
//  #<n> <hart> <pc> <opcode> <resource> <address> <value>
// where resource is one of 'x', 'f', 'c', or 'm'.
//


bool
processRecord(size_t lineNum, const std::string& record,
	      const std::vector<std::string>& annotations)
{
  unsigned recNum = 0, hart = 0;
  uint64_t pc = 0;
  uint32_t opcode = 0;
  size_t recLineNum = lineNum;
  int length = 0; // Length of text containing 1st 6 tokens of record.

  if (sscanf(record.c_str(), "#%ud core %ud: %lx (%ux)%n", &recNum, &hart,
	     &pc, &opcode, &length) != 4)
    {
      std::cerr << "Line " << lineNum << ": invalid record: " << record
		<< '\n';
      return false;
    }
  pc = (pc << 32) >> 32;  // Clear upper 32-bits

  const char* assemText = record.data() + length;

  unsigned valid = 0; // valid annotations.
  for (auto& ann : annotations)
    {
      lineNum++;
  
      // Annotation line has the form:
      //     <mode> <pc>       (<opcode>)  <tag> <addr> <value>
      //     3      0x00001000 (0x00000297) x    5      0x00001000
      // or
      //     <mode> <pc>       (<opcode>)  <addr>       <value>
      //     3      0x00001000 (0x00000297) 0x00400000  0x5fff5fff
      //
      uint64_t pc2 = 0;
      uint32_t opcode2 = 0, mode = 0;
      char resChar;  // First character of resource.
      if (sscanf(ann.c_str(), "%ud %lx (%x) %c%n", &mode, &pc2, &opcode2,
		 &resChar, &length) != 4)
	continue; // Spurious line.  Ignore.

      if (pc != pc2)
	std::cerr << "Warning: pc mismatch on lines " << recLineNum
		  << " and " << lineNum << '\n';
      if (opcode != opcode2)
	std::cerr << "Warning: opcode mismatch on lines " << recLineNum
		  << " and " << lineNum << '\n';

      uint64_t addr = 0, val = 0;

      // If resource char is 0 then it is a memory record.
      if (resChar == '0')
	if (sscanf(ann.c_str() + length - 1, "%lx %lx", &addr, &val) == 2)
	  {
	    printf("#%d %d %08lx %08x m %08lx 0x%08lx %s\n", recNum,
		   hart, pc2, opcode2, addr, val, assemText);
	    valid++;
	    continue;
	  }

      // If next char is x then it is an integer register record.
      addr = 0;
      if (resChar == 'x')
	if (sscanf(ann.c_str() + length, "%lud %lx", &addr, &val) == 2)
	  {
	    printf("#%d %d %08lx %08x r %lx 0x%08lx %s\n", recNum, hart, pc2,
		   opcode2, addr, val, assemText);
	    valid++;
	    continue;
	  }

      // If next char is c then it is a CSR record.
      if (resChar == 'c')
	{
	  // We may have 'c' or 'csr' so scan and discard a string.
	  uint64_t val = 0;
	  if (sscanf(ann.c_str() + length-1, "%*s %lx %lx", &addr, &val) == 2)
	    {
	      printf("#%d %d %08lx %08x c 0x%08lx 0x%08lx %s\n", recNum,
		     hart, pc2, opcode2, addr, val, assemText);
	      valid++;
	      continue;
	    }
	}

      std::cerr << "Line " << lineNum << ": Bad annotation line: "
		<< ann << '\n';
      return false;
    }

  if (not valid)  // Nothing printed so far.
    {
      printf("#%d %d %08lx %08x r %x %x %s\n", recNum, hart, pc, opcode,
	     0, 0, assemText);
    }

  return true;
}


int
main(int argc, char* argv[])
{
  std::string pending; // pending input line.
  size_t lineNum = 0;
  std::vector<std::string> annotations;

  while (true)
    {
      std::string record = pending;
      pending.clear();
      if (record.empty())
	{
	  while (std::getline(std::cin, record))
	    {
	      ++lineNum;
	      if (not record.empty() and record[0] == '#')
		break;
	    }
	}

      if (record.empty())
	break;

      size_t recLineNum = lineNum;

      // Read all the annotation lines (lines that do not begin with #)
      // belonging to current record.
      annotations.clear();

      while (std::getline(std::cin, pending))
	{
	  lineNum++;
	  if (not pending.empty() and pending[0] == '#')
	    break;
	  annotations.push_back(pending);
	}

      if (not processRecord(recLineNum, record, annotations))
	return 1;
    }

  return 0;
}
