#include "frames.h"

#include <cstddef>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <vector>

namespace augusta::server {

void WriteFrame(std::ostream& out, std::span<const std::byte> payload) {
  out.put(static_cast<char>(payload.size()));
  out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
}

Frame ReadFrame(std::istream& in, std::vector<std::byte>& payload) {
  char length = 0;
  in.read(&length, 1);
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  if (in.gcount() == 0) {
    return Frame::kEnd;
  }
  payload.resize(static_cast<unsigned char>(length));
  in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  return static_cast<std::size_t>(in.gcount()) == payload.size() ? Frame::kRead : Frame::kTorn;
}

}  // namespace augusta::server
