#include "augusta/uuid.h"

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include <gtest/gtest.h>

namespace {

using augusta::uuid::Generate;
using augusta::uuid::Parse;
using augusta::uuid::Uuid;

// RFC 9562's UUIDv7 example (appendix A.6).
constexpr Uuid::ByteArray kExampleBytes{0x01, 0x7f, 0x22, 0xe2, 0x79, 0xb0, 0x7c, 0xc3,
                                        0x98, 0xc4, 0xdc, 0x0c, 0x0c, 0x07, 0x39, 0x8f};
constexpr std::string_view kExampleText = "017f22e2-79b0-7cc3-98c4-dc0c0c07398f";

bool IsCanonical(const std::string& text) {
  if (text.size() != 36) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
    if (hyphen ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

TEST(Uuid, FormatsAsTheCanonicalLowercaseHyphenatedString) {
  const Uuid id = Uuid::FromBytes(kExampleBytes);

  EXPECT_EQ(id.ToString(), kExampleText);
  EXPECT_EQ(std::format("run={}", id), std::format("run={}", kExampleText));
}

TEST(Uuid, AGeneratedIdIsA36CharacterLowercaseHyphenatedVersion7) {
  const std::string text = Generate().ToString();

  EXPECT_TRUE(IsCanonical(text)) << text;
  EXPECT_EQ(text[14], '7') << text;
}

TEST(Uuid, TwoIdsTakenOneAfterTheOtherDifferAndSortInTheOrderTaken) {
  const Uuid first = Generate();
  const Uuid second = Generate();

  EXPECT_NE(first, second);
  EXPECT_LT(first, second);
  EXPECT_LT(first.ToString(), second.ToString());
}

TEST(Uuid, ManyIdsTakenInARowAreEachLaterThanTheOneBefore) {
  Uuid previous = Generate();
  for (int i = 0; i < 10000; ++i) {
    const Uuid next = Generate();
    ASSERT_LT(previous, next) << previous.ToString() << " " << next.ToString();
    previous = next;
  }
}

TEST(Uuid, BytesRoundTrip) { EXPECT_EQ(Uuid::FromBytes(kExampleBytes).Bytes(), kExampleBytes); }

TEST(Uuid, ParsesItsOwnString) {
  const Uuid id = Generate();

  EXPECT_EQ(Parse(id.ToString()), id);
  EXPECT_EQ(Parse(kExampleText), Uuid::FromBytes(kExampleBytes));
}

TEST(Uuid, ParseRejectsAnythingButTheCanonicalForm) {
  EXPECT_EQ(Parse(""), std::nullopt);
  EXPECT_EQ(Parse("017F22E2-79B0-7CC3-98C4-DC0C0C07398F"), std::nullopt);
  EXPECT_EQ(Parse("017f22e279b07cc398c4dc0c0c07398f"), std::nullopt);
  EXPECT_EQ(Parse("{017f22e2-79b0-7cc3-98c4-dc0c0c07398f}"), std::nullopt);
  EXPECT_EQ(Parse("017f22e2-79b0-7cc3-98c4-dc0c0c07398g"), std::nullopt);
  EXPECT_EQ(Parse("017f22e2-79b0-7cc3-98c4-dc0c0c07398f0"), std::nullopt);
}

TEST(Uuid, EqualIdsHashAlikeAndDistinctOnesAreKeptApart) {
  const Uuid id = Generate();
  std::unordered_set<Uuid> ids{id, Generate(), Generate()};

  EXPECT_EQ(std::hash<Uuid>{}(id), std::hash<Uuid>{}(Uuid::FromBytes(id.Bytes())));
  EXPECT_EQ(ids.size(), 3U);
  EXPECT_TRUE(ids.contains(Uuid::FromBytes(id.Bytes())));
}

TEST(Uuid, DefaultIsTheNilId) { EXPECT_EQ(Uuid{}.ToString(), "00000000-0000-0000-0000-000000000000"); }

}  // namespace
