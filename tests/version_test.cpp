#include "augusta/version.h"

#include <gtest/gtest.h>

TEST(EngineVersion, IsNotEmpty) {
  EXPECT_FALSE(augusta::EngineVersion().empty());
}
