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

#include <string>
#include <nlohmann/json_fwd.hpp>


namespace WdRiscv
{

  template <typename URV>
  class Hart;


  /// Manage loading of configuration file and applying it to a core.
  class HartConfig
  {
  public:

    /// Constructor.
    HartConfig();

    /// Destructor.
    ~HartConfig();

    /// Load given configuration file (JSON file) into this object.
    /// Return true on success and false if file cannot be opened or if the file
    /// does not contain a valid JSON object.
    bool loadConfigFile(const std::string& filePath);

    /// Apply the configurations in this object (as loaded by
    /// loadConfigFile) to the given hart. Return true on success and
    /// false on failure. URV stands for unsigned register type and is
    /// the type associated with the integer registers of a hart. Use
    /// uint32_t for 32-bit harts and uint64_t for 64-bit harts.
    template<typename URV>
    bool applyConfig(Hart<URV>&, bool verbose) const;

    /// Apply the memory configuration in this object.
    template<typename URV>
    bool applyMemoryConfig(Hart<URV>&, bool verbose) const;
    
    /// Set xeln to the register width configuration held in this
    /// object returning true on success and false if this object does
    /// not contain a register width (xlen) configuration.
    bool getXlen(unsigned& xlen) const;

    /// Set pageSize to the page size configuration held in this
    /// object returning true on success and false if this object does
    /// not contain a page size configuration.
    bool getPageSize(size_t& pageSize) const;

    /// Set memSize to the memory size configuration held in this
    /// object returning true on success and false if this object does
    /// not contain a memory size configuration.
    bool getMemorySize(size_t& memSize) const;

    /// Clear (make empty) the set of configurations held in this object.
    void clear();

    /// Configure actions of non-standard CSRs. Configure shared CSRs
    /// in multi-hart configurations.
    template<typename URV>
    bool finalizeCsrConfig(std::vector<Hart<URV>*>& harts) const;

  private:

    HartConfig(const HartConfig&) = delete;
    void operator= (const HartConfig&) = delete;

    nlohmann::json* config_;
  };

}
