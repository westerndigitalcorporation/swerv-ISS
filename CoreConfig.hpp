#pragma once

#include <string>
#include <nlohmann/json_fwd.hpp>


namespace WdRiscv
{

  template <typename URV>
  class Core;


  /// Manage loading of conficuration file and applying it to a core.
  class CoreConfig
  {
  public:

    /// Constructor.
    CoreConfig();

    /// Destructor.
    ~CoreConfig();

    /// Load given configuration file (JOSN file) into this object.
    /// Return true on success and false if file cannot be opened or if the file
    /// does not contain a valid JSON object.
    bool loadConfigFile(const std::string& filePath);

    /// Apply the configurations in this object (as loaded by
    /// loadConfigFile) to the given core. Return true on success and
    /// false on failure. URV stands for unsigned register type and is
    /// the type associated with the integer registers of a core. Use
    /// uint32_t for 32-bit cores and uint64_t for 64-bit cores.
    template<typename URV>
    bool applyConfig(Core<URV>&, bool verbose) const;

    /// Set xeln to the register width configuration held in this
    /// object returning true on success and false if this object does
    /// not contain a register width (xlen) configuration.
    bool getXlen(unsigned& registerWidth) const;

    /// Clear (make emtpy) the set of configurations held in this object.
    void clear();

  private:

    /// Force instantiation of applyConfig(Core<uint32_t>, bool).
    static bool apply(CoreConfig&, Core<uint32_t>&, bool);

    /// Force instantiation of applyConfig(Core<uint64_t>, bool).
    static bool apply(CoreConfig&, Core<uint64_t>&, bool);

    CoreConfig(const CoreConfig&) = delete;
    void operator= (const CoreConfig&) = delete;

    nlohmann::json* config_;
  };

}

