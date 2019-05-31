#include "Core.hpp"
#include "DecodedInst.hpp"


using namespace WdRiscv;


uint32_t
DecodedInst::ithOperand(unsigned i) const
{
  if (i == 0) return op0();
  if (i == 1) return op1();
  if (i == 2) return op2();
  if (i == 3) return op3();
  return 0;
}


int32_t
DecodedInst::ithOperandAsInt(unsigned i) const
{
  return ithOperand(i);
}


template <typename URV>
void
DecodedInst::fetchOperands(const Core<URV>& core)
{
  for (unsigned i = 0; i < 4; ++i)
    {
      uint32_t operand = ithOperand(i);
      uint64_t val = 0;

      URV urv = 0;

      OperandType type = ithOperandType(i);
      switch(type)
	{
	case OperandType::IntReg:
	  core.peekIntReg(operand, urv);
	  val = urv;
	  break;

	case OperandType::FpReg:
	  core.peekFpReg(operand, val);
	  break;

	case OperandType::CsReg:
	  core.peekCsr(CsrNumber(operand), urv);
	  val = urv;
	  break;

	case OperandType::Imm:
	  val = int64_t(ithOperandAsInt(i));
	  break;

	case OperandType::None:
	  break;
	}

      assert(i < sizeof(values_));
      values_[i] = val;
    }
}


// Explicit instantiation of the fetchOperands method for uint32_t.
template<>
void
DecodedInst::fetchOperands(const Core<uint32_t>&);


// Explicit instantiation of the fetchOperands method for uint64_t.
template<>
void
DecodedInst::fetchOperands(const Core<uint64_t>&);


void
DecodedInst::setIthOperandValue(unsigned i, uint64_t value)
{
  OperandType type = ithOperandType(i);
  switch(type)
    {
    case OperandType::IntReg:
    case OperandType::FpReg:
    case OperandType::CsReg:
      if (i < sizeof(values_))
	values_[i] = value;
      break;
      
    case OperandType::Imm:
      break;

    case OperandType::None:
      break;
    }
}



