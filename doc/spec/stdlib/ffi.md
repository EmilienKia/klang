# Runtime C FFI

**Module:** `k`  
**Source:** `libk/libk/src/ffi_runtime.k`  
**Related:** [Module and native symbol lookup](module.md)

## Scope and safety

The runtime C API invokes a dynamically looked-up `ffi::NativeSymbol` using an
explicit, fixed C signature. It is separate from the compile-time
`@ffi::Extern("C")` annotation.

This initial API supports scalar integers, C booleans, `float`, `double`,
opaque pointers and `void` returns, borrowed numeric/pointer arrays, owned
native byte buffers and explicitly converted UTF-8 C strings. Only the default Linux LP64 C ABI with
signed 8-bit plain `char`, 16-bit `short`, 32-bit `int`, 64-bit `long`,
`long long`, pointers, `size_t` and `ptrdiff_t`, and IEEE binary32/binary64
floating-point representations is supported. Signature preparation must fail
with `UNSUPPORTED_ABI` on incompatible hosts, including unsigned-char hosts.

Aggregates, unions, variadic functions, callbacks, non-default calling
conventions, C++ member calls and ownership of arbitrary foreign allocations
are not provided by this API. Managed `NativeBuffer` storage is allocated by
the K runtime; a foreign function's returned pointer is not automatically an
allocation owner.

The caller **must** provide the function's actual C signature. Symbol lookup
does not discover types or distinguish a function from a data symbol. Calling
a data symbol or a function with an incompatible signature is unsafe; scalar
argument validation cannot make a fabricated signature safe. A foreign call
must not throw across this boundary. Pointer validity, bounds, mutability and
any foreign retention beyond the call remain the caller's responsibility.

## Namespace map

| Namespace | Public declarations |
|---|---|
| `::k::ffi` | `NativePointer`, `NativeBuffer`, `FfiError`, `FfiErrorCode`, `CallResult`, `ResultKind` |
| `::k::ffi::c` | C aliases, `CType`, `CAbi`, `typeSize`, `canonicalType`, `CSignature`, `CSignatureBuilder`, `CPreparedSignature`, `CValue`, `CFunction`, `CCallBuilder`, `Utf8CString` |

`NativeSymbol`, `SymbolLookupError` and `SymbolLookupErrorCode` already belong
to `::k::ffi`; they are documented with `Module.lookupSymbol()`.

## C aliases and type IDs

Aliases are transparent, not strong typedefs. They describe the supported
host, not every possible C implementation.

| Alias | K type |
|---|---|
| `Char`, `SignedChar`, `Int8` | `byte` |
| `UnsignedChar`, `UInt8` | `unsigned byte` |
| `Short`, `Int16` | `short` |
| `UShort`, `UInt16` | `unsigned short` |
| `Int`, `Int32` | `int` |
| `UInt`, `UInt32` | `unsigned int` |
| `Long`, `LongLong`, `Ptrdiff`, `PtrdiffT`, `Int64` | `long` |
| `ULong`, `ULongLong`, `Size`, `SizeT`, `UInt64` | `unsigned long` |
| `Float`, `Double`, `Bool` | `float`, `double`, `bool`, respectively |

In particular, C `Char` is **not** K's UTF-32 `char`, and C `LongLong` is
64-bit K `long`, not a wider K integer.

```k
namespace k::ffi::c {
    public enum CType {
        VOID = 0;
        SINT8 = 1; UINT8 = 2;
        SINT16 = 3; UINT16 = 4;
        SINT32 = 5; UINT32 = 6;
        SINT64 = 7; UINT64 = 8;
        FLOAT = 9; DOUBLE = 10; POINTER = 11;
        CHAR = 12; SCHAR = 13; UCHAR = 14;
        SHORT = 15; USHORT = 16;
        INT = 17; UINT = 18;
        LONG = 19; ULONG = 20;
        LLONG = 21; ULLONG = 22;
        SIZE_T = 23; PTRDIFF_T = 24;
        BOOL = 25;
    }
    public enum CAbi { DEFAULT = 0; }

    public typeSize(type : CType) : Expected<unsigned int, ffi::FfiError>;
    public canonicalType(type : CType) : Expected<CType, ffi::FfiError>;
}
```

`typeSize()` returns the native C size in bytes (`VOID` has size zero).
`canonicalType()` maps native integer aliases to their fixed-width IDs;
`FLOAT`, `DOUBLE`, `POINTER`, `VOID` and `BOOL` remain unchanged.
Invalid IDs return `INVALID_TYPE`. These queries alone do not establish host
support: `prepare()` performs the ABI/environment check. `BOOL` remains distinct
from `UINT8`, even though its native storage occupies one byte.

