# 5. Standard Library Essentials

The compiler automatically makes the base `k` module available. Its types and
namespaces cover strings, console I/O, collections, optional values, and more.

## Strings and output

`String` is the standard owned string type. Construct it from a string literal
when an API requires a `String`; output accepts literals directly:

```k
module greetings;

main() : int {
    name : String("K developer");
    k::io::stdout.print("Hello, ").println(name);
    return 0;
}
```

`print` does not add a newline; `println` does. Both return the printer, so
they can be chained.

## Dynamic collections

`Vector<T>` is the general-purpose contiguous collection. It owns values
inserted into it:

```k
module collections;

main() : int {
    numbers : Vector<int>;
    first : int = 10;
    second : int = 20;

    numbers.pushBack(first);
    numbers.pushBack(second);
    numbers.get(1) = 25;

    k::io::stdout.println(numbers.getSize());
    k::io::stdout.println(numbers.get(0) + numbers.get(1));
    return 0;
}
```

This prints `2`, then `35`. `LinkedList<T>` and `DoubleLinkedList<T>` provide
alternatives when front operations or stable element addresses are more
important than indexed access.

Collections store values by value. Passing a value to `pushBack` copies it;
the collection destroys its stored elements when they are removed or when the
collection leaves scope.

## Optional values

Use `Optional<T>` when a value may be absent without using a sentinel:

```k
findAnswer(found : bool) : Optional<int> {
    if (found) {
        value : int = 42;
        return Optional<int>(value);
    }
    return Optional<int>::empty();
}

main() : int {
    answer : Optional<int> = findAnswer(true);
    if (answer.hasValue()) {
        k::io::stdout.println(answer.get());
    }
    return 0;
}
```

Call `get()` only after testing `hasValue()`. When a fallback is appropriate,
use `getOr(defaultValue)` instead.

## Where to look next

The standard library also supplies maps, sets, mathematical utilities,
threading, futures, I/O streams, and runtime type information. The
[standard library reference](../spec/stdlib/index.md) lists the available
modules and their APIs.

**Next:** [Resources and errors](06-resources-and-errors.md)
