#pragma once

#include <stdint.h>


enum WhisperMessageType { Peek, Poke, Step, Until, Change, ChangeCount,
			  Quit, Invalid };

struct WhisperMessage
{
  uint32_t hart;
  uint32_t type;
  uint32_t resource;
  uint64_t address;
  uint64_t value;
};

