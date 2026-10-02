#ifndef AUGUSTA_COUNTER_H_
#define AUGUSTA_COUNTER_H_

#include <concepts>
#include <limits>

// The one ordering every protocol counter is compared by - a server tick, a
// command sequence (ADR-0038): serial-number arithmetic (RFC 1982), so a
// counter that wraps still orders the numbers on either side of the wrap. Every
// counter is wide enough never to wrap in a session (ADR-0038); comparing them
// here rather than with < keeps that a property of the widths alone, not of
// every receiver that orders them.
namespace augusta::counter {

/// Whether candidate comes after reference: it is less than half of T's range
/// ahead of it, counting on past T's maximum to 0. A number is not newer than
/// itself, and of two numbers exactly half the range apart neither is newer.
template <std::unsigned_integral T>
[[nodiscard]] constexpr bool IsNewer(T candidate, T reference) {
  // Cast back to T: a narrower T is promoted to int by the subtraction.
  const auto ahead = static_cast<T>(candidate - reference);
  constexpr T kHalf = static_cast<T>(T{1} << (std::numeric_limits<T>::digits - 1));
  return ahead != 0 && ahead < kHalf;
}

}  // namespace augusta::counter

#endif  // AUGUSTA_COUNTER_H_
