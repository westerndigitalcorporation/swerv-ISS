#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include "CoreConfig.hpp"
#include "Core.hpp"


using namespace WdRiscv;


CoreConfig::CoreConfig()
{
  config_ = new nlohmann::json();
}


CoreConfig::~CoreConfig()
{
  delete config_;
  config_ = nullptr;
}


bool
CoreConfig::loadConfigFile(const std::string& filePath)
{
  std::ifstream ifs(filePath);
  if (not ifs.good())
    {
      std::cerr << "Failed to open config file '" << filePath
		<< "' for input.\n";
      return false;
    }

  try
    {
      ifs >> *config_;
    }
  catch (std::exception& e)
    {
      std::cerr << e.what() << "\n";
      return false;
    }
  catch (...)
    {
      std::cerr << "Caught unknown exception while parsing "
		<< " config file '" << filePath << "'\n";
      return false;
    }

  return true;
}


namespace WdRiscv
{
  
  /// Convert given json value to an unsigned integer honoring
  /// hexadecimal prefix (0x) if any.
  uint64_t
  getJsonUnsigned(const std::string& tag, const nlohmann::json& js)
  {
    if (js.is_number())
      return js.get<unsigned>();
    if (js.is_string())
      {
	char *end = nullptr;
	std::string str = js.get<std::string>();
	uint64_t x = strtoull(str.c_str(), &end, 0);
	if (end and *end)
	  std::cerr << "Invalid config file value for '" << tag << "': "
		    << str << '\n';
	return x;
      }
    std::cerr << "Config file entry '" << tag << "' must contain a number\n";
    return 0;
  }


  /// Convert given json array value to an vector of unsigned integers
  /// honoring any hexadecimal prefix (0x) if any.
  std::vector<uint64_t>
  getJsonUnsignedVec(const std::string& tag, const nlohmann::json& js)
  {
    std::vector<uint64_t> vec;

    if (not js.is_array())
      {
	std::cerr << "Invalid config file value for '" << tag << "'"
		  << " -- expecting array of numbers\n";
	return vec;
      }

    for (const auto& item :js)
      {
	if (item.is_number())
	  vec.push_back(item.get<unsigned>());
	else if (item.is_string())
	  {
	    char *end = nullptr;
	    std::string str = item.get<std::string>();
	    uint64_t x = strtoull(str.c_str(), &end, 0);
	    if (end and *end)
	      std::cerr << "Invalid config file value for '" << tag << "': "
			<< str << '\n';
	    else
	      vec.push_back(x);
	  }
	else
	  std::cerr << "Invalid config file value for '" << tag << "'"
		    << " -- expecting array of number\n";
      }

    return vec;
  }


  /// Convert given json value to a boolean.
  bool
  getJsonBoolean(const std::string& tag, const nlohmann::json& js)
  {
    if (js.is_boolean())
      return js.get<bool>();
    if (js.is_number())
      return js.get<unsigned>();
    if (js.is_string())
      {
	std::string str = js.get<std::string>();
	if (str == "0" or str == "false" or str == "False")
	  return false;
	if (str == "1" or str == "true" or str == "True")
	  return true;
	std::cerr << "Invalid config file value for '" << tag << "': "
		  << str << '\n';
	return false;
      }
    std::cerr << "Config file entry '" << tag << "' must contain a bool\n";
    return false;
  }

}


