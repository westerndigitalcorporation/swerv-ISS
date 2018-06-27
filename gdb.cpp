#include <stdio.h>
#include <iostream>
#include <sstream>
#include <boost/format.hpp>
#include "Core.hpp"


static
int
putDebugChar(char c)
{
  return putchar(c);
}


static
int
getDebugChar()
{
  return getchar();
}


static
int
hexCharToInt(unsigned char c)
{
  if (c >= 'a' and c <= 'f')
    return 10 + c - 'a';
  if (c >= 'A' and c <= 'F')
    return 10 + c - 'A';
  if (c >= '0' and c <= '9')
    return c - '0';
  return -1;
}


// Receive a packet from gdb. Request a retransmit from gdb if packet
// checksum is incorrect. Return succesfully received packet.
static
std::string
receivePacketFromGdb()
{
  std::string data;  // Data part of packet.

  unsigned char ch = ' '; // Anything besides $ will do.

  while (1)
    {
      while (ch != '$')
	ch = getDebugChar();

      uint8_t sum = 0;  // checksum
      while (1)
	{
	  ch = getDebugChar();
          if (ch == '$')
            break;
	  if (ch == '#')
	    break;
	  sum += ch;
	  data.push_back(ch);
	}

      if (ch == '$')
	continue;

      if (ch == '#')
	{
	  ch = getDebugChar();
	  uint8_t pacSum = hexCharToInt(ch) << 4; // Packet checksum
	  ch = getDebugChar();
	  pacSum += hexCharToInt(ch);

	  if (sum != pacSum)
	    {
	      std::cerr << "Bad checksum form gdb: "
			<< (boost::format("%02x v %02x") % unsigned(sum) %
			    unsigned(pacSum))
			<< '\n';
	      putDebugChar('-'); // Signal failed reception.
	    }
	  else
	    {
	      putDebugChar('+');  // Signal successul reception.
	      fflush(stdout);

	      // If sequence char present, reply with sequence id.
	      if (data.size() >= 3 and data.at(2) == ':')
		{
		  putDebugChar(data.at(0));
		  putDebugChar(data.at(1));
		  data = data.substr(3);
		}
	      std::cerr << "Received from gdb: $" << data << "#"
			<< (boost::format("%02x") % unsigned(pacSum)) << '\n';
	      return data;
	    }
	}
    }

  return data; // Passify compiler
}


// Send given data string as a gdb remote packet. Resend until a
// positive ack is received.
//
// Format of packet:  $<data>#<checksum>
//
// TODO: quote special characters.
static void
sendPacketToGdb(const std::string& data)
{
  const char hexDigit[] = "0123456789abcdef";

  while (true)
    {
      putDebugChar('$');
      unsigned char checksum = 0;
      for (unsigned char c : data)
	{
	  putDebugChar(c);
	  checksum += c;
	}

      putDebugChar('#');
      putDebugChar(hexDigit[checksum >> 4]);
      putDebugChar(hexDigit[checksum & 0xf]);
      fflush(stdout);

      std::cerr << "Send to gdb: " << data << '\n';

      char c = getDebugChar();
      if (c == '+')
	return;
    }
}



