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

inline constexpr int __POW10_ADDITIONAL_BITS = 120;

#ifdef _M_X64
// Returns the low 64 bits of the high 128 bits of the 256-bit product of a and b.
_NODISCARD inline uint64_t __umul256_hi128_lo64(
  const uint64_t __aHi, const uint64_t __aLo, const uint64_t __bHi, const uint64_t __bLo) {
  uint64_t __b00Hi;
  const uint64_t __b00Lo = __ryu_umul128(__aLo, __bLo, &__b00Hi);
  uint64_t __b01Hi;
  const uint64_t __b01Lo = __ryu_umul128(__aLo, __bHi, &__b01Hi);
  uint64_t __b10Hi;
  const uint64_t __b10Lo = __ryu_umul128(__aHi, __bLo, &__b10Hi);
  uint64_t __b11Hi;
  const uint64_t __b11Lo = __ryu_umul128(__aHi, __bHi, &__b11Hi);
  (void) __b00Lo; // unused
  (void) __b11Hi; // unused
  const uint64_t __temp1Lo = __b10Lo + __b00Hi;
  const uint64_t __temp1Hi = __b10Hi + (__temp1Lo < __b10Lo);
  const uint64_t __temp2Lo = __b01Lo + __temp1Lo;
  const uint64_t __temp2Hi = __b01Hi + (__temp2Lo < __b01Lo);
  return __b11Lo + __temp1Hi + __temp2Hi;
}

_NODISCARD inline uint32_t __uint128_mod1e9(const uint64_t __vHi, const uint64_t __vLo) {
  // After multiplying, we're going to shift right by 29, then truncate to uint32_t.
  // This means that we need only 29 + 32 = 61 bits, so we can truncate to uint64_t before shifting.
  const uint64_t __multiplied = __umul256_hi128_lo64(__vHi, __vLo, 0x89705F4136B4A597u, 0x31680A88F8953031u);

  // For uint32_t truncation, see the __mod1e9() comment in d2s_intrinsics.h.
  const uint32_t __shifted = static_cast<uint32_t>(__multiplied >> 29);

  return static_cast<uint32_t>(__vLo) - 1000000000 * __shifted;
}
#endif // ^^^ intrinsics available ^^^

_NODISCARD inline uint32_t __mulShift_mod1e9(const uint64_t __m, const uint64_t* const __mul, const int32_t __j) {
  uint64_t __high0;                                               // 64
  const uint64_t __low0 = __ryu_umul128(__m, __mul[0], &__high0); // 0
  uint64_t __high1;                                               // 128
  const uint64_t __low1 = __ryu_umul128(__m, __mul[1], &__high1); // 64
  uint64_t __high2;                                               // 192
  const uint64_t __low2 = __ryu_umul128(__m, __mul[2], &__high2); // 128
  const uint64_t __s0low = __low0;                  // 0
  (void) __s0low; // unused
  const uint64_t __s0high = __low1 + __high0;       // 64
  const uint32_t __c1 = __s0high < __low1;
  const uint64_t __s1low = __low2 + __high1 + __c1; // 128
  const uint32_t __c2 = __s1low < __low2; // __high1 + __c1 can't overflow, so compare against __low2
  const uint64_t __s1high = __high2 + __c2;         // 192
  _STL_INTERNAL_CHECK(__j >= 128);
  _STL_INTERNAL_CHECK(__j <= 180);
#ifdef _M_X64
  const uint32_t __dist = static_cast<uint32_t>(__j - 128); // __dist: [0, 52]
  const uint64_t __shiftedhigh = __s1high >> __dist;
  const uint64_t __shiftedlow = __ryu_shiftright128(__s1low, __s1high, __dist);
  return __uint128_mod1e9(__shiftedhigh, __shiftedlow);
#else // ^^^ intrinsics available ^^^ / vvv intrinsics unavailable vvv
  if (__j < 160) { // __j: [128, 160)
    const uint64_t __r0 = __mod1e9(__s1high);
    const uint64_t __r1 = __mod1e9((__r0 << 32) | (__s1low >> 32));
    const uint64_t __r2 = ((__r1 << 32) | (__s1low & 0xffffffff));
    return __mod1e9(__r2 >> (__j - 128));
  } else { // __j: [160, 192)
    const uint64_t __r0 = __mod1e9(__s1high);
    const uint64_t __r1 = ((__r0 << 32) | (__s1low >> 32));
    return __mod1e9(__r1 >> (__j - 160));
  }
#endif // ^^^ intrinsics unavailable ^^^
}

