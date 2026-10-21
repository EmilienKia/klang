# Borrowed Arguments

[← Standard Library](index.md)

**Module:** `k` (automatically imported)
**Source:** `libk/libk/src/argument.k`

> **Synchronous borrowed use only.** Do not retain or enqueue holders for
> asynchronous use, return holders backed by expired locals, invalidate their
> source slots, or relocate/destroy containing storage while holders are used.
> Copies do not extend any lifetime. Keeping an object alive does not keep the
> slot that addressed it alive. There is no general borrow/escape checker.

`Argument` is an immutable, reference-only binding descriptor. It is not an
owning `Any`, a boxed payload, or a persistent value store. Its ordinary array
alias is exactly:

```k
alias ArgumentPack : Argument[];
```

There is no second argument-pack container and no heterogeneous vararg syntax
in this API. An `ArgumentPack` uses the usual array initialization, indexing,
copying, assignment, and lifetime rules.

## Capture

```k
source : int! = new int(42);
argument : Argument = Argument::capture(source);
copy : Argument = argument;
// source is still non-null; neither holder owns the integer.
taken : int! = copy.takeOwner<int>();
// source is null, and argument.isNull() and copy.isNull() are true.
source = new int(73);
// Both holders now observe 73 through the SAME original source slot.
```

| Operation | Contract |
|-----------|----------|
| `Argument::capture(source)` | Borrow an addressable value lvalue, original addresser slot, reference, or explicitly authorized drain. Never copy or move the source value. |
| `Argument::borrow(source)` | Capture with readonly, non-consuming capabilities. Copies cannot recover mutation, owner-take, or drain permission. |

Both factories are compiler intrinsics with a reference parameter (`T&`), not
by-value owner factories. A local, field, or array element retains its original
slot identity. Reassigning a pointer, link, or owner is visible to every holder
alias. Capture evaluates the source expression, including array indices, once.

Known non-addressable explicit captures are compile-time errors:

```k
// Invalid: no stable caller slot.
// holder : Argument = Argument::capture(new int(42));
// holder : Argument = Argument::capture(42);
```

A reference returned by a function has its ordinary reference lifetime
contract. A field of a known temporary is not a stable source slot. Explicit
capture never silently allocates a persistent payload or prolongs a temporary.

Default construction yields **unbound**. This is valid for ordinary array
construction. A bound nullable slot containing `null` is not unbound, missing,
or void. A value lvalue containing numeric zero is bound and non-null.

## Inspection

These methods inspect metadata only and do not establish lifetime or race
safety. All holders remain subject to the synchronous warning above.

| Method | Meaning |
|--------|---------|
| `isBound()` | Whether the holder has a borrowed source binding. |
| `isNull()` | Whether the current addressed binding is null; throws on unbound. |
| `getLogicalType()` | Exact supplied semantic type, before hidden transport storage. |
| `getBindingType()` | Exact binding/slot type, including its qualifiers. |
| `getReferentType()` | Exact addressed value type, including nominal identity and constness. |
| `getCategory()` | `ArgumentCategory`: `UNBOUND`, `VALUE_LVALUE`, `ADDRESSER_SLOT`, `DRAIN`, `REFERENCE`, or reserved call-scoped `VALUE_TEMPORARY`. |
| `canRead()`, `canMutate()` | Granted referent access. |
| `canReadSlot()`, `isSlotWritable()` | Whether there is an original addresser slot and whether it can be changed. |
| `canTakeOwner()`, `canDrain()` | Granted consumption/resource-acquisition permissions. |

Type descriptors are the immutable [`k::rtti::Type`](rtti.md) graph. Equal LLVM
pointer representations do not make an owner, pointer, view, reference, or
drain interchangeable. Plain value lvalues remain values logically; internal
transport indirection is not a user pointer or a public double pointer.

## Checked typed access

Every access below is synchronous and borrowed except the owner explicitly
transferred by `takeOwner` or `take`. Except for `take<T>()`, `T` names a value
referent, not another addresser, owner, or callable. A read must not expose a
copyable owner or a double pointer.