template <typename URV>
static
bool
applyCsrConfig(Core<URV>& core, const nlohmann::json& config, bool verbose)
{
  if (not config.count("csr"))
    return true;  // Nothing to apply

  const auto& csrs = config.at("csr");
  if (not csrs.is_object())
    {
      std::cerr << "Invalid csr entry in config file (expecting an object)\n";
      return false;
    }

  unsigned errors = 0;
  for (auto it = csrs.begin(); it != csrs.end(); ++it)
    {
      const std::string& csrName = it.key();
      const auto& conf = it.value();

      URV reset = 0, mask = 0, pokeMask = 0;
      bool isDebug = false, exists = true;

      const Csr<URV>* csr = core.findCsr(csrName);
      if (csr)
	{
	  reset = csr->getResetValue();
	  mask = csr->getWriteMask();
	  pokeMask = csr->getPokeMask();
	  isDebug = csr->isDebug();
	}

      if (conf.count("reset"))
	reset = getJsonUnsigned(csrName + ".reset", conf.at("reset"));

      if (conf.count("mask"))
	mask = getJsonUnsigned(csrName + ".mask", conf.at("mask"));

      if (conf.count("poke_mask"))
	pokeMask = getJsonUnsigned(csrName + ".poke_mask",
				   conf.at("poke_mask"));

      if (conf.count("debug"))
	isDebug = getJsonBoolean(csrName + ".bool", conf.at("debug"));

      if (conf.count("exists"))
	exists = getJsonBoolean(csrName + ".bool", conf.at("exists"));

      // If number pesent and csr is not defined, then define a new
      // CSR; otehrwise, configure.
      if (conf.count("number"))
	{
	  unsigned number = getJsonUnsigned(csrName + ".number",
					    conf.at("number"));
	  if (csr)
	    {
	      if (csr->getNumber() != CsrNumber(number))
		{
		  std::cerr << "Invalid config file entry for CSR "
			    << csrName << ": Number (0x" << std::hex << number
			    << ") does not match that of previous definition ("
			    << "0x" << std::hex << unsigned(csr->getNumber())
			    << ")\n";
		  errors++;
		  continue;
		}
	      // If number matches we configure below
	    }
	  else if (not core.defineCsr(csrName, CsrNumber(number), exists,
				      reset, mask, pokeMask, isDebug))
	    {
	      std::cerr << "Invalid config file CSR definition with name "
			<< csrName << " and number 0x" << std::hex << number
			<< ": Number already in use\n";
	      errors++;
	      continue;
	    }
	}

      bool exists0 = csr->isImplemented(), isDebug0 = csr->isDebug();
      URV reset0 = csr->getResetValue(), mask0 = csr->getWriteMask();
      URV pokeMask0 = csr->getPokeMask();

      if (not core.configCsr(csrName, exists, reset, mask, pokeMask,
				  isDebug))
	{
	  std::cerr << "Invalid CSR (" << csrName << ") in config file.\n";
	  errors++;
	}
      else if (verbose)
	{
	  if (exists0 != exists or isDebug0 != isDebug or reset0 != reset or
	      mask0 != mask or pokeMask0 != pokeMask)
	    {
	      std::cerr << "Configuration of CSR (" << csrName <<
		") changed in config file:\n";

	      if (exists0 != exists)
		std::cerr << "  implemented: " << exists0 << " to "
			  << exists << '\n';

	      if (isDebug0 != isDebug)
		std::cerr << "  debug: " << isDebug0 << " to "
			  << isDebug << '\n';

	      if (reset0 != reset)
		std::cerr << "  reset: 0x" << std::hex << reset0
			  << " to 0x" << std::hex << reset << '\n';

	      if (mask0 != mask)
		std::cerr << "  mask: 0x" << std::hex << mask0
			  << " to 0x" << std::hex << mask << '\n';

	      if (pokeMask0 != pokeMask)
		std::cerr << "  poke_mask: " << std::hex << pokeMask0
			  << " to 0x" << pokeMask << '\n';
	    }
	}
    }

  return errors == 0;
}


template <typename URV>
static
bool
applyPicConfig(Core<URV>& core, const nlohmann::json& config)
{
  if (not config.count("pic"))
    return true;  // Nothing to apply.

  const auto& pic = config.at("pic");
  bool badPic = false;
  for (const auto& tag : { "region", "size", "offset", "mpiccfg_offset",
	"meipl_offset", "meip_offset", "meie_offset", "meigwctrl_offset",
	"meigwclr_offset", "total_int", "int_words" } )
    {
      if (not pic.count(tag))
	{
	  std::cerr << "Missing '" << tag << "' entry in "
		    << "config file PIC section\n";
	  badPic = true;
	}
    }

  if (badPic)
    ;

  // Define pic region.
  uint64_t region = getJsonUnsigned("region", pic.at("region"));
  uint64_t size = getJsonUnsigned("size", pic.at("size"));
  uint64_t regionOffset = getJsonUnsigned("offset", pic.at("offset"));
  if (not core.defineMemoryMappedRegisterRegion(region, regionOffset, size))
    return false;

  // Define the memory mapped registers.
  uint64_t smax = getJsonUnsigned("pic.total_int", pic.at("total_int"));
  uint64_t xmax = getJsonUnsigned("pic.int_words", pic.at("int_words"));

  unsigned errors = 0;

  // Start by giving all registers in region a mask of zero.
  size_t possibleRegCount = size / 4;
  for (size_t ix = 0; ix < possibleRegCount; ++ix)
    core.defineMemoryMappedRegisterWriteMask(region, regionOffset, 0, ix, 0);

  std::vector<std::string> names = { "mpiccfg_offset", "meipl_offset",
				     "meip_offset", "meie_offset",
				     "meigwctrl_offset", "meigwclr_offset" };

  // These should be in the config file. The mask for meigwclr is zero
  // because the state is always zero.
  std::vector<uint32_t> masks = { 1, 0xf, 0, 1, 3, 0 };
  std::vector<size_t> counts = { 1, smax, xmax, smax, smax, smax };

  // meipl, meie, meigwctrl and meigwclr indexing start at 1 (instead
  // of 0): adjust
  std::vector<size_t> adjust = { 0, 4, 0, 4, 4, 4 };

  for (size_t i = 0; i < names.size(); ++i)
    {
      auto mask = masks.at(i);
      const auto& name = names.at(i);
      auto count = counts.at(i);

      if (not pic.count(name))
	continue;  // Should be an error.

      uint64_t registerOffset = getJsonUnsigned(("pic." + name), pic.at(name));
      registerOffset += adjust.at(i);
      for (size_t regIx = 0; regIx < count; ++regIx)
	if (not core.defineMemoryMappedRegisterWriteMask(region, regionOffset,
							 registerOffset, regIx,
							 mask))
	  errors++;
    }

  return errors == 0;
}


