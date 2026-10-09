# Runtime C FFI

**Module:** `k`  
**Source:** `libk/libk/src/ffi_runtime.k`  
**Related:** [Module and native symbol lookup](module.md)

## Scope and safety

The runtime C API invokes a dynamically looked-up `ffi::NativeSymbol` using an
explicit, fixed C signature. It is separate from the compile-time
`@ffi::Extern("C")` annotation.

This initial API supports scalar integers, C booleans, `float`, `double`,
opaque pointers and `void` returns. Only the default Linux LP64 C ABI with
signed 8-bit plain `char`, 16-bit `short`, 32-bit `int`, 64-bit `long`,
`long long`, pointers, `size_t` and `ptrdiff_t`, and IEEE binary32/binary64
floating-point representations is supported. Signature preparation must fail
with `UNSUPPORTED_ABI` on incompatible hosts, including unsigned-char hosts.

Aggregates, unions, variadic functions, callbacks, non-default calling
conventions, C++ member calls, native allocation owners, string conversion and
array/buffer adapters are not provided by this API.

The caller **must** provide the function's actual C signature. Symbol lookup
does not discover types or distinguish a function from a data symbol. Calling
a data symbol or a function with an incompatible signature is unsafe; scalar
argument validation cannot make a fabricated signature safe. A foreign call
must not throw across this boundary. Pointer validity, bounds, mutability and
any foreign retention beyond the call remain the caller's responsibility.

## Namespace map

| Namespace | Public declarations |
|---|---|
| `::k::ffi` | `NativePointer`, `FfiError`, `FfiErrorCode`, `CallResult`, `ResultKind` |
| `::k::ffi::c` | C aliases, `CType`, `CAbi`, `typeSize`, `canonicalType`, `CSignature`, `CSignatureBuilder`, `CPreparedSignature`, `CValue`, `CFunction`, `CCallBuilder` |

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
status; `code()` exposes the corresponding enum.

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
    static signedValue(type : CType, value : long) : CValue;
    static unsignedValue(type : CType, value : unsigned long) : CValue;
    static floatingValue(type : CType, value : double) : CValue;
    static pointerValue(value : const ffi::NativePointer&) : CValue;
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
    reset();
    argument(value : const CValue&) : CCallBuilder&;
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
