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

// We need a 64x128-bit multiplication and a subsequent 128-bit shift.
// Multiplication:
//   The 64-bit factor is variable and passed in, the 128-bit factor comes
//   from a lookup table. We know that the 64-bit factor only has 55
//   significant bits (i.e., the 9 topmost bits are zeros). The 128-bit
//   factor only has 124 significant bits (i.e., the 4 topmost bits are
//   zeros).
// Shift:
//   In principle, the multiplication result requires 55 + 124 = 179 bits to
//   represent. However, we then shift this value to the right by __j, which is
//   at least __j >= 115, so the result is guaranteed to fit into 179 - 115 = 64
//   bits. This means that we only need the topmost 64 significant bits of
//   the 64x128-bit multiplication.
//
// There are several ways to do this:
// 1. Best case: the compiler exposes a 128-bit type.
//    We perform two 64x64-bit multiplications, add the higher 64 bits of the
//    lower result to the higher result, and shift by __j - 64 bits.
//
//    We explicitly cast from 64-bit to 128-bit, so the compiler can tell
//    that these are only 64-bit inputs, and can map these to the best
//    possible sequence of assembly instructions.
//    x64 machines happen to have matching assembly instructions for
//    64x64-bit multiplications and 128-bit shifts.
//
// 2. Second best case: the compiler exposes intrinsics for the x64 assembly
//    instructions mentioned in 1.
//
// 3. We only have 64x64 bit instructions that return the lower 64 bits of
//    the result, i.e., we have to use plain C.
//    Our inputs are less than the full width, so we have three options:
//    a. Ignore this fact and just implement the intrinsics manually.
//    b. Split both into 31-bit pieces, which guarantees no internal overflow,
//       but requires extra work upfront (unless we change the lookup table).
//    c. Split only the first factor into 31-bit pieces, which also guarantees
//       no internal overflow, but requires extra work since the intermediate
//       results are not perfectly aligned.
#ifdef _M_X64

_NODISCARD inline uint64_t __mulShift(const uint64_t __m, const uint64_t* const __mul, const int32_t __j) {
  // __m is maximum 55 bits
  uint64_t __high1;                                               // 128
  const uint64_t __low1 = __ryu_umul128(__m, __mul[1], &__high1); // 64
  uint64_t __high0;                                               // 64
  (void) __ryu_umul128(__m, __mul[0], &__high0);                  // 0
  const uint64_t __sum = __high0 + __low1;
  if (__sum < __high0) {
    ++__high1; // overflow into __high1
  }
  return __ryu_shiftright128(__sum, __high1, __j - 64);
}

_NODISCARD inline uint64_t __mulShiftAll(const uint64_t __m, const uint64_t* const __mul, const int32_t __j,
  uint64_t* const __vp, uint64_t* const __vm, const uint32_t __mmShift) {
  *__vp = __mulShift(4 * __m + 2, __mul, __j);
  *__vm = __mulShift(4 * __m - 1 - __mmShift, __mul, __j);
  return __mulShift(4 * __m, __mul, __j);
}

#else // ^^^ intrinsics available ^^^ / vvv intrinsics unavailable vvv

