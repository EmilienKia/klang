/*
 * K standard library — exact arbitrary-precision integers.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module k;

struct NativeBigInteger {}

/**
 * An independently owned arbitrary-precision signed integer.
 *
 * Copies are deep; draining leaves the source as a valid zero. Division and
 * remainder truncate towards zero. Bounded integer casts are checked.
 *
 * ## Usage
 *
 * Parsing and arithmetic can exceed the primitive integer range. Inside a
 * function, using only the implicit base library:
 *
 * ```k
 * huge : BigInteger = BigInteger::fromString("0x10000000000000000");
 * saved : BigInteger(huge);
 * huge += 1;
 * difference : int = (int) (huge - saved); // 1; saved is unchanged.
 * text : String = saved.toString(); // "18446744073709551616"
 * ```
 *
 * ## Pitfalls
 *
 * Arbitrary precision does not make a bounded cast safe for every value.
 * Casts to `byte`, `short`, `int`, `long` and `unsigned byte` run checked
 * conversions: out-of-range values throw NumericOverflowError, and negative
 * values cast to `unsigned byte` throw InvalidSignedToUnsignedError.
 *
 * The declared `unsigned short`, `unsigned int` and `unsigned long`
 * conversions currently fail compiler lookup (diagnostic `00945`), before
 * their checked conversion helpers can run.
 *
 * Division or remainder by zero throws DivisionByZeroError. These arithmetic
 * errors are exceptions, not sentinel values returned by the operators.
 *
 * This function-body example makes truncating signed division explicit:
 *
 * ```k
 * number : BigInteger(-17);
 * quotient : BigInteger = number / 5; // -3
 * remainder : BigInteger = number % 5; // -2
 * moved : BigInteger(#number);
 * number += 3; // Reuses the drained zero; moved remains -17.
 * ```
 */
public final class BigInteger {
private:
    _native : NativeBigInteger*;

    @ffi::Extern("C") private static __k_big_integer_create() : NativeBigInteger*;
    @ffi::Extern("C") private static __k_big_integer_release(value: NativeBigInteger*);
    @ffi::Extern("C") private static __k_big_integer_copy(destination: NativeBigInteger*, source: const NativeBigInteger*);
    @ffi::Extern("C") private static __k_big_integer_swap(left: NativeBigInteger*, right: NativeBigInteger*);
    @ffi::Extern("C") private static __k_big_integer_set_i64(destination: NativeBigInteger*, value: long);
    @ffi::Extern("C") private static __k_big_integer_set_u64(destination: NativeBigInteger*, value: unsigned long);
    @ffi::Extern("C") private static __k_big_integer_parse(destination: NativeBigInteger*, text: const char*,
        length: unsigned int, base: int) : int;
    @ffi::Extern("C") private static __k_big_integer_binary(destination: NativeBigInteger*, left: const NativeBigInteger*,
        right: const NativeBigInteger*, operation: int) : int;
    @ffi::Extern("C") private static __k_big_integer_negate(destination: NativeBigInteger*, source: const NativeBigInteger*);
    @ffi::Extern("C") private static __k_big_integer_sign(value: const NativeBigInteger*) : int;
    @ffi::Extern("C") private static __k_big_integer_compare(left: const NativeBigInteger*, right: const NativeBigInteger*) : int;
    @ffi::Extern("C") private static __k_big_integer_get_i64(value: const NativeBigInteger*, bits: int, result: long*) : int;
    @ffi::Extern("C") private static __k_big_integer_get_u64(value: const NativeBigInteger*, bits: int, result: unsigned long*) : int;
    @ffi::Extern("C") private static __k_big_integer_decimal_capacity(value: const NativeBigInteger*, capacity: int*) : int;
    @ffi::Extern("C") private static __k_big_integer_decimal_write(value: const NativeBigInteger*, buffer: unsigned byte*, capacity: int) : int;