## Errors

```k
namespace k::ffi {
    public enum FfiErrorCode {
        OK = 0;
        INVALID_TYPE = 1;
        UNSUPPORTED_ABI = 2;
        INVALID_SIGNATURE = 3;
        ARGUMENT_COUNT = 4;
        ARGUMENT_TYPE = 5;
        OUT_OF_RANGE = 6;
        NULL_FUNCTION = 7;
        OUT_OF_MEMORY = 8;
        INVALID_FRAME = 9;
        BUFFER_IN_USE = 100;
        EMBEDDED_ZERO = 101;
    }
    public final class FfiError {
        FfiError(status : int, argumentIndex : int = -1);
        FfiError(code : FfiErrorCode, argumentIndex : int = -1);
        const code() : FfiErrorCode;
        const status() : int;
        const message() : const String&;
        const argumentIndex() : int;
    }
}
```

Errors own an English diagnostic independent of the native runtime's C string
storage. A rejected argument write reports its zero-based index. Other errors
use `-1`, including an arity mismatch. `status()` preserves the native numeric
status; `code()` exposes the corresponding enum. Values `0..9` are native call
engine statuses; `100` and `101` are adapter errors. Buffer bounds failures use
`OUT_OF_RANGE`; checked buffer access during a managed pointer lease uses
`BUFFER_IN_USE`. UTF-8 conversion rejects embedded zero bytes as `EMBEDDED_ZERO`.

Preparation, binding, invocation, type queries and result extraction return
an `Expected` with either a result or an error, never an intentionally undefined
state. Native allocation failures return `OUT_OF_MEMORY`. K allocations or
copies may still throw; the API does not translate every K exception into an
`FfiError`. Indexed metadata access throws `IndexOutOfBoundsError` when the
index is invalid.

## Signatures and preparation

All declarations in this section belong to `::k::ffi::c`.

```k
public final class CSignature {
    CSignature();
    CSignature(returnType : CType, parameters : const CType[]&,
               abi : CAbi = CAbi::DEFAULT);
    CSignature(returnType : CType, parameters : const Vector<CType>&,
               abi : CAbi = CAbi::DEFAULT);
    const getReturnType() : CType;
    const getAbi() : CAbi;
    const getParameterCount() : unsigned int;
    const getParameterTypes() : Vector<CType>;
    const getParameterType(index : unsigned int) : CType;
    const prepare() : Expected<CPreparedSignature, ffi::FfiError>;
}

public final class CSignatureBuilder {
    CSignatureBuilder();
    setReturnType(type : CType);
    addParameter(type : CType);
    setAbi(abi : CAbi);
    reset();
    returns(type : CType) : CSignatureBuilder&;
    parameter(type : CType) : CSignatureBuilder&;
    callingConvention(abi : CAbi) : CSignatureBuilder&;
    const getReturnType() : CType;
    const getAbi() : CAbi;
    const getParameterCount() : unsigned int;
    const getParameterTypes() : Vector<CType>;
    const getParameterType(index : unsigned int) : CType;
    const build() : CSignature;
    const prepare() : Expected<CPreparedSignature, ffi::FfiError>;
}

public final class CPreparedSignature {
    CPreparedSignature();
    const isPrepared() : bool;
    const getSignature() : CSignature;
    const getReturnType() : CType;
    const getAbi() : CAbi;
    const getParameterCount() : unsigned int;
    const getParameterTypes() : Vector<CType>;
    const getParameterType(index : unsigned int) : CType;
}
```

The default signature and builder describe `void(void)` with `DEFAULT` ABI.
`reset()` restores that state. Non-fluent setters return no value; fluent
methods return a reference to the same builder. `build()` copies a snapshot;
`prepare()` on the builder prepares such a snapshot.

`CSignature` does not expose mutation of its contents. Input arrays/vectors
are copied; `getParameterTypes()` returns an independent mutable vector. Copies
and assignment replace whole signature values, never share mutable parameter
storage. Subsequent builder/input/vector changes cannot alter a signature or
its prepared form.

Prepared signatures share an immutable resource and release it when its final
owner is destroyed. Each invocation uses a new frame; no global or shared
mutable call frame is cached. Distinct calls may share a prepared signature.
Mutable builders and individual wrapper objects must not be modified
concurrently with a read or call.

