# Calling Native C Code from K

K can call a C library without translating its implementation to K. The usual
approach is to declare a small, ABI-compatible native interface and put a K API
around it to manage ownership, errors, strings, and lifetimes.

For a complete example, see
[How-To: Wrap a Native C Library](../howtos/wrap-native-c-libraries.md).
The [language FFI reference](../spec/language/functions/ffi.md) describes the
annotations; the [runtime FFI reference](../spec/stdlib/ffi.md) describes dynamic
invocation.

## 1. Choose the kind of FFI

| Approach | How functions are found | Typical use |
|----------|-------------------------|-------------|
| Direct `@ffi::Extern("C")` declarations | The native linker resolves an exact C symbol. | A wrapper for a known C library, with signatures known when compiling K. |
| Runtime `Module.lookupSymbol()` and `ffi::c` | A module supplies a symbol address; the caller constructs and prepares its C signature. | Plugins or optional functions selected at runtime. |

These are separate mechanisms. `@Extern` does not dynamically open a library,
and symbol lookup does not discover a function's signature. Neither mechanism
reads C headers or checks that a K declaration matches the C implementation.

The examples below use direct declarations. Module `k` is imported automatically
outside the standard library, so its `k::ffi` annotations are available without
an explicit `import k;`.

## 2. Declare a C function, then call it normally

Given this C declaration:

```c
#include <stdint.h>

int32_t vendor_add(int32_t a, int32_t b);
```

the corresponding K declaration and call are:

```k
@k::ffi::Extern("C")
vendor_add(a : int, b : int) : int;

sum() : int {
    return vendor_add(20, 22);
}
```

The declaration has no body. Its short name is the native symbol name, without
K name mangling. A K namespace does not add a prefix to that C symbol.
To expose a different K name, specify the symbol explicitly:

```k
@k::ffi::Extern("C", null, "vendor_add")
add(a : int, b : int) : int;
```

The second annotation argument is the optional `library` member. It is currently
accepted with a warning but **does not link or load a library**. Leave it null
and use the compiler's linking options.

An extern declaration must not have a body or be abstract. A member declaration
must be `static`: a normal K instance method has an implicit `this` argument
that a C function does not expect. A useful wrapper pattern is a public K
instance method calling a private static extern and passing its native handle
explicitly.

## 3. Match the ABI, not just the spelling

The ABI includes widths, signedness, calling convention, alignment, layout, and
how arguments and results are passed. The following are useful starting points
for the usual target with 8-bit bytes and IEEE floating-point types:

| C type | K boundary type |
|--------|-----------------|
| `int8_t` / `uint8_t` | `byte` / `unsigned byte` |
| `int16_t` / `uint16_t` | `short` / `unsigned short` |
| `int32_t` / `uint32_t` | `int` / `unsigned int` |
| `int64_t` / `uint64_t` | `long` / `unsigned long` |
| `float` / `double` | `float` / `double` |
| `void` return | `void` return |
| `T*`, with a matched pointee representation | `T*` |
| Opaque `void*` | A raw pointer such as `byte*`, or an empty-struct pointer for a distinct handle type |

C `long`, `long long`, `size_t`, enums, and plain `char` must be mapped for the
actual target, not by name alone. On Linux LP64, `long` and `size_t` are 64-bit;
that is not a portable rule for every C platform. K `long` is always 64-bit.
K `char` is a 32-bit Unicode scalar, **not C's one-byte `char`**.
For ABI-sensitive booleans, enums, or platform typedefs, a C shim using explicit
integer types can make the boundary unambiguous.

Do not expose arbitrary K objects, `String`, containers, owners, or array
descriptors as if they were C types. An accepted extern declaration is not
proof of C ABI compatibility. In particular, do not assume a K aggregate passed
or returned by value receives the target-specific C aggregate ABI lowering.
Use pointers or a C shim instead.

### Buffers and out parameters

K arrays include count metadata; a C pointer normally addresses only element
storage. Pass `&buffer[0]`, not the address of the array descriptor, and pass a
separate length in the exact type expected by C. Do not index an empty array:
use the library's documented null/empty-buffer convention instead.

For a C `int32_t*` out parameter, declare `int*` in K and pass `&result`, where
`result` is a local `int`. The C function must finish using that pointer before
the local goes out of scope. Constness, bounds, and whether a pointer is retained
are API contracts, not facts established by the extern annotation.

### Byte strings are not K character strings

For a C function expecting a NUL-terminated UTF-8 or ASCII `const char*`, use
8-bit storage:

```k
@k::ffi::Extern("C")
puts(@k::ffi::CString text : const unsigned byte*) : int;

greet() : int {
    text : const unsigned byte[] = u8"Hello from K";
    return puts(&text[0]);
}
```

The `u8` literal provides UTF-8 bytes and an implicit trailing zero byte.
An ordinary K `"..."` literal has UTF-32 `char` elements; taking its first
element's address does **not** convert it to a C byte string.

`@CString` is a parameter marker and compile-time type check. It does not
transcode UTF-32, allocate a buffer, append a terminator, check encoding, or
extend a pointer's lifetime. For non-literal text, explicitly encode and
terminate the bytes, or use a C bridge that converts the representation. The
runtime FFI API also supplies `ffi::c::Utf8CString.fromString()` for explicit
conversion; its storage must remain alive while native code uses it.

A returned `const char*` is likewise not automatically a K `String`. Establish
its encoding, length/termination, owner, and validity period before decoding or
copying it. If C retains an argument, a temporary K buffer is not sufficient.

## 4. Represent native state

### Opaque structures

For an incomplete C type:

```c
typedef struct vendor_device VendorDevice;
VendorDevice *vendor_open(void);
void vendor_close(VendorDevice *device);
```

use an empty K struct only as a pointer target:

```k
struct NativeDevice {}

@k::ffi::Extern("C")
vendor_open() : NativeDevice*;

@k::ffi::Extern("C")
vendor_close(device : NativeDevice*) : void;
```

The empty declaration does not reproduce the native object's size or allocate
one. Never instantiate it to obtain native storage, access its fields, or
release a returned handle with K `delete`. Only the native library knows how to
create and destroy that object.

`NativeDevice*` is nullable, so check a creation result before using it.
An observer pointer does not own the allocation. A K wrapper can store it
privately, release it in its destructor, and forbid copying so that exactly
one wrapper owns each native handle.

This pattern is used by the standard library's
[`CFile`](../../libk/libk/src/io/file/file.k) and
[`NativeThread`](../../libk/libk/src/thread.k) handles.

### Structures with visible fields

A simple C layout can be mirrored for pointer-based access on a verified target:

```c
typedef struct vendor_point {
    double x;
    double y;
} VendorPoint;

void vendor_translate(VendorPoint *point, double dx, double dy);
```

```k
struct Point {
    x : double;
    y : double;
}

@k::ffi::Extern("C")
vendor_translate(point : Point*, dx : double, dy : double) : void;
```

Use the same field order, widths, alignment, and target ABI, and verify native
reads and writes through the pointer. The compiler does not compare this
declaration with the C header. C-side `sizeof`/`offsetof` assertions are useful,
but alone do not prove that the K layout matches.

Do not extrapolate this to packed structs, bitfields, flexible array members,
unions, inheritance, or K classes with virtual state. If layout is uncertain,
keep the native struct opaque and expose C getters/setters or a bridge that
copies individual scalar fields. A stable accessor boundary also avoids
coupling consumers to the library's struct layout.

## 5. Give the K API an ownership and error contract

For each native pointer, document who allocates it, who frees it, which function
performs the release, whether null is valid, and which operations invalidate
borrowed views. Match native allocator/deallocator pairs; K `T!` ownership is
not a substitute for a foreign destructor.

An owning wrapper should release exactly once, prevent accidental copying,
and make explicit `close()` safe to repeat. If closing can fail, expose that
failure through an explicit operation; do not rely on a destructor throwing
during unwinding. Borrowed wrappers must not destroy the resource they observe.
If construction fails after acquiring a handle, release it before propagating
the failure.

Extern calls do not turn status codes, null pointers, or `errno` into K errors.
Translate the native convention into a documented K result or exception.
Checked `k::Exception` subclasses require appropriate `throws` declarations;
`k::OutOfMemory` is unchecked. Map null to allocation failure only when the C
API actually defines it that way. For `errno`-based APIs, a C shim can capture
the error immediately and return it alongside the result.

Do not let K or C++ exceptions cross an arbitrary C boundary. Likewise, do not
treat a K callable as a C callback pointer. K callables can carry context and
have a different representation. Callback APIs need a specifically designed
trampoline, lifetime rules, and thread/exception handling.

Direct extern declarations use fixed signatures; a K variadic parameter is
not C's `...`. Wrap variadic C entry points, macros, and header-only `static
inline` helpers in ordinary exported C functions when needed.

## 6. Link binaries and make shared libraries loadable

With the native library already built:

```sh
klangc main.k -L /path/to/native/lib -l vendor -o app
```

`-l vendor` requests the platform library, such as `libvendor.so` or
`libvendor.a`. A full library path can select a particular archive:

```sh
klangc main.k -l /path/to/native/lib/libvendor.a -o app
```

To include a small C bridge directly:

```sh
cc -std=c11 -fPIC -c bridge.c -o bridge.o
klangc main.k bridge.o -l vendor -o app
```

`klangc` accepts native object inputs; compile `.c` sources with a C compiler.
`-l` accepts library names or paths, not arbitrary linker flags. Archives do not
carry the native dependency closure for the final executable: supply their
dependencies in the final link too.

Link-time search and runtime loading are distinct. `-L` selects where to find
binaries for linking; do not assume it sets an RPATH. On Linux, a non-system
shared library may need a configured loader path, RPATH, or `LD_LIBRARY_PATH`.
The same applies to its transitive shared dependencies.

For a reusable K wrapper, distribute its binary and KDI metadata. Consumers
`import` the K module, not the C library. `-I` searches for KDI files, `-i`
maps an explicit KDI file to a module, and `-L` searches for binary libraries.
A raw native C library does not need a KDI. See the
[wrapping how-to](../howtos/wrap-native-c-libraries.md) for exact producer and
consumer commands, and the
[CMake integration guide](../howtos/cmake-integration-k-projects.md) for build
dependencies and packaging.

## 7. When runtime invocation is appropriate

The runtime API combines `Module.lookupSymbol()`, an explicit
`ffi::c::CSignature`, signature preparation, and a bound `ffi::c::CFunction`.
Its current backend supports the default Linux LP64 C ABI, scalar types, opaque
pointers, explicit buffer adapters, and UTF-8 C strings. It does not provide
aggregate-by-value calls, callbacks, variadic functions, or arbitrary calling
conventions.

A symbol address is not type information. The caller must supply the actual
signature and keep pointed-to storage alive; retaining a module does not take
ownership of arbitrary pointers returned by its functions. Use the
[runtime FFI reference](../spec/stdlib/ffi.md) and
[module lookup reference](../spec/stdlib/module.md) for its separate error and
lifetime contracts.