_NODISCARD __forceinline uint64_t __mulShiftAll(uint64_t __m, const uint64_t* const __mul, const int32_t __j,
  uint64_t* const __vp, uint64_t* const __vm, const uint32_t __mmShift) { // TRANSITION, VSO#634761
  __m <<= 1;
  // __m is maximum 55 bits
  uint64_t __tmp;
  const uint64_t __lo = __ryu_umul128(__m, __mul[0], &__tmp);
  uint64_t __hi;
  const uint64_t __mid = __tmp + __ryu_umul128(__m, __mul[1], &__hi);
  __hi += __mid < __tmp; // overflow into __hi

  const uint64_t __lo2 = __lo + __mul[0];
  const uint64_t __mid2 = __mid + __mul[1] + (__lo2 < __lo);
  const uint64_t __hi2 = __hi + (__mid2 < __mid);
  *__vp = __ryu_shiftright128(__mid2, __hi2, static_cast<uint32_t>(__j - 64 - 1));

  if (__mmShift == 1) {
    const uint64_t __lo3 = __lo - __mul[0];
    const uint64_t __mid3 = __mid - __mul[1] - (__lo3 > __lo);
    const uint64_t __hi3 = __hi - (__mid3 > __mid);
    *__vm = __ryu_shiftright128(__mid3, __hi3, static_cast<uint32_t>(__j - 64 - 1));
  } else {
    const uint64_t __lo3 = __lo + __lo;
    const uint64_t __mid3 = __mid + __mid + (__lo3 < __lo);
    const uint64_t __hi3 = __hi + __hi + (__mid3 < __mid);
    const uint64_t __lo4 = __lo3 - __mul[0];
    const uint64_t __mid4 = __mid3 - __mul[1] - (__lo4 > __lo3);
    const uint64_t __hi4 = __hi3 - (__mid4 > __mid3);
    *__vm = __ryu_shiftright128(__mid4, __hi4, static_cast<uint32_t>(__j - 64));
  }

  return __ryu_shiftright128(__mid, __hi, static_cast<uint32_t>(__j - 64 - 1));
}

#endif // ^^^ intrinsics unavailable ^^^

_NODISCARD inline uint32_t __decimalLength17(const uint64_t __v) {
  // This is slightly faster than a loop.
  // The average output length is 16.38 digits, so we check high-to-low.
  // Function precondition: __v is not an 18, 19, or 20-digit number.
  // (17 digits are sufficient for round-tripping.)
  _STL_INTERNAL_CHECK(__v < 100000000000000000L);
  if (__v >= 10000000000000000L) { return 17; }
  if (__v >= 1000000000000000L) { return 16; }
  if (__v >= 100000000000000L) { return 15; }
  if (__v >= 10000000000000L) { return 14; }
  if (__v >= 1000000000000L) { return 13; }
  if (__v >= 100000000000L) { return 12; }
  if (__v >= 10000000000L) { return 11; }
  if (__v >= 1000000000L) { return 10; }
  if (__v >= 100000000L) { return 9; }
  if (__v >= 10000000L) { return 8; }
  if (__v >= 1000000L) { return 7; }
  if (__v >= 100000L) { return 6; }
  if (__v >= 10000L) { return 5; }
  if (__v >= 1000L) { return 4; }
  if (__v >= 100L) { return 3; }
  if (__v >= 10L) { return 2; }
  return 1;
}

// A floating decimal representing m * 10^e.
struct __floating_decimal_64 {
  uint64_t __mantissa;
  int32_t __exponent;
};

