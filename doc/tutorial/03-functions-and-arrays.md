# 3. Functions and Arrays

Functions make behavior reusable. K writes parameter names before their types,
and places the optional result type after the parameter list.

```k
module functions;

clamp(value : int, low : int, high : int) : int {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

main() : int {
    k::io::stdout.println(clamp(120, 0, 100));
    return 0;
}
```

The program prints `100`.

## Parameters and return values

Parameters are passed by value unless their type uses an indirection marker.
A by-value parameter receives a copy:

```k
add(left : int, right : int) : int {
    return left + right;
}
```

Use `T&` for a non-null reference binding when a function must modify the
caller’s object:

```k
increment(value : int&) {
    value += 1;
}

main() : int {
    count : int = 41;
    increment(count);
    return count;  // 42
}
```

The reference is bound to an existing object. `increment(41)` is invalid
because a literal has no addressable storage.

## Default arguments and varargs

Trailing parameters may provide default values:

```k
repeat(value : int, times : int = 2) : int {
    result : int = 0;
    for (i : int = 0; i < times; i += 1) {
        result += value;
    }
    return result;
}

// repeat(5) is 10; repeat(5, 3) is 15.
```

Use `...` after a parameter name for a final varargs parameter. Inside the
function it is an unsized array:

```k
sum(values... : int) : int {
    total : int = 0;
    for (i : int = 0; i < values.size; i += 1) {
        total += values[i];
    }
    return total;
}

// sum(1, 2, 3) is 6.
```

## Returning aggregates efficiently

Functions may return structs and other aggregate values. Give the return
variable a name to construct it directly in the caller’s destination:

```k
struct Pair {
    first : int;
    second : int;
}

makePair(value : int) result : Pair {
    result.first = value;
    result.second = value * 2;
}
```

The named `result` is returned automatically at the closing brace. This form
guarantees named return value optimization for aggregate returns.

## Function design

Prefer an explicit return type for value-returning functions. It makes public
APIs clear and avoids the compiler warning issued for an omitted, inferred
value type. Keep parameters narrow: pass an unsized array (`T[]`) for a
read-only fixed array argument, and a `T&` only when mutation is intended.

**Next:** [Structs and object-oriented programming](04-structs-and-oop.md)
