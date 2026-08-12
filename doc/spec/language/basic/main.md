# Main Function

[← Index](../index.md)

A K program may declare a special function named `main` that serves as the program entry point.
When a K compilation unit defines `main` (or an executable-mode `class Application`), the compiler
generates a platform-standard C `main(int, char**)` wrapper that constructs the application object
and invokes its entry-point `main` method.

---

## Contents

1. [Entry point requirements](#1-entry-point-requirements)
2. [Supported signatures](#2-supported-signatures)
3. [Return value](#3-return-value)
4. [`::k::Application`](#4-kapplication)
5. [User-declared `class Application`](#5-user-declared-class-application)
6. [Application entry-point chain](#6-application-entry-point-chain)
7. [Compiler-generated wrapper](#7-compiler-generated-wrapper)

---

## 1. Entry point requirements

- The `main` function must be defined at the module (top-level namespace) level, or as a
  non-static member of the module's `Application` class (see §4–§6). A `main` inside any
  other struct, or in a nested namespace, is not recognised as the program entry point.
- At most one *usable* (non-deleted) `main` overload may be selected as the entry point per
  compilation unit — see §6 for the rules governing chains of several `main` overloads.
- `main` is a regular K function/method with one of the four accepted standard signatures
  (§2), or a custom signature reachable through an application entry-point chain (§6).

---

## 2. Supported signatures

K supports four standard `main` signatures:

```k
// No parameters
main() { ... }
main() : int { ... }

// Command-line arguments, as an array of constant Strings
main(args : const String[]) { ... }
main(args : const String[]) : int { ... }
```

- The parameter name (`args` above) is not mandated — any identifier may be used.
- The parameter type must be exactly `const String[]` (an array of `String`, passed
  by constant reference, per the language's array-parameter conventions).
- When `main` declares this parameter, the compiler-generated wrapper builds the array
  from the process's `argc`/`argv` before invoking `main` (see §7). `argv[0]` (the
  program name) is included as `args[0]`.

---

## 3. Return value

- If the selected `main` returns `int`, the value is used as the process exit code.
- If it returns nothing (void), the generated wrapper returns `0` to the OS.

---

## 4. `::k::Application`

The base standard library module `k` declares an abstract base class,
`::k::Application`, providing common entry-point infrastructure:

```k
public abstract class Application : public Object {
    public const env() : const EnvironmentMap&;
    // (main is provided by subclasses — see §5, §6)
}
```

- `env()` returns a read-only view of the process environment variables, populated once
  at start-up (before any user code runs) from the OS environment.
- `Application` cannot be instantiated directly; it exists to be extended.

### Compiler-synthesised `Application` (implicit mode)

When a module defines one or more top-level `main` overloads but does **not** declare its
own `class Application`, the compiler synthesises a private class named `Application` that:

- extends `::k::Application`,
- adopts the module's `main` overload(s) as its own (non-static) methods,
- is instantiated once, and its `main` is invoked, by the generated C wrapper (§7).

This is fully transparent — user code simply defines a top-level `main()` exactly as in
earlier language versions.

---

## 5. User-declared `class Application`

A module may instead declare its own, explicit `class Application` at the top-level
namespace to add fields and methods (of any visibility) alongside the entry point:

```k
class Application : public k::Application {
    private _counter : int = 0;

    public main() : int {
        _counter = _counter + 1;
        return _counter;
    }
}
```

Rules:

- `class Application` must (directly or transitively) extend `::k::Application`.
- `class Application` must not be `abstract` — it is instantiated directly.
- It must declare, or inherit through an application entry-point chain (§6), exactly one
  usable `main` method. That `main` is treated as a regular non-static member: it may
  call other members, use `this` implicitly, etc.
- The user-declared class's own visibility (`public`/`protected`/`private`) is preserved
  as written; a compiler-synthesised `Application` is always `private`.
- The compiler constructs a single global instance of the class (default constructor),
  then invokes its resolved `main` method, exactly as in the synthesised case.

---

## 6. Application entry-point chain

A library or executable module may declare one or more **abstract** classes that derive
(directly or transitively) from `::k::Application`, sitting between it and the final
concrete `class Application`. Such classes may be exported (public) so a library can
provide a reusable application "layer" — e.g. one that pre-parses command-line arguments
into a structured object before calling a differently-shaped `main`.

Each such abstract class must resolve exactly one of the four standard `main`
signatures (§2) into either:

- **(a) an abstract standard `main`** — left without a body for a subclass to implement, or
- **(b) an implemented standard `main` that delegates** to exactly one other, custom-shaped,
  abstract `main` overload declared in the *same* class.

The other 0–3 standard signatures not used at that level may be explicitly marked
`-> delete` (see §6.2) to make clear they must not be used further down the chain.

### 6.1 Chain resolution algorithm

Starting from `::k::Application` and walking down to the final concrete `class Application`:

1. At the first class in the chain that declares any `main` overload (the "deciding"
   class), exactly one of the four standard signatures must remain non-deleted.
   - If that signature is left `abstract`, it becomes the signature the **next** class in
     the chain must override.
   - If that signature is **implemented**, the implementation must call exactly one other,
     custom-shaped, `abstract main` overload also declared in that class; that custom
     signature becomes the signature the next class must override.
2. Each subsequent class either:
   - declares nothing matching the required signature (pure pass-through — allowed for any
     abstract class in the chain), or
   - re-declares the required signature `abstract` (still deferring implementation), or
   - **implements** the required signature. If this class is the final, concrete
     `class Application`, the chain is resolved. Otherwise (an intermediate abstract
     class), it must, like step 1(b), also introduce exactly one new custom `abstract main`
     to keep delegating further down the chain.
3. The final, concrete `class Application` must implement whichever signature the chain
   currently requires when reached.

Method virtuality applies throughout the chain using ordinary K override rules — no special
syntax is required for overriding `main` beyond a normal method declaration with a matching
signature. The compiler-generated wrapper always invokes the topmost declared standard
`main` in the chain (via virtual dispatch when that method is itself abstract), so the call
correctly cascades through however many delegating implementations exist down to the final
override.

### 6.2 Deleting standard `main` signatures

`-> delete` (normally reserved for non-static constructors and assignment operators) is
additionally accepted on a non-static member function literally named `main`, to explicitly
discard one of the four standard signatures at a given level of the chain:

```k
public main() -> delete;
public main() : int -> delete;
```

A deleted `main` signature cannot be declared, implemented, or un-deleted by any class
further down the chain.

### 6.3 Example — single delegating layer

```k
abstract class ConsoleApp : public k::Application {
    public main() -> delete;
    public main() : int -> delete;
    public main(args : const String[]) : int -> delete;

    public main(args : const String[]) : int {
        return main(args.size);
    }

    protected abstract main(argCount : int) : int;
}

class Application : public ConsoleApp {
    protected main(argCount : int) : int {
        return argCount;
    }
}
```

### 6.4 Example — a fully abstract standard signature

```k
abstract class Layer1 : public k::Application {
    public main() : int -> delete;
    public main(args : const String[]) -> delete;
    public main(args : const String[]) : int -> delete;
    public abstract main() : int;
}

class Application : public Layer1 {
    public main() : int {
        return 0;
    }
}
```

---

## 7. Compiler-generated wrapper

The K compiler synthesises a C-compatible `main(int argc, char** argv)` entry point that:

1. Calls the global constructor function (runs global variable initialisers and struct
   static constructors in dependency order).
2. Allocates the (synthesised or user-declared) `Application` instance and calls its
   default constructor — which, via `::k::Application`'s own constructor, populates the
   environment map returned by `env()`.
3. If the resolved entry-point `main` declares a `const String[]` parameter, builds the
   array from `argc`/`argv`.
4. Invokes the resolved entry-point `main` (§5, §6) — directly, or through virtual
   dispatch when required by an application entry-point chain.
5. Calls the `Application` instance's destructor.
6. Calls the global destructor function (runs destructors in reverse initialisation order).
7. Returns the exit code.

The wrapper is transparent to the programmer; user code simply defines `main()` (optionally
with a `const String[]` parameter), or a full `class Application` when more structure is
needed.

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

**Example — command-line arguments:**

```k
module echo_args;
import k;

main(args : const String[]) : int {
    return args.size;
}
```

---

*See also:* [Functions](../functions/functions.md) · [Module System](modules.md) · [Structures — Static constructors](../structs/constructors.md) · [Classes](../structs/classes.md)