_NODISCARD inline __floating_decimal_64 __d2d(const uint64_t __ieeeMantissa, const uint32_t __ieeeExponent) {
  int32_t __e2;
  uint64_t __m2;
  if (__ieeeExponent == 0) {
    // We subtract 2 so that the bounds computation has 2 additional bits.
    __e2 = 1 - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS - 2;
    __m2 = __ieeeMantissa;
  } else {
    __e2 = static_cast<int32_t>(__ieeeExponent) - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS - 2;
    __m2 = (1ull << __DOUBLE_MANTISSA_BITS) | __ieeeMantissa;
  }
  const bool __even = (__m2 & 1) == 0;
  const bool __acceptBounds = __even;

  // Step 2: Determine the interval of valid decimal representations.
  const uint64_t __mv = 4 * __m2;
  // Implicit bool -> int conversion. True is 1, false is 0.
  const uint32_t __mmShift = __ieeeMantissa != 0 || __ieeeExponent <= 1;
  // We would compute __mp and __mm like this:
  // uint64_t __mp = 4 * __m2 + 2;
  // uint64_t __mm = __mv - 1 - __mmShift;

  // Step 3: Convert to a decimal power base using 128-bit arithmetic.
  uint64_t __vr, __vp, __vm;
  int32_t __e10;
  bool __vmIsTrailingZeros = false;
  bool __vrIsTrailingZeros = false;
  if (__e2 >= 0) {
    // I tried special-casing __q == 0, but there was no effect on performance.
    // This expression is slightly faster than max(0, __log10Pow2(__e2) - 1).
    const uint32_t __q = __log10Pow2(__e2) - (__e2 > 3);
    __e10 = static_cast<int32_t>(__q);
    const int32_t __k = __DOUBLE_POW5_INV_BITCOUNT + __pow5bits(static_cast<int32_t>(__q)) - 1;
    const int32_t __i = -__e2 + static_cast<int32_t>(__q) + __k;
    __vr = __mulShiftAll(__m2, __DOUBLE_POW5_INV_SPLIT[__q], __i, &__vp, &__vm, __mmShift);
    if (__q <= 21) {
      // This should use __q <= 22, but I think 21 is also safe. Smaller values
      // may still be safe, but it's more difficult to reason about them.
      // Only one of __mp, __mv, and __mm can be a multiple of 5, if any.
      const uint32_t __mvMod5 = static_cast<uint32_t>(__mv) - 5 * static_cast<uint32_t>(__div5(__mv));
      if (__mvMod5 == 0) {
        __vrIsTrailingZeros = __multipleOfPowerOf5(__mv, __q);
      } else if (__acceptBounds) {
        // Same as min(__e2 + (~__mm & 1), __pow5Factor(__mm)) >= __q
        // <=> __e2 + (~__mm & 1) >= __q && __pow5Factor(__mm) >= __q
        // <=> true && __pow5Factor(__mm) >= __q, since __e2 >= __q.
        __vmIsTrailingZeros = __multipleOfPowerOf5(__mv - 1 - __mmShift, __q);
      } else {
        // Same as min(__e2 + 1, __pow5Factor(__mp)) >= __q.
        __vp -= __multipleOfPowerOf5(__mv + 2, __q);
      }
    }
  } else {
    // This expression is slightly faster than max(0, __log10Pow5(-__e2) - 1).
    const uint32_t __q = __log10Pow5(-__e2) - (-__e2 > 1);
    __e10 = static_cast<int32_t>(__q) + __e2;
    const int32_t __i = -__e2 - static_cast<int32_t>(__q);
    const int32_t __k = __pow5bits(__i) - __DOUBLE_POW5_BITCOUNT;
    const int32_t __j = static_cast<int32_t>(__q) - __k;
    __vr = __mulShiftAll(__m2, __DOUBLE_POW5_SPLIT[__i], __j, &__vp, &__vm, __mmShift);
    if (__q <= 1) {
      // {__vr,__vp,__vm} is trailing zeros if {__mv,__mp,__mm} has at least __q trailing 0 bits.
      // __mv = 4 * __m2, so it always has at least two trailing 0 bits.
      __vrIsTrailingZeros = true;
      if (__acceptBounds) {
        // __mm = __mv - 1 - __mmShift, so it has 1 trailing 0 bit iff __mmShift == 1.
        __vmIsTrailingZeros = __mmShift == 1;
      } else {
        // __mp = __mv + 2, so it always has at least one trailing 0 bit.
        --__vp;
      }
    } else if (__q < 63) { // TODO(ulfjack): Use a tighter bound here.
      // We need to compute min(ntz(__mv), __pow5Factor(__mv) - __e2) >= __q - 1
      // <=> ntz(__mv) >= __q - 1 && __pow5Factor(__mv) - __e2 >= __q - 1
      // <=> ntz(__mv) >= __q - 1 (__e2 is negative and -__e2 >= __q)
      // <=> (__mv & ((1 << (__q - 1)) - 1)) == 0
      // We also need to make sure that the left shift does not overflow.
      __vrIsTrailingZeros = __multipleOfPowerOf2(__mv, __q - 1);
    }
  }

  // Step 4: Find the shortest decimal representation in the interval of valid representations.
  int32_t __removed = 0;
  uint8_t __lastRemovedDigit = 0;
  uint64_t __output;
  // On average, we remove ~2 digits.
  if (__vmIsTrailingZeros || __vrIsTrailingZeros) {
    // General case, which happens rarely (~0.7%).
    for (;;) {
      const uint64_t __vpDiv10 = __div10(__vp);
      const uint64_t __vmDiv10 = __div10(__vm);
      if (__vpDiv10 <= __vmDiv10) {
        break;
      }
      const uint32_t __vmMod10 = static_cast<uint32_t>(__vm) - 10 * static_cast<uint32_t>(__vmDiv10);
      const uint64_t __vrDiv10 = __div10(__vr);
      const uint32_t __vrMod10 = static_cast<uint32_t>(__vr) - 10 * static_cast<uint32_t>(__vrDiv10);
      __vmIsTrailingZeros &= __vmMod10 == 0;
      __vrIsTrailingZeros &= __lastRemovedDigit == 0;
      __lastRemovedDigit = static_cast<uint8_t>(__vrMod10);
      __vr = __vrDiv10;
      __vp = __vpDiv10;
      __vm = __vmDiv10;
      ++__removed;
    }
    if (__vmIsTrailingZeros) {
      for (;;) {
        const uint64_t __vmDiv10 = __div10(__vm);
        const uint32_t __vmMod10 = static_cast<uint32_t>(__vm) - 10 * static_cast<uint32_t>(__vmDiv10);
        if (__vmMod10 != 0) {
          break;
        }
        const uint64_t __vpDiv10 = __div10(__vp);
        const uint64_t __vrDiv10 = __div10(__vr);
        const uint32_t __vrMod10 = static_cast<uint32_t>(__vr) - 10 * static_cast<uint32_t>(__vrDiv10);
        __vrIsTrailingZeros &= __lastRemovedDigit == 0;
        __lastRemovedDigit = static_cast<uint8_t>(__vrMod10);
        __vr = __vrDiv10;
        __vp = __vpDiv10;
        __vm = __vmDiv10;
        ++__removed;
      }
    }
    if (__vrIsTrailingZeros && __lastRemovedDigit == 5 && __vr % 2 == 0) {
      // Round even if the exact number is .....50..0.
      __lastRemovedDigit = 4;
    }
    // We need to take __vr + 1 if __vr is outside bounds or we need to round up.
    __output = __vr + ((__vr == __vm && (!__acceptBounds || !__vmIsTrailingZeros)) || __lastRemovedDigit >= 5);
  } else {
    // Specialized for the common case (~99.3%). Percentages below are relative to this.
    bool __roundUp = false;
    const uint64_t __vpDiv100 = __div100(__vp);
    const uint64_t __vmDiv100 = __div100(__vm);
    if (__vpDiv100 > __vmDiv100) { // Optimization: remove two digits at a time (~86.2%).
      const uint64_t __vrDiv100 = __div100(__vr);
      const uint32_t __vrMod100 = static_cast<uint32_t>(__vr) - 100 * static_cast<uint32_t>(__vrDiv100);
      __roundUp = __vrMod100 >= 50;
      __vr = __vrDiv100;
      __vp = __vpDiv100;
      __vm = __vmDiv100;
      __removed += 2;
    }
    // Loop iterations below (approximately), without optimization above:
    // 0: 0.03%, 1: 13.8%, 2: 70.6%, 3: 14.0%, 4: 1.40%, 5: 0.14%, 6+: 0.02%
    // Loop iterations below (approximately), with optimization above:
    // 0: 70.6%, 1: 27.8%, 2: 1.40%, 3: 0.14%, 4+: 0.02%
    for (;;) {
      const uint64_t __vpDiv10 = __div10(__vp);
      const uint64_t __vmDiv10 = __div10(__vm);
      if (__vpDiv10 <= __vmDiv10) {
        break;
      }
      const uint64_t __vrDiv10 = __div10(__vr);
      const uint32_t __vrMod10 = static_cast<uint32_t>(__vr) - 10 * static_cast<uint32_t>(__vrDiv10);
      __roundUp = __vrMod10 >= 5;
      __vr = __vrDiv10;
      __vp = __vpDiv10;
      __vm = __vmDiv10;
      ++__removed;
    }
    // We need to take __vr + 1 if __vr is outside bounds or we need to round up.
    __output = __vr + (__vr == __vm || __roundUp);
  }
  const int32_t __exp = __e10 + __removed;

  __floating_decimal_64 __fd;
  __fd.__exponent = __exp;
  __fd.__mantissa = __output;
  return __fd;
}