    static check(status: int) {
        if (status == 0) return;
        if (status == 1) throw OutOfMemory();
        if (status == 2) throw DivisionByZeroError();
        if (status == 3) throw NumericOverflowError();
        if (status == 4) throw InvalidSignedToUnsignedError();
        if (status == 6) throw NumberFormatError();
        throw ArithmeticError();
    }

    static allocate() : NativeBigInteger* {
        result : NativeBigInteger* = __k_big_integer_create();
        if (result == null) throw OutOfMemory();
        return result;
    }

    static checkBase(base: int) {
        if (base < 2 || base > 62) throw NumberFormatError();
    }

    static parseCharacters(text: const char[], length: unsigned int, base: int) : BigInteger {
        if (length == 0u) throw NumberFormatError();
        result : BigInteger;
        check(__k_big_integer_parse(result._native, &text[0], length, base));
        return result;
    }

    static parseArray(text: const char[], base: int) : BigInteger {
        length : unsigned int = text.size;
        if (length > 0u && text[length - 1u] == (char) 0) --length;
        return parseCharacters(text, length, base);
    }

    static parseString(text: const String&, base: int) : BigInteger {
        length : unsigned int = text.size();
        if (length == 0u) throw NumberFormatError();
        characters : char[]! = text.toUtf32();
        return parseCharacters(characters, length, base);
    }

    const calculate(other: const BigInteger&, operation: int) : BigInteger {
        result : BigInteger;
        check(__k_big_integer_binary(result._native, _native, other._native, operation));
        return result;
    }

    update(other: const BigInteger&, operation: int) : BigInteger& {
        check(__k_big_integer_binary(_native, _native, other._native, operation));
        return this;
    }

    const signedValue(bits: int) : long {
        result : long = 0L;
        check(__k_big_integer_get_i64(_native, bits, &result));
        return result;
    }

    const unsignedValue(bits: int) : unsigned long {
        result : unsigned long = 0uL;
        check(__k_big_integer_get_u64(_native, bits, &result));
        return result;
    }

public:
    BigInteger() : _native(allocate()) {}
    BigInteger(other: const BigInteger&) : _native(allocate()) {
        __k_big_integer_copy(_native, other._native);
    }
    BigInteger(other: BigInteger#) : _native(allocate()) {
        __k_big_integer_swap(_native, other._native);
    }
    BigInteger(value: byte) : _native(allocate()) { __k_big_integer_set_i64(_native, (long) value); }
    BigInteger(value: short) : _native(allocate()) { __k_big_integer_set_i64(_native, (long) value); }
    BigInteger(value: int) : _native(allocate()) { __k_big_integer_set_i64(_native, (long) value); }
    BigInteger(value: long) : _native(allocate()) { __k_big_integer_set_i64(_native, value); }
    BigInteger(value: unsigned byte) : _native(allocate()) { __k_big_integer_set_u64(_native, (unsigned long) value); }
    BigInteger(value: unsigned short) : _native(allocate()) { __k_big_integer_set_u64(_native, (unsigned long) value); }
    BigInteger(value: unsigned int) : _native(allocate()) { __k_big_integer_set_u64(_native, (unsigned long) value); }
    BigInteger(value: unsigned long) : _native(allocate()) { __k_big_integer_set_u64(_native, value); }

    ~BigInteger() { __k_big_integer_release(_native); _native = null; }

    /**
     * Parse decimal text, with optional signed 0b, 0o, or 0x prefixes.
     *
     * Leading zeroes alone do not select octal. Whitespace, separators and
     * trailing junk are rejected. The input is borrowed only during parsing.
     *
     * @param str ASCII integer text; an array may have one trailing NUL.
     * @return An independent integer value.
     * @throws NumberFormatError If the text is empty or malformed.
     */
    static fromString(str: const char[]) : BigInteger {
        return parseArray(str, 0);
    }
    static fromString(str: const String&) : BigInteger {
        return parseString(str, 0);
    }
    /** Parse strict ASCII digits in a base from 2 through 62. */
    static fromString(str: const char[], base: int) : BigInteger {
        checkBase(base);
        return parseArray(str, base);
    }
    static fromString(str: const String&, base: int) : BigInteger {
        checkBase(base);
        return parseString(str, base);
    }

