#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

// Runs a fuzz target's entry point once over each file named on the command
// line, without libFuzzer: how ctest replays the seeds and the regression
// fixtures (tests/fuzz/README.md) on every preset, MSVC included. A crash or an
// abort in the target fails the test like any other.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

int main(int argc, char** argv) {
  const std::vector<const char*> paths(argv + 1, argv + argc);
  for (const char* path : paths) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      std::cerr << "cannot read " << path << "\n";
      return 1;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
  }
  std::cout << "replayed " << paths.size() << " input(s)\n";
  return 0;
}
