// -*- c++ -*-

#include <vector>
#include <map>
#include "InstId.hpp"


namespace WdRiscv
{

  enum class OperandType { IntReg, FpReg, CsReg, Imm, None };
  enum class OperandMode { Read, Write, ReadWrite, None };

  /// Opcode and operands of an instruction.
  class InstInfo
  {
  public:

    // Constructor.
    InstInfo(std::string name = "", InstId id = InstId::illegal,
	     uint32_t code = 0, uint32_t mask = ~0,
	     OperandType op0Type = OperandType::None,
	     OperandMode op0Mode = OperandMode::None,
	     uint32_t op0Mask = 0,
	     OperandType op1Type = OperandType::None,
	     OperandMode op1Mode = OperandMode::None,
	     uint32_t op1Mask = 0,
	     OperandType op2Type = OperandType::None,
	     OperandMode op2Mode = OperandMode::None,
	     uint32_t op2Mask = 0);

    /// Return the name of the instruction.
    const std::string& name() const { return name_; }

    /// Return the id of the instruction (an integer between 0 and n
    /// where n is the number of defined instructions). Note that it is
    /// possible for two instructions with the same code to have
    /// different ids. This is because RISCV has instruction aliaes:
    /// same code corresponds to different instruction depending on the
    /// feature set and mode of the processor.
    InstId instId() const
    { return id_; }

    /// Return the instruction bits with all the operand specifiers set
    /// to zero.
    uint32_t code() const
    { return code_; }

    /// Return the maske corresponding to the code bis: Returned value
    /// has a 1 for each non-operand-specifier bit.
    uint32_t codeMask() const
    { return codeMask_; }

    /// Return valid operand count
    unsigned operandCount() const
    { return opCount_; }

    // Return the type of the ith operand of None if no such operand.
    // First operand corresponds to an index of zero.
    OperandType ithOperandType(unsigned i) const
    {
      if (i == 0) return op0Type_;
      if (i == 1) return op1Type_;
      if (i == 2) return op2Type_;
      return OperandType::None;
    }

    // Return the mode of the ith operand of None if no such operand.
    // First operand corresponds to an index of zero.
    OperandMode ithOperandMode(unsigned i) const
    {
      if (i == 0) return op0Mode_;
      if (i == 1) return op1Mode_;
      if (i == 2) return op2Mode_;
      return OperandMode::None;
    }

    /// Return true if the ith operand is a write operand.
    bool isIthOperandWrite(unsigned i) const
    {
      OperandMode mode = ithOperandMode(i);
      return mode == OperandMode::Write or mode == OperandMode::ReadWrite;
    }

    /// Return true if the ith operand is a read operand.
    bool isIthOperandRead(unsigned i) const
    {
      OperandMode mode = ithOperandMode(i);
      return mode == OperandMode::Read or mode == OperandMode::ReadWrite;
    }

    /// Return the mask corresponding to the bits of the specifier of the
    /// ith operand. Return 0 if no such operand.
    uint32_t ithOperandMask(unsigned i) const
    {
      if (i == 0) return op0Mask_;
      if (i == 1) return op1Mask_;
      if (i == 2) return op2Mask_;
      return 0;
    }

  private:

    std::string name_;
    InstId id_;
    uint32_t code_;      // Code with all operand bits set to zero.
    uint32_t codeMask_;  // Bit corresponding to code bits are 1. Bits

    uint32_t op0Mask_;
    uint32_t op1Mask_;
    uint32_t op2Mask_;

    OperandType op0Type_;
    OperandType op1Type_;
    OperandType op2Type_;

    OperandMode op0Mode_;
    OperandMode op1Mode_;
    OperandMode op2Mode_;

    unsigned opCount_;
  };


  // Instruction table: Map an instruction id or an instruction name to
  // the opcode/operand information corresponding to that instruction.
  class InstInfoTable
  {
  public:
    InstInfoTable();

    // Return the info corresponding to the given id or the info of the
    // illegal instruction if no such id.
    const InstInfo& getInstInfo(InstId) const;

    // Return the info corresponding to the given name or the info of
    // the illegal instruction if no such instruction.
    const InstInfo& getInstInfo(const std::string& name) const;

    // Return true if given id is present in the table.
    bool hasInfo(InstId) const;

    // Return true if given instance name is present in the table.
    bool hasInfo(const std::string& name) const;

  private:

    // Helper to the constructor.
    void setupInstVec();

  private:

    std::vector<InstInfo> instVec_;
    std::map<std::string, InstId> instMap_;
  };
}
