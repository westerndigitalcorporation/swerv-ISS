#include "Inst.hpp"

using namespace WdRiscv;

bool
RFormInst::encodeAdd(unsigned rdv, unsigned rs1v, unsigned rs2v)
{
  if (rdv > 31 or rs1v > 31 or rs2v > 31)
    return false;
  opcode = 0x33;
  rd = rdv;
  funct3 = 0;
  rs1 = rs1v;
  rs2 = rs2v;
  funct7 = 0;
  return true;
}


bool
RFormInst::encodeSub(unsigned rd, unsigned rs1, unsigned rs2)
{
  if (not encodeAdd(rd, rs1, rs2))
    return false;
  funct7 = 0x20;
  return true;
}


bool
RFormInst::encodeOr(unsigned rd, unsigned rs1, unsigned rs2)
{
  if (not encodeAdd(rd, rs1, rs2))
    return false;
  funct3 = 0x6;
  return true;
}


bool
RFormInst::encodeXor(unsigned rdv, unsigned rs1v, unsigned rs2v)
{
  if (not encodeAdd(rdv, rs1v, rs2v))
    return false;
  funct3 = 4;
  return true;
}


bool
RFormInst::encodeAnd(unsigned rdv, unsigned rs1v, unsigned rs2v)
{
  if (not encodeAdd(rdv, rs1v, rs2v))
    return false;
  funct3 = 7;
  return true;
}


bool
RFormInst::encodeAddw(unsigned rdv, unsigned rs1v, unsigned rs2v)
{
  opcode = 0x3b;
  rd = rdv;
  funct3 = 0;
  rs1 = rs1v;
  rs2 = rs2v;
  funct7 = 0;
  return true;
}


bool
RFormInst::encodeSubw(unsigned rdv, unsigned rs1v, unsigned rs2v)
{
  opcode = 0x3b;
  rd = rdv;
  funct3 = 0;
  rs1 = rs1v;
  rs2 = rs2v;
  funct7 = 0x20;
  return true;
}


