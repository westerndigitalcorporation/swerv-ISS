//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2018 Western Digital Corporation or its affiliates.
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
// 
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//

#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <boost/format.hpp>
#include "Core.hpp"

#ifdef __MINGW64__
#define SIGTRAP 5
#endif

static
int
putDebugChar(char c)
{
  return putchar(c);
}


static
uint8_t
getDebugChar()
{
  return static_cast<uint8_t>(getchar());
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


static
bool
hexCharToInt(char c, unsigned& value)
{
  if (c >= 'a' and c <= 'f')
    {
      value = 10 + c - 'a';
      return true;
    }

  if (c >= 'A' and c <= 'F')
    {
      value = 10 + c - 'A';
      return true;
    }

  if (c >= '0' and c <= '9')
    {
      value = c - '0';
      return true;
    }

  return false;
}


static
bool
getStringComponents(const std::string& str, char delim,
		    std::string& comp1, std::string& comp2)
{
  auto sepIx = str.find(delim);
  if (sepIx == std::string::npos)
    return false;

  comp1 = str.substr(0, sepIx);
  comp2 = str.substr(sepIx + 1);
  return true;
}


static
bool
getStringComponents(const std::string& str, char delim1, char delim2,
		    std::string& comp1, std::string& comp2, std::string& comp3)
{
  auto delim1Ix = str.find(delim1);
  if (delim1Ix == std::string::npos)
    return false;

  comp1 = str.substr(0, delim1Ix);

  auto delim2Ix = str.find(delim2, delim1Ix + 1);
  if (delim1Ix == std::string::npos)
    return false;

  comp2 = str.substr(delim1Ix + 1, delim2Ix - delim1Ix - 1);
  comp3 = str.substr(delim2Ix + 1);
  return true;
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
	  sum = static_cast<uint8_t>(sum + ch);
	  data.push_back(ch);
	}

      if (ch == '$')
	continue;

      if (ch == '#')
	{
	  ch = getDebugChar();
	  uint8_t pacSum = static_cast<uint8_t>(hexCharToInt(ch) << 4); // Packet checksum
	  ch = getDebugChar();
	  pacSum = static_cast<uint8_t>(pacSum + hexCharToInt(ch));

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
#if 0
	      std::cerr << "Received from gdb: $" << data << "#"
			<< (boost::format("%02x") % unsigned(pacSum)) << '\n';
#endif
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
	  checksum = static_cast<uint8_t>(checksum + c);
	}

      putDebugChar('#');
      putDebugChar(hexDigit[checksum >> 4]);
      putDebugChar(hexDigit[checksum & 0xf]);
      fflush(stdout);

      // std::cerr << "Send to gdb: " << data << '\n';

      char c = getDebugChar();
      if (c == '+')
	return;
    }
}


/// Return hexadecimal representation of given integer register value.
template <typename T>
std::string
littleEndianIntToHex(T val)
{
  std::ostringstream oss;

  for (size_t i = 0; i < sizeof(T); ++i)
    {
      unsigned byte = val & 0xff;
      val = val >> 8;
      oss << (boost::format("%02x") % byte);
    }

  return oss.str();
}


/// Convert given little-endian hexadecimal string to an integer value
/// of type T. Return true on success and false if the string does not
/// contain a hexadecimal string or if it is too large for type T.
template <typename T>
bool
littleEndianHexToInt(const std::string& str, T& value)
{
  size_t byteCount = 0;

  value = 0;

  for (size_t i = 0; i < str.size(); i += 2)
    {
      char c = str.at(i);
      unsigned byte =  0;
      if (not hexCharToInt(c, byte))
	return false;

      if (i + 1 < str.size())
	{
	  byte <<= 4;

	  char c2 = str.at(i+1);
	  unsigned x = 0;
	  if (not hexCharToInt(c2, x))
	    return false;
	  byte |= x;
	}
      value |= byte << (byteCount*8);
      byteCount++;
    }

  return byteCount <= sizeof(T);
}


/// Convert given hexadecimal string to an integer value of type
/// T. Return true on success and false if the string does not contain
/// a hexadecimal string or if it is too large for type T.
template <typename T>
bool
hexToInt(const std::string& str, T& value)
{
  value = 0;

  if (str.empty())
    return false;

  const char* data = str.c_str();
  char* end = nullptr;
  uint64_t v = strtoull(data, &end, 16);
  if (*end)
    return false;

  value = static_cast<T>(v);
  if (v != value)
    return false; // Overflow

  return true;
}