inline void __append_n_digits(const uint32_t __olength, uint32_t __digits, char* const __result) {
  uint32_t __i = 0;
  while (__digits >= 10000) {
#ifdef __clang__ // TRANSITION, LLVM#38217
    const uint32_t __c = __digits - 10000 * (__digits / 10000);
#else
    const uint32_t __c = __digits % 10000;
#endif
    __digits /= 10000;
    const uint32_t __c0 = (__c % 100) << 1;
    const uint32_t __c1 = (__c / 100) << 1;
    _CSTD memcpy(__result + __olength - __i - 2, __DIGIT_TABLE + __c0, 2);
    _CSTD memcpy(__result + __olength - __i - 4, __DIGIT_TABLE + __c1, 2);
    __i += 4;
  }
  if (__digits >= 100) {
    const uint32_t __c = (__digits % 100) << 1;
    __digits /= 100;
    _CSTD memcpy(__result + __olength - __i - 2, __DIGIT_TABLE + __c, 2);
    __i += 2;
  }
  if (__digits >= 10) {
    const uint32_t __c = __digits << 1;
    _CSTD memcpy(__result + __olength - __i - 2, __DIGIT_TABLE + __c, 2);
  } else {
    __result[0] = static_cast<char>('0' + __digits);
  }
}

inline void __append_d_digits(const uint32_t __olength, uint32_t __digits, char* const __result) {
  uint32_t __i = 0;
  while (__digits >= 10000) {
#ifdef __clang__ // TRANSITION, LLVM#38217
    const uint32_t __c = __digits - 10000 * (__digits / 10000);
#else
    const uint32_t __c = __digits % 10000;
#endif
    __digits /= 10000;
    const uint32_t __c0 = (__c % 100) << 1;
    const uint32_t __c1 = (__c / 100) << 1;
    _CSTD memcpy(__result + __olength + 1 - __i - 2, __DIGIT_TABLE + __c0, 2);
    _CSTD memcpy(__result + __olength + 1 - __i - 4, __DIGIT_TABLE + __c1, 2);
    __i += 4;
  }
  if (__digits >= 100) {
    const uint32_t __c = (__digits % 100) << 1;
    __digits /= 100;
    _CSTD memcpy(__result + __olength + 1 - __i - 2, __DIGIT_TABLE + __c, 2);
    __i += 2;
  }
  if (__digits >= 10) {
    const uint32_t __c = __digits << 1;
    __result[2] = __DIGIT_TABLE[__c + 1];
    __result[1] = '.';
    __result[0] = __DIGIT_TABLE[__c];
  } else {
    __result[1] = '.';
    __result[0] = static_cast<char>('0' + __digits);
  }
}

inline void __append_c_digits(const uint32_t __count, uint32_t __digits, char* const __result) {
  uint32_t __i = 0;
  for (; __i < __count - 1; __i += 2) {
    const uint32_t __c = (__digits % 100) << 1;
    __digits /= 100;
    _CSTD memcpy(__result + __count - __i - 2, __DIGIT_TABLE + __c, 2);
  }
  if (__i < __count) {
    const char __c = static_cast<char>('0' + (__digits % 10));
    __result[__count - __i - 1] = __c;
  }
}

