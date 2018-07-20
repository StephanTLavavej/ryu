// Copyright 2018 Ulf Adams
//
// The contents of this file may be used under the terms of the Apache License,
// Version 2.0.
//
//    (See accompanying file LICENSE-Apache or copy at
//     http://www.apache.org/licenses/LICENSE-2.0)
//
// Alternatively, the contents of this file may be used under the terms of
// the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE-Boost or copy at
//     https://www.boost.org/LICENSE_1_0.txt)
//
// Unless required by applicable law or agreed to in writing, this software
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.

_NODISCARD inline uint32_t __decimalLength9(const uint32_t __v) {
  // Function precondition: __v is not a 10-digit number.
  // (f2s: 9 digits are sufficient for round-tripping.)
  // (d2fixed: We print 9-digit blocks.)
  _STL_INTERNAL_CHECK(__v < 1000000000);
  if (__v >= 100000000) { return 9; }
  if (__v >= 10000000) { return 8; }
  if (__v >= 1000000) { return 7; }
  if (__v >= 100000) { return 6; }
  if (__v >= 10000) { return 5; }
  if (__v >= 1000) { return 4; }
  if (__v >= 100) { return 3; }
  if (__v >= 10) { return 2; }
  return 1;
}

// Returns __e == 0 ? 1 : ceil(log_2(5^__e)).
_NODISCARD inline int32_t __pow5bits(const int32_t __e) {
  // This approximation works up to the point that the multiplication overflows at __e = 3529.
  // If the multiplication were done in 64 bits, it would fail at 5^4004 which is just greater
  // than 2^9297.
  _STL_INTERNAL_CHECK(__e >= 0);
  _STL_INTERNAL_CHECK(__e <= 3528);
  return static_cast<int32_t>(((static_cast<uint32_t>(__e) * 1217359) >> 19) + 1);
}

// Returns floor(log_10(2^__e)).
_NODISCARD inline uint32_t __log10Pow2(const int32_t __e) {
  // The first value this approximation fails for is 2^1651 which is just greater than 10^297.
  _STL_INTERNAL_CHECK(__e >= 0);
  _STL_INTERNAL_CHECK(__e <= 1650);
  return (static_cast<uint32_t>(__e) * 78913) >> 18;
}

// Returns floor(log_10(5^__e)).
_NODISCARD inline uint32_t __log10Pow5(const int32_t __e) {
  // The first value this approximation fails for is 5^2621 which is just greater than 10^1832.
  _STL_INTERNAL_CHECK(__e >= 0);
  _STL_INTERNAL_CHECK(__e <= 2620);
  return (static_cast<uint32_t>(__e) * 732923) >> 20;
}

_NODISCARD inline uint32_t __float_to_bits(const float __f) {
  uint32_t __bits = 0;
  _CSTD memcpy(&__bits, &__f, sizeof(float));
  return __bits;
}

_NODISCARD inline uint64_t __double_to_bits(const double __d) {
  uint64_t __bits = 0;
  _CSTD memcpy(&__bits, &__d, sizeof(double));
  return __bits;
}
