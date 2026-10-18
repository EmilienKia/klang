# BigInteger

**Module:** `k` (auto-imported)\
**Type:** `::k::BigInteger`\
**Source:** `libk/libk/src/big_integer.k`

## Overview

`public final class BigInteger` is a mutable, arbitrary-precision signed
integer in the core standard library. It is **not** in `k::math`.
Arithmetic is exact, subject to available memory. Values do not wrap at a
machine-integer boundary.

GMP's C library (`libgmp`) is a required native dependency of `libk`.
`libgmpxx` is not used. K programs do not depend on GMP's representation.
Shared `libk` loads its GMP dependency normally; native static consumers
must link the dependencies exported by `pkg-config --libs --static libk`.
See [CMake integration](../../howtos/cmake-integration-k-projects.md#12-build-tree-package-metadata-and-static-consumers).

## Construction and Ownership

In the signatures below, `I` denotes each of the following eight types:
`byte`, `short`, `int`, `long`, `unsigned byte`, `unsigned short`,
`unsigned int`, and `unsigned long`. Every overload is provided separately.

| Signature | Meaning |
|-----------|---------|
| `BigInteger()` | Construct zero |
| `BigInteger(value: I)` | Construct the exact mathematical value of the primitive |
| `BigInteger(other: const BigInteger&)` | Construct an independent deep copy |
| `BigInteger(other: BigInteger#)` | Acquire the value and leave `other` as a valid zero |
| `operator =(other: const BigInteger&) : BigInteger&` | Deep-copy assignment; self-copy leaves the value unchanged |
| `operator =(other: BigInteger#) : BigInteger&` | Drain assignment; source becomes zero; self-drain leaves the value unchanged |
| `operator =(value: I) : BigInteger&` | Replace the value exactly |

Each value exclusively owns its resources. Copies do not share mutable
state, and destruction releases the owned resources. A drained value may
immediately be read, assigned, or used in arithmetic.

```k
module example;

example() : int {
    original : BigInteger(18446744073709551615uL);
    original *= original;                  // Exact result beyond 64 bits.
    copied : BigInteger(original);
    copied += 1;                          // Does not change original.
    moved : BigInteger(#copied);
    if (!copied.isZero()) return 1;
    copied = -17;                         // A drained instance remains usable.
    return (int) (copied / 5);             // -3
}
```

No text-parsing constructor, floating-point conversion, or `char` conversion
is provided. Text is parsed with the static factories below.

## Parsing

Exactly four public static factories return an independently owned
`BigInteger` **by value**:

| Signature | Interpretation |
|-----------|----------------|
| `static fromString(str: const char[]) : BigInteger` | Decimal by default, with optional binary/octal/hexadecimal prefix |
| `static fromString(str: const ::k::String&) : BigInteger` | Same, using the String's logical characters |
| `static fromString(str: const char[], base: int) : BigInteger` | Explicit base from 2 through 62 |
| `static fromString(str: const ::k::String&, base: int) : BigInteger` | Same, using the String's logical characters |

### Signs, bases, and digits

An optional single leading ASCII `+` or `-` is accepted. At least one digit
must follow the sign and any recognized prefix. Negative zero is ordinary
zero.

Without a `base` argument, text is decimal unless `0b`/`0B`, `0o`/`0O`, or
`0x`/`0X` occurs immediately after the optional sign. These prefixes select
base 2, 8, or 16 respectively. A leading zero alone does **not** select octal:
`"010"` is ten and `"09"` is nine.

With a `base` argument, the base must be **2 through 62 inclusive**. Zero,
one, negative bases, and bases greater than 62 are invalid; explicit zero
does not request automatic detection.

- For bases 2 through 36, ASCII letter digits are case-insensitive:
  `A` and `a` both mean 10, through `Z` and `z` meaning 35.
- For bases 37 through 62, ASCII letter digits are case-sensitive:
  `0`–`9` mean 0–9, `A`–`Z` mean 10–35, and `a`–`z` mean 36–61.
- Every digit's value must be less than the selected base.
- A matching binary, octal, or hexadecimal prefix is accepted for explicit
  base 2, 8, or 16. Other prefix-looking characters are ordinary digits,
  not a request to infer a different base. Thus `"0b10"` in base 16 is
  hexadecimal `B10` (2832), and `"0x"` in base 36 is 33. `"0x10"` in
  base 10 is invalid because `x` is not a decimal digit.

Parsing is arbitrary precision, without intermediate bounded-integer
accumulation. Large valid input does not raise `NumericOverflowError`
merely because its mathematical value exceeds 64 or 128 bits.

### Strict input and terminators

The entire input must match the rules above. No leading, trailing, or
internal whitespace, underscores, grouping separators, non-ASCII digits,
embedded NULs, or trailing junk is accepted. Empty text, sign-only text,
and recognized prefix-only text are invalid. Inputs are not modified.

For `const char[]`, the array may contain **one optional final NUL
terminator**, which is excluded from the parsed text. Any remaining NUL
is invalid. Both unterminated character arrays and ordinary terminated
string literals are accepted. An empty array or an array containing only
one terminator is invalid.

For `String`, parsing uses exactly `size()` logical characters, independent
of the String's storage encoding. Its storage terminator is already
excluded from that length. A NUL **inside the logical content**, including
its last character, must be rejected rather than stripped. An empty
String is invalid.

Malformed text and invalid public bases throw unchecked
[`NumberFormatError`](exceptions.md#arithmetic-errors), a final subclass
of `ArithmeticError` with default code **16**, not `NumericOverflowError`.
Empty inputs raise this format error rather than an array-bounds error.

```k
parseExample() : bool {
    decimal : BigInteger = BigInteger::fromString("010");        // 10
    octal : BigInteger = BigInteger::fromString("-0o17");         // -15
    hexadecimal : BigInteger = BigInteger::fromString("0b10", 16); // 2832
    text : String(u8"zZ");
    radix62 : BigInteger = BigInteger::fromString(text, 62);      // 3817
    large : BigInteger = BigInteger::fromString(
        "340282366920938463463374607431768211456");
    canonical : String = large.toString();
    if (canonical != String("340282366920938463463374607431768211456")) return false;
    try {
        invalid : BigInteger = BigInteger::fromString("12 ", 10);
    } catch (error: NumberFormatError&) {
        return decimal == 10 && octal == -15 && hexadecimal == 2832 && radix62 == 3817;
    }
    return false;
}
```

## Arithmetic

The const member operators `+`, `-`, `*`, `/`, and `%` each accept either
`rhs: const BigInteger&` or `rhs: I` and return an independent `BigInteger`.
The corresponding compound operators `+=`, `-=`, `*=`, `/=`, and `%=` accept
the same operands, mutate the receiver, and return `BigInteger&`.

Primitive operands retain their mathematical signed or unsigned value.
There is no primitive common-type coercion: adding a negative `long` to an
`unsigned long` does not first reinterpret the negative operand as unsigned.
In particular, `INT64_MIN` and `UINT64_MAX` are both represented exactly.

| Unary operator | Result |
|----------------|--------|
| `const operator +() : BigInteger` | Independent copy |
| `const operator -() : BigInteger` | Negated value |
| `operator ++_() : BigInteger&` | Prefix increment, returning the modified receiver |
| `operator --_() : BigInteger&` | Prefix decrement, returning the modified receiver |
| `operator _++() : BigInteger` | Postfix increment, returning the independent previous value |
| `operator _--() : BigInteger` | Postfix decrement, returning the independent previous value |

For nonzero divisor `b`, division truncates **towards zero** and remainder has
the sign of the dividend or is zero:

```text
 a     b       a / b       a % b
-17    5        -3          -2
 17   -5        -3           2
-17   -5         3          -2
```

The identity `a == (a / b) * b + a % b` holds, with `abs(a % b) < abs(b)`.
Both `/` and `%` throw `DivisionByZeroError` when the divisor is zero.
Failed `/=` and `%=` leave the receiver unchanged, including when the
divisor aliases it.

### Primitive-left static members

For every `I`, the class declares two-explicit-operand static members:

```k
static operator +(lhs: I, rhs: const BigInteger&) : BigInteger
static operator -(lhs: I, rhs: const BigInteger&) : BigInteger
static operator *(lhs: I, rhs: const BigInteger&) : BigInteger
static operator /(lhs: I, rhs: const BigInteger&) : BigInteger
static operator %(lhs: I, rhs: const BigInteger&) : BigInteger
static operator <=>(lhs: I, rhs: const BigInteger&) : int
```

`I` here is a signature abbreviation, not a K template parameter. Operand
order is preserved for subtraction, division, remainder, and comparison.
These are class members, not namespace-level free operators.

**Current compiler limitation:** automatic lookup of these static members
for primitive-left expressions such as `3 + big` or `3 < big` is deferred.
Do not rely on such expressions yet. Use `BigInteger(3) + big` or
`BigInteger(3) < big` to express the intended mathematical operation with
the currently supported aggregate-left syntax.

## Comparison and Truth

| Signature | Meaning |
|-----------|---------|
| `const operator <=>(rhs: const BigInteger&) : int` | Normalized ordering: exactly -1, 0, or 1 |
| `const operator <=>(rhs: I) : int` | Exact mathematical comparison against a primitive |
| `const isZero() : bool` | True exactly for zero |
| `const signum() : int` | -1 for negative, 0 for zero, 1 for positive |
| `const operator() : bool` | False for zero; true for every nonzero value |
| `const operator !() : bool` | True exactly for zero |
| `const abs() : BigInteger` | Independent, nonnegative absolute value |

The six comparisons `==`, `!=`, `<`, `<=`, `>`, and `>=` are synthesized
by the language from spaceship. There is no `compareTo()` API.
Negative values convert to `true`, not `false`. Bool conversion is not a
bounded integer conversion and cannot fail because of the value's size.

## Checked Integer Casts

`const operator() : I` is provided for each of the eight primitive integer
types, and is used by explicit casts such as `(int) big`.

**Current compiler limitation:** conversion-operator lookup does not recognize
the `unsigned short`, `unsigned int`, and `unsigned long` targets. The class
declares these checked conversions, but expressions such as
`(unsigned short) big` currently fail to compile. This separate compiler issue
is recorded in the repository's `TODO.md`; it is not repaired by this library
implementation.

The value must lie in the exact target range:

| Target | Range |
|--------|-------|
| `byte` | -128 through 127 |
| `short` | -32768 through 32767 |
| `int` | -2147483648 through 2147483647 |
| `long` | -9223372036854775808 through 9223372036854775807 |
| `unsigned byte` | 0 through 255 |
| `unsigned short` | 0 through 65535 |
| `unsigned int` | 0 through 4294967295 |
| `unsigned long` | 0 through 18446744073709551615 |

A negative value cast to an unsigned type throws
`InvalidSignedToUnsignedError`, a subclass of `NumericOverflowError`.
Any other out-of-range cast throws `NumericOverflowError`. Conversion is
based on the current mathematical value, not the signedness of the
primitive originally used to construct it. Positive signed-source values
are accepted by unsigned targets whenever representable. A failed cast
does not modify the source.

```k
checkedExample() : bool {
    value : BigInteger(256);
    try {
        tooSmall : unsigned byte = (unsigned byte) value;
    } catch (error: NumericOverflowError&) {
        return true;                      // 256 cannot fit unsigned byte.
    }
    return false;
}
```

## Decimal Text

`const toString() : String` returns independent canonical base-ten text.
Zero is `"0"`; negative values have one leading `-`; positive values have
no leading `+`. There are no grouping separators or leading zeroes.
If the formatted result exceeds the supported array/string capacity,
`NumericOverflowError` is raised. Formatting in alternative radices is
outside this API; parsing supports the bases described above.

## Error and Allocation Contract

The [arithmetic error family](exceptions.md#arithmetic-errors) derives from
unchecked `FatalError`; no `throws` declaration is required.
Division, conversion, and parsing errors from native operations are raised
by K only after returning from the native call. K exceptions never unwind
through GMP frames.

Failure to allocate the native wrapper or the parsing character buffer
raises `OutOfMemory`. GMP-internal allocation failure follows GMP's fatal
allocator contract; it is not translated into a recoverable K exception.
No process-global GMP allocation hooks are installed. Independent instances
can be used independently; concurrent mutation of the same instance requires
external synchronization.
