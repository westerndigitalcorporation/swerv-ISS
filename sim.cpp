#include <iostream>
#include "Core.hpp"


using namespace WdRiscv;

int
main(int argc, char* argv[])
{
  size_t memorySize = 3*1024*1024*size_t(1024);
  size_t registerCount = 32;
  Core<uint32_t> core(memorySize, registerCount);

  core.initialize();
  core.selfTest();

  size_t entryPoint = 0, exitPoint = 0;
  if (core.loadElfFile("diag.exe", entryPoint, exitPoint))
    core.pokePc(entryPoint);

  core.snapshotState();
 
  core.runUntilAddress(exitPoint);
  uint32_t val = 0;

  core.peekIntReg(1, val);
  std::cout << val << std::endl;

  core.printStateDiff(std::cout);
  return 0;
}