    operator =(other: const BigInteger&) : BigInteger& {
        __k_big_integer_copy(_native, other._native);
        return this;
    }
    operator =(other: BigInteger#) : BigInteger& {
        if (_native != other._native) {
            __k_big_integer_set_i64(_native, 0L);
            __k_big_integer_swap(_native, other._native);
        }
        return this;
    }
    operator =(value: byte) : BigInteger& { __k_big_integer_set_i64(_native, (long) value); return this; }
    operator =(value: short) : BigInteger& { __k_big_integer_set_i64(_native, (long) value); return this; }
    operator =(value: int) : BigInteger& { __k_big_integer_set_i64(_native, (long) value); return this; }
    operator =(value: long) : BigInteger& { __k_big_integer_set_i64(_native, value); return this; }
    operator =(value: unsigned byte) : BigInteger& { __k_big_integer_set_u64(_native, (unsigned long) value); return this; }
    operator =(value: unsigned short) : BigInteger& { __k_big_integer_set_u64(_native, (unsigned long) value); return this; }
    operator =(value: unsigned int) : BigInteger& { __k_big_integer_set_u64(_native, (unsigned long) value); return this; }
    operator =(value: unsigned long) : BigInteger& { __k_big_integer_set_u64(_native, value); return this; }

    const isZero() : bool { return __k_big_integer_sign(_native) == 0; }
    const signum() : int { return __k_big_integer_sign(_native); }
    const operator() : bool { return !isZero(); }
    const operator !() : bool { return isZero(); }
    const operator() : byte { return (byte) signedValue(8); }
    const operator() : short { return (short) signedValue(16); }
    const operator() : int { return (int) signedValue(32); }
    const operator() : long { return signedValue(64); }
    const operator() : unsigned byte { return (unsigned byte) unsignedValue(8); }
    const operator() : unsigned short { return (unsigned short) unsignedValue(16); }
    const operator() : unsigned int { return (unsigned int) unsignedValue(32); }
    const operator() : unsigned long { return unsignedValue(64); }

    /** Return canonical base-ten text, without grouping or a leading plus. */
    const toString() : String {
        capacity : int = 0;
        check(__k_big_integer_decimal_capacity(_native, &capacity));
        text : unsigned byte[]! = new unsigned byte[capacity];
        check(__k_big_integer_decimal_write(_native, &text[0], capacity));
        length : int = 0;
        while (text[length] != (unsigned byte) 0) ++length;
        characters : char[]! = new char[length + 1];
        i : int = 0;
        while (i < length) {
            characters[i] = (char) text[i];
            ++i;
        }
        characters[length] = (char) 0;
        return String(characters, (unsigned int) length);
    }

    const operator +() : BigInteger { return BigInteger(this); }
    const operator -() : BigInteger {
        result : BigInteger;
        __k_big_integer_negate(result._native, _native);
        return result;
    }
    const abs() : BigInteger {
        result : BigInteger(this);
        if (result.signum() < 0) __k_big_integer_negate(result._native, result._native);
        return result;
    }
    operator ++_() : BigInteger& { one : BigInteger(1); return update(one, 0); }
    operator --_() : BigInteger& { one : BigInteger(1); return update(one, 1); }
    operator _++() : BigInteger { previous : BigInteger(this); one : BigInteger(1); update(one, 0); return previous; }
    operator _--() : BigInteger { previous : BigInteger(this); one : BigInteger(1); update(one, 1); return previous; }

    const operator +(rhs: const BigInteger&) : BigInteger { return calculate(rhs, 0); }
    const operator -(rhs: const BigInteger&) : BigInteger { return calculate(rhs, 1); }
    const operator *(rhs: const BigInteger&) : BigInteger { return calculate(rhs, 2); }
    const operator /(rhs: const BigInteger&) : BigInteger { return calculate(rhs, 3); }
    const operator %(rhs: const BigInteger&) : BigInteger { return calculate(rhs, 4); }
    operator +=(rhs: const BigInteger&) : BigInteger& { return update(rhs, 0); }
    operator -=(rhs: const BigInteger&) : BigInteger& { return update(rhs, 1); }
    operator *=(rhs: const BigInteger&) : BigInteger& { return update(rhs, 2); }
    operator /=(rhs: const BigInteger&) : BigInteger& { return update(rhs, 3); }
    operator %=(rhs: const BigInteger&) : BigInteger& { return update(rhs, 4); }
    const operator <=>(rhs: const BigInteger&) : int { return __k_big_integer_compare(_native, rhs._native); }

