// -*- c++ -*-

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>

namespace WdRiscv
{

  template <typename URV>
  union Data1Bits
  {
    Data1Bits(URV value) :
      value_(value)
    { }

    URV value_ = 0;

    struct
    {
      URV data_       : 8*sizeof(URV) - 5;
      unsigned dmode_ : 1;
      unsigned type_  : 4;
    } data1_;

    struct
    {
      // SPEC is bogus it has an extra zero bit.
      unsigned load_    : 1;   // trigger on load
      unsigned store_   : 1;   // trigger on store
      unsigned execute_ : 1;   // trigger on instruction
      unsigned u_       : 1;   // enable in user mode
      unsigned s_       : 1;   // enable in supervisor mode
      unsigned          : 1;
      unsigned m_       : 1;   // enable in machine mode
      unsigned match_   : 4;   // controls what is considered to be a match
      unsigned chain_   : 1;
      unsigned action_  : 6;
      unsigned timing_  : 1;
      unsigned select_  : 1;
      unsigned hit_     : 1;
      // URV               : 8*sizeof(URV) - 32;  // zero
      unsigned maskMax_ : 6;
      unsigned dmode_   : 1;
      unsigned type_    : 4;
    } mcontrol_;

    struct
    {
      unsigned action_  : 6;
      unsigned u_       : 1;
      unsigned s_       : 1;
      unsigned          : 1;
      unsigned m_       : 1;
      unsigned count_   : 14;
      unsigned hit_     : 1;
      URV               : 8*sizeof(URV) - 30;
      unsigned dmode_   : 1;
      unsigned type_    : 4;
    } icount_;
  };
      

  template <typename URV>
  struct Trigger
  {
    Trigger(URV data1 = 0, URV data2 = 0, URV data1Mask = ~URV(0),
	    URV data2Mask = ~URV(0))
      : data1_(data1), data2_(data2), data1Mask_(data1Mask),
	data2Mask_(data2Mask)
    { }

    URV readData1() const
    { return data1_.value_; }

    URV readData2() const
    { return data2_; }

    void writeData1(URV x)
    { data1_.value_ = (x & data1Mask_) | (data1_.value_ & ~data1Mask_); }

    void writeData2(URV x)
    { data2_ = (x & data2Mask_) | (data2_ & ~data2Mask_); }

    Data1Bits<URV> data1_ = Data1Bits<URV> (0);
    URV data2_ = 0;
    URV data1Mask_ = ~URV(0);
    URV data2Mask_ = ~URV(0);
  };


  template <typename URV>
  class Triggers
  {
  public:

    enum class Type { None, Legacy, Address, InstCount, Unavailable };

    enum class Mode { DM, D };  // Modes allowed to write trigger regiters.

    enum class Selet { MatchAddress, MatchData };

    enum class Timing { BeforeInst, AfterInst };

    enum class Action { RaiseBreak, EnterDebug, StartTrace, StopTrace, EmitTrace };

    enum class Chain { No, Yes };

    enum class Match { Equal, Masked, GE, LT, MaskHighEqualLow, MaskLowEqualHigh };

    Triggers(unsigned count)
      : triggers_(count)
    { }

    unsigned size() const
    { return triggers_.size(); }

    bool readData1(URV trigger, URV& value) const;

    bool readData2(URV trigger, URV& value) const;

    bool readData3(URV trigger, URV& value) const;

    bool writeData1(URV trigger, URV value);

    bool writeData2(URV trigger, URV value);

    bool writeData3(URV trigger, URV value);

    bool reset(URV trigger, URV data1, URV data2, URV mask1, URV mask2);

  private:
    std::vector< Trigger<URV> > triggers_;
  };
}
