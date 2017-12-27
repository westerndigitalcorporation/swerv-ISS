#include <iostream>
#include "Core.hpp"


using namespace WdRiscv;

int
main(int argc, char* argv[])
{
  size_t memorySize = 2*1024;
  size_t registerCount = 32;
  Core<uint32_t> core(memorySize, registerCount);

  core.initialize();
  core.loadHexFile("hex.hex");
  
  core.runUntilAddress(0x118);
  uint32_t val = 0;
  core.peekIntReg(1, val);
  std::cout << val << std::endl;
}
