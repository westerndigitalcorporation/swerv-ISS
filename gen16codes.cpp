#include <stdio.h>
#include "Core.hpp"
#include "Inst.hpp"

using namespace WdRiscv;


static void
printExpanded(Core<uint32_t>& core, uint16_t inst16)
{
  uint32_t inst32 = 0;
  if (not core.expandInst(inst16, inst32))
    return;

  printf("%04x %08x -- ", inst16, inst32);
  std::string asm16, asm32;
  core.disassembleInst(inst16, asm16);
  if (not asm16.empty())
    printf("%s", asm16.c_str());
  printf(" -- ");
  core.disassembleInst(inst32, asm32);
  if (not asm32.empty())
    printf("%s", asm32.c_str());
  printf("\n");
}


int
main(int argc, char* argv[])
{
  Core<uint32_t> core(1024, 32);

  printExpanded(core, 0x1002);
  return 0;

  for (unsigned rd = 0; rd < 32; ++rd)
    for (unsigned rs2 = 0; rs2 < 32; ++rs2)
      {
	CiFormInst cif(0);
	if (cif.encodeCadd(rd, rs2))
	  printExpanded(core, cif.code);
      }

  for (unsigned rd = 0; rd < 32; ++rd)
    for (int imm = -33; imm <= 32; ++imm)
      {
	CiFormInst cif(0);
	if (cif.encodeCaddi(rd, imm))
	  printExpanded(core, cif.code);
      }

  for (unsigned imm = 0; imm <= 256; ++imm)
    {
      CiFormInst cif(0);
      if (cif.encodeCaddi16sp(imm))
	printExpanded(core, cif.code);
    }

  for (unsigned rd = 0; rd <= 8; ++rd)
    for (unsigned offset = 0; offset <= 256; ++offset)
      {
	CiwFormInst ciwf(0);
	if (ciwf.encodeCaddi4spn(rd, offset))
	  printExpanded(core, ciwf.code);
      }

  for (unsigned rdp = 0; rdp <= 8; rdp++)
    for (unsigned rs2p = 0; rs2p <= 8; rs2p++)
      {
	CaiFormInst caif(0);
	if (caif.encodeCand(rdp, rs2p))
	  printExpanded(core, caif.code);
      }

  for (unsigned rdp = 0; rdp <= 8; rdp++)
    for (int imm = -33; imm <= 32; imm++)
      {
	CaiFormInst caif(0);
	if (caif.encodeCandi(rdp, imm))
	  printExpanded(core, caif.code);
      }

  for (unsigned rs1p = 0; rs1p <= 8; rs1p++)
    for (int offset = -1 << 9; offset < (1 << 9); offset++)
      {
	CbFormInst cbf(0);
	if (cbf.encodeCbeqz(rs1p, offset))
	  printExpanded(core, cbf.code);
      }

  for (unsigned rs1p = 0; rs1p <= 8; rs1p++)
    for (int offset = -1 << 9; offset < (1 << 9); offset++)
      {
	CbFormInst cbf(0);
	if (cbf.encodeCbnez(rs1p, offset))
	  printExpanded(core, cbf.code);
      }

  CiFormInst cif(0);
  if (cif.encodeCebreak())
    printExpanded(core, cif.code);

  for (int offset = -1 << 12; offset < (1 << 12); ++offset)
    {
      CjFormInst cjf(0);
      if (cjf.encodeCj(offset))
	printExpanded(core, cjf.code);
    }

  for (int offset = -1 << 12; offset < (1 << 12); ++offset)
    {
      CjFormInst cjf(0);
      if (cjf.encodeCjal(offset))
	printExpanded(core, cjf.code);
    }

  for (unsigned rs1 = 0; rs1 < 34; ++rs1)
    {
      CiFormInst cif(0);
      if (cif.encodeCjalr(rs1))
	printExpanded(core, cif.code);
    }

  for (unsigned rs1 = 0; rs1 < 34; ++rs1)
    {
      CiFormInst cif(0);
      if (cif.encodeCjr(rs1))
	printExpanded(core, cif.code);
    }

  return 0;
}
