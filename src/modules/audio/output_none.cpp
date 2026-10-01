#include <expected>
#include <memory>

#include "output.h"

namespace augusta::audio {

std::expected<std::unique_ptr<Output>, OutputError> OpenOutput() {
  return std::unexpected(OutputError{.step = OutputStep::kUnsupported, .code = 0});
}

}  // namespace augusta::audio