`VOID` is valid as a return type, but invalid as a parameter type. The prepared
metadata preserves the declared type IDs, not only their canonical types.
A default `CPreparedSignature` is unprepared and cannot bind a function.
Its metadata describes the default empty signature; indexed access still
throws.

## Native pointers

```k
namespace k::ffi {
    public final class NativePointer {
        NativePointer();
        static borrowed(address : byte*) : NativePointer;
        static retained(address : byte*, keepalive : const Shared<Object>&) : NativePointer;
        static fromSymbol(symbol : const NativeSymbol&) : NativePointer;
        static fromArray(values : const byte[]&) : NativePointer;
        static fromArray(values : const unsigned byte[]&) : NativePointer;
        static fromArray(values : const short[]&) : NativePointer;
        static fromArray(values : const unsigned short[]&) : NativePointer;
        static fromArray(values : const int[]&) : NativePointer;
        static fromArray(values : const unsigned int[]&) : NativePointer;
        static fromArray(values : const long[]&) : NativePointer;
        static fromArray(values : const unsigned long[]&) : NativePointer;
        static fromArray(values : const float[]&) : NativePointer;
        static fromArray(values : const double[]&) : NativePointer;
        static fromArray(values : const byte*[]&) : NativePointer;
        static fromArray(values : byte*[]&) : NativePointer;
        const rawAddress() : byte*;
        const isNull() : bool;
        const keepalive() : Shared<Object>;
    }
}
```

`borrowed()` never owns or frees the pointer. `retained()` additionally shares
the supplied K object's lifetime; it does not claim that this object owns the
address. The object can implement an allocation lease, but establishing that
relationship is the caller's responsibility. `keepalive()` returns a shared
handle copy, not the address's allocation owner.

`fromSymbol()` retains the symbol and its module. A zero-address symbol produces
a null pointer but still retains that symbol. A default pointer is null.

Returned pointer results retain the bound native symbol/module and the call's
argument values, including their pointer keepalives. Thus a result that aliases
a retained pointer argument keeps that associated K object alive after the call
builder and function wrappers are destroyed. This is conservative lifetime
retention, not proof that the returned address is valid or caller-owned. A
borrowed argument remains borrowed.

## Transparent array borrowing

`NativePointer::fromArray()`, `CValue::array()`, `CCallBuilder.argument()` and
`CCallBuilder.addArgument()` have explicit overloads for the following array
element types:

```
byte, unsigned byte, short, unsigned short, int, unsigned int,
long, unsigned long, float, double, byte*
```

Transparent aliases such as `c::Int`, `c::UShort`, `c::Double` and `c::Char`
use these same overloads. There is no unchecked public template accepting an
arbitrary element type. K `char[]` is UTF-32 and is **not** a byte C string;
it is excluded, as are `bool[]`, structs/classes, owners, function references
and `NativePointer[]`. A `byte*[]` is an array of raw native addresses, not of
managed pointer wrappers.
Raw pointer arrays also have a `byte*[]&` overload in addition to
`const byte*[]&`; each adapter therefore has twelve explicit array overloads.

For a nonempty source, the address is obtained from the compiler's
`&values[0]`, preserving the actual payload layout and element alignment.
The K array descriptor/header is never passed to C, and no fixed header offset
is assumed. For an empty array, the pointer is null without indexing element
zero. The array count is not an implicit C argument: the caller must supply the
foreign function's count/length/index arguments explicitly.

These adapters **borrow**, even when the source is a heap-array owner. They do
not move the array, copy its contents, retain its allocation, or keep the
allocations addressed by a raw pointer array alive. The source array and all
necessary pointees must remain alive and at the same addresses through the
call and any subsequent use of a saved builder or returned aliased pointer.
Call snapshots copy the borrowed address, not the array contents; mutations
of a mutable source before invocation are visible to C.

For a const source, the foreign function must treat the storage as read-only.
The opaque pointer/signature representation does not enforce C pointee
constness. A mutable source may be passed to a foreign read/write function
whose extent fits the source. The caller must prevent concurrent access.
Neither this borrowing nor explicit `NativePointer::borrowed()` establishes a
lease on some other buffer or permits bypassing its existing operation leases.

```k
// Assume sumInts is int64_t sum_ints(const int32_t*, size_t).
values : c::Int[3] = {1, 2, 3};
fluent : Expected<CallResult, FfiError> =
    sumInts.call().argument(values)
        .argument(c::CValue::unsignedValue(c::CType::SIZE_T, 3uL)).invoke();

arguments : c::CValue[2];
arguments[0] = c::CValue::array(values);
arguments[1] = c::CValue::unsignedValue(c::CType::SIZE_T, 3uL);
direct : Expected<CallResult, FfiError> = sumInts.invoke(arguments);
```

