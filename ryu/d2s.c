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
  return __ryu_shiftright128(__sum, __high1, static_cast<uint32_t>(__j - 64));
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
  _STL_INTERNAL_CHECK(__v < 100000000000000000u);
  if (__v >= 10000000000000000u) { return 17; }
  if (__v >= 1000000000000000u) { return 16; }
  if (__v >= 100000000000000u) { return 15; }
  if (__v >= 10000000000000u) { return 14; }
  if (__v >= 1000000000000u) { return 13; }
  if (__v >= 100000000000u) { return 12; }
  if (__v >= 10000000000u) { return 11; }
  if (__v >= 1000000000u) { return 10; }
  if (__v >= 100000000u) { return 9; }
  if (__v >= 10000000u) { return 8; }
  if (__v >= 1000000u) { return 7; }
  if (__v >= 100000u) { return 6; }
  if (__v >= 10000u) { return 5; }
  if (__v >= 1000u) { return 4; }
  if (__v >= 100u) { return 3; }
  if (__v >= 10u) { return 2; }
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

_NODISCARD inline to_chars_result __to_chars(char* const _First, char* const _Last, const __floating_decimal_64 __v,
  chars_format _Fmt, const double __f) {
  // Step 5: Print the decimal representation.
  uint64_t __output = __v.__mantissa;
  int32_t _Ryu_exponent = __v.__exponent;
  const uint32_t __olength = __decimalLength17(__output);
  int32_t _Scientific_exponent = _Ryu_exponent + static_cast<int32_t>(__olength) - 1;

  if (_Fmt == chars_format{}) {
    int32_t _Lower;
    int32_t _Upper;

    if (__olength == 1) {
      // Value | Fixed   | Scientific
      // 1e-3  | "0.001" | "1e-03"
      // 1e4   | "10000" | "1e+04"
      _Lower = -3;
      _Upper = 4;
    } else {
      // Value   | Fixed       | Scientific
      // 1234e-7 | "0.0001234" | "1.234e-04"
      // 1234e5  | "123400000" | "1.234e+08"
      _Lower = -static_cast<int32_t>(__olength + 3);
      _Upper = 5;
    }

    if (_Lower <= _Ryu_exponent && _Ryu_exponent <= _Upper) {
      _Fmt = chars_format::fixed;
    } else {
      _Fmt = chars_format::scientific;
    }
  } else if (_Fmt == chars_format::general) {
    // C11 7.21.6.1 "The fprintf function"/8:
    // "Let P equal [...] 6 if the precision is omitted [...].
    // Then, if a conversion with style E would have an exponent of X:
    // - if P > X >= -4, the conversion is with style f [...].
    // - otherwise, the conversion is with style e [...]."
    if (-4 <= _Scientific_exponent && _Scientific_exponent < 6) {
      _Fmt = chars_format::fixed;
    } else {
      _Fmt = chars_format::scientific;
    }
  }

  if (_Fmt == chars_format::fixed) {
    // Example: __output == 1729, __olength == 4

    // _Ryu_exponent | Printed  | _Whole_digits | _Total_fixed_length  | Notes
    // --------------|----------|---------------|----------------------|---------------------------------------
    //             2 | 172900   |  6            | _Whole_digits        | Ryu can't be used for printing
    //             1 | 17290    |  5            | (sometimes adjusted) | when the trimmed digits are nonzero.
    // --------------|----------|---------------|----------------------|---------------------------------------
    //             0 | 1729     |  4            | _Whole_digits        | Unified length cases.
    // --------------|----------|---------------|----------------------|---------------------------------------
    //            -1 | 172.9    |  3            | __olength + 1        | This case can't happen for
    //            -2 | 17.29    |  2            |                      | __olength == 1, but no additional
    //            -3 | 1.729    |  1            |                      | code is needed to avoid it.
    // --------------|----------|---------------|----------------------|---------------------------------------
    //            -4 | 0.1729   |  0            | 2 - _Ryu_exponent    | C11 7.21.6.1 "The fprintf function"/8:
    //            -5 | 0.01729  | -1            |                      | "If a decimal-point character appears,
    //            -6 | 0.001729 | -2            |                      | at least one digit appears before it."

    const int32_t _Whole_digits = static_cast<int32_t>(__olength) + _Ryu_exponent;

    uint32_t _Total_fixed_length;
    if (_Ryu_exponent >= 0) { // cases "172900" and "1729"
      _Total_fixed_length = static_cast<uint32_t>(_Whole_digits);
      if (__output == 1) {
        // Rounding can affect the number of digits.
        // For example, 1e23 is exactly "99999999999999991611392" which is 23 digits instead of 24.
        // We can use a lookup table to detect this and adjust the total length.
        static constexpr uint8_t _Adjustment[309] = {
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,1,0,1,1,1,0,0,0,0,0,
          1,1,0,0,1,0,1,1,1,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,0,0,0,1,1,1,1,0,1,0,1,0,1,1,0,0,0,0,1,1,1,
          1,0,0,0,0,0,0,0,1,1,0,1,1,0,0,1,0,1,0,1,0,1,1,0,0,0,0,0,1,1,1,0,0,1,1,1,1,1,0,1,0,1,1,0,1,
          1,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,0,0,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,1,1,0,1,0,0,0,1,0,0,0,1,
          0,1,0,1,0,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,1,0,1,1,1,0,0,0,1,0,1,1,1,1,1,1,0,1,0,1,1,0,0,0,1,
          1,1,0,1,1,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,1,0,1,1,0,0,1,1,1,0,0,0,1,0,1,0,0,0,0,0,1,1,0,
          0,1,0,1,1,1,0,0,1,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,1,1,0,1,0,0,0,0,0,1,1,0,1,0 };
        _Total_fixed_length -= _Adjustment[_Ryu_exponent];
        // _Whole_digits doesn't need to be adjusted because these cases won't refer to it later.
      }
    } else if (_Whole_digits > 0) { // case "17.29"
      _Total_fixed_length = __olength + 1;
    } else { // case "0.001729"
      _Total_fixed_length = static_cast<uint32_t>(2 - _Ryu_exponent);
    }

    if (_Last - _First < static_cast<ptrdiff_t>(_Total_fixed_length)) {
      return { _Last, errc::value_too_large };
    }

    char* _Mid;
    if (_Ryu_exponent > 0) { // case "172900"
      bool _Can_use_ryu;

      if (_Ryu_exponent > 22) { // 10^22 is the largest power of 10 that's exactly representable as a double.
        _Can_use_ryu = false;
      } else {
        // Ryu generated X: __v.__mantissa * 10^_Ryu_exponent
        // __v.__mantissa == 2^_Trailing_zero_bits * (__v.__mantissa >> _Trailing_zero_bits)
        // 10^_Ryu_exponent == 2^_Ryu_exponent * 5^_Ryu_exponent

        // _Trailing_zero_bits is [0, 56] (aside: because 2^56 is the largest power of 2
        // with 17 decimal digits, which is double's round-trip limit.)
        // _Ryu_exponent is [1, 22].
        // Normalization adds [2, 52] (aside: at least 2 because the pre-normalized mantissa is at least 5).
        // This adds up to [3, 130], which is well below double's maximum binary exponent 1023.

        // Therefore, we just need to consider (__v.__mantissa >> _Trailing_zero_bits) * 5^_Ryu_exponent.

        // If that product would exceed 53 bits, then X can't be exactly represented as a double.
        // (That's not a problem for round-tripping, because X is close enough to the original double,
        // but X isn't mathematically equal to the original double.) This requires a high-precision fallback.

        // If the product is 53 bits or smaller, then X can be exactly represented as a double (and we don't
        // need to re-synthesize it; the original double must have been X, because Ryu wouldn't produce the
        // same output for two different doubles X and Y). This allows Ryu's output to be used (zero-filled).

        // (2^53 - 1) / 5^0 (for indexing), (2^53 - 1) / 5^1, ..., (2^53 - 1) / 5^22
        static constexpr uint64_t _Max_shifted_mantissa[23] = {
          9007199254740991u, 1801439850948198u, 360287970189639u, 72057594037927u, 14411518807585u,
          2882303761517u, 576460752303u, 115292150460u, 23058430092u, 4611686018u, 922337203u, 184467440u,
          36893488u, 7378697u, 1475739u, 295147u, 59029u, 11805u, 2361u, 472u, 94u, 18u, 3u };

        unsigned long _Trailing_zero_bits;
#ifdef _WIN64
        (void) _BitScanForward64(&_Trailing_zero_bits, __v.__mantissa); // __v.__mantissa is guaranteed nonzero
#else // ^^^ 64-bit ^^^ / vvv 32-bit vvv
        const uint32_t _Low_mantissa = static_cast<uint32_t>(__v.__mantissa);
        if (_Low_mantissa != 0) {
          (void) _BitScanForward(&_Trailing_zero_bits, _Low_mantissa);
        } else {
          const uint32_t _High_mantissa = static_cast<uint32_t>(__v.__mantissa >> 32); // nonzero here
          (void) _BitScanForward(&_Trailing_zero_bits, _High_mantissa);
          _Trailing_zero_bits += 32;
        }
#endif // ^^^ 32-bit ^^^
        const uint64_t _Shifted_mantissa = __v.__mantissa >> _Trailing_zero_bits;
        _Can_use_ryu = _Shifted_mantissa <= _Max_shifted_mantissa[_Ryu_exponent];
      }

      if (!_Can_use_ryu) {
        // Print the integer exactly.
        // Performance note: This will redundantly perform bounds checking.
        // Performance note: This will redundantly decompose the IEEE representation.
        return __d2fixed_buffered_n(_First, _Last, __f, 0);
      }

      // _Can_use_ryu
      // Print the decimal digits, left-aligned within [_First, _First + _Total_fixed_length).
      _Mid = _First + __olength;
    } else { // cases "1729", "17.29", and "0.001729"
      // Print the decimal digits, right-aligned within [_First, _First + _Total_fixed_length).
      _Mid = _First + _Total_fixed_length;
    }

    // We prefer 32-bit operations, even on 64-bit platforms.
    // We have at most 17 digits, and uint32_t can store 9 digits.
    // If __output doesn't fit into uint32_t, we cut off 8 digits,
    // so the rest will fit into uint32_t.
    if ((__output >> 32) != 0) {
      // Expensive 64-bit division.
      const uint64_t __q = __div1e8(__output);
      uint32_t __output2 = static_cast<uint32_t>(__output - 100000000 * __q);
      __output = __q;

      const uint32_t __c = __output2 % 10000;
      __output2 /= 10000;
      const uint32_t __d = __output2 % 10000;
      const uint32_t __c0 = (__c % 100) << 1;
      const uint32_t __c1 = (__c / 100) << 1;
      const uint32_t __d0 = (__d % 100) << 1;
      const uint32_t __d1 = (__d / 100) << 1;

      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __c0, 2);
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __c1, 2);
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __d0, 2);
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __d1, 2);
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
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __c0, 2);
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __c1, 2);
    }
    if (__output2 >= 100) {
      const uint32_t __c = (__output2 % 100) << 1;
      __output2 /= 100;
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __c, 2);
    }
    if (__output2 >= 10) {
      const uint32_t __c = __output2 << 1;
      _CSTD memcpy(_Mid -= 2, __DIGIT_TABLE + __c, 2);
    } else {
      *--_Mid = static_cast<char>('0' + __output2);
    }

    if (_Ryu_exponent > 0) { // case "172900" with _Can_use_ryu
      // Performance note: it might be more efficient to do this immediately after setting _Mid.
      _CSTD memset(_First + __olength, '0', static_cast<size_t>(_Ryu_exponent));
    } else if (_Ryu_exponent == 0) { // case "1729"
      // Done!
    } else if (_Whole_digits > 0) { // case "17.29"
      // Performance note: moving digits might not be optimal.
      _CSTD memmove(_First, _First + 1, static_cast<size_t>(_Whole_digits));
      _First[_Whole_digits] = '.';
    } else { // case "0.001729"
      // Performance note: a larger memset() followed by overwriting '.' might be more efficient.
      _First[0] = '0';
      _First[1] = '.';
      _CSTD memset(_First + 2, '0', static_cast<size_t>(-_Whole_digits));
    }

    return { _First + _Total_fixed_length, errc{} };
  }

  const uint32_t _Total_scientific_length = __olength + (__olength > 1) // digits + possible decimal point
    + (-100 < _Scientific_exponent && _Scientific_exponent < 100 ? 4 : 5); // + scientific exponent
  if (_Last - _First < static_cast<ptrdiff_t>(_Total_scientific_length)) {
    return { _Last, errc::value_too_large };
  }
  char* const __result = _First;

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
  __result[__index++] = 'e';
  if (_Scientific_exponent < 0) {
    __result[__index++] = '-';
    _Scientific_exponent = -_Scientific_exponent;
  } else {
    __result[__index++] = '+';
  }

  if (_Scientific_exponent >= 100) {
    const int32_t __c = _Scientific_exponent % 10;
    _CSTD memcpy(__result + __index, __DIGIT_TABLE + 2 * (_Scientific_exponent / 10), 2);
    __result[__index + 2] = static_cast<char>('0' + __c);
    __index += 3;
  } else {
    _CSTD memcpy(__result + __index, __DIGIT_TABLE + 2 * _Scientific_exponent, 2);
    __index += 2;
  }

  return { _First + _Total_scientific_length, errc{} };
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

