// -*- c++ -*-

#pragma once

#include <cstdint>
#include <type_traits>


namespace WdRiscv
{

  // Structures useful for decoding a risc-v instructions.

  /// Pack/unpack an r-form instruction.
  union RFormInst
  {
    /// Constructor: Either pass a valid r-form value or start with any
    /// value and then use an encode method.
    RFormInst(uint32_t inst)
    { code = inst; }

    /// Encode "add rd, rs1, rs2" into this object.
    bool encodeAdd(unsigned rd, unsigned rs1, unsigned rs2);

    /// Encode "sub rd, rs1, rs2" into this object.
    bool encodeSub(unsigned rd, unsigned rs1, unsigned rs2);

    /// Encode "xor rd, rs1, rs2" into this object.
    bool encodeXor(unsigned rd, unsigned rs1, unsigned rs2);

    /// Encode "or rd, rs1, rs2" into this object.
    bool encodeOr(unsigned rd, unsigned rs1, unsigned rs2);

    /// Encode "and rd, rs1, rs2" into this object.
    bool encodeAnd(unsigned rd, unsigned rs1, unsigned rs2);

    /// Encode "add rdv, rs1v, rs2v" into inst.
    static bool encodeAdd(unsigned rd, unsigned rs1, unsigned rs2,
			  uint32_t& inst);

    /// Encode "sub rdv, rs1v, rs2v" into inst.
    static bool encodeSub(unsigned rd, unsigned rs1, unsigned rs2,
			  uint32_t& inst);
    /// Encode "xor rdv, rs1v, rs2v" into inst.
    static bool encodeXor(unsigned rd, unsigned rs1, unsigned rs2,
			  uint32_t& inst);
    /// Encode "or rdv, rs1v, rs2v" into inst.
    static bool encodeOr(unsigned rd, unsigned rs1, unsigned rs2,
			  uint32_t& inst);

    /// Encode "and rd, rs1, rs2" into inst.
    static bool encodeAnd(unsigned rdv, unsigned rs1, unsigned rs2,
			  uint32_t& inst);

    uint32_t code;

    struct
    {
      unsigned opcode : 7;
      unsigned rd     : 5;
      unsigned funct3 : 3;
      unsigned rs1    : 5;
      unsigned rs2    : 5;
      unsigned funct7 : 7;
    };
  };


  /// Pack/unpack a b-form instruction.
  union BFormInst
  {
    /// Constructor: Either pass a valid b-form value or start with any
    /// value and then use an encode method.
    BFormInst(uint32_t inst)
    { code = inst; }

    /// Sign extend the instruction immediate value to a value of type
    /// T.
    template <typename T> T immed() const
    { return T(typename std::make_signed<T>::type((imm12   << 12)  |
						  (imm11   << 11)  |
						  (imm10_5 << 5)   |
						  (imm4_1  << 1))); }

    /// Encode a "beq rs1, rs2, imm" into this object.
    bool encodeBeq(unsigned rs1, unsigned rs2, int imm);

    /// Encode a "bne rs1, rs2, imm" into this object.
    bool encodeBne(unsigned rs1, unsigned rs2, int imm);

    /// Encode a "beq rs1, rs2, imm" into inst.
    static bool encodeBeq(unsigned rs1v, unsigned rs2v, int imm,
			  uint32_t& inst);

    /// Encode a "bne rs1, rs2, imm" into this object.
    static bool encodeBne(unsigned rs1v, unsigned rs2v, int imm,
			  uint32_t& inst);

    uint32_t code;
    
    struct
    {
      unsigned opcode  : 7;
      unsigned imm11   : 1;
      unsigned imm4_1  : 4;
      unsigned funct3  : 3;
      unsigned rs1     : 5;
      unsigned rs2     : 5;
      unsigned imm10_5 : 6;
      int      imm12   : 1;  // Note: int for sign extension
    };
  };


  /// Pack/unpack a i-form instruction.
  union IFormInst
  {
    /// Constructor: Either pass a valid i-form value or start with any
    /// value and then use an encode method.
    IFormInst(uint32_t inst)
    { code = inst; }

    /// Sign extend immediate value to (signed) type T.
    template <typename T> T immed() const
    { return T(typename std::make_signed<T>::type(fields.imm)); }