bool
RFormInst::encodeAdd(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeAdd(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
RFormInst::encodeSub(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeSub(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
RFormInst::encodeOr(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeOr(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
RFormInst::encodeXor(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeXor(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
RFormInst::encodeAnd(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeAnd(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
RFormInst::encodeAddw(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeAddw(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
RFormInst::encodeSubw(unsigned rd, unsigned rs1, unsigned rs2, uint32_t& inst)
{
  RFormInst rfi(0);
  if (not rfi.encodeSubw(rd, rs1, rs2))
    return false;
  inst = rfi.code;
  return true;
}


bool
BFormInst::encodeBeq(unsigned rs1v, unsigned rs2v, int imm)
{
  if (imm & 0x1)
    return false;  // Least sig bit must be 0.
  if (rs1 > 31 or rs2 > 31 or imm >= (1 << 12) or imm < (-1 << 12))
    return false;  // Immediate must fit in 13 bits.

  opcode = 0x63;
  imm11 = (imm >> 11) & 1;
  imm4_1 = (imm >> 1) & 0xf;
  imm10_5 = (imm >> 5) & 0x3f;
  imm12 = (imm >> 12) & 0x1;
  funct3 = 0;
  rs1 = rs1v;
  rs2 = rs2v;
  return true;
}


bool
BFormInst::encodeBne(unsigned rs1, unsigned rs2, int imm)
{
  if (not encodeBeq(rs1, rs2, imm))
    return false;
  funct3 = 1;
  return true;
}


bool
BFormInst::encodeBge(unsigned rs1, unsigned rs2, int imm)
{
  if (not encodeBeq(rs1, rs2, imm))
    return false;
  funct3 = 5;
  return true;
}


bool
BFormInst::encodeBeq(unsigned rs1, unsigned rs2, int imm,
		     uint32_t& inst)
{
  BFormInst bf(0);
  if (not bf.encodeBeq(rs1, rs2, imm))
    return false;
  inst = bf.code;
  return true;
}


bool
BFormInst::encodeBne(unsigned rs1, unsigned rs2, int imm,
		     uint32_t& inst)
{
  BFormInst bf(0);
  if (not bf.encodeBne(rs1, rs2, imm))
    return false;
  inst = bf.code;
  return true;
}


bool
BFormInst::encodeBge(unsigned rs1, unsigned rs2, int imm,
		     uint32_t& inst)
{
  BFormInst bf(0);
  if (not bf.encodeBge(rs1, rs2, imm))
    return false;
  inst = bf.code;
  return true;
}


bool
IFormInst::encodeAddi(unsigned rdv, unsigned rs1v, int imm)
{
  if (rdv > 32 or rs1v > 32)
    return false;
  if (imm > (1 << 11) or imm < (-1 << 11))
    return false;
  fields.opcode = 0x13;
  fields.rd = rdv;
  fields.funct3 = 0;
  fields.rs1 = rs1v;
  fields.imm = imm;
  return true;
}


bool
IFormInst::encodeAndi(unsigned rdv, unsigned rs1v, int imm)
{
  if (not encodeAddi(rdv, rs1v, imm))
    return false;
  fields.funct3 = 7;
  return true;
}


bool
IFormInst::encodeEbreak()
{
  fields.opcode = 0x73;
  fields.rd = 0;
  fields.funct3 = 0;
  fields.rs1 = 0;
  fields.imm = 1;
  return true;
}


bool
IFormInst::encodeEcall()
{
  fields.opcode = 0x73;
  fields.rd = 0;
  fields.funct3 = 0;
  fields.rs1 = 0;
  fields.imm = 0;
  return true;
}


bool
IFormInst::encodeJalr(unsigned rdv, unsigned rs1v, int offset)
{
  if (rdv > 31 or rs1v > 31 or offset >= (1<<11) or offset < (-1 << 11))
    return false;
  fields.opcode = 0x67;
  fields.rd = rdv;
  fields.funct3 = 0;
  fields.rs1 = rs1v;
  fields.imm = offset;
  return true;
}


bool
IFormInst::encodeLb(unsigned rdv, unsigned rs1v, int offset)
{
 if (rdv > 31 or rs1v > 31 or offset >= (1<<11) or offset < (-1 << 11))
    return false;
 fields.opcode = 0x03;
 fields.rd = rdv;
 fields.funct3 = 0;
 fields.rs1 = rs1v;
 fields.imm = offset;
 return true;
}

bool
IFormInst::encodeLh(unsigned rd, unsigned rs1, int offset)
{
  if (not encodeLb(rd, rs1, offset))
    return false;
  fields.funct3 = 1;
  return true;
}


bool
IFormInst::encodeLw(unsigned rd, unsigned rs1, int offset)
{
  if (not encodeLb(rd, rs1, offset))
    return false;
  fields.funct3 = 2;
  return true;
}


bool
IFormInst::encodeLbu(unsigned rd, unsigned rs1, int offset)
{
  if (not encodeLb(rd, rs1, offset))
    return false;
  fields.funct3 = 4;
  return true;
}


bool
IFormInst::encodeLhu(unsigned rd, unsigned rs1, int offset)
{
  if (not encodeLb(rd, rs1, offset))
    return false;
  fields.funct3 = 5;
  return true;
}


bool
IFormInst::encodeLwu(unsigned rd, unsigned rs1, int offset)
{
  if (not encodeLb(rd, rs1, offset))
    return false;
  fields.funct3 = 6;
  return true;
}


bool
IFormInst::encodeLd(unsigned rd, unsigned rs1, int offset)
{
  if (not encodeLb(rd, rs1, offset))
    return false;
  fields.funct3 = 3;
  return true;
}


bool
IFormInst::encodeSlli(unsigned rd, unsigned rs1, unsigned shamt)
{
  if (rd > 31 or rs1 > 31 or shamt > 31)
    return false;
  fields2.opcode = 0x13;
  fields2.rd = rd;
  fields2.funct3 = 1;
  fields2.rs1 = rs1;
  fields2.shamt = shamt;
  fields2.top7 = 0;
  return true;
}


bool
IFormInst::encodeSrli(unsigned rd, unsigned rs1, unsigned shamt)
{
  if (not encodeSlli(rd, rs1, shamt))
    return false;
  fields2.funct3 = 5;
  return true;
}


bool
IFormInst::encodeSrai(unsigned rd, unsigned rs1, unsigned shamt)
{
  if (not encodeSlli(rd, rs1, shamt))
    return false;
  fields2.funct3 = 5;
  fields2.top7 = 0x20;
  return true;
}


bool
IFormInst::encodeAddi(unsigned rd, unsigned rs1, int imm, uint32_t& inst)
{
  IFormInst ifs(0);
  if (not ifs.encodeAddi(rd, rs1,imm))
    return false;
  inst = ifs.code;
  return true;
}


bool
IFormInst::encodeAndi(unsigned rd, unsigned rs1, int imm, uint32_t& inst)
{
  IFormInst ifs(0);
  if (not ifs.encodeAndi(rd, rs1, imm))
    return false;
  inst = ifs.code;
  return true;
}


bool
IFormInst::encodeEbreak(uint32_t& inst)
{
  IFormInst ifs(0);
  if (not ifs.encodeEbreak())
    return false;
  inst = ifs.code;
  return true;
}


bool
IFormInst::encodeEcall(uint32_t& inst)
{
  IFormInst ifs(0);
  if (not ifs.encodeEcall())
    return false;
  inst = ifs.code;
  return true;
}


bool
IFormInst::encodeJalr(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifs(0);
  if (not ifs.encodeJalr(rd, rs1, offset))
    return false;
  inst = ifs.code;
  return true;
}


bool
IFormInst::encodeLb(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLb(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeLh(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLh(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeLw(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLw(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeLbu(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLbu(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeLhu(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLhu(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeLwu(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLwu(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeLd(unsigned rd, unsigned rs1, int offset, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeLd(rd, rs1, offset))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeSlli(unsigned rd, unsigned rs1, unsigned shamt, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeSlli(rd, rs1, shamt))
    return false;
  inst = ifi.code;
  return true;
}

bool
IFormInst::encodeSrli(unsigned rd, unsigned rs1, unsigned shamt, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeSrli(rd, rs1, shamt))
    return false;
  inst = ifi.code;
  return true;
}


bool
IFormInst::encodeSrai(unsigned rd, unsigned rs1, unsigned shamt, uint32_t& inst)
{
  IFormInst ifi(0);
  if (not ifi.encodeSrai(rd, rs1, shamt))
    return false;
  inst = ifi.code;
  return true;
}


bool
SFormInst::encodeSb(unsigned rs1v, unsigned rs2v, int imm)
{
  if (rs1v > 31 or rs2v > 31 or imm >= (1<<11) or imm < (-1<<11))
    return false;
  opcode = 0x23;
  imm4_0 = imm & 0x1f;
  funct3 = 0;
  rs1 = rs1v;
  rs2 = rs2v;
  imm11_5 = (imm >> 5) & 0x7f;
  return true;
}


bool
SFormInst::encodeSh(unsigned rs1, unsigned rs2, int imm)
{
  if (not encodeSb(rs1, rs2, imm))
    return false;
  funct3 = 1;
  return true;
}


bool
SFormInst::encodeSw(unsigned rs1, unsigned rs2, int imm)
{
  if (not encodeSb(rs1, rs2, imm))
    return false;
  funct3 = 2;
  return true;
}


bool
SFormInst::encodeSd(unsigned rs1, unsigned rs2, int imm)
{
  if (not encodeSb(rs1, rs2, imm))
    return false;
  funct3 = 3;
  return true;
}


bool
SFormInst::encodeSb(unsigned rs1, unsigned rs2, int imm, uint32_t& inst)
{
  SFormInst sfi(0);
  if (not sfi.encodeSb(rs1, rs2, imm))
    return false;
  inst = sfi.code;
  return true;
}


bool
SFormInst::encodeSh(unsigned rs1, unsigned rs2, int imm, uint32_t& inst)
{
  SFormInst sfi(0);
  if (not sfi.encodeSh(rs1, rs2, imm))
    return false;
  inst = sfi.code;
  return true;
}


bool
SFormInst::encodeSw(unsigned rs1, unsigned rs2, int imm, uint32_t& inst)
{
  SFormInst sfi(0);
  if (not sfi.encodeSw(rs1, rs2, imm))
    return false;
  inst = sfi.code;
  return true;
}


bool
SFormInst::encodeSd(unsigned rs1, unsigned rs2, int imm, uint32_t& inst)
{
  SFormInst sfi(0);
  if (not sfi.encodeSd(rs1, rs2, imm))
    return false;
  inst = sfi.code;
  return true;
}


bool
UFormInst::encodeLui(unsigned rdv, int immed)
{
  if (immed >= (1 << 19) or immed < (-1 << 19) or rdv > 31)
    return false;
  opcode = 0x37;
  rd = rdv;
  imm = (immed >> 12);
  return true;
}


bool
UFormInst::encodeLui(unsigned rd, unsigned immed, uint32_t& inst)
{
  UFormInst uf(0);
  if (not uf.encodeLui(rd, immed))
    return false;
  inst = uf.code;
  return true;
}


bool
JFormInst::encodeJal(uint32_t rdv, int offset)

{
  if (rd > 31 or offset >= (1 << 20) or offset < (-1 << 20))
    return false;
  opcode = 0x6f;
  rd = rdv;
  imm20 = (offset >> 20) & 1;
  imm19_12 = (offset >> 12) & 0xff;
  imm11 = (offset >> 11) & 1;
  imm10_1 = (offset >> 1) & 0x3ff;
  return true;
}


bool
JFormInst::encodeJal(unsigned rd, int offset, uint32_t& inst)
{
  JFormInst jf(0);
  if (not jf.encodeJal(rd, offset))
    return false;
  inst = jf.code;
  return true;
}


bool
CbFormInst::encodeCbeqz(unsigned rs1pv, int imm)
{
  if ((imm & 1) != 0)
    return false;
  if (rs1pv > 7 or imm >= (1<<8) or imm < (-1<<8))
    return false;

  opcode = 1;
  ic0 = (imm >> 5) & 1;
  ic1 = (imm >> 1) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 6) & 1;
  ic4 = (imm >> 7) & 1;
  rs1p = rs1pv;
  ic5 = (imm >> 3) & 1;
  ic6 = (imm >> 4) & 1;
  ic7 = (imm >> 8) & 1;
  funct3 = 6;
  return true;
}


bool
CbFormInst::encodeCbnez(unsigned rs1p, int imm)
{
  if (not encodeCbeqz(rs1p, imm))
    return false;
  funct3 = 7;
  return true;
}


bool
CbFormInst::encodeCbeqz(unsigned rs1p, int imm, uint32_t& inst)
{
  CbFormInst cb(0);
  if (not cb.encodeCbeqz(rs1p, imm))
    return false;
  inst = cb.code;
  return false;
}


bool
CbFormInst::encodeCbnez(unsigned rs1p, int imm, uint32_t& inst)
{
  CbFormInst cb(0);
  if (not cb.encodeCbnez(rs1p, imm))
    return false;
  inst = cb.code;
  return false;
}


bool
CaiFormInst::encodeCsrli(unsigned rdpv, unsigned imm)
{
  if (rdpv > 7 or imm >= (1 << 6))
    return false;
  opcode = 1;
  ic0 = imm & 1;
  ic1 = (imm >> 1) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 3) & 1;
  ic4 = (imm >> 4) & 1;
  rdp = rdpv;
  funct2 = 0;
  ic5 = (imm >> 5) & 1;
  funct3 = 4;
  unused = 0;
  return true;
}


bool
CaiFormInst::encodeCsrai(unsigned rdpv, unsigned imm)
{
  if (not encodeCsrli(rdpv, imm))
    return false;
  funct2 = 1;
  return true;
}


bool
CaiFormInst::encodeCandi(unsigned rdpv, int imm)
{
  if (rdpv > 7 or imm >= (1 << 5) or imm < (-1 << 5))
    return false;
  opcode = 1;
  ic0 = imm & 1;
  ic1 = (imm >> 1) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 3) & 1;
  ic4 = (imm >> 4) & 1;
  rdp = rdpv;
  funct2 = 2;
  ic5 = (imm >> 5) & 1;
  funct3 = 4;
  unused = 0;
  return true;
}


bool
CaiFormInst::encodeCsub(unsigned rdpv, unsigned rs2pv)
{
  if (rdpv > 7 or rs2pv > 7)
    return false;
  opcode = 1;
  ic0 = rs2pv & 1;
  ic1 = (rs2pv >> 1) & 1;
  ic2 = (rs2pv >> 2) & 1;
  ic3 = 0;
  ic4 = 0;
  rdp = rdpv;
  funct2 = 3;
  ic5 = 0;
  funct3 = 4;
  unused = 0;
  return true;
}


bool
CaiFormInst::encodeCxor(unsigned rdpv, unsigned rs2pv)
{
  if (not encodeCsub(rdpv, rs2pv))
    return false;
  ic3 = 1;
  return true;
}


bool
CaiFormInst::encodeCor(unsigned rdpv, unsigned rs2pv)
{
  if (not encodeCsub(rdpv, rs2pv))
    return false;
  ic4 = 1;
  return true;
}


bool
CaiFormInst::encodeCand(unsigned rdpv, unsigned rs2pv)
{
  if (not encodeCsub(rdpv, rs2pv))
    return false;
  ic3 = 1;
  ic4 = 1;
  return true;
}


bool
CiFormInst::encodeCadd(unsigned rdv, unsigned rs2v)
{
  if (rdv > 31 or rs2v > 31 or rs2v == 0)
    return false;
  opcode = 2;
  ic0 = rs2v & 0x1;
  ic1 = (rs2v >> 1) & 1;
  ic2 = (rs2v >> 2) & 1;
  ic3 = (rs2v >> 3) & 1;
  ic4 = (rs2v >> 4) & 1;
  ic5 = 1;
  rd = rdv;
  funct3 = 4;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeCaddi(unsigned rdv, int imm)
{
  if (rdv > 31 or imm < (-1 << 5) or imm > (1 << 5))
    return false;
  opcode = 1;
  ic0 = imm & 0x1;
  ic1 = (imm >> 1) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 3) & 1;
  ic4 = (imm >> 4) & 1;
  rd = rdv;
  ic5 = (imm >> 5) & 1;
  funct3 = 0;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeCaddi16sp(int imm)
{
  if (imm >= (1 << 5) or imm < (-1 << 5))
    return false;
  imm = imm * 16;

  opcode = 1;
  ic0 = (imm >> 5) & 1;
  ic1 = (imm >> 7) & 1;
  ic2 = (imm >> 8) & 1;
  ic3 = (imm >> 6) & 1;
  ic4 = (imm >> 4) & 1;
  rd = 2;
  ic5 = (imm >> 9) & 1;
  funct3 = 1;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeClui(unsigned rdv, int imm)
{
  if (rd == 2)
    return false;
  opcode = 1;
  ic0 = (imm >> 12) & 1;
  ic1 = (imm >> 13) & 1;
  ic2 = (imm >> 14) & 1;
  ic3 = (imm >> 15) & 1;
  ic4 = (imm >> 16) & 1;
  rd = rdv;
  ic5 = (imm >> 17) & 1;
  funct3 = 3;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeClwsp(unsigned rdv, unsigned imm)
{
  if (imm >= (1 << 7))
    return false;
  imm = imm << 2;  // Scale by 4.
  opcode = 2;
  ic0 = (imm >> 6) & 1;
  ic1 = (imm >> 7) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 3) & 1;
  ic4 = (imm >> 4) & 1;
  ic5 = (imm >> 5) & 1;
  rd = rdv;
  funct3 = 2;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeCslli(unsigned rdv, unsigned shift)
{
  if (shift & 0x20)
    return false;
  opcode = 2;
  ic0 = shift & 1; ic1 = (shift >> 1) & 1; ic2 = (shift >> 2) & 1;
  ic3 = (shift >> 3) & 1; ic4 = (shift >> 4) & 1; ic5 = (shift >> 5) & 1;
  rd = rdv;
  funct3 = 0;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeCebreak()
{
  opcode = 2;
  ic0 = ic1 = ic2 = ic3 = ic4 = 0;
  rd = 0;
  ic5 = 1;
  funct3 = 4;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeCjalr(unsigned rs1)
{
  if (rs1 == 0 or rs1 > 31)
    return false;
  opcode = 2;
  ic0 = ic1 = ic2 = ic3 = ic4 = 0;
  rd = rs1;
  ic5 = 1;
  funct3 = 4;
  unused = 0;
  return true;
}


bool
CiFormInst::encodeCjr(unsigned rs1)
{
  if (not encodeCjalr(rs1))
    return false;
  ic5 = 0;
  return true;
}


bool
CiwFormInst::encodeCaddi4spn(unsigned rdpv, unsigned immed)
{
  if (immed == 0 or immed >= (1 << 8))
    return false;

  immed = immed << 2;  // Times 4
  opcode = 0;
  rdp = rdpv;
  ic0 = (immed >> 3) & 1;
  ic1 = (immed >> 2) & 1;
  ic2 = (immed >> 6) & 1;
  ic3 = (immed >> 7) & 1;
  ic4 = (immed >> 8) & 1;
  ic5 = (immed >> 9) & 1;
  ic6 = (immed >> 4) & 1;
  ic7 = (immed >> 5) & 1;
  funct3 = 0;
  unused = 0;
  return true;
}


bool
CjFormInst::encodeCjal(int imm)
{
  if (imm >= (1 << 11) or imm < (-1 << 11))
    return false;

  opcode = 1;
  ic0 = (imm >> 5) & 1;
  ic1 = (imm >> 1) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 3) & 1;
  ic4 = (imm >> 7) & 1;
  ic5 = (imm >> 6) & 1;
  ic6 = (imm >> 10) & 1;
  ic7 = (imm >> 8) & 1;
  ic8 = (imm >> 9) & 1;
  ic9 = (imm >> 4) & 1;
  ic10 = (imm >> 11) & 1;
  funct3 = 1;
  unused = 0;
  return true;
}

 
bool
CjFormInst::encodeCj(int imm)
{
  if (not encodeCjal(imm))
    return false;
  funct3 = 5;
  return true;
}


bool
CswspFormInst::encodeCswsp(unsigned rs2v, unsigned imm)
{
  if (imm >= (1 << 6))
    return false;
  imm = imm * 4;
  opcode = 2;
  rs2 = rs2;
  ic0 = (imm >> 6) & 1;
  ic1 = (imm >> 7) & 1;
  ic2 = (imm >> 2) & 1;
  ic3 = (imm >> 3) & 1;
  ic4 = (imm >> 4) & 1;
  ic5 = (imm >> 5) & 1;
  funct3 = 6;
  unused = 0;
  return true;
}


bool
CsFormInst::encodeCsw(unsigned rs1pv, unsigned rs2pv, unsigned imm)
{
  if (rs1pv > 7 or rs2pv > 7 or imm >= (1 << 7))
    return false;
  opcode = 0;
  rs2p = rs2pv;
  ic0 = (imm >> 6) & 1;
  ic1 = (imm >> 2) & 1;
  rs1p = rs1pv;
  ic2 = (imm >> 3) & 1;
  ic3 = (imm >> 4) & 1;
  ic4 = (imm >> 5) & 1;
  funct3 = 6;
  unused = 0;
  return true;
}


bool
CsFormInst::encodeCsd(unsigned rs1pv, unsigned rs2pv, unsigned imm)
{
  if (rs1pv > 7 or rs2pv > 7 or imm >= (1 << 7))
    return false;
  opcode = 0;
  rs2p = rs2pv;
  ic0 = (imm >> 6) & 1;
  ic1 = (imm >> 7) & 1;
  rs1p = rs1pv;
  ic2 = (imm >> 3) & 1;
  ic3 = (imm >> 4) & 1;
  ic4 = (imm >> 5) & 1;
  funct3 = 7;
  unused = 0;
  return true;
}