_NODISCARD inline int __to_chars(const __floating_decimal_64 __v, char* const __result) {
  // Step 5: Print the decimal representation.
  uint64_t __output = __v.__mantissa;
  const uint32_t __olength = __decimalLength17(__output);

  // Print the decimal digits.
  uint32_t __i = 0;
  // We prefer 32-bit operations, even on 64-bit platforms.
  // We have at most 17 digits, and uint32_t can store 9 digits.
  // If __output doesn't fit into uint32_t, we cut off 8 digits,
  // so the rest will fit into uint32_t.
  if ((__output >> 32) != 0) {
    // Expensive 64-bit division.
    const uint64_t __q = __div1e8(__output);
    uint32_t __output2 = static_cast<uint32_t>(__output) - 100000000 * static_cast<uint32_t>(__q);
    __output = __q;

    const uint32_t __c = __output2 % 10000;
    __output2 /= 10000;
    const uint32_t __d = __output2 % 10000;
    const uint32_t __c0 = (__c % 100) << 1;
    const uint32_t __c1 = (__c / 100) << 1;
    const uint32_t __d0 = (__d % 100) << 1;
    const uint32_t __d1 = (__d / 100) << 1;
    _CSTD memcpy(__result + __olength - __i - 1, __DIGIT_TABLE + __c0, 2);
    _CSTD memcpy(__result + __olength - __i - 3, __DIGIT_TABLE + __c1, 2);
    _CSTD memcpy(__result + __olength - __i - 5, __DIGIT_TABLE + __d0, 2);
    _CSTD memcpy(__result + __olength - __i - 7, __DIGIT_TABLE + __d1, 2);
    __i += 8;
  }
  uint32_t __output2 = static_cast<uint32_t>(__output);
  while (__output2 >= 10000) {
#ifdef __clang__ // TRANSITION, LLVM#38217
    const uint32_t __c = __output2 - 10000 * (__output2 / 10000);
#else
    const uint32_t __c = __output2 % 10000;
#endif
    __output2 /= 10000;
    const uint32_t __c0 = (__c % 100) << 1;
    const uint32_t __c1 = (__c / 100) << 1;
    _CSTD memcpy(__result + __olength - __i - 1, __DIGIT_TABLE + __c0, 2);
    _CSTD memcpy(__result + __olength - __i - 3, __DIGIT_TABLE + __c1, 2);
    __i += 4;
  }
  if (__output2 >= 100) {
    const uint32_t __c = (__output2 % 100) << 1;
    __output2 /= 100;
    _CSTD memcpy(__result + __olength - __i - 1, __DIGIT_TABLE + __c, 2);
    __i += 2;
  }
  if (__output2 >= 10) {
    const uint32_t __c = __output2 << 1;
    // We can't use memcpy here: the decimal dot goes between these two digits.
    __result[2] = __DIGIT_TABLE[__c + 1];
    __result[0] = __DIGIT_TABLE[__c];
  } else {
    __result[0] = static_cast<char>('0' + __output2);
  }

  // Print decimal point if needed.
  uint32_t __index;
  if (__olength > 1) {
    __result[1] = '.';
    __index = __olength + 1;
  } else {
    __index = 1;
  }

  // Print the exponent.
  __result[__index++] = 'E';
  int32_t __exp = __v.__exponent + static_cast<int32_t>(__olength) - 1;
  if (__exp < 0) {
    __result[__index++] = '-';
    __exp = -__exp;
  }

  if (__exp >= 100) {
    const int32_t __c = __exp % 10;
    _CSTD memcpy(__result + __index, __DIGIT_TABLE + 2 * (__exp / 10), 2);
    __result[__index + 2] = static_cast<char>('0' + __c);
    __index += 3;
  } else if (__exp >= 10) {
    _CSTD memcpy(__result + __index, __DIGIT_TABLE + 2 * __exp, 2);
    __index += 2;
  } else {
    __result[__index++] = static_cast<char>('0' + __exp);
  }

  return __index;
}

