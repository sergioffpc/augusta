#include "augusta/version.h"

#include <regex>
#include <string>

#include <gtest/gtest.h>

TEST(EngineVersion, IsNotEmpty) { EXPECT_FALSE(augusta::EngineVersion().empty()); }

// What matches a dump of the running executable to its symbols: on Windows its
// PDB's GUID and age, which a Debug build links this test with (a Release one
// only augustac, ADR-0055); on Linux its build ID, which only the executables
// are linked with (cmake/AugustaDebugInfo.cmake).
TEST(BuildIdentity, NamesWhatMatchesTheRunningExecutableToItsSymbols) {
  const std::string described = augusta::DescribeBuild();
#ifdef _WIN32
  const std::regex linked("pdb_guid=[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12} pdb_age=[0-9]+");
#ifdef NDEBUG
  EXPECT_TRUE(std::regex_match(described, linked) || described == "pdb_guid=unknown pdb_age=unknown") << described;
#else
  EXPECT_TRUE(std::regex_match(described, linked)) << described;
#endif
#else
  EXPECT_TRUE(std::regex_match(described, std::regex("build_id=([0-9a-f]{2})+|build_id=unknown"))) << described;
#endif
}
