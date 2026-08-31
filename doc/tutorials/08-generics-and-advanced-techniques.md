# 8. Generics and Advanced Techniques

Templates let one definition work with multiple types. K monomorphizes each
instantiation at compile time, so generated code is type-specific and does not
depend on runtime type erasure.

## Generic functions

This function selects the larger of two values of the same type:

```k
module template_demo;

template<typename T>
larger(left : T&, right : T&) : T& {
    if (left > right) return left;
    return right;
}

main() : int {
    first : int = 7;
    second : int = 11;
    k::io::stdout.println(larger(first, second));
    return 0;
}
```

The compiler deduces `T` as `int` from the arguments. You may specify template
arguments explicitly when deduction cannot determine them:

```k
result : int& = larger<int>(first, second);
```

## Generic aggregates

Templates also define reusable structs, classes, and interfaces:

```k
template<typename T>
struct Pair {
    first : T;
    second : T;

    Pair(first : T, second : T) : first(first), second(second) {
    }
}

main() : int {
    coordinates : Pair<int>(3, 4);
    return coordinates.first + coordinates.second;
}
```

K can often deduce aggregate arguments from constructor arguments:

```k
coordinates : Pair(3, 4);
```

Use the explicit form in public APIs when it improves clarity.

## Value parameters

Template parameters may also be compile-time constants:

```k
template<typename T, unsigned int count>
struct Buffer {
    values : T[count];
}

main() : int {
    buffer : Buffer<int, 4>;
    buffer.values[0] = 42;
    return buffer.values[0];
}
```

`count` is part of the type: `Buffer<int, 4>` and `Buffer<int, 8>` are
different concrete types.

## Function references and lambdas

K can store references to functions. This makes callback-style APIs possible:

```k
twice(value : int) : int {
    return value * 2;
}

apply(value : int, transform : *(int):int) : int {
    return transform(value);
}

main() : int {
    return apply(21, twice);
}
```

Lambda expressions are useful for local behavior:

```k
increment : *(int):int = [](value : int) : int {
    return value + 1;
};
```

Use templates for compile-time reuse, interfaces for runtime polymorphism, and
function references or lambdas for a small, behavior-oriented dependency.

## Continue learning

You now have the building blocks for native K programs. Use the
[language reference](../spec/language/index.md) for complete syntax,
the [standard library reference](../spec/stdlib/index.md) for APIs, and
[`klangc(1)`](../man/klangc.md) for compiler and linker options.
