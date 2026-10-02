# 07. `Shared<T>` / `Weak<T>` Ownership Architecture

This note describes the current standard-library implementation of shared
ownership in `libk`. It complements the public contracts in
[Shared and Weak](../spec/stdlib/shared.md) and the practical
[memory guide](../guides/memory.md).

## 1. Handle and control-block model

Each `Shared<T>` and `Weak<T>` value stores two pointers:

```text
Shared<T> / Weak<T>
┌───────────────────┬──────────────────┐
│ SharedControlBlock*│ T* typed view    │
└─────────┬─────────┴────────┬─────────┘
          │                  │
          ▼                  ▼
   shared ownership     current adjusted
       identity          class/interface view
```

The ownership identity is independent of the handle's static type. The private
`SharedBlock<T>` retains the original `T!` owner and implements destruction
through that original type. A converting handle therefore stores an adjusted
view in `_view` but does not replace the owner or the destruction callback.
Deleting through an adjusted base/interface pointer is not required to recover
the original allocation type.

The empty state is represented by null `_block` and `_view` fields. Creating an
empty handle allocates no metadata. Constructing `Shared<T>` from a non-null
owner creates one `SharedBlock<T>` and adopts the owner; constructing from a
null owner produces the empty state.

## 2. Strong and weak counts

`SharedControlBlock` owns two signed 32-bit counters:

- `_strong` counts strong handles.
- `_weak` counts explicit weak handles plus one implicit weak reference held
  through destruction after the final strong release.

The block starts with one strong reference and the implicit weak reference.
Copying a `Shared` retains strong ownership; copying a `Weak` retains weak
metadata only. Moving a handle transfers its two fields without changing
either count.

The final strong release first invokes the original typed block's
`destroyObject()` callback. That callback moves the retained owner into a local,
empties the block's owner field, and deletes the object. It then releases the
implicit weak reference. The control block is deleted only when its final
explicit or implicit weak reference is released. Keeping the implicit weak
reference through object destruction allows the destructor itself to release
other weak handles safely.

Atomic counter operations are implemented by the runtime C bridge
`runtime/shared_ffi.c`; K remains responsible for the counter storage layout.
Promotion uses a compare-and-exchange loop which increments strong count only
while it is nonzero. Once the count reaches zero, the group cannot be
resurrected. Final release uses release ordering and an acquire fence before
destruction. Invalid overflow, underflow, or retaining a released group is a
runtime-fatal invariant violation rather than counter wraparound.

These atomics synchronize ownership accounting, not the managed object's
fields. They support concurrent operations on distinct handles. Concurrent
mutation of a single handle still requires synchronization, and object access
requires whatever synchronization its mutable state needs. The object's
destructor runs on the thread that releases the last strong handle.

## 3. Typed-view conversions

Converting `Shared<U>` to `Shared<T>` follows two independent steps:

1. Adapt the source `_view` to a `T*` using the compiler's available static
   upcast or checked class/interface RTTI conversion.
2. Only if that view is non-null, retain the source `_block` and store the
   adapted view.

A cast mismatch or empty source creates an empty destination and does not
retain the control block. A successful cross-cast shares the same ownership
group; the source and destination may consequently expose different
interfaces at different subobject offsets while keeping the same object alive.
This relies on the subobject-aware RTTI adjustment described in
[Class Layout, Vtables, RTTI & Call Synthesis](03-class-layout-vtables-rtti-and-calls.md).

Converting copy assignment builds/adapts a replacement before releasing the
destination's previous group. Thus a mismatch clears the destination, leaves
the source unchanged, and does not transiently destroy a group that the new
handle might share. Converting drain construction/assignment adapts before
consuming the source: success transfers the handle without a count increment;
failure leaves the source unchanged and the destination empty.

The same separation applies to `Weak` handles, with lifetime protection around
any cast that might inspect object metadata:

- Static upcasts attempt a temporary strong retention before computing an
  adjusted view, because virtual-base adjustment may read the live object.
  If the group has expired, the weak group identity can be preserved with an
  unusable null view.
- Dynamic conversions lock temporarily, perform the RTTI adjustment only
  while a strong reference keeps the object alive, then retain the weak
  observation if the conversion succeeds.
- A failed dynamic weak drain does not consume its source. A successful
  conversion transfers the observation and empties the source.

Constructing `Shared<T>` from `Weak<T>` uses an atomic try-retain and throws
`BadWeakReferenceException` for an empty or expired source. A converting weak
source is promoted temporarily; a live but mismatched object produces an
empty destination, whereas an empty or expired source retains the exception
contract. `Weak<T>::lock()` uses the same atomic promotion but returns an empty
`Shared<T>` on failure rather than throwing.

## 4. ABI and module compatibility

The handle representation changed from a single `SharedBlock<T>*` pointer to
a common erased control block plus an adjusted typed view. The FFI counter bridge,
`Shared<T>`/`Weak<T>` template bodies, and compiled users must agree on that
representation. It is not ABI-compatible with modules built against the
previous handle layout.

Checked conversions also rely on compiler-emitted cast maps, RTTI descriptor
field positions, and qualified secondary-vtable identities. An ABI change to
those structures requires rebuilding the compiler/runtime pair and all
dependent K modules and libraries that emit or consume the metadata. Do not
mix stale KDI-compiled artifacts with a runtime expecting a different layout.

The control block and its destruction callback are implementation details.
They do not add public aliasing constructors, custom deleters, array
specializations, or a user-accessible control-block API.

## See also

- [Public `Shared<T>` / `Weak<T>` API](../spec/stdlib/shared.md)
- [Memory, Storage, and Ownership guide](../guides/memory.md)
- [RTTI and cast synthesis](03-class-layout-vtables-rtti-and-calls.md#3-runtime-type-information-rtti)