_NODISCARD inline to_chars_result __d2s_buffered_n(char* const _First, char* const _Last, const double __f,
  const chars_format _Fmt) {

  // Step 1: Decode the floating-point number, and unify normalized and subnormal cases.
  const uint64_t __bits = __double_to_bits(__f);

  // Case distinction; exit early for the easy cases.
  if (__bits == 0) {
    if (_Fmt == chars_format::scientific) {
      if (_Last - _First < 5) {
        return { _Last, errc::value_too_large };
      }

      _CSTD memcpy(_First, "0e+00", 5);

      return { _First + 5, errc{} };
    }

    // Print "0" for chars_format::fixed, chars_format::general, and chars_format{}.
    if (_First == _Last) {
      return { _Last, errc::value_too_large };
    }

    *_First = '0';

    return { _First + 1, errc{} };
  }

  // Decode __bits into mantissa and exponent.
  const uint64_t __ieeeMantissa = __bits & ((1ull << __DOUBLE_MANTISSA_BITS) - 1);
  const uint32_t __ieeeExponent = static_cast<uint32_t>(__bits >> __DOUBLE_MANTISSA_BITS);

  if (_Fmt == chars_format::fixed) {
    // const uint64_t _Mantissa2 = __ieeeMantissa | (1ull << __DOUBLE_MANTISSA_BITS); // restore implicit bit
    const int32_t _Exponent2 = static_cast<int32_t>(__ieeeExponent)
      - __DOUBLE_BIAS - __DOUBLE_MANTISSA_BITS; // bias and normalization

    // Normal values are equal to _Mantissa2 * 2^_Exponent2.
    // (Subnormals are different, but they'll be rejected by the _Exponent2 test here, so they can be ignored.)

    // For nonzero integers, _Exponent2 >= -52. (The minimum value occurs when _Mantissa2 * 2^_Exponent2 is 1.
    // In that case, _Mantissa2 is the implicit 1 bit followed by 52 zeros, so _Exponent2 is -52 to shift away
    // the zeros.) The dense range of exactly representable integers has negative or zero exponents
    // (as positive exponents make the range non-dense). For that dense range, Ryu will always be used:
    // every digit is necessary to uniquely identify the value, so Ryu must print them all.

    // Positive exponents are the non-dense range of exactly representable integers. This contains all of the values
    // for which Ryu can't be used (and a few Ryu-friendly values). We can save time by detecting positive
    // exponents here and skipping Ryu. Calling __d2fixed_buffered_n() with precision 0 is valid for all integers
    // (so it's okay if we call it with a Ryu-friendly value).
    if (_Exponent2 > 0) {
      return __d2fixed_buffered_n(_First, _Last, __f, 0);
    }
  }

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

  return __to_chars(_First, _Last, __v, _Fmt, __f);
}