_NODISCARD inline bool __d2d_small_int(const uint64_t __ieeeMantissa, const uint32_t __ieeeExponent,
  __floating_decimal_64* const __v) {
  const uint64_t __m2 = (1ull << __DOUBLE_MANTISSA_BITS) | __ieeeMantissa;
  const int32_t __e2 = static_cast<int32_t>(__ieeeExponent) - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS;

  if (__e2 > 0) {
    // f = __m2 * 2^__e2 >= 2^53 is an integer.
    // Ignore this case for now.
    return false;
  }

  if (__e2 < -52) {
    // f < 1.
    return false;
  }

  // Since 2^52 <= __m2 < 2^53 and 0 <= -__e2 <= 52: 1 <= f = __m2 / 2^-__e2 < 2^53.
  // Test if the lower -__e2 bits of the significand are 0, i.e. whether the fraction is 0.
  const uint64_t __mask = (1ull << -__e2) - 1;
  const uint64_t __fraction = __m2 & __mask;
  if (__fraction != 0) {
    return false;
  }

  // f is an integer in the range [1, 2^53).
  // Note: __mantissa might contain trailing (decimal) 0's.
  // Note: since 2^53 < 10^16, there is no need to adjust __decimalLength17().
  __v->__mantissa = __m2 >> -__e2;
  __v->__exponent = 0;
  return true;
}