## Managed native byte storage

```k
// In ::k::ffi
public final class NativeBuffer {
    NativeBuffer();
    static allocate(capacity : unsigned int) : Expected<NativeBuffer, FfiError>;
    static fromBytes(source : const unsigned byte[]&) : Expected<NativeBuffer, FfiError>;
    const size() : unsigned int;
    const isEmpty() : bool;
    const isAllocated() : bool;
    const hasLease() : bool;
    const get(index : unsigned int) : Expected<unsigned byte, FfiError>;
    put(index : unsigned int, value : unsigned byte) : Expected<unsigned byte, FfiError>;
    copyFrom(source : const unsigned byte[]&, offset : unsigned int = 0u)
        : Expected<unsigned int, FfiError>;
    copyFrom(source : const byte[]&, offset : unsigned int = 0u)
        : Expected<unsigned int, FfiError>;
    const copyTo(destination : unsigned byte[]&, offset : unsigned int = 0u)
        : Expected<unsigned int, FfiError>;
    const copyTo(destination : byte[]&, offset : unsigned int = 0u)
        : Expected<unsigned int, FfiError>;
    const borrowedPointer(offset : unsigned int = 0u) : Expected<NativePointer, FfiError>;
    const pointer() : NativePointer;
}
```

`allocate()` creates zero-filled, fixed-capacity contiguous native storage.
The base address has the alignment guarantee of the C runtime's `calloc`
allocation, not the alignment of a K byte-array payload after its header.
`fromBytes()` copies bytes into an independent allocation. Native allocation
failure returns `OUT_OF_MEMORY`; allocation of K ownership wrappers may still
throw. A default buffer has no allocation and size zero. An allocated
zero-capacity buffer is distinguishable through `isAllocated()`.

Copies share storage and see each other's changes; they do not deep-copy it.
There is no cursor, resizing, manual native-handle access, I/O registration,
mapping, or wrapping of a caller's `io::ByteBuffer`. The implementation uses
fresh native buffer storage and honors its operation-lease checks.

`get()` and `put()` require `index < size()`. A successful `put()` returns the
written byte. Copies transfer exactly the array's element count at the given
buffer offset and return the number of bytes copied. Bounds are checked using
subtraction before any transfer; no overflowing offset-plus-count calculation
is relied on. A zero-length transfer at `offset == size()` succeeds without
indexing an empty array. No partial transfer is performed on a bounds error.

`pointer()` returns a managed base address whose keepalive retains both storage
and a native operation lease. Multiple live pointer exports share that lease.
It lasts until the final managed pointer/value/result keepalive is destroyed,
not merely until the foreign call returns. Checked reads, writes, nonempty
copies and nonempty borrowed-pointer exports return `BUFFER_IN_USE` while it
is active. A copied wrapper cannot bypass this restriction. A builder retaining
a buffer argument therefore holds the lease until its arguments are cleared or
the builder is destroyed; an aliased pointer result can extend it further.
Foreign access through that managed pointer is the intended leased operation.
The caller must synchronize accesses through raw pointers.

An empty buffer's managed pointer is null. `borrowedPointer()` does not retain
storage or acquire a lease; it returns `OUT_OF_RANGE` for `offset > size()`
and a null pointer at `offset == size()`. Its non-null result must not outlive
the buffer's allocation or be used concurrently with a conflicting operation.
Use `pointer()` for safe storage retention in C argument values. Lease
publication through `pointer()` on wrappers sharing one storage object must be
externally synchronized; atomic reference counts do not make that publication
or foreign memory access thread-safe.

Buffers can be passed as `CValue(buffer)`, `CValue::buffer(buffer)`,
`call.argument(buffer)` or `call.addArgument(buffer)`. These all export a
managed pointer. As with arrays, lengths must be supplied separately.

## Explicit UTF-8 C strings

```k
// In ::k::ffi::c
public final class Utf8CString {
    Utf8CString();
    static fromString(text : const String&) : Expected<Utf8CString, ffi::FfiError>;
    const isValid() : bool;
    const isEmpty() : bool;
    const byteLength() : unsigned int;
    const pointer() : ffi::NativePointer;
    const borrowedPointer() : Expected<ffi::NativePointer, ffi::FfiError>;
}
```

