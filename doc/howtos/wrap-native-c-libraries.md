# How-To: Wrap a Native C Library

This walkthrough builds a small library written entirely in C, wraps it in a
K module, and calls it from a separate K application. It covers an opaque
handle, a visible struct passed by pointer, scalar and out parameters, a C byte
string, ownership, native errors, and shared/object linkage.

The commands target Linux with the Klang compiler, K runtime, and KDI metadata
already available. For toolchain setup and build-tree paths, see
[CMake integration](cmake-integration-k-projects.md). The
[FFI guide](../guides/ffi.md) explains the ABI constraints and the difference
between direct calls and runtime symbol lookup.

## 1. Choose a small boundary

For an existing library, start with its C header and identify exact signatures,
native symbols, status codes, allocation/release pairs, pointer lifetimes, and
link dependencies. The K compiler does not parse that header for you.

This example uses the following files in an independent project directory:

```text
native/toy.h
native/toy.c
wrapper.k
main.k
cmake-build-debug/
```

The native API deliberately uses fixed-width integers and pointer-based
structures. A larger library can often be wrapped directly in the same way.
Use a thin C shim for macros, inline-only functions, variadic calls,
layout-sensitive structures, or signatures that cannot be matched safely.

## 2. Define and implement the pure C library

Create `native/toy.h`:

```c
#ifndef TOY_H
#define TOY_H

#include <stdint.h>

typedef struct toy_counter ToyCounter;

typedef struct toy_point {
    double x;
    double y;
} ToyPoint;

enum {
    TOY_OK = 0,
    TOY_INVALID = 1,
    TOY_OVERFLOW = 2
};

ToyCounter *toy_counter_create(void);
void toy_counter_destroy(ToyCounter *counter);
int32_t toy_counter_add(ToyCounter *counter, int32_t delta);
int32_t toy_counter_read(const ToyCounter *counter, int32_t *out);
void toy_point_translate(ToyPoint *point, double dx, double dy);
int32_t toy_label_length(const char *label);

#endif
```

Create `native/toy.c`:

```c
#include "toy.h"

#include <stdlib.h>
#include <string.h>

struct toy_counter {
    int32_t value;
};

ToyCounter *toy_counter_create(void) {
    ToyCounter *counter = malloc(sizeof *counter);
    if (counter != NULL) {
        counter->value = 0;
    }
    return counter;
}

void toy_counter_destroy(ToyCounter *counter) {
    free(counter);
}

int32_t toy_counter_add(ToyCounter *counter, int32_t delta) {
    if (counter == NULL || delta < 0) {
        return TOY_INVALID;
    }
    if (delta > INT32_MAX - counter->value) {
        return TOY_OVERFLOW;
    }
    counter->value += delta;
    return TOY_OK;
}

int32_t toy_counter_read(const ToyCounter *counter, int32_t *out) {
    if (counter == NULL || out == NULL) {
        return TOY_INVALID;
    }
    *out = counter->value;
    return TOY_OK;
}

void toy_point_translate(ToyPoint *point, double dx, double dy) {
    if (point != NULL) {
        point->x += dx;
        point->y += dy;
    }
}

int32_t toy_label_length(const char *label) {
    if (label == NULL) {
        return -1;
    }
    size_t length = strlen(label);
    if (length > INT32_MAX) {
        return -1;
    }
    return (int32_t)length;
}
```

The contract is deliberately explicit:

- Creation returns null only on allocation failure; destruction accepts null.
- A counter starts at zero and accepts only nonnegative increments. Operations
  return a status, and `read` writes its result only on success.
- `ToyPoint` has two consecutive `double` fields on the intended ABI; no
  aggregate is passed or returned by value.
- Labels are borrowed, NUL-terminated byte strings. Their length is a byte
  count, not a Unicode character count. The library retains no argument pointer.
- Calls do not throw, and a counter is not safe for concurrent access without
  external synchronization.

## 3. Write the K wrapper

Create `wrapper.k`:

```k
module toy;

public struct Point {
    x : double;
    y : double;
}

private struct NativeCounter {}

@k::ffi::Extern("C", null, "toy_point_translate")
private translateNative(point : Point*, dx : double, dy : double) : void;

@k::ffi::Extern("C", null, "toy_label_length")
private labelLengthNative(
    @k::ffi::CString label : const unsigned byte*
) : int;

public translate(point : Point&, dx : double, dy : double) : void {
    translateNative(&point, dx, dy);
}

public labelLength(label : const unsigned byte*) : int
    throws(k::IllegalArgumentException) {
    if (label == null) {
        throw k::IllegalArgumentException();
    }
    length : int = labelLengthNative(label);
    if (length < 0) {
        throw k::IllegalArgumentException();
    }
    return length;
}

public struct Counter {
private:
    _handle : NativeCounter* = null;

    @k::ffi::Extern("C", null, "toy_counter_create")
    static createNative() : NativeCounter*;

    @k::ffi::Extern("C", null, "toy_counter_destroy")
    static destroyNative(handle : NativeCounter*) : void;

    @k::ffi::Extern("C", null, "toy_counter_add")
    static addNative(handle : NativeCounter*, delta : int) : int;

    @k::ffi::Extern("C", null, "toy_counter_read")
    static readNative(handle : const NativeCounter*, out : int*) : int;

    requireOpen() : void throws(k::IllegalStateException) {
        if (_handle == null) {
            throw k::IllegalStateException();
        }
    }

public:
    Counter() {
        _handle = createNative();
        if (_handle == null) {
            throw k::OutOfMemory();
        }
    }

    Counter(other : const Counter&) -> delete;
    operator =(const other : Counter&) -> delete;

    close() : void {
        if (_handle != null) {
            destroyNative(_handle);
            _handle = null;
        }
    }

    ~Counter() {
        close();
    }

    add(delta : int) : void throws(k::Exception) {
        requireOpen();
        status : int = addNative(_handle, delta);
        if (status != 0) {
            throw k::Exception(status);
        }
    }

    value() : int throws(k::Exception) {
        requireOpen();
        result : int = 0;
        status : int = readNative(_handle, &result);
        if (status != 0) {
            throw k::Exception(status);
        }
        return result;
    }
}
```

The module declaration puts this public API under `toy::` for consumers.
The native functions remain private; their annotation's third argument selects
the exact C symbol independently of the K helper's name.

`NativeCounter` is only a nominal pointer target. Its empty K declaration does not
describe the C allocation, and the wrapper never constructs or dereferences it.
`Counter` owns that native handle and uses the matching C destructor rather
than K `delete`. `close()` is idempotent, methods reject a closed handle, and
scope exit releases a still-open handle. Deleted copying and assignment prevent
two wrappers from freeing the same pointer. Pass the wrapper by reference
instead of copying it.

Current compiler limitation: copying this wrapper can produce an internal
constructor diagnostic, and imported assignment can fail at link time instead
of reporting a deleted operation. Neither operation is supported; do not remove
the ownership restrictions to work around those diagnostics.

The owning wrapper is a K struct, but it is not a mirror of a C struct and
is never passed to C. Only its `NativeCounter*` field crosses that boundary.
Keep the opaque type at module scope: a nested non-static K struct has an
enclosing-instance relationship that is inappropriate for this native handle.

This wrapper preserves native status codes in `k::Exception(code)`:
`1` means invalid input and `2` means overflow. A production binding can define
domain-specific error types or an `Expected` result instead. The checked
`throws` clauses are part of the public K contract. Native allocation failure
uses unchecked `k::OutOfMemory` because that is the only meaning of null for
this particular create function.

`Point` mirrors the simple native field layout and is passed only by pointer.
The public `translate` method takes a non-null K reference. For an unfamiliar
or platform-dependent native struct, prefer an opaque handle with C accessors
instead of guessing its layout.

`labelLength` still requires a valid terminated byte buffer: its null check
cannot establish bounds or termination. `@CString` does not convert K UTF-32
strings to C bytes, and the C code does not retain the buffer.

## 4. Write a separate K consumer

Create `main.k`:

```k
module demo;
import toy;

main() : int throws(k::Exception) {
    {
        counter : toy::Counter;
        counter.add(40);
        counter.add(2);
        if (counter.value() != 42) {
            return 1;
        }
    }

    point : toy::Point { .x = 1.5d, .y = 2.5d };
    toy::translate(point, 2.0d, -1.0d);
    if (point.x != 3.5d || point.y != 1.5d) {
        return 2;
    }

    label : const unsigned byte[] = u8"hello";
    if (toy::labelLength(&label[0]) != 5) {
        return 3;
    }

    return 0;
}
```

The counter's destructor runs when its inner block exits, including on an early
return. The point check exercises C writes into K-provided struct storage.
The `u8` literal provides the five text bytes plus a trailing zero; the C call
receives the address of the first byte, not a K array descriptor.

## 5. Build, link, and run

From the example project directory, first build the C shared library:

