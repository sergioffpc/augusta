#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/scripting.h"

// What a script running in the sandbox can see of the Lua state it runs in: no
// value derived from where Lua allocated an object, so the same script decides
// the same in a Match and in its Replay (ADR-0022), each in a state of its own.
namespace {

using augusta::scripting::Script;
using augusta::scripting::Value;

// What the hook answer of script, freshly loaded into a sandbox of its own,
// returns, called once.
std::expected<Value, augusta::scripting::HookError> CallFresh(std::string_view script) {
  auto loaded = Script::Load("sandbox_test", script);
  EXPECT_TRUE(loaded.has_value()) << (loaded ? "" : loaded.error().message);
  if (!loaded) {
    return Value{};
  }
  return loaded->Call("answer", {});
}

std::string StringOf(const std::expected<Value, augusta::scripting::HookError>& answer) {
  EXPECT_TRUE(answer.has_value()) << (answer ? "" : augusta::scripting::DescribeHookError(answer.error()));
  const auto* text = answer ? std::get_if<std::string>(&answer->data) : nullptr;
  EXPECT_NE(text, nullptr);
  return text != nullptr ? *text : std::string();
}

// Requirements: NFR-09
TEST(LuaSandboxTest, ATableAsAStringIsTheSameInEveryFreshSandboxAndHoldsNoAddress) {
  constexpr std::string_view kScript = "function answer() return tostring({}) end";

  const std::string first = StringOf(CallFresh(kScript));
  const std::string second = StringOf(CallFresh(kScript));

  EXPECT_EQ(first, "table");
  EXPECT_EQ(first, second);
}

// Requirements: NFR-09
TEST(LuaSandboxTest, AFunctionAsAStringIsItsTypeName) {
  EXPECT_EQ(StringOf(CallFresh("function answer() return tostring(answer) .. ' ' .. tostring(string.len) end")),
            "function function");
}

// A metatable's __name is what Lua's own tostring puts before the address: it
// names nothing here either.
// Requirements: NFR-09
TEST(LuaSandboxTest, ATableWithANameInItsMetatableAsAStringIsStillItsTypeName) {
  EXPECT_EQ(StringOf(CallFresh("function answer() return tostring(setmetatable({}, {__name = 'x'})) end")), "table");
}

// A script's own __tostring is as deterministic as the script.
// Requirements: NFR-09
TEST(LuaSandboxTest, ATableWithItsOwnTostringIsWhatItReturns) {
  EXPECT_EQ(
      StringOf(CallFresh(
          "function answer() return tostring(setmetatable({}, {__tostring = function() return 'mine' end})) end")),
      "mine");
}

// Requirements: NFR-09
TEST(LuaSandboxTest, PlainValuesAsStringsAreLuasOwn) {
  EXPECT_EQ(StringOf(CallFresh("function answer() return tostring(nil) .. tostring(true) .. tostring(1) .. "
                               "tostring(1.5) .. tostring('s') end")),
            "niltrue11.5s");
}

// string.format's %s turns a value into a string as tostring does.
// Requirements: NFR-09
TEST(LuaSandboxTest, AFormattedTableIsItsTypeName) {
  EXPECT_EQ(StringOf(CallFresh("function answer() return string.format('%5.3s|%s|%d|%%s', {}, answer, 7) end")),
            "  tab|function|7|%s");
}

// %p is the address itself, of a string too.
// Requirements: NFR-09
TEST(LuaSandboxTest, FormattingAnAddressFailsTheHook) {
  for (const char* format : {"'%p', {}", "'%-8p', 's'", "'%d %p', 1, {}"}) {
    const auto answer = CallFresh(std::string("function answer() return string.format(") + format + ") end");
    ASSERT_FALSE(answer.has_value()) << format;
    EXPECT_NE(answer.error().subject.find("%p"), std::string::npos) << format << ": " << answer.error().subject;
  }
}

// Control for the case above: the same call with an address-free conversion succeeds.
// Requirements: NFR-09
TEST(LuaSandboxTest, FormattingAValueFormatsIt) {
  EXPECT_EQ(StringOf(CallFresh("function answer() return string.format('%d', 1) end")), "1");
}

}  // namespace