Conversion uses `String.toUtf8()`, explicitly checks every payload byte for an
embedded zero, and stores the encoded bytes plus their final zero terminator
in managed native storage. `byteLength()` excludes that terminator and counts
bytes, not Unicode characters. A successfully converted empty string has
length zero and a **non-null** pointer to a zero terminator. No implicit
conversion from K `String` or UTF-32 `char[]` is performed.

Copies share immutable string storage. The returned pointer must be used only
with foreign APIs that treat its bytes as read-only. Managed pointers retain
the terminated UTF-8 allocation after string wrappers/builders are destroyed,
including when foreign code returns an aliased pointer. Borrowed pointers
follow the buffer lifetime and lease restrictions above.

A default wrapper is invalid, not a converted empty string. Its `pointer()`
throws `NullDereferenceError`, and its `borrowedPointer()` returns `INVALID_FRAME`.
It reports `isValid() == false` and `isEmpty() == false`.

Strings can be passed as `CValue(string)`, `CValue::cstring(string)`,
`call.argument(string)` or `call.addArgument(string)`, all with managed retention.

## Argument values

```k
// In ::k::ffi::c
public final class CValue {
    CValue();
    CValue(value : byte);           CValue(value : unsigned byte);
    CValue(value : short);          CValue(value : unsigned short);
    CValue(value : int);            CValue(value : unsigned int);
    CValue(value : long);           CValue(value : unsigned long);
    CValue(value : float);          CValue(value : double);
    CValue(value : bool);
    CValue(value : const ffi::NativePointer&);
    CValue(value : const ffi::NativeBuffer&);
    CValue(value : const Utf8CString&);
    static signedValue(type : CType, value : long) : CValue;
    static unsignedValue(type : CType, value : unsigned long) : CValue;
    static floatingValue(type : CType, value : double) : CValue;
    static pointerValue(value : const ffi::NativePointer&) : CValue;
    static buffer(value : const ffi::NativeBuffer&) : CValue;
    static cstring(value : const Utf8CString&) : CValue;
    // Twelve array() overloads: the const T[]& types above, plus byte*[]&.
    const getType() : CType;
    const kind() : ffi::ResultKind;
}
```

Scalar constructors select `SCHAR`, `UCHAR`, `SHORT`, `USHORT`, `INT`, `UINT`,
`LONG`, `ULONG`, `FLOAT`, `DOUBLE` or `BOOL` according to the K argument type.
Transparent aliases cannot create distinct constructor overloads:
`CValue(Char(...))` and `CValue(SignedChar(...))` both select `SCHAR`.
Use an explicit factory when the declared type ID matters.

Factories store the unconverted payload and requested type; they do not
silently convert between signed, unsigned, floating and pointer representations.
Invocation checks both the provided type's canonical compatibility with the
parameter and the payload representation. Integer narrowing must be in range.
`BOOL` requires an unsigned payload of exactly zero or one; its scalar
constructor implements that rule. A signed `BOOL` or an ordinary `UINT8`
argument for a `BOOL` parameter is rejected.

`FLOAT` arguments are rounded to native float. Finite overflow and rounding a
nonzero finite value to zero are rejected; infinities and NaNs are accepted.
Use an explicit `FLOAT` factory with a double payload to validate before
narrowing. A value already narrowed to K `float` cannot recover lost range.
A default `CValue` is not a valid argument.

## Functions and fluent calls

```k
// In ::k::ffi::c
public final class CFunction {
    CFunction();
    static bind(symbol : const ffi::NativeSymbol&, prepared : const CPreparedSignature&,
                captureErrno : bool = false) : Expected<CFunction, ffi::FfiError>;
    const getSymbol() : ffi::NativeSymbol;
    const getSignature() : CSignature;
    const getPreparedSignature() : CPreparedSignature;
    const capturesErrno() : bool;
    const invoke(arguments : const CValue[]&) : Expected<ffi::CallResult, ffi::FfiError>;
    const call() : CCallBuilder;
}

public final class CCallBuilder {
    CCallBuilder(function : const CFunction&);
    addArgument(value : const CValue&);
    addArgument(value : const ffi::NativeBuffer&);
    addArgument(value : const Utf8CString&);
    // Twelve addArgument() overloads: the const T[]& types above, plus byte*[]&.
    reset();
    argument(value : const CValue&) : CCallBuilder&;
    argument(value : const ffi::NativeBuffer&) : CCallBuilder&;
    argument(value : const Utf8CString&) : CCallBuilder&;
    // Twelve argument() overloads for those arrays, returning CCallBuilder&.
    const getArgumentCount() : unsigned int;
    const getFunction() : CFunction;
    const getArguments() : Vector<CValue>;
    const invoke() : Expected<ffi::CallResult, ffi::FfiError>;
}
```

