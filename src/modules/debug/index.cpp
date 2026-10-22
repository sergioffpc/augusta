#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/debug_index.h"
#include "augusta/debug_panels.h"

namespace augusta::debug {

DebugIndex::DebugIndex(std::vector<IndexEntry> entries) : entries_(std::move(entries)) {}

Panel DebugIndex::BuildPanel() const {
  Panel panel{.id = std::string(kDebugIndexPanelId), .title = "Debug index", .elements = {}};
  panel.elements.reserve(entries_.size());
  for (const IndexEntry& entry : entries_) {
    panel.elements.emplace_back(Toggle{.id = entry.id, .label = entry.name, .on = entry.on, .disabled = false});
  }
  return panel;
}

void DebugIndex::Update(const ControlEvent& event) {
  const auto* on = std::get_if<bool>(&event.value);
  if (event.panel != kDebugIndexPanelId || on == nullptr) {
    return;
  }
  const auto entry = std::ranges::find(entries_, event.control, &IndexEntry::id);
  if (entry != entries_.end()) {
    entry->on = *on;
  }
}

bool DebugIndex::IsOn(std::string_view id) const {
  const auto entry = std::ranges::find(entries_, id, &IndexEntry::id);
  return entry != entries_.end() && entry->on;
}

}  // namespace augusta::debug