inline void __append_nine_digits(uint32_t __digits, char* const __result) {
  if (__digits == 0) {
    _CSTD memset(__result, '0', 9);
    return;
  }

  for (uint32_t __i = 0; __i < 5; __i += 4) {
#ifdef __clang__ // TRANSITION, LLVM#38217
    const uint32_t __c = __digits - 10000 * (__digits / 10000);
#else
    const uint32_t __c = __digits % 10000;
#endif
    __digits /= 10000;
    const uint32_t __c0 = (__c % 100) << 1;
    const uint32_t __c1 = (__c / 100) << 1;
    _CSTD memcpy(__result + 7 - __i, __DIGIT_TABLE + __c0, 2);
    _CSTD memcpy(__result + 5 - __i, __DIGIT_TABLE + __c1, 2);
  }
  __result[0] = static_cast<char>('0' + __digits);
}

_NODISCARD inline uint32_t __indexForExponent(const uint32_t __e) {
  return (__e + 15) / 16;
}

_NODISCARD inline uint32_t __pow10BitsForIndex(const uint32_t __idx) {
  return 16 * __idx + __POW10_ADDITIONAL_BITS;
}

_NODISCARD inline uint32_t __lengthForIndex(const uint32_t __idx) {
  // +1 for ceil, +16 for mantissa, +8 to round up when dividing by 9
  return (__log10Pow2(16 * static_cast<int32_t>(__idx)) + 1 + 16 + 8) / 9;
}

