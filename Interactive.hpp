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

#include <unordered_map>
#include "Core.hpp"


namespace WdRiscv
{

  /// Manage session.
  template <typename URV>
  class Interactive
  {
  public:

    /// Constructor.
    Interactive(std::vector< Core<URV>* >& coreVec);

    bool untilCommand(Core<URV>& core, const std::string& line,
		     const std::vector<std::string>& tokens,
		     FILE* traceFile);

    bool stepCommand(Core<URV>& core, const std::string& line,
		     const std::vector<std::string>& tokens, FILE* traceFile);

    bool peekCommand(Core<URV>& core, const std::string& line,
		     const std::vector<std::string>& tokens);

    bool pokeCommand(Core<URV>& core, const std::string& line,
		     const std::vector<std::string>& tokens);

    bool disassCommand(Core<URV>& core, const std::string& line,
		       const std::vector<std::string>& tokens);

    bool elfCommand(Core<URV>& core, const std::string& line,
		    const std::vector<std::string>& tokens);

    bool hexCommand(Core<URV>& core, const std::string& line,
		    const std::vector<std::string>& tokens);

    bool resetCommand(Core<URV>& core, const std::string& line,
		     const std::vector<std::string>& tokens);

    bool replayFileCommand(const std::string& line,
			   const std::vector<std::string>& tokens,
			   std::ifstream& stream);

    bool exceptionCommand(Core<URV>& core, const std::string& line,
			  const std::vector<std::string>& tokens);

    bool loadFinishedCommand(Core<URV>& core, const std::string& line,
			     const std::vector<std::string>& tokens);

    void helpCommand(const std::vector<std::string>& tokens);

    bool replayCommand(unsigned& currentHartId,
		       const std::string& line,
		       const std::vector<std::string>& tokens,
		       FILE* traceFile, FILE* commandLog,
		       std::ifstream& replayStream, bool& done);

    bool interact(FILE* traceFile, FILE* commandLog);

  protected:

    bool executeLine(unsigned& currentHartId,
		     const std::string& inLine, FILE* traceFile,
		     FILE* commandLog,
		     std::ifstream& replayStream, bool& done);

  private:

    std::vector< Core<URV>* >& cores_;

    // Initial resets do not reset memory mapped registers.
    bool resetMemoryMappedRegs_ = false;
  };

}