    /// Return immediate value as unsigned.
    unsigned uimmed() const  // Immediate as unsigned.
    { return fields.imm & 0xfff; }

    /// Encode "addi rd, rs1, imm" into this object returning
    /// true on success and false if any of the parameters are out of
    /// range.
    bool encodeAddi(unsigned rd, unsigned rs1, int imm);

    /// Encode "andi rd, rs1, imm" into this object.
    bool encodeAndi(unsigned rd, unsigned rs1, int imm);

    /// Encode "ebreak" into this object.
    bool encodeEbreak();

    /// Encode "ecall" into this object.
    bool encodeEcall();

    /// Encode "jalr rd, offset(rs1)" into this object.
    bool encodeJalr(unsigned rd, unsigned rs1, int offset);

    /// Encode "lb rd, offset(rs1)" into this object.
    bool encodeLb(unsigned rd, unsigned rs1, int offset);

    /// Encode "lh rd, offset(rs1)" into this object.
    bool encodeLh(unsigned rd, unsigned rs1, int offset);

    /// Encode "lw rd, offset(rs1)" into this object.
    bool encodeLw(unsigned rd, unsigned rs1, int offset);

    /// Encode "lbu rd, offset(rs1)" into this object.
    bool encodeLbu(unsigned rd, unsigned rs1, int offset);

    /// Encode "lhu rd, offset(rs1)" into this object.
    bool encodeLhu(unsigned rd, unsigned rs1, int offset);

    /// Encode "lwu rd, offset(rs1)" into this object.
    bool encodeLwu(unsigned rd, unsigned rs1, int offset);

    /// Encode "slli rd, rs1, shamt" into this object.
    bool encodeSlli(unsigned rd, unsigned rs1, unsigned shamt);

    /// Encode "srli rd, rs1, shamt" into this object.
    bool encodeSrli(unsigned rd, unsigned rs1, unsigned shamt);

    /// Encode "srai rd, rs1, shamt" into this object.
    bool encodeSrai(unsigned rd, unsigned rs1, unsigned shamt);

    /// Encode "addi rd, rs1, imm" into inst.
    static bool encodeAddi(unsigned rd, unsigned rs1, int imm, uint32_t& inst);

    /// Encode "andi rd, rs1, imm" into inst.
    static bool encodeAndi(unsigned rd, unsigned rs1, int imm, uint32_t& inst);

    /// Encode "ebreak" into inst.
    static bool encodeEbreak(uint32_t& inst);

    /// Encode "ecall" into inst.
    static bool encodeEcall(uint32_t& inst);

    /// Encode "jalr rd, offset(rs1)" into inst.
    static bool encodeJalr(unsigned rd, unsigned rs1, int offset,
			   uint32_t& inst);

    /// Encode "lb rd, offset(rs1)" into inst.
    static bool encodeLb(unsigned rd, unsigned rs1, int offset, uint32_t& inst);

    /// Encode "lh rd, offset(rs1)" into inst.
    static bool encodeLh(unsigned rd, unsigned rs1, int offset, uint32_t& inst);

    /// Encode "lw rd, offset(rs1)" into inst.
    static bool encodeLw(unsigned rd, unsigned rs1, int offset, uint32_t& inst);

    /// Encode "lbu rd, offs(rs1)" into inst.
    static bool encodeLbu(unsigned rd, unsigned rs1, int offs, uint32_t& inst);

    /// Encode "lhu rd, offs(rs1)" into inst.
    static bool encodeLhu(unsigned rd, unsigned rs1, int offs, uint32_t& inst);

    /// Encode "lwu rd, offs(rs1)" into inst.
    static bool encodeLwu(unsigned rd, unsigned rs1, int offs, uint32_t& inst);

    /// Encode "slli rd, rs1, shamt" into inst.
    static bool encodeSlli(unsigned rd, unsigned rs1, unsigned shamt,
			   uint32_t& inst);

    /// Encode "srli rd, rs1, shamt" into inst.
    static bool encodeSrli(unsigned rd, unsigned rs1, unsigned shamt,
			   uint32_t& inst);

