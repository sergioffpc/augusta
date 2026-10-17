#include "augusta/window_events.h"

namespace augusta::window {

bool IsMouseButton(Key key) { return key == Key::kMouseLeft || key == Key::kMouseRight || key == Key::kMouseMiddle; }

}  // namespace augusta::window
