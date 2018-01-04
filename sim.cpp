#include <iostream>
#include "Core.hpp"


using namespace WdRiscv;

int
main(int argc, char* argv[])
{
  size_t memorySize = 20*1024*1024;
  size_t registerCount = 32;
  Core<uint32_t> core(memorySize, registerCount);

  core.initialize();
  core.loadHexFile("hex.hex");

  size_t entryPoint = 0;
  core.loadElfFile("gen16codesb", entryPoint);

  core.snapshotState();
 
  core.runUntilAddress(0x120);
  uint32_t val = 0;

  core.peekIntReg(1, val);
  std::cout << val << std::endl;

  core.printStateDiff(std::cout);
  return 0;
}