    /// Encode "srai rd, rs1, shamt" into inst.
    static bool encodeSrai(unsigned rd, unsigned rs1, unsigned shamt,
			   uint32_t& inst);

    uint32_t code;

    struct
    {
      unsigned opcode : 7;
      unsigned rd     : 5;
      unsigned funct3 : 3;
      unsigned rs1    : 5;
      int      imm    : 12;
    } fields;

    struct
    {
      unsigned opcode : 7;
      unsigned rd     : 5;
      unsigned funct3 : 3;
      unsigned rs1    : 5;
      unsigned shamt  : 5;
      unsigned top7   : 7;
    } fields2;
  };


  /// Pack/unpack a s-form instruction.
  union SFormInst
  {
    /// Constructor: Either pass a valid s-form value or start with any
    /// value and then use an encode method.
    SFormInst(uint32_t inst)
    { code = inst; }

    /// Sign extend immediate value to (signed) type T.
    template <typename T> T immed() const
    { return T(typename std::make_signed<T>::type((imm11_5 << 5) | imm4_0)); }

    /// Encode "sb rs2, imm(rs1)" into this object.
    bool encodeSb(unsigned rs1, unsigned rs2, int imm);

    /// Encode "sh rs2, imm(rs1)" into this object.
    bool encodeSh(unsigned rs1, unsigned rs2, int imm);

    /// Encode "sw rs2, imm(rs1)" into this object.
    bool encodeSw(unsigned rs1, unsigned rs2, int imm);

    /// Encode "sb rs2, imm(rs1)" into inst.
    static bool encodeSb(unsigned rs1, unsigned rs2, int imm, uint32_t& inst);

    /// Encode "sh rs2, imm(rs1)" into inst.
    static bool encodeSh(unsigned rs1, unsigned rs2, int imm, uint32_t& inst);

    /// Encode "sw rs2, imm(rs1)" into inst.
    static bool encodeSw(unsigned rs1, unsigned rs2, int imm, uint32_t& inst);

    uint32_t code;

    struct
    {
      unsigned opcode  : 7;
      unsigned imm4_0  : 5;
      unsigned funct3  : 3;
      unsigned rs1     : 5;
      unsigned rs2     : 5;
      int      imm11_5 : 7;
    };
  };


  /// Pack/unpack a u-form instruction.
  union UFormInst
  {
    /// Constructor: Either pass a valid u-form value or start with
    /// any value and then use an encode method.
    UFormInst(uint32_t inst)
    { code = inst; }

    /// Sign extend immediate value to (signed) type T.
    template <typename T> T immed() const
    { return T(typename std::make_signed<T>::type(imm)); }

    /// Encode "lui rd, immed" into this object.
    bool encodeLui(unsigned rd, int immed);

    /// Encode "lui rd, immed" into inst.
    static bool encodeLui(unsigned rd, unsigned immed, uint32_t& inst);

    uint32_t code;

    struct
    {
      unsigned opcode  : 7;
      unsigned rd      : 5;
      int      imm     : 20;
    };
  };


  /// Pack/unpack a j-form instruction.
  union JFormInst
  {
    /// Constructor: Either pass a valid u-form value or start with
    /// any value and then use an encode method.
    JFormInst(uint32_t inst)
    { code = inst; }

    /// Sign extend immediate value to (signed) type T.
    template <typename T> T immed() const
    { return T(typename std::make_signed<T>::type((imm20 << 20)    |
						  (imm19_12 << 12) |
						  (imm11 << 11)    |
						  (imm10_1 << 1))); }

    /// Encode "jal rd, offset" into this object.
    bool encodeJal(unsigned rd, int offset);

    /// Encode "jal rd, offset" into inst.
    static bool encodeJal(unsigned rd, int offset, uint32_t& inst);

    uint32_t code;

    struct
    {
      unsigned opcode   : 7;
      unsigned rd       : 5;
      unsigned imm19_12 : 8;
      unsigned imm11    : 1;
      unsigned imm10_1  : 10;
      int      imm20    : 1;
    };
  };


  /// Pack/unpack a cb-form instruction.
  union CbFormInst
  {
    /// Constructor: Either pass a valid cb-form value or start with
    /// any value and then use an encode method.
    CbFormInst(uint16_t inst)
    { code = inst; }

