#include "replay_catalog.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "capture.h"

namespace augusta::server {

namespace {

// The extension of every file a Capturer writes (CaptureFileName).
constexpr std::string_view kCaptureExtension = ".capture";

std::optional<Capture> ReadCaptureAt(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  auto capture = ReadCapture(in);
  if (!capture.has_value()) {
    return std::nullopt;
  }
  return *std::move(capture);
}

}  // namespace

std::optional<ReplayListing> ListingOf(std::string name, const Capture& capture, const ReplayTerms& terms) {
  if (capture.header.server_pack != terms.server_pack || capture.header.tick_rate_hz != terms.tick_rate_hz) {
    return std::nullopt;
  }
  ReplayListing listing{.name = std::move(name),
                        .started = capture.header.started,
                        .ticks = 0,
                        .tick_rate_hz = capture.header.tick_rate_hz,
                        .characters = {}};
  for (const CaptureRecord& record : capture.records) {
    const auto* join = std::get_if<CapturedJoin>(&record.event);
    if (join == nullptr) {
      break;
    }
    if (!std::ranges::contains(terms.characters, join->character)) {
      return std::nullopt;
    }
    listing.characters.push_back(join->character);
  }
  if (listing.characters.empty()) {
    return std::nullopt;
  }
  // Offsets count from 0, so the last tick's offset is one short of the length.
  listing.ticks = capture.records.back().offset + 1;
  return listing;
}

ReplayCatalog::ReplayCatalog(std::filesystem::path directory, ReplayTerms terms)
    : directory_(std::move(directory)), terms_(std::move(terms)) {}

std::vector<std::filesystem::directory_entry> ReplayCatalog::Entries() const {
  std::vector<std::filesystem::directory_entry> entries;
  std::error_code error;
  for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end; it.increment(error)) {
    std::error_code type_error;
    if (it->is_regular_file(type_error) && it->path().extension() == kCaptureExtension) {
      entries.push_back(*it);
    }
  }
  std::ranges::sort(entries, {}, [](const std::filesystem::directory_entry& entry) { return entry.path().filename(); });
  return entries;
}

std::vector<ReplayListing> ReplayCatalog::List() const {
  std::vector<ReplayListing> listings;
  for (const std::filesystem::directory_entry& entry : Entries()) {
    const std::optional<Capture> capture = ReadCaptureAt(entry.path());
    if (!capture.has_value()) {
      continue;
    }
    if (std::optional<ReplayListing> listing = ListingOf(entry.path().filename().string(), *capture, terms_)) {
      listings.push_back(*std::move(listing));
    }
  }
  return listings;
}

std::optional<Capture> ReplayCatalog::Find(std::string_view name) const {
  // name is only ever compared with what the directory lists: a path from the
  // network names no file here, so it reaches none (ADR-0051).
  for (const std::filesystem::directory_entry& entry : Entries()) {
    const std::string listed = entry.path().filename().string();
    if (listed != name) {
      continue;
    }
    std::optional<Capture> capture = ReadCaptureAt(entry.path());
    if (!capture.has_value() || !ListingOf(listed, *capture, terms_).has_value()) {
      return std::nullopt;
    }
    return capture;
  }
  return std::nullopt;
}

}  // namespace augusta::server
