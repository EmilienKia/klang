# k::functional — Functional Programming Utilities

[← Index](../../index.md) · [Standard Library](index.md)

The `k::functional` namespace provides **template callable aliases** for common
functional programming patterns, modelled after the Java `java.util.function` package
but adapted for the K type system.

All aliases are defined in module `k`, namespace `k::functional`.
They are available without an explicit `import` statement (the base library is
auto-imported). A `using k::functional;` directive brings them into scope without
full qualification.

---

## Contents

1. [Overview](#1-overview)
2. [Aliases reference](#2-aliases-reference)
3. [Usage examples](#3-usage-examples)
4. [Not included](#4-not-included)

---

## 1. Overview

Each alias in `k::functional` is a template that expands to a **callable prototype**
(see [Callables](../../language/functions/callables.md)).

The aliases define *prototypes* — you instantiate them with an addresser to get a
concrete callable type:

```k
using k::functional;

f : Function<int,int>& = (x:int):int { return x * 2; };
p : Predicate<int>* = null;
```

---

## 2. Aliases reference

| Alias | Equivalent | Java analog |
|---|---|---|
| `Function<T,R>` | `(T):R` | `Function<T,R>` |
| `BiFunction<T,U,R>` | `(T,U):R` | `BiFunction<T,U,R>` |
| `Supplier<T>` | `():T` | `Supplier<T>` |
| `Consumer<T>` | `(T)` | `Consumer<T>` (void return) |
| `BiConsumer<T,U>` | `(T,U)` | `BiConsumer<T,U>` |
| `Predicate<T>` | `Function<T,bool>` → `(T):bool` | `Predicate<T>` |
| `BiPredicate<T,U>` | `(T,U):bool` | `BiPredicate<T,U>` |
| `UnaryOperator<T>` | `Function<T,T>` → `(T):T` | `UnaryOperator<T>` |
| `BinaryOperator<T>` | `BiFunction<T,T,T>` → `(T,T):T` | `BinaryOperator<T>` |
| `Comparator<T>` | `(T,T):int` | `Comparator<T>` |

**Definitions:**

```k
namespace k::functional {
    template<typename T, typename R>             alias Function       : (T):R;
    template<typename T, typename U, typename R> alias BiFunction     : (T,U):R;
    template<typename T>                         alias Supplier       : ():T;
    template<typename T>                         alias Consumer       : (T);
    template<typename T, typename U>             alias BiConsumer     : (T,U);
    template<typename T>                         alias Predicate      : Function<T, bool>;
    template<typename T, typename U>             alias BiPredicate    : (T,U):bool;
    template<typename T>                         alias UnaryOperator  : Function<T,T>;
    template<typename T>                         alias BinaryOperator : BiFunction<T,T,T>;
    template<typename T>                         alias Comparator     : (T,T):int;
}
```

---

## 3. Usage examples

### Basic function alias

```k
using k::functional;

double_it : Function<int,int>& = (x:int):int { return x * 2; };
result : int = double_it(21); // → 42
```

### Predicate

```k
using k::functional;

is_positive : Predicate<int>& = (x:int):bool { return x > 0; };
assert(is_positive(42));
assert(!is_positive(-1));
```

### Supplier

```k
using k::functional;

counter : int = 0;
next_id : Supplier<int>& = [&counter]():int {
    counter = counter + 1;
    return counter;
};
id1 : int = next_id(); // → 1
id2 : int = next_id(); // → 2
```

### Consumer

```k
using k::functional;

print_int : Consumer<int>& = (x:int) { /* write x to stdout */ };
print_int(42);
```

### BiFunction / BinaryOperator

```k
using k::functional;

add : BinaryOperator<int>& = (a:int, b:int):int { return a + b; };
result : int = add(20, 22); // → 42
```

### Comparator

```k
using k::functional;

cmp_int : Comparator<int>& = (a:int, b:int):int {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
};
```

### Chaining via a higher-order function

```k
using k::functional;

map_value(f : Function<int,int>&, v : int) : int { return f(v); }

result : int = map_value((x:int):int { return x * 2; }, 21); // → 42
```

---

## 4. Not included

The following are intentionally **excluded** from `k::functional`:

* **Primitive-specialised variants** (`IntPredicate`, `DoubleSupplier`,
  `ToLongFunction`, …) — K does not have type erasure based on primitives;
  `Predicate<int>` works directly.
* **`Runnable`** — already defined as `k::Runnable` (interface in `thread.k`).
* **Utility methods** (`compose`, `andThen`, `negate`) — out of scope for v1;
  will be added in a future iteration.

---

*See also:* [Callables](../../language/functions/callables.md) · [Lambdas](../../language/functions/lambdas.md) · [Threading](threading.md)