    /// Return immediate value encoded in this object.
    int immed() const
    { return (ic0 << 5) | (ic1 << 1) | (ic2 << 2) | (ic3 << 6) | (ic4 << 7) |
	(ic5 << 3) | (ic6 << 4) | int(ic7 << 8); }

    /// Encode "c.beqz rs1p, imm" into this object.
    bool encodeCbeqz(unsigned rs1p, int imm);

    /// Encode "c.bnez rs1p, imm" into this object.
    bool encodeCbnez(unsigned rs1p, int imm);

    /// Encode "c.beqz rs1p, imm" into inst.
    static bool encodeCbeqz(unsigned rs1p, int imm, uint32_t& inst);

    /// Encode "c.bnez rs1p, imm" into inst.
    static bool encodeCbnez(unsigned rs1p, int imm, uint32_t& inst);

    uint32_t code;
    struct
    {
      unsigned opcode : 2;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned ic2    : 1;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned rs1p   : 3;
      unsigned ic5    : 1;
      unsigned ic6    : 1;
      int      ic7    : 1;
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


  //// Used to pack/unpack c.slri, c.slri64, c.srai, c.srai64, c.andi,
  //// c.sub, c.xor, c.or and c.and.
  union CaiFormInst
  {
    CaiFormInst(uint16_t inst)
    { code = inst; }
    
    int andiImmed() const
    {
      return int(ic5 << 5) | (ic4 << 4) | (ic3 << 3) | (ic2 << 2) |
	(ic1 << 1) | ic0;
    }

    unsigned shiftImmed() const
    { return unsigned(andiImmed()) & 0x1f; }

    bool encodeCsrli(unsigned rdpv, unsigned imm)
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

    bool encodeCsrai(unsigned rdpv, unsigned imm)
    {
      if (not encodeCsrli(rdpv, imm))
	return false;
      funct2 = 1;
      return true;
    }

    bool encodeCandi(unsigned rdpv, int imm)
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

    bool encodeCsub(unsigned rdpv, unsigned rs2pv)
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

    bool encodeCxor(unsigned rdpv, unsigned rs2pv)
    {
      if (not encodeCsub(rdpv, rs2pv))
	return false;
      ic3 = 1;
      return true;
    }

    bool encodeCor(unsigned rdpv, unsigned rs2pv)
    {
      if (not encodeCsub(rdpv, rs2pv))
	return false;
      ic4 = 1;
      return true;
    }

    bool encodeCand(unsigned rdpv, unsigned rs2pv)
    {
      if (not encodeCsub(rdpv, rs2pv))
	return false;
      ic3 = 1;
      ic4 = 1;
      return true;
    }

    uint32_t code;

    struct
    {
      unsigned opcode : 2;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned ic2    : 1;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned rdp    : 3;
      unsigned funct2 : 2;
      int      ic5    : 1;
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


  /// Pack-unpack ci-form compressed instructions: c.addi, c.addi16sp,
  /// c.lui, c.lwsp, c.slli, c.ebreak, c.jalr and c.jr
  union CiFormInst
  {
    CiFormInst(uint16_t inst)
    { code = inst; }

    uint32_t code;

    int addiImmed() const
    { return int(ic5 << 5) | (ic4 << 4) | (ic3 << 3) | (ic2 << 2) |
	(ic1 << 1) | ic0; }

    int addi16spImmed() const
    { return int(ic5 << 9) | (ic4 << 4) | (ic3 << 6) | (ic2 << 8) |
	(ic1 << 7) | (ic0 << 5); }

    int luiImmed() const
    { return int(ic5 << 17) | (ic4 << 16) | (ic3 << 15) | (ic2 << 14) |
	(ic1 << 13) | (ic0 << 12); }

    unsigned slliImmed() const
    { return unsigned(addiImmed()) & 0x3f; }

    unsigned lwspImmed() const
    { return (ic0 << 6) | (ic1 << 7) | (ic2 << 2) | (ic3 << 3) | (ic4 << 4) |
	((unsigned(ic5) & 1) << 5); }