template <typename URV>
void
handleExceptionForGdb(WdRiscv::Core<URV>& core)
{
  // The trap handler is expected to set the PC to point to the instruction
  // after the one with the exception if necessary/possible.

  // Construct a reply of the form T xx n1:r1;n2:r2;... where xx is
  // the trap cause and ni is a resource (e.g. register number) and ri
  // is the resource data (e.g. content of register).
  std::ostringstream reply;

  unsigned signalNum = 0;
  URV cause = 0;
  if (core.peekCsr(WdRiscv::CsrNumber::MCAUSE, cause))
    signalNum = cause; // FIX.

  reply << "T" << (boost::format("%02x") % signalNum);

  const char* hexForm = sizeof(URV) == 4 ?  "%08x" : "%016x";

  URV spVal = 0;
  URV spNum = WdRiscv::RegSp;
  core.peekIntReg(spNum, spVal);
  reply << (boost::format("%02x") % spNum) << ':'
	<< (boost::format(hexForm) % spVal) << ';';

  sendPacketToGdb(reply.str());

  while (1)
    {
      reply.str("");
      reply.clear();

      std::string packet = receivePacketFromGdb();
      if (packet.empty())
	continue;

      switch (packet.at(0))
	{
	case '?':
	  // Return signal number
	  reply << "S" << (boost::format("%02x") % signalNum);
	  break;

	case 'g':  // return the value of the CPU registers
	  {
	    for (unsigned i = 0; i < core.intRegCount(); ++i)
	      {
		URV val = 0;
		core.peekIntReg(i, val);
		reply << (boost::format(hexForm) % val);
	      }
	  }
	  break;

	case 'G':  // set the value of the CPU registers - return OK
	  {
	    std::string status = "OK";
	    size_t len = packet.length();
	    if (len + 1 < core.intRegCount() * sizeof(URV) * 2)
	      status = "E01";
	    else
	      {
		const char* ptr = packet.c_str() + 1;
		for (unsigned i = 0; i < core.intRegCount(); ++i)
		  {
		    std::string buffer;
		    for (unsigned i = 0; i < 2*sizeof(URV); ++i)
		      buffer += *ptr++;
		    char* end = nullptr;
		    URV val = strtoull(buffer.c_str(), &end, 16);
		    if (*end != 0)
		      {
			status  = "E01";
			break;
		      }
		    core.pokeIntReg(i, val);
		  }
	      }

	    reply << status;
	  }
	  break;

	case 'm': // mAA..AA,LLLL  Read LLLL bytes at address AA..AA
	  {
	    const char* ptr = packet.c_str() + 1;
	    char* end = nullptr;
	    URV addr = strtoull(ptr, &end, 16);
	    if (*end != ',')
	      reply << "E01";
	    else
	      {
		URV len = strtoull(end+1, &end, 16);
		if (*end != 0)
		  reply << "E01";
		else
		  {
		    for (URV ix = 0; ix < len; ++ix)
		      {
			uint8_t byte = 0;
			core.peekMemory(addr, byte);
			addr++;
			reply << (boost::format("%02x") % unsigned(byte));
		      }
		  }
	      }
	  }
	  break;

	case 'M': // MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK
	  {
	    const char* ptr = packet.c_str() + 1;
	    char* end = nullptr;
	    URV addr = strtoull(ptr, &end, 16);
	    if (*end != ',')
	      reply << "E01";
	    else
	      {
		URV len = strtoull(end+1, &end, 16);
		if (*end != ':')
		  reply << "E01";
		else
		  {
		    ptr = end + 1;
		    for (URV ix = 0; ix < len; ++ix, ++addr)
		      {
			uint8_t byte = hexCharToInt(*ptr++);
			byte = (byte << 4) | hexCharToInt(*ptr++);
			core.pokeMemory(addr, byte);
		      }
		    reply << "OK";
		  }
	      }
	  }
	  break;

	case 'c':  // cAA..AA    Continue at address AA..AA(optional)
	  {
	    const char* ptr = packet.c_str() + 1;
	    if (*ptr)
	      {
		char* end = nullptr;
		URV newPc = strtoull(ptr, &end, 16);
		if (*end != 0)
		  core.pokePc(newPc);
	      }
	  }
	  return;

	case 'p':  // pn    Read value of register n
	  {
	    const char* ptr = packet.c_str() + 1;
	    if (not *ptr)
	      reply << "E01";
	    else
	      {
		char* end = nullptr;
		unsigned n = strtoul(ptr, &end, 16);
		if (*end != 0)
		  reply << "E01";
		else
		  {
		    URV value = 0; bool ok = true;
		    if (n < core.intRegCount())
		      ok = core.peekIntReg(n, value);
		    else if (n == core.intRegCount())
		      value = core.peekPc();
		    else
		      ok = core.peekCsr(WdRiscv::CsrNumber(n), value);
		    if (ok)
		      reply << (boost::format(hexForm) % value);
		    else
		      reply << "E01";
		  }
	      }
	  }
	  break;

	case 'k':  // kill
	  break;

	default:
	  reply << "";   // Unsupported comand: Empty response.
	}

      // Reply to the request
      sendPacketToGdb(reply.str());
    }
}


template void handleExceptionForGdb<uint32_t>(WdRiscv::Core<uint32_t>&);
template void handleExceptionForGdb<uint64_t>(WdRiscv::Core<uint64_t>&);
