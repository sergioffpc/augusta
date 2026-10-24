#ifndef AUGUSTA_DEBUG_INDEX_H_
#define AUGUSTA_DEBUG_INDEX_H_

#include <string>
#include <string_view>
#include <vector>

#include "augusta/debug_panels.h"

/// \file
/// augusta::debug's Debug index: the Debug panel listing the debug tools a
/// client carries, each switched on or off from it (GLOSSARY.md). The client
/// Runtime gives its entries, in the `debug` config section's fixed order and
/// as that section starts them, shows its panel in Debug interaction only, and
/// routes its control events back to it; what a switch does to a tool is the
/// Runtime's, and a switch lasts until the client exits, never written back to
/// the config. Pure, and compiled only in a Development build (ADR-0074,
/// ADR-0075).
namespace augusta::debug {

/// The Debug index's Panel id, which its ControlEvents name.
inline constexpr std::string_view kDebugIndexPanelId = "debug_index";

/// One debug tool the Debug index lists.
struct IndexEntry {
  /// Unique among the entries: its toggle's control id.
  std::string id;
  /// What the index shows it as.
  std::string name;
  bool on = false;
};

class DebugIndex {
 public:
  explicit DebugIndex(std::vector<IndexEntry> entries);

  /// The index's panel: one toggle per entry, in order, each showing whether
  /// the entry is on.
  [[nodiscard]] Panel BuildPanel() const;

  /// Switches the entry a toggle event of the index's panel names; any other
  /// event, another panel's included, changes nothing.
  void Update(const ControlEvent& event);

  /// Whether the entry with this id is on: false for an id no entry has.
  [[nodiscard]] bool IsOn(std::string_view id) const;

 private:
  std::vector<IndexEntry> entries_;
};

}  // namespace augusta::debug

#endif  // AUGUSTA_DEBUG_INDEX_H_