    const operator +(rhs: byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: byte) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: byte) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: byte) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: byte) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: byte) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: byte) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: short) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: short) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: short) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: short) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: short) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: short) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: short) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: short) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: short) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: short) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: short) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: int) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: int) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: int) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: int) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: int) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: int) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: int) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: int) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: int) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: int) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: int) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: long) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: long) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: long) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: long) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: long) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: long) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: long) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: long) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: long) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: long) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: long) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: unsigned byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: unsigned byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: unsigned byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: unsigned byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: unsigned byte) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: unsigned byte) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: unsigned byte) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: unsigned byte) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: unsigned byte) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: unsigned byte) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: unsigned byte) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: unsigned short) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: unsigned short) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: unsigned short) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: unsigned short) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: unsigned short) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: unsigned short) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: unsigned short) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: unsigned short) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: unsigned short) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: unsigned short) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: unsigned short) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: unsigned int) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: unsigned int) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: unsigned int) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: unsigned int) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: unsigned int) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: unsigned int) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: unsigned int) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: unsigned int) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: unsigned int) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: unsigned int) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: unsigned int) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    const operator +(rhs: unsigned long) : BigInteger { value : BigInteger(rhs); return calculate(value, 0); }
    const operator -(rhs: unsigned long) : BigInteger { value : BigInteger(rhs); return calculate(value, 1); }
    const operator *(rhs: unsigned long) : BigInteger { value : BigInteger(rhs); return calculate(value, 2); }
    const operator /(rhs: unsigned long) : BigInteger { value : BigInteger(rhs); return calculate(value, 3); }
    const operator %(rhs: unsigned long) : BigInteger { value : BigInteger(rhs); return calculate(value, 4); }
    operator +=(rhs: unsigned long) : BigInteger& { value : BigInteger(rhs); return update(value, 0); }
    operator -=(rhs: unsigned long) : BigInteger& { value : BigInteger(rhs); return update(value, 1); }
    operator *=(rhs: unsigned long) : BigInteger& { value : BigInteger(rhs); return update(value, 2); }
    operator /=(rhs: unsigned long) : BigInteger& { value : BigInteger(rhs); return update(value, 3); }
    operator %=(rhs: unsigned long) : BigInteger& { value : BigInteger(rhs); return update(value, 4); }
    const operator <=>(rhs: unsigned long) : int { value : BigInteger(rhs); return __k_big_integer_compare(_native, value._native); }

    // Two explicit operands, with no implicit receiver. Automatic primitive-left
    // candidate discovery is a separate compiler feature.
    static operator +(lhs: byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: byte, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: short, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: int, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: long, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: unsigned byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: unsigned byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: unsigned byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: unsigned byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: unsigned byte, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: unsigned byte, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: unsigned short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: unsigned short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: unsigned short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: unsigned short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: unsigned short, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: unsigned short, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: unsigned int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: unsigned int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: unsigned int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: unsigned int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: unsigned int, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: unsigned int, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }

    static operator +(lhs: unsigned long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 0); }
    static operator -(lhs: unsigned long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 1); }
    static operator *(lhs: unsigned long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 2); }
    static operator /(lhs: unsigned long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 3); }
    static operator %(lhs: unsigned long, rhs: const BigInteger&) : BigInteger { value : BigInteger(lhs); return value.calculate(rhs, 4); }
    static operator <=>(lhs: unsigned long, rhs: const BigInteger&) : int { value : BigInteger(lhs); return __k_big_integer_compare(value._native, rhs._native); }
}