    bool encodeCadd(unsigned rdv, unsigned rs2v)
    {
      if (rdv > 31 or rs2v > 31 or rdv == 0 or rs2v == 0)
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

    bool encodeCaddi(unsigned rdv, int imm)
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

    bool encodeCaddi16sp(int imm)
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

    bool encodeClui(unsigned rdv, int imm)
    {
      if (rd == 0 or rd == 2)
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

    bool encodeClwsp(unsigned rdv, unsigned imm)
    {
      // TBD: check size of imm
      if (rd == 0)
	return false;
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

    bool encodeCslli(unsigned rdv, unsigned shift)
    {
      if (shift == 0)
	return false;
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

    bool encodeCebreak()
    {
      opcode = 2;
      ic0 = ic1 = ic2 = ic3 = ic4 = 0;
      rd = 0;
      ic5 = 1;
      funct3 = 4;
      unused = 0;
      return true;
    }

    bool encodeCjalr(unsigned rs1)
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

    bool encodeCjr(unsigned rs1)
    {
      if (not encodeCjalr(rs1))
	return false;
      ic5 = 0;
      return true;
    }

    struct
    {
      unsigned opcode : 2;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned ic2    : 1;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned rd     : 5;
      int      ic5    : 1;
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


  /// Pack/unpack cl-form instructions: c.lw
  union ClFormInst
  {
    ClFormInst(uint16_t inst)
    { code = inst; }

    uint32_t code;

    /// Return immediate value for c.lw instruction encoded in this
    /// objec.
    unsigned lwImmed() const
    { return (ic0 << 6) | (ic1 << 2) | (ic3 << 3) | (ic4 << 4) | (ic5 << 5); }

    struct
    {
      unsigned opcode : 2;
      unsigned rdp    : 3;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned rs1p   : 3;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned ic5    : 1;
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


  // Encode c.addi4spn
  union CiwFormInst
  {
    CiwFormInst(uint16_t inst)
    { code = inst; }

    uint32_t code;

    unsigned immed() const
    { return (ic0 << 3) | (ic1 << 2) | (ic2 << 6) | (ic3 << 7) | (ic4 << 8) |
	(ic5 << 9) | (ic6 << 4) | (ic7 << 5); }

    bool encodeCaddi4spn(unsigned rdpv, unsigned immed)
    {
      if (immed == 0 or immed >= 256 or rdpv >= 8)
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

    struct
    {
      unsigned opcode : 2;
      unsigned rdp    : 3;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned ic2    : 1;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned ic5    : 1;
      unsigned ic6    : 1;
      unsigned ic7    : 1;
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


  /// Pack/unpack compressed cj-form instructions: c.jal and c.j
  union CjFormInst
  {
    CjFormInst(uint16_t inst)
    { code = inst; }

    uint32_t code;

    int immed() const
    { return (ic0 << 5) | (ic1 << 1) | (ic2 << 2) | (ic3 << 3) | (ic4 << 7) |
	(ic5 << 6) | (ic6 << 10) | (ic7 << 8) | (ic8 << 9) | (ic9 << 4) |
	(ic10 << 11); }

    bool encodeCjal(int imm)
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

    bool encodeCj(int imm)
    {
      if (not encodeCjal(imm))
	return false;
      funct3 = 5;
      return true;
    }

    struct
    {
      unsigned opcode : 2;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned ic2    : 1;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned ic5    : 1;
      unsigned ic6    : 1;
      unsigned ic7    : 1;
      unsigned ic8    : 1;
      unsigned ic9    : 1;
      int ic10        : 1;   // Int used for sign extension.
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


  union CswFormInst
  {
    CswFormInst(uint16_t inst)
    { code = inst; }

    uint32_t code;

    unsigned immed() const
    { return (ic0 << 6) | (ic1 << 7) | (ic2 << 2) | (ic3 << 3) | (ic4 << 4) |
	(ic5 << 5); }

    void encodeCswsp(unsigned rs2v, unsigned imm)
    {
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
    }

    struct
    {
      unsigned opcode : 2;
      unsigned rs2    : 5;
      unsigned ic0    : 1;
      unsigned ic1    : 1;
      unsigned ic2    : 1;
      unsigned ic3    : 1;
      unsigned ic4    : 1;
      unsigned ic5    : 1;
      unsigned funct3 : 3;
      unsigned unused : 16;
    };
  };


}
