//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2018 Western Digital Corporation or its affiliates.
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
// 
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//

#pragma once

#include "Hart.hpp"


namespace WdRiscv
{

  /// Manage server mode.
  template <typename URV>
  class Server
  {
  public:

    /// Constructor.
    Server(std::vector< Hart<URV>* >&);

    /// Server mode poke command.
    bool pokeCommand(const WhisperMessage& req, WhisperMessage& reply);

    /// Server mode peek command.
    bool peekCommand(const WhisperMessage& req, WhisperMessage& reply);

    // Server mode disassemble command.
    void disassembleAnnotateInst(uint32_t inst, bool interrupted,
				 bool hasPreTrigger, bool hasPostTrigger,
				 std::string& text);

    /// Server mode step command.
    bool stepCommand(const WhisperMessage& req, 
		     std::vector<WhisperMessage>& pendingChanges,
		     WhisperMessage& reply,
		     FILE* traceFile);

    /// Server mode exception command.
    bool exceptionCommand(const WhisperMessage& req, WhisperMessage& reply,
			  std::string& text);

    /// Server mode loop: Receive command and send reply till a quit
    /// command is received. Return true on successful termination (quit
    /// received). Return false otherwise.
    bool interact(int soc, FILE* traceFile, FILE* commandLog);

  protected:

    /// Process changes of a single-step command. Put the changes in the
    /// pendingChanges vector (which is cleared on entry). Put the
    /// number of change record in the reply parameter along with the
    /// instruction address, opcode and assembly text. Use hasPre
    /// (instruction tripped a "before" trigger), hasPost (tripped an
    /// "after" trigger) and interrupted (instruction encountered an
    /// external interrupt) to annotate the assembly text.
    void processStepCahnges(Hart<URV>&, uint32_t inst,
			    std::vector<WhisperMessage>& pendingChanges,
			    bool interrupted, bool hasPre, bool hasPost,
			    WhisperMessage& reply);

  private:

    std::vector< Hart<URV>* >& harts_;
  };

}