_NODISCARD inline int __d2fixed_buffered_n(const double __d, const uint32_t __precision, char* const __result) {
  const uint64_t __bits = __double_to_bits(__d);

  // Case distinction; exit early for the easy cases.
  if (__bits == 0) {
    int __index = 0;
    __result[__index++] = '0';
    if (__precision > 0) {
      __result[__index++] = '.';
      _CSTD memset(__result + __index, '0', __precision);
      __index += __precision;
    }
    return __index;
  }

  // Decode __bits into mantissa and exponent.
  const uint64_t __ieeeMantissa = __bits & ((1ull << __DOUBLE_MANTISSA_BITS) - 1);
  const uint32_t __ieeeExponent = static_cast<uint32_t>(__bits >> __DOUBLE_MANTISSA_BITS);

  int32_t __e2;
  uint64_t __m2;
  if (__ieeeExponent == 0) {
    __e2 = 1 - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS;
    __m2 = __ieeeMantissa;
  } else {
    __e2 = static_cast<int32_t>(__ieeeExponent) - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS;
    __m2 = (1ull << __DOUBLE_MANTISSA_BITS) | __ieeeMantissa;
  }

  int __index = 0;
  bool __nonzero = false;
  if (__e2 >= -52) {
    const uint32_t __idx = __e2 < 0 ? 0 : __indexForExponent(static_cast<uint32_t>(__e2));
    const uint32_t __p10bits = __pow10BitsForIndex(__idx);
    const int32_t __len = static_cast<int32_t>(__lengthForIndex(__idx));
    for (int32_t __i = __len - 1; __i >= 0; --__i) {
      const uint32_t __j = __p10bits - __e2;
      // Temporary: __j is usually around 128, and by shifting a bit, we push it to 128 or above, which is
      // a slightly faster code path in __mulShift_mod1e9. Instead, we can just increase the multipliers.
      const uint32_t __digits = __mulShift_mod1e9(__m2 << 8, __POW10_SPLIT[__POW10_OFFSET[__idx] + __i], static_cast<int32_t>(__j + 8));
      if (__nonzero) {
        __append_nine_digits(__digits, __result + __index);
        __index += 9;
      } else if (__digits != 0) {
        const uint32_t __olength = __decimalLength9(__digits);
        __append_n_digits(__olength, __digits, __result + __index);
        __index += __olength;
        __nonzero = true;
      }
    }
  }
  if (!__nonzero) {
    __result[__index++] = '0';
  }
  if (__precision > 0) {
    __result[__index++] = '.';
  }
  if (__e2 < 0) {
    const int32_t __idx = -__e2 / 16;
    const uint32_t __blocks = __precision / 9 + 1;
    // 0 = don't round up; 1 = round up unconditionally; 2 = round up if odd.
    int __roundUp = 0;
    uint32_t __i = 0;
    if (__blocks <= __MIN_BLOCK_2[__idx]) {
      __i = __blocks;
      _CSTD memset(__result + __index, '0', __precision);
      __index += __precision;
    } else if (__i < __MIN_BLOCK_2[__idx]) {
      __i = __MIN_BLOCK_2[__idx];
      _CSTD memset(__result + __index, '0', 9 * __i);
      __index += 9 * __i;
    }
    for (; __i < __blocks; ++__i) {
      const int32_t __j = __ADDITIONAL_BITS_2 + (-__e2 - 16 * __idx);
      const uint32_t __p = __POW10_OFFSET_2[__idx] + __i - __MIN_BLOCK_2[__idx];
      if (__p >= __POW10_OFFSET_2[__idx + 1]) {
        // If the remaining digits are all 0, then we might as well use memset.
        // No rounding required in this case.
        const uint32_t __fill = __precision - 9 * __i;
        _CSTD memset(__result + __index, '0', __fill);
        __index += __fill;
        break;
      }
      // Temporary: __j is usually around 128, and by shifting a bit, we push it to 128 or above, which is
      // a slightly faster code path in __mulShift_mod1e9. Instead, we can just increase the multipliers.
      uint32_t __digits = __mulShift_mod1e9(__m2 << 8, __POW10_SPLIT_2[__p], __j + 8);
      if (__i < __blocks - 1) {
        __append_nine_digits(__digits, __result + __index);
        __index += 9;
      } else {
        const uint32_t __maximum = __precision - 9 * __i;
        uint32_t __lastDigit = 0;
        for (uint32_t __k = 0; __k < 9 - __maximum; ++__k) {
          __lastDigit = __digits % 10;
          __digits /= 10;
        }
        if (__lastDigit != 5) {
          __roundUp = __lastDigit > 5;
        } else {
          // Is m * 10^(additionalDigits + 1) / 2^(-__e2) integer?
          const int32_t __requiredTwos = -__e2 - static_cast<int32_t>(__precision) - 1;
          const bool __trailingZeros = __requiredTwos <= 0
            || (__requiredTwos < 60 && __multipleOfPowerOf2(__m2, static_cast<uint32_t>(__requiredTwos)));
          __roundUp = __trailingZeros ? 2 : 1;
        }
        if (__maximum > 0) {
          __append_c_digits(__maximum, __digits, __result + __index);
          __index += __maximum;
        }
        break;
      }
    }
    if (__roundUp != 0) {
      int __roundIndex = __index;
      int __dotIndex = 0; // '.' can't be located at index 0
      while (true) {
        --__roundIndex;
        if (__roundIndex == -1) {
          __result[0] = '1';
          if (__dotIndex > 0) {
            __result[__dotIndex] = '0';
            __result[__dotIndex + 1] = '.';
          }
          __result[__index++] = '0';
          break;
        }
        const char __c = __result[__roundIndex];
        if (__c == '.') {
          __dotIndex = __roundIndex;
          continue;
        } else if (__c == '9') {
          __result[__roundIndex] = '0';
          __roundUp = 1;
          continue;
        } else {
          if (__roundUp == 2 && __c % 2 == 0) {
            break;
          }
          __result[__roundIndex] = __c + 1;
          break;
        }
      }
    }
  } else {
    _CSTD memset(__result + __index, '0', __precision);
    __index += __precision;
  }
  return __index;
}