```sh
mkdir -p cmake-build-debug
cc -std=c11 -Wall -Wextra -fPIC -shared native/toy.c \
    -o "$PWD/cmake-build-debug/libtoy_native.so"
```

Make the example's shared libraries discoverable by the Linux loader, preserving
any runtime paths already configured for the K toolchain:

```sh
export LD_LIBRARY_PATH="$PWD/cmake-build-debug${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

Build the K wrapper, explicitly linking its native dependency:

```sh
klangc --dyn-lib --module-name toy wrapper.k \
    -L "$PWD/cmake-build-debug" -l toy_native \
    -o "$PWD/cmake-build-debug/libtoy.so"
```

This produces `libtoy.so` and `libtoy.kdi`. The C library has no KDI: the K
module is what supplies a typed public API to other K code.

Build and run the consumer:

```sh
klangc main.k \
    -i "toy=$PWD/cmake-build-debug/libtoy.kdi" \
    -L "$PWD/cmake-build-debug" \
    -o "$PWD/cmake-build-debug/toy-demo"
"$PWD/cmake-build-debug/toy-demo"
echo "$?"
```

The expected exit status is `0`. The import supplies the K declarations and
causes the wrapper library to be linked; its native shared dependency must also
be discoverable. No `@Extern` declaration in the consumer is necessary.

The explicit `-i` mapping is intentional: the output is named `libtoy.kdi`,
whereas ordinary `import toy` directory lookup searches for `toy.kdi`.
Alternatively, supply a `toy.kdi` alias in a directory searched with `-I`.
`-I` is for K metadata, `-L` is for binary library search, and `-l` adds a native
library by name or path. Neither a KDI nor the `library` annotation argument
replaces native linkage. Do not rely on `-L` alone to configure runtime loading.

### Alternative: embed the C object in the K wrapper

For a small bridge, avoid a separate native shared library by compiling a PIC
object and including it in the wrapper:

```sh
cc -std=c11 -Wall -Wextra -fPIC -c native/toy.c \
    -o "$PWD/cmake-build-debug/toy-native.o"
klangc --dyn-lib --module-name toy wrapper.k \
    "$PWD/cmake-build-debug/toy-native.o" \
    -o "$PWD/cmake-build-debug/libtoy.so"
```

Rebuild and run the consumer with the same commands as above. This variant
does not depend on `libtoy_native.so`; its C implementation is part of
`libtoy.so`. It still depends on the K runtime and any other native libraries
used by the bridge. `klangc` takes `.o` inputs, not C source compilation duties.

For a prebuilt native archive, pass its full path through `-l` to select it
unambiguously when a shared library with the same name is present:

```sh
klangc --dyn-lib --module-name toy wrapper.k \
    -l /path/to/libtoy_native.a \
    -o "$PWD/cmake-build-debug/libtoy.so"
```

Objects extracted from an archive into a shared wrapper must be PIC. If the K
wrapper itself is produced with `--static-lib`, the final consumer must also
link the native dependency closure; a static archive does not record shared
dependencies the way a shared library does. See
[CMake integration](cmake-integration-k-projects.md) for K runtime static-link
requirements.

## 6. Apply this pattern to a real library

Replace the toy C declarations with the library's exported API, retaining the
same separation: exact private externs at the ABI boundary, public K operations
for ownership and errors, and explicit build/link dependencies. A pure C API
does not require a C++ wrapper; add C bridge functions only where adaptation is
necessary.

For CMake, compile native sources with normal C targets or custom object rules,
then pass their objects or libraries to the K compilation step. The
[CMake integration how-to](cmake-integration-k-projects.md) provides
`add_k_library`/`add_k_executable` helper definitions with `OBJECTS` and
`LINK_LIBS`, plus a C bridge example. Those helpers must be defined by the
project; they are not built-in CMake commands. Declare dependencies on the
native build products so changing a C source or header rebuilds the wrapper.

Before distributing a binding, exercise successful calls, each native failure
convention, null/empty inputs allowed by the contract, repeated close, use after
close, and cleanup on early return or exception. Check visible struct fields in
both directions across the C boundary and on every supported target. Never
assume a wrapper for one ABI, library version, or ownership convention is safe
for another.

For established repository patterns, see the native handle ownership in
[`Mutex`](../../libk/libk/src/sync/mutex.k) and
[`FileInputStream`](../../libk/libk/src/io/file/file.k), and the C bridge
compilation in [`libk/libk/CMakeLists.txt`](../../libk/libk/CMakeLists.txt).