template <typename URV>
static
bool
applyTriggerConfig(Core<URV>& core, const nlohmann::json& config)
{
  if (not config.count("triggers"))
    return true;  // Nothing to apply

  const auto& triggers = config.at("triggers");
  if (not triggers.is_array())
    {
      std::cerr << "Invalid triggers entry in config file (expecting an array)\n";
      return false;
    }

  unsigned errors = 0;
  unsigned ix = 0;
  for (auto it = triggers.begin(); it != triggers.end(); ++it, ++ix)
    {
      const auto& trig = *it;
      std::string name = std::string("trigger") + std::to_string(ix);
      if (not trig.is_object())
	{
	  std::cerr << "Invalid trigger in config file triggers array "
		    << "(expecting an object at index " << std::dec << ix << ")\n";
	  ++errors;
	  break;
	}
      bool ok = true;
      for (const auto& tag : {"reset", "mask", "poke_mask"})
	if (not trig.count(tag))
	  {
	    std::cerr << "Trigger " << name << " has no '" << tag
		      << "' entry in config file\n";
	    ok = false;
	  }
      if (not ok)
	{
	  errors++;
	  continue;
	}
      auto resets = getJsonUnsignedVec(name + ".reset", trig.at("reset"));
      auto masks = getJsonUnsignedVec(name + ".mask", trig.at("mask"));
      auto pokeMasks = getJsonUnsignedVec(name + ".poke_mask", trig.at("poke_mask"));

      if (resets.size() != 3)
	{
	  std::cerr << "Trigger " << name << ": Bad item count (" << resets.size()
		    << ") for 'reset' field in config file. Expecting 3.\n";
	  ok = false;
	}

      if (masks.size() != 3)
	{
	  std::cerr << "Trigger " << name << ": Bad item count (" << masks.size()
		    << ") for 'mask' field in config file. Expecting 3.\n";
	  ok = false;
	}

      if (pokeMasks.size() != 3)
	{
	  std::cerr << "Trigger " << name << ": Bad item count (" << pokeMasks.size()
		    << ") for 'poke_mask' field in config file. Expecting 3.\n";
	  ok = false;
	}

      if (not ok)
	{
	  errors++;
	  continue;
	}
      if (not core.configTrigger(ix, resets.at(0), resets.at(1), resets.at(2),
				 masks.at(0), masks.at(1), masks.at(2),
				 pokeMasks.at(0), pokeMasks.at(1), pokeMasks.at(2)))
	{
	  std::cerr << "Failed to configure trigger " << std::dec << ix << '\n';
	  ++errors;
	}
    }

  return errors == 0;
}


