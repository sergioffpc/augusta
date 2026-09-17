// Asset cooker CLI (ADR-0015 through ADR-0019, ADR-0030, ROADMAP.md M2):
// a thin wrapper over augusta::asset_cooking::Cook - the real CLI
// (signing key handling, client/server output split) is a later ticket.
#include "augusta/asset_cooking.h"

#include <print>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::println(stderr, "usage: augusta_asset_cooking <stage.usd> <output.pack>");
    return 1;
  }

  const auto report = augusta::asset_cooking::Cook(argv[1], argv[2]);
  if (!report) {
    std::println(stderr, "cook failed");
    return 1;
  }

  std::println("cooked {} mesh(es) into {}", report->mesh_count, argv[2]);
  return 0;
}
