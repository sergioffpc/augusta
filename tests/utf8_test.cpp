#include "augusta/utf8.h"

#include <filesystem>

#include <gtest/gtest.h>

namespace {

// A name in another script than the code page's comes out as its UTF-8 bytes,
// where path::string() would throw on Windows.
TEST(Utf8FromPath, WritesAPathInAnyScriptAsItsUtf8Bytes) {
  const std::filesystem::path path(u8"logs/жé.log");

  EXPECT_EQ(augusta::utf8::FromPath(path), "logs/\xd0\xb6\xc3\xa9.log");
}

}  // namespace