_NODISCARD inline int __d2s_buffered_n(const double __f, char* const __result) {
  // Step 1: Decode the floating-point number, and unify normalized and subnormal cases.
  const uint64_t __bits = __double_to_bits(__f);

  // Case distinction; exit early for the easy cases.
  if (__bits == 0) {
    _CSTD memcpy(__result, "0E0", 3);
    return 3;
  }

  // Decode __bits into mantissa and exponent.
  const uint64_t __ieeeMantissa = __bits & ((1ull << __DOUBLE_MANTISSA_BITS) - 1);
  const uint32_t __ieeeExponent = static_cast<uint32_t>(__bits >> __DOUBLE_MANTISSA_BITS);

  __floating_decimal_64 __v;
  const bool __isSmallInt = __d2d_small_int(__ieeeMantissa, __ieeeExponent, &__v);
  if (__isSmallInt) {
    // For small integers in the range [1, 2^53), __v.__mantissa might contain trailing (decimal) zeros.
    // For scientific notation we need to move these zeros into the exponent.
    // (This is not needed for fixed-point notation, so it might be beneficial to trim
    // trailing zeros in __to_chars only if needed - once fixed-point notation output is implemented.)
    for (;;) {
      const uint64_t __q = __div10(__v.__mantissa);
      const uint32_t __r = static_cast<uint32_t>(__v.__mantissa) - 10 * static_cast<uint32_t>(__q);
      if (__r != 0) {
        break;
      }
      __v.__mantissa = __q;
      ++__v.__exponent;
    }
  } else {
    __v = __d2d(__ieeeMantissa, __ieeeExponent);
  }

  return __to_chars(__v, __result);
}