template <typename URV>
void
handlePeekRegisterForGdb(WdRiscv::Core<URV>& core, unsigned regNum,
			 std::ostream& stream)
{
  // Not documented but GBD uses indices 0-31 for integer registers,
  // 32 for pc, 33-64 for floating-point registers, 65 and higher for
  // CSRs.
  unsigned fpRegOffset = 33, pcOffset = 32, csrOffset = 65;
  URV value = 0; bool ok = true, fp = false;
  if (regNum < pcOffset)
    ok = core.peekIntReg(regNum, value);
  else if (regNum == pcOffset)
    value = core.peekPc();
  else if (regNum >= fpRegOffset and regNum < csrOffset)
    {
      fp = true;
      if (core.isRvf() or core.isRvd())
	{
	  unsigned fpReg = regNum - fpRegOffset;
	  uint64_t val64 = 0;
	  ok = core.peekFpReg(fpReg, val64);
	  if (ok)
	    stream << littleEndianIntToHex(val64);
	}
      else
	stream << "E03";
    }
  else
    {
      URV csr = regNum - csrOffset;
      ok = core.peekCsr(WdRiscv::CsrNumber(csr), value);
    }

  if (ok)
    {
      if (not fp)
	stream << littleEndianIntToHex(value);
    }
  else
    stream << "E04";
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

  unsigned signalNum = SIGTRAP;
  URV cause = 0;
  if (core.peekCsr(WdRiscv::CsrNumber::MCAUSE, cause))
    {
      if (cause == URV(WdRiscv::ExceptionCause::BREAKP))
	signalNum = SIGTRAP;
      // FIX:  implement other caues.
    }

  reply << "T" << (boost::format("%02x") % signalNum);

  URV spVal = 0;
  unsigned spNum = WdRiscv::RegSp;
  core.peekIntReg(spNum, spVal);
  reply << (boost::format("%02x") % spNum) << ':'
	<< littleEndianIntToHex(spVal) << ';';
  sendPacketToGdb(reply.str());

  bool gotQuit = false;

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
		reply << littleEndianIntToHex(val);
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
		    URV val = 0;
		    if (littleEndianHexToInt(buffer, val))
		      core.pokeIntReg(i, val);
		    else
		      {
			status  = "E01";
			break;
		      }
		  }
	      }

	    reply << status;
	  }
	  break;

	case 'm': // mAA..AA,LLLL  Read LLLL bytes at address AA..AA
	  {
	    std::string addrStr, lenStr;
	    if (not getStringComponents(packet.substr(1), ',', addrStr, lenStr))
	      reply << "E01";
	    else
	      {
		URV addr = 0, len = 0;
		if (not hexToInt(addrStr, addr) or not hexToInt(lenStr, len))
		  reply << "E02";
		else
		  {
		    for (URV ix = 0; ix < len; ++ix)
		      {
			uint8_t byte = 0;
			core.peekMemory(addr++, byte);
			reply << (boost::format("%02x") % unsigned(byte));
		      }
		  }
	      }
	  }
	  break;

	case 'M': // MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK
	  {
	    std::string addrStr, lenStr, data;
	    if (not getStringComponents(packet.substr(1), ',', ':', addrStr,
					lenStr, data))
	      reply << "E01";
	    else
	      {
		URV addr = 0, len = 0;
		if (not hexToInt(addrStr, addr) or not hexToInt(lenStr, len))
		  reply << "E02";
		else
		  {
		    if (data.size() < len*2)
		      reply << "E03";
		    else
		      {
			for (URV ix = 0; ix < len; ++ix)
			  {
			    int bb = hexCharToInt(data.at(2*ix));
			    bb = (bb << 4) | hexCharToInt(data.at(2*ix+1));
			    core.pokeMemory(addr++, static_cast<uint8_t>(bb));
			  }
			reply << "OK";
		      }
		  }
	      }
	  }
	  break;

	case 'c':  // cAA..AA    Continue at address AA..AA(optional)
	  {
	    if (packet.size() == 1)
	      return;

	    URV newPc = 0;
	    if (hexToInt(packet.substr(1), newPc))
	      {
		core.pokePc(newPc);
		return;
	      }

	    reply << "E01";
	  }
	  break;

	case 'p':  // pn    Read value of register n
	  {
	    std::string regNumStr = packet.substr(1);
	    unsigned regNum = 0;

	    if (not hexToInt(regNumStr, regNum))
	      reply << "E01";
	    else
	      handlePeekRegisterForGdb(core, regNum, reply);
	  }
	  break;

	case 'P':   // Pn=v   Set register n to v
	  {
	    auto eqIx = packet.find('=');
	    if (eqIx == std::string::npos or eqIx == 1)
	      reply << "E01";
	    else
	      {
		std::string regNumStr = packet.substr(1, eqIx - 1);
		std::string valueStr = packet.substr(eqIx + 1);
		unsigned regNum = 0;
		URV value = 0;
		if (not hexToInt(regNumStr, regNum))
		  reply << "E02";
		else if (not littleEndianHexToInt(valueStr, value))
		  reply << "E03";
		else
		  {
		    bool ok = true;
		    if (regNum < core.intRegCount())
		      ok = core.pokeIntReg(regNum, value);
		    else if (regNum == core.intRegCount())
		      core.pokePc(value);
		    else
		      ok = core.pokeCsr(WdRiscv::CsrNumber(regNum), value);
		    reply << (ok? "OK" : "E04");
		  }
	      }
	  }
	  break;

	case 's':
	  core.singleStep(nullptr);
	  handleExceptionForGdb(core);
	  return;

	case 'k':  // kill
	  reply << "OK";
	  gotQuit = true;
	  break;

	case 'q':
	  if (packet == "qAttached")
	    reply << "0";
	  else if (packet == "qOffsets")
	    reply << "Text=0;Data=0;Bss=0";
	  else if (packet == "qSymbol::")
	    reply << "OK";
	  else
	    {
	      std::cerr << "Unhandled gdb request: " << packet << '\n';
	      reply << ""; // Unsupported: Empty response.
	    }
	  break;

	case 'v':
	  if (packet == "vMustReplyEmpty")
	    reply << "";
	  else if (packet.find("vKill;") == 0)
	    {
	      reply << "OK";
	      gotQuit = true;
	    }
	  else
	    {
	      std::cerr << "Unhandled gdb request: " << packet << '\n';
	      reply << ""; // Unsupported: Empty response.
	    }
	  break;

	default:
	  std::cerr << "Unhandled gdb request: " << packet << '\n';
	  reply << "";   // Unsupported comand: Empty response.
	}

      // Reply to the request
      sendPacketToGdb(reply.str());

      if (gotQuit)
	exit(0);
    }
}


template void handleExceptionForGdb<uint32_t>(WdRiscv::Core<uint32_t>&);
template void handleExceptionForGdb<uint64_t>(WdRiscv::Core<uint64_t>&);
