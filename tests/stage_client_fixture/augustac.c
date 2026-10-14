// What scripts/stage-client.sh runs: --version, through a call into the
// shared library the fixture bundles, so a missing library fails to start.
#include <stdio.h>
#include <string.h>

int FixtureBundled(void);

// Which fixture build this is: kept, though unused, so builds differ.
__attribute__((used)) static const char kFixtureBuild[] = FIXTURE_BUILD;

int main(int argc, char** argv) {
  if (argc != 2 || strcmp(argv[1], "--version") != 0) {
    return 1;
  }
  printf("augustac %s\n", FIXTURE_VERSION);
  return FixtureBundled();
}