| Method | Result | Contract |
|--------|--------|----------|
| `read<T>()` | `const T&` | Checked, non-null readonly referent access. Does not copy the value. |
| `get<T>()` | `T*` | Checked, nullable, non-owning referent pointer. Mutable `T` requires mutation permission; `const T` narrows access. |
| `takeOwner<T>()` | `T!` | Checked owner load-and-clear of the original writable owner slot. Bound-null returns a null owner. |
| `take<T>()` | `T` | Exact logical owner take, including a strong alias of an owner type. `T` itself must be an owner type. |
| `drain<T>()` | `T#` | Recover an explicitly granted, non-null drain authorization. Normal typed drain constructors/operations acquire resources; the holder does not own its source object. |

Access checks exact type identity, including strong aliases, signedness,
qualifiers, and array extents. No implicit numeric widening or arbitrary
converting constructor is performed. Readonly access may add constness but
never remove it. Owner take uses the exact referent type (or const narrowing);
ordinary typed owner upcasts can then be applied to the returned owner.
For a strong alias of an owner, use `take<Alias>()`: `takeOwner<T>()` must not
silently erase the binding's nominal alias. A strong alias of a non-owning
pointer still never grants owner permission.

Slot writability is independent of referent mutability. An owner of a const
object can be transferred when its original slot is writable. An owner field
accessed through a readonly containing object cannot be cleared. A borrowed
pointer never grants owner take, and a readonly capture never grants drain.

Capture uses the ordinary resolved declaration type. For example,
`const pointer : int* = &value;` has the same pointee qualification as
`pointer : const int* = &value;`: the holder must reject `get<int>()`, while
`get<const int>()` is allowed and the original pointer slot remains writable.
Rebinding that slot must still be observed by every holder copy.

Copying or reassigning a holder copies/replaces only its borrowed binding
metadata. Replacing an array element does not rebind the external slot to which
the old element referred. A take does not permanently consume the holder:
later source-owner reassignment is visible and can be taken again.

## Errors and provenance

`ArgumentError : FatalError` is unchecked and catchable. `getReason()` returns
`ArgumentErrorReason`; the same ordinal is available through `getCode()`.

| Reason | Meaning |
|--------|---------|
| `UNBOUND` | Access requires a source binding. |
| `TYPE_MISMATCH` | Wrong exact value type, alias, signedness, or extent. |
| `CONST_VIOLATION` | Requested mutable access is not granted. |
| `OWNER_TAKE_FORBIDDEN` | Source is not an authorized writable owner slot. |
| `DRAIN_FORBIDDEN` | No drain authorization was granted. |
| `UNTRUSTED_TYPE` | Nominal producer identity is unavailable or unsupported. |
| `NULL_NOT_ALLOWED` | A non-null read/drain was requested from bound-null. |

Primitive access and same-compilation nominal access are supported. Cross-image
nominal producer binding is not implemented by this holder API: imported
unresolved provenance must not be accepted through name/layout equality.
Capture/inspection can preserve such descriptors, but typed access reports
`UNTRUSTED_TYPE` rather than guessing identity.

## Compiler and future call packaging

Canonical declarations use versioned `@k::annotations::Intrinsic("argument.….v1")`
(`@annotations::Intrinsic` inside module `k`). An arbitrary function carrying
the same annotation string is not an Argument intrinsic. Capture/access
operations must be called directly; they are not callable function bodies or
reflection-specific target invokers.

Explicit storable capture does not accept temporary sources. Future concise
call packaging must preserve the logical source category, evaluate each source
once, materialize only for that call, and keep ordinary normal/unwind cleanup
active through the entire call. It must not hand off ownership of temporary
storage merely because a holder was forwarded.
Scoped compiler materialization preserves original aggregate and sized-array
storage rather than copying resource-bearing payloads. Retained sources are
destroyed exactly once on normal exit and unwind. Owned results remain guarded
until retained cleanup succeeds, including when normal destruction throws.
Owner-bearing unions and arrays retain their active payload or constructed
prefix until every remaining child has been cleaned up. Owner, owned callable,
union and array results must be cleaned up if retained-source destruction fails;
successful cleanup transfers the result to its caller without consuming it.
These infrastructure rules do not make explicit temporary capture valid or
introduce concise-call syntax.

This API does not implement reflective invocation, a generic FFI call planner,
result storage/observers, heterogeneous source varargs, ownership transactions,
source-change detection, asynchronous retention, or cross-image type binding.
