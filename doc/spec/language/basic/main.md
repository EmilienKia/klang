# Main Function

[← Index](../index.md)

A K program may declare a special function named `main` that serves as the program entry point.
When a K compilation unit defines `main`, the compiler generates a platform-standard C `main(int, char**)` wrapper that calls it.

---

## Contents

1. [Entry point requirements](#1-entry-point-requirements)
2. [Supported signatures](#2-supported-signatures)
3. [Return value](#3-return-value)
4. [Compiler-generated wrapper](#4-compiler-generated-wrapper)

---

## 1. Entry point requirements

- The `main` function must be defined at the module (top-level namespace) level. A `main` inside a struct or nested namespace is not recognised as the program entry point.
- At most one `main` function may be declared per compilation unit.
- `main` is a regular K function with one of the accepted signatures.

---

## 2. Supported signatures

K supports two `main` signatures:

```k
// Returns an integer exit code
main() : int { ... }

// Returns nothing (exit code will be 0)
main() { ... }
```

Both variants take no K-level parameters.
Command-line arguments are not directly accessible in the current language version.

---

## 3. Return value

- If `main` returns `int`, the value is used as the process exit code.
- If `main` returns nothing (void), the generated wrapper returns `0` to the OS.

---

## 4. Compiler-generated wrapper

The K compiler synthesises a C-compatible `main(int argc, char** argv)` entry point that:

1. Calls the global constructor function (runs global variable initialisers and struct static constructors in dependency order).
2. Calls the user-defined `main()`.
3. Calls the global destructor function (runs destructors in reverse initialisation order).
4. Returns the exit code.

The wrapper is transparent to the programmer; user code simply defines `main()`.

**Example:**

```k
module fibo;

fibo(i: unsigned short) : unsigned int {
    if (i == 0) return 1;
    else if (i == 1) return 1;
    return fibo(i - 1) + fibo(i - 2);
}

main() : int {
    return fibo(8);   // returns 55
}
```

**Example — void main:**

```k
module hello;

main() {
    // do something
}
// exit code is 0
```

---

*See also:* [Functions](../functions/functions.md) · [Module System](modules.md) · [Structures — Static constructors](../structs/constructors.md)
