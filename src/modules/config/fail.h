#ifndef AUGUSTA_CONFIG_FAIL_H_
#define AUGUSTA_CONFIG_FAIL_H_

#include <expected>
#include <string>

#include "augusta/config.h"

/// \file
/// How the config file's reading (config.cpp) and the command line's parsing
/// (command_line.cpp) build an error, private to the module.
namespace augusta::config {

/// An error about subject, not yet tied to a config file: the Load* functions
/// set that once they know it.
std::unexpected<ConfigError> Fail(ConfigErrorCode code, std::string subject = {});

}  // namespace augusta::config

#endif  // AUGUSTA_CONFIG_FAIL_H_
