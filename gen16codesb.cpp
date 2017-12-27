#include <stdio.h>
#include "Core.hpp"
#include "Inst.hpp"

using namespace WdRiscv;


static void
printExpanded(Core<uint32_t>& core, uint16_t inst16)
{
  uint32_t inst32 = 0;
  if (core.expandInst(inst16, inst32))
    {
#if 0
      std::string asm16, asm32;
      core.disassembleInst(inst16, asm16);
      printf("%s", asm16.c_str());
      core.disassembleInst(inst32, asm32);
      printf("   %s\n", asm32.c_str());
#endif
    }
  printf("%04x %08x\n", inst16, inst32);
}


int
main(int argc, char* argv[])
{
  Core<uint32_t> core(1024, 32);

  for (unsigned inst16 = 0; inst16 <= 0xffff; ++inst16)
    printExpanded(core, inst16);

  return 0;
}