_NODISCARD inline int __d2exp_buffered_n(const double __d, uint32_t __precision, char* const __result) {
  const uint64_t __bits = __double_to_bits(__d);

  // Case distinction; exit early for the easy cases.
  if (__bits == 0) {
    int __index = 0;
    __result[__index++] = '0';
    if (__precision > 0) {
      __result[__index++] = '.';
      _CSTD memset(__result + __index, '0', __precision);
      __index += __precision;
    }
    _CSTD memcpy(__result + __index, "e+00", 4);
    __index += 4;
    return __index;
  }

  // Decode __bits into mantissa and exponent.
  const uint64_t __ieeeMantissa = __bits & ((1ull << __DOUBLE_MANTISSA_BITS) - 1);
  const uint32_t __ieeeExponent = static_cast<uint32_t>(__bits >> __DOUBLE_MANTISSA_BITS);

  int32_t __e2;
  uint64_t __m2;
  if (__ieeeExponent == 0) {
    __e2 = 1 - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS;
    __m2 = __ieeeMantissa;
  } else {
    __e2 = static_cast<int32_t>(__ieeeExponent) - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS;
    __m2 = (1ull << __DOUBLE_MANTISSA_BITS) | __ieeeMantissa;
  }

  const bool __printDecimalPoint = __precision > 0;
  ++__precision;
  int __index = 0;
  uint32_t __digits = 0;
  uint32_t __printedDigits = 0;
  uint32_t __availableDigits = 0;
  int32_t __exp = 0;
  if (__e2 >= -52) {
    const uint32_t __idx = __e2 < 0 ? 0 : __indexForExponent(static_cast<uint32_t>(__e2));
    const uint32_t __p10bits = __pow10BitsForIndex(__idx);
    const int32_t __len = static_cast<int32_t>(__lengthForIndex(__idx));
    for (int32_t __i = __len - 1; __i >= 0; --__i) {
      const uint32_t __j = __p10bits - __e2;
      // Temporary: __j is usually around 128, and by shifting a bit, we push it to 128 or above, which is
      // a slightly faster code path in __mulShift_mod1e9. Instead, we can just increase the multipliers.
      __digits = __mulShift_mod1e9(__m2 << 8, __POW10_SPLIT[__POW10_OFFSET[__idx] + __i], static_cast<int32_t>(__j + 8));
      if (__printedDigits != 0) {
        if (__printedDigits + 9 > __precision) {
          __availableDigits = 9;
          break;
        }
        __append_nine_digits(__digits, __result + __index);
        __index += 9;
        __printedDigits += 9;
      } else if (__digits != 0) {
        __availableDigits = __decimalLength9(__digits);
        __exp = __i * 9 + static_cast<int32_t>(__availableDigits) - 1;
        if (__availableDigits > __precision) {
          break;
        }
        if (__printDecimalPoint) {
          __append_d_digits(__availableDigits, __digits, __result + __index);
          __index += __availableDigits + 1; // +1 for decimal point
        } else {
          __result[__index++] = static_cast<char>('0' + __digits);
        }
        __printedDigits = __availableDigits;
        __availableDigits = 0;
      }
    }
  }

  if (__e2 < 0 && __availableDigits == 0) {
    const int32_t __idx = -__e2 / 16;
    for (int32_t __i = __MIN_BLOCK_2[__idx]; __i < 200; ++__i) {
      const int32_t __j = __ADDITIONAL_BITS_2 + (-__e2 - 16 * __idx);
      const uint32_t __p = __POW10_OFFSET_2[__idx] + static_cast<uint32_t>(__i) - __MIN_BLOCK_2[__idx];
      // Temporary: __j is usually around 128, and by shifting a bit, we push it to 128 or above, which is
      // a slightly faster code path in __mulShift_mod1e9. Instead, we can just increase the multipliers.
      __digits = (__p >= __POW10_OFFSET_2[__idx + 1]) ? 0 : __mulShift_mod1e9(__m2 << 8, __POW10_SPLIT_2[__p], __j + 8);
      if (__printedDigits != 0) {
        if (__printedDigits + 9 > __precision) {
          __availableDigits = 9;
          break;
        }
        __append_nine_digits(__digits, __result + __index);
        __index += 9;
        __printedDigits += 9;
      } else if (__digits != 0) {
        __availableDigits = __decimalLength9(__digits);
        __exp = -(__i + 1) * 9 + static_cast<int32_t>(__availableDigits) - 1;
        if (__availableDigits > __precision) {
          break;
        }
        if (__printDecimalPoint) {
          __append_d_digits(__availableDigits, __digits, __result + __index);
          __index += __availableDigits + 1; // +1 for decimal point
        } else {
          __result[__index++] = static_cast<char>('0' + __digits);
        }
        __printedDigits = __availableDigits;
        __availableDigits = 0;
      }
    }
  }

  const uint32_t __maximum = __precision - __printedDigits;
  if (__availableDigits == 0) {
    __digits = 0;
  }
  uint32_t __lastDigit = 0;
  if (__availableDigits > __maximum) {
    for (uint32_t __k = 0; __k < __availableDigits - __maximum; ++__k) {
      __lastDigit = __digits % 10;
      __digits /= 10;
    }
  }
  // 0 = don't round up; 1 = round up unconditionally; 2 = round up if odd.
  int __roundUp = 0;
  if (__lastDigit != 5) {
    __roundUp = __lastDigit > 5;
  } else {
    // Is m * 2^__e2 * 10^(__precision + 1 - __exp) integer?
    // __precision was already increased by 1, so we don't need to write + 1 here.
    const int32_t __rexp = static_cast<int32_t>(__precision) - __exp;
    const int32_t __requiredTwos = -__e2 - __rexp;
    bool __trailingZeros = __requiredTwos <= 0
      || (__requiredTwos < 60 && __multipleOfPowerOf2(__m2, static_cast<uint32_t>(__requiredTwos)));
    if (__rexp < 0) {
      const int32_t __requiredFives = -__rexp;
      __trailingZeros = __trailingZeros && __multipleOfPowerOf5(__m2, static_cast<uint32_t>(__requiredFives));
    }
    __roundUp = __trailingZeros ? 2 : 1;
  }
  if (__printedDigits != 0) {
    if (__digits == 0) {
      _CSTD memset(__result + __index, '0', __maximum);
    } else {
      __append_c_digits(__maximum, __digits, __result + __index);
    }
    __index += __maximum;
  } else {
    if (__printDecimalPoint) {
      __append_d_digits(__maximum, __digits, __result + __index);
      __index += __maximum + 1; // +1 for decimal point
    } else {
      __result[__index++] = static_cast<char>('0' + __digits);
    }
  }
  if (__roundUp != 0) {
    int __roundIndex = __index;
    while (true) {
      --__roundIndex;
      if (__roundIndex == -1) {
        __result[0] = '1';
        ++__exp;
        break;
      }
      const char __c = __result[__roundIndex];
      if (__c == '.') {
        continue;
      } else if (__c == '9') {
        __result[__roundIndex] = '0';
        __roundUp = 1;
        continue;
      } else {
        if (__roundUp == 2 && __c % 2 == 0) {
          break;
        }
        __result[__roundIndex] = __c + 1;
        break;
      }
    }
  }
  __result[__index++] = 'e';
  if (__exp < 0) {
    __result[__index++] = '-';
    __exp = -__exp;
  } else {
    __result[__index++] = '+';
  }

  if (__exp >= 100) {
    const int32_t __c = __exp % 10;
    _CSTD memcpy(__result + __index, __DIGIT_TABLE + 2 * (__exp / 10), 2);
    __result[__index + 2] = static_cast<char>('0' + __c);
    __index += 3;
  } else {
    _CSTD memcpy(__result + __index, __DIGIT_TABLE + 2 * __exp, 2);
    __index += 2;
  }

  return __index;
}