template<typename URV>
bool
CoreConfig::applyConfig(Core<URV>& core, bool verbose) const
{
  // Define PC value after reset.
  if (config_ -> count("reset_vec"))
    {
      URV resetPc = getJsonUnsigned("reset_vec", config_ -> at("reset_vec"));
      core.defineResetPc(resetPc);
    }

  // Define non-maskable-interrupt pc
  if (config_ -> count("nmi_vec"))
    {
      URV nmiPc = getJsonUnsigned("nmi_vec", config_ -> at("nmi_vec"));
      core.defineNmiPc(nmiPc);
    }

  if (config_ -> count("store_error_rollback"))
    {
      bool ser = getJsonBoolean("store_error_rollback",
				config_ -> at("store_error_rollback"));
      core.enableStoreErrorRollback(ser);
    }

  if (config_ -> count("load_error_rollback"))
    {
      bool ler = getJsonBoolean("load_error_rollback",
				config_ -> at("load_error_rollback"));
      core.enableLoadErrorRollback(ler);
    }

  if (config_ -> count("load_queue_size"))
    {
      unsigned lqs = getJsonUnsigned("load_queue_size",
				     config_ -> at("load_queue_size"));
      if (lqs > 64)
	{
	  std::cerr << "Config file load queue size (" << lqs << ") too large"
		    << " -- using 64.\n";
	  lqs = 64;
	}
      core.setLoadQueueSize(lqs);
    }

  if (config_ -> count("memmap"))
    {
      const auto& memmap = config_ -> at("memmap");
      if (memmap.count("consoleio"))
	{
	  URV io = getJsonUnsigned("memmap.consoleio", memmap.at("consoleio"));
	  core.setConsoleIo(io);
	}
    }

  if (config_ -> count("even_odd_trigger_chains"))
    {
      bool chainPairs = getJsonBoolean("even_odd_trigger_chains",
				       config_ -> at("even_odd_trigger_chains"));
      core.configEvenOddTriggerChaining(chainPairs);
    }

  unsigned errors = 0;

  if (config_ -> count("iccm"))
    {
      const auto& iccm = config_ -> at("iccm");
      if (iccm.count("region") and iccm.count("size") and iccm.count("offset"))
	{
	  size_t region = getJsonUnsigned("iccm.region", iccm.at("region"));
	  size_t size   = getJsonUnsigned("iccm.size",   iccm.at("size"));
	  size_t offset = getJsonUnsigned("iccm.offset", iccm.at("offset"));
	  if (not core.defineIccm(region, offset, size))
	    errors++;
	}
      else
	{
	  std::cerr << "The ICCM entry in the configuration file must contain "
		    << "a region, offset and a size entry.\n";
	  errors++;
	}
    }

  if (config_ -> count("dccm"))
    {
      const auto& dccm = config_ -> at("dccm");
      if (dccm.count("region") and dccm.count("size") and dccm.count("offset"))
	{
	  size_t region = getJsonUnsigned("dccm.region", dccm.at("region"));
	  size_t size   = getJsonUnsigned("dccm.size",   dccm.at("size"));
	  size_t offset = getJsonUnsigned("dccm.offset", dccm.at("offset"));
	  if (not core.defineDccm(region, offset, size))
	    errors++;
	}
      else
	{
	  std::cerr << "The DCCM entry in the configuration file must contain "
		    << "a region, offset and a size entry.\n";
	  errors++;
	}
    }

  std::string tag = "num_mmode_perf_regs";
  if (config_ -> count(tag))
    {
      unsigned count = getJsonUnsigned(tag, config_ -> at(tag));
      if (not core.configMachineModePerfCounters(count))
	errors++;
    }

  tag = "max_mmode_perf_event";
  if (config_ -> count(tag))
    {
      unsigned maxId = getJsonUnsigned(tag, config_ -> at(tag));
      core.configMachineModeMaxPerfEvent(maxId);
    }

  if (not applyCsrConfig(core, *config_, verbose))
    errors++;

  if (not applyPicConfig(core, *config_))
    errors++;

  if (not applyTriggerConfig(core, *config_))
    errors++;

  core.finishMemoryConfig();

  return errors == 0;
}


bool
CoreConfig::getXlen(unsigned& xlen) const
{
  if (config_ -> count("xlen"))
    {
      xlen = getJsonUnsigned("xlen", config_ -> at("xlen"));
      return true;
    }
  return false;
}


void
CoreConfig::clear()
{
  config_ -> clear();
}


bool
CoreConfig::apply(CoreConfig& conf, Core<uint32_t>& core, bool verbose)
{
  return conf.applyConfig(core, verbose);
}


bool
CoreConfig::apply(CoreConfig& conf, Core<uint64_t>& core, bool verbose)
{
  return conf.applyConfig(core, verbose);
}