Binding rejects an empty symbol or a successful lookup with address zero as
`NULL_FUNCTION`, and rejects an unprepared signature as `INVALID_SIGNATURE`.
The bound function retains its symbol and prepared signature. Binding cannot
check that a nonzero address is really a compatible C function.

Direct and fluent invocation use the same checked call path. Values and
keepalives are copied into a call-local snapshot. Arity is checked before
creating a frame, every argument must validate before foreign code is entered,
and frame cleanup occurs on both normal return and K exception unwind.
`CCallBuilder.reset()` clears arguments without changing its function.
Copied builders and vectors returned by `getArguments()` have independent
mutable argument storage. Invoking a builder does not consume its arguments.

With `captureErrno = true`, the native call clears `errno` immediately before
entry and captures it immediately after return. With `false`, the engine
restores the pre-call value and the result does not expose captured errno.
An errno value does not itself make invocation fail; interpret it according
to the foreign API's result convention.

## Copied results

```k
namespace k::ffi {
    public enum ResultKind {
        UNAVAILABLE = 0; VOID = 1;
        SIGNED = 2; UNSIGNED = 3; FLOATING = 4; POINTER = 5;
    }
    public final class CallResult {
        CallResult();
        const kind() : ResultKind;
        const isVoid() : bool;
        const hasReturnValue() : bool;
        const asSigned() : Expected<long, FfiError>;
        const asUnsigned() : Expected<unsigned long, FfiError>;
        const asFloating() : Expected<double, FfiError>;
        const asPointer() : Expected<NativePointer, FfiError>;
        const errnoCaptured() : bool;
        const getErrno() : Expected<int, FfiError>;
    }
}
```

Results are detached from the native frame. Copies preserve scalars, category,
errno metadata and pointer keepalives. Extraction is category-strict:
signed integers use `asSigned()`, unsigned integers and C booleans use
`asUnsigned()`, floats/doubles use `asFloating()`, and pointers use `asPointer()`.
All other extraction attempts return `ARGUMENT_TYPE`, not a zero-valued
success. The C function's signature supplies the actual declared return type;
the generic result does not contain a C-specific type enum.

`void` is a successful call with `kind() == VOID`, `isVoid() == true` and
`hasReturnValue() == false`; it is not an integer zero. A default result is
`UNAVAILABLE`, not a successful void call. `getErrno()` returns `INVALID_FRAME`
unless `errnoCaptured()` is true.

All public value wrappers support copying and copy assignment. `FfiError` and
`CCallBuilder` require their documented constructor arguments; the other
wrappers also have the documented default states.

## Example

Assume `image : Shared<Module>` represents a loaded native image exporting
`int add(int, int)`. Lookup, preparation and binding failures are separate:

```k
using namespace k::ffi;
using namespace k::ffi::c;

lookup : Expected<NativeSymbol, SymbolLookupError> =
    image.get()->lookupSymbol(String("add"));
if (lookup.hasResult()) {
    symbol : NativeSymbol = lookup.getResult();
    builder : CSignatureBuilder;
    preparedResult : Expected<CPreparedSignature, FfiError> =
        builder.returns(CType::INT).parameter(CType::INT).parameter(CType::INT).prepare();
    if (preparedResult.hasResult()) {
        prepared : CPreparedSignature = preparedResult.getResult();
        bound : Expected<CFunction, FfiError> = CFunction::bind(symbol, prepared);
        if (bound.hasResult()) {
            function : CFunction = bound.getResult();
            callResult : Expected<CallResult, FfiError> =
                function.call().argument(CValue(19)).argument(CValue(23)).invoke();
            if (callResult.hasResult()) {
                value : CallResult = callResult.getResult();
                sum : Expected<long, FfiError> = value.asSigned();
                // sum holds 42; value.asPointer() would return ARGUMENT_TYPE.
            }
        }
    }
}
```

The non-fluent equivalent prepares via `setReturnType()`/`addParameter()` and
invokes with a `CValue[2]` array. Calling the function with only one argument,
with a `DOUBLE` argument for an `INT` parameter, or with an out-of-range
`signedValue(CType::INT, ...)` must fail without entering foreign code.
