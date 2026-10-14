# Shared<T> and Weak<T> — Atomic Shared Ownership

> **Module:** `k`  
> **Source:** `libk/libk/src/shared.k`  
> **Status:** Working Draft — 2026

## Overview

`Shared<T>` shares ownership of a dynamically allocated class or interface
view. `Weak<T>` observes the same ownership group without extending the
object's lifetime.

Copying either handle does not copy `T`. Draining a handle with `#` transfers
its participation in the group and leaves the source empty on success.
A failed checked converting drain leaves the source unchanged and makes the
destination empty.

The last strong handle destroys the object exactly once. Weak handles may
outlive the object; they do not delay its destruction. An expired group must
not be resurrected, even if another group subsequently manages a new object.
Converting a handle changes only its typed pointer view: all converted handles
continue to share the original control block and its original-type destruction
callback. The control-block types are implementation plumbing, not user-facing
ownership APIs.

Converting from a const handle must preserve the managed type's constness, not
impose the handle's constness on it. A `const Shared<Derived>&` may therefore
produce a `Shared<Base>` or `Weak<Base>` for a compatible base. A
`const Weak<Derived>&` may likewise produce a `Weak<Base>` or promote to a
`Shared<Base>` while the object is alive. Const qualification may be added to
the pointee without changing ownership. A `Shared<const Derived>` may produce
only const-compatible views such as
`Shared<const Base>` or `Weak<const Base>`; conversion must not remove pointee
constness. This restriction applies to copying, draining, assignment, sharing
and weak promotion, including conversions that would otherwise use a static
upcast. Const pointees must still be destroyed exactly once when their last
strong handle is released.

Converting a const wrapper preserves the managed type's constness, not the
wrapper's constness. A `const Shared<Derived>&` can produce `Shared<Base>`,
`Shared<Object>`, or `Weak<Base>` views of the same mutable object. An empty
source stays empty, and a successful shared conversion adds one strong
reference. This never permits removing pointee constness:
`Shared<const Derived>` cannot convert to `Shared<Base>` or `Weak<Base>`.

## Shared<T>

```k
template<class T>
final struct Shared {
    Shared();
    Shared(ptr : T!);
    Shared(other : const Shared<T>&);
    Shared(other : Shared<T>#);
    template<typename U> Shared(other : const Shared<U>&);
    template<typename U> Shared(other : Shared<U>#);
    Shared(other : const Weak<T>&) throws(BadWeakReferenceException);
    template<typename U> Shared(other : const Weak<U>&) throws(BadWeakReferenceException);
    ~Shared();

    operator =(other : const Shared<T>&) : Shared<T>&;
    operator =(other : Shared<T>#) : Shared<T>&;
    template<typename U> operator =(other : const Shared<U>&) : Shared<T>&;
    template<typename U> operator =(other : Shared<U>#) : Shared<T>&;
    operator =(ptr : T!) : Shared<T>&;

    acquire(ptr : T!);
    share(other : const Shared<T>&);
    template<typename U> share(other : const Shared<U>&);
    reset();
    reset(ptr : T!);
    swap(other : Shared<T>&);

    const get() : T*;
    const isNull() : bool;
    const useCount() : int;
    const operator() : bool;
    const operator !() : bool;
    const operator ==(other : const Shared<T>&) : bool;
    const operator !=(other : const Shared<T>&) : bool;
}
```

### Construction and assignment

| Operation | Contract |
|-----------|----------|
| Default construction | Create an empty handle; no allocation occurs. |
| Construction from `T!` | Transfer the owner into the group. The supplied owner becomes null. A null owner creates an empty handle. |
| Copy construction or assignment | Retain the same object. An empty source produces an empty destination. |
| Drain construction or assignment | Transfer the handle without incrementing the count. The source becomes empty. |
| Construction from `Weak<T>` | Atomically acquire a strong reference, or throw `BadWeakReferenceException` if empty or expired. |
| Conversion from `Shared<U>` | Adapt the typed view by accessible, unambiguous static upcast when possible, otherwise a checked RTTI conversion supported by K's current class/interface cast rules. A match shares the original control block; mismatch or an empty source produces an empty destination. |
| Converting copy assignment | Apply the same checked adaptation before releasing the destination's previous group. A mismatch clears the destination and leaves the source unchanged. |
| Converting drain construction or assignment | Transfer the handle without retaining when adaptation succeeds and empty the source. On mismatch, clear the destination but preserve the source. |
| Conversion from `Weak<U>` | Promote temporarily to perform checked RTTI adaptation. A compatible live object shares its original group; a live mismatch produces an empty destination. Empty or expired input retains the existing `BadWeakReferenceException` contract. |
| `acquire(ptr)`, `reset(ptr)`, assignment from `T!` | Replace the current ownership by acquiring the supplied owner. Allocation failure must leave the destination's previous ownership unchanged. |
| `share(other)` | Equivalent to copy assignment. Sharing an empty source clears the destination. |
| `reset()` | Release this handle's strong reference and leave it empty. |
| `swap(other)` | Exchange handles without allocating or changing reference counts. |

Copy self-assignment and self-sharing must preserve ownership. Drain
self-assignment must also leave the handle unchanged. Draining between two
distinct handles in the same group empties the source and releases the
destination's previous reference.
Replacement installs the new handle before releasing the previous object, so
a reentrant destructor observes the replacement rather than an intermediate
empty state.

### Observers and operators

`get()` returns a borrowed pointer, or null if empty. The pointer must not be
used after the last strong reference is released. A const handle does not make
`T` const: `const Shared<T>` still returns `T*`; `Shared<const T>` returns
`const T*`. `Weak<T>.lock()` preserves the same distinction in its returned
`Shared<T>`.

`useCount()` returns an atomic snapshot of the number of strong handles.
Weak handles are not included. An empty handle returns zero.

K spells a boolean conversion as `const operator() : bool`, not
`operator bool`. Conversion returns true for a non-empty handle.
`operator !()` returns true for an empty handle.

Equality compares the managed object addresses. Two empty handles compare
equal. To access the object, use `get()` and K's built-in pointer operators,
for example `s.get()->method()` or `*s.get()`.

## Weak<T>

```k
template<class T>
final struct Weak {
    Weak();
    Weak(other : const Shared<T>&);
    Weak(other : const Weak<T>&);
    Weak(other : Weak<T>#);
    template<typename U> Weak(other : const Shared<U>&);
    template<typename U> Weak(other : const Weak<U>&);
    template<typename U> Weak(other : Weak<U>#);
    ~Weak();

    operator =(other : const Shared<T>&) : Weak<T>&;
    operator =(other : const Weak<T>&) : Weak<T>&;
    operator =(other : Weak<T>#) : Weak<T>&;
    template<typename U> operator =(other : const Shared<U>&) : Weak<T>&;
    template<typename U> operator =(other : const Weak<U>&) : Weak<T>&;
    template<typename U> operator =(other : Weak<U>#) : Weak<T>&;

    const useCount() : int;
    const expired() : bool;
    const lock() : Shared<T>;

    reset();
    swap(other : Weak<T>&);
}
```

| Operation | Contract |
|-----------|----------|
| Default construction | Create an empty handle. |
| Construction or assignment from `Shared<T>` | Observe its group without adding a strong reference. An empty source produces an empty handle. |
| Conversion from `Shared<U>` | Observe the original group through an adjusted view when the checked conversion succeeds; mismatch or empty input produces an empty handle. |
| Copy construction or assignment | Observe the same group, including when it has already expired. |
| Drain construction or assignment | Transfer the observation and empty the source. Self-draining is a no-op. |
| Conversion from `Weak<U>` | Dynamic conversions lock temporarily, then retain the target observation only if the cast succeeds. Static upcasts also acquire a temporary strong reference while computing their adjusted view, because virtual-base adjustment may read object metadata. An expired static source preserves the group identity with an unusable view; an expired dynamic source or mismatch produces an empty destination. Failed dynamic drains preserve their source; successful drains empty it. |
| `useCount()` | Return a snapshot of the strong count, or zero if empty. |
| `expired()` | Return true if empty or if the strong count is zero. |
| `lock()` | Atomically return a new strong handle if the object is alive; otherwise return an empty `Shared<T>`. Failure is a normal result and does not throw. |
| `reset()` | Release the observation and leave an empty handle. |
| `swap(other)` | Exchange observations without changing reference counts. |

`Weak<T>` deliberately has no raw-object accessor, boolean conversion, or
dereference operation. Access must first acquire a strong handle through
`lock()` or the checked `Shared<T>` constructor.

Adding pointee constness to a static weak upcast follows the same lifetime
rules as any other static weak upcast: an expired source preserves the group
identity but must not expose or inspect the former object.

Checked conversions use K's runtime RTTI cast lowering. They support class to
interface, interface to class, and sibling-interface cross-casts when the
most-derived object has the requested subobject. Successful conversions retain
the same ownership group while storing the adjusted target view; mismatches
produce an empty nullable handle. Weak dynamic conversions lock temporarily
while computing the adjusted view, so an expired object's memory is never
inspected.

### Compiled-module compatibility

Dynamic casts depend on matching compiler/runtime RTTI layouts and
secondary-vtable symbols. When upgrading a compiler or `libk` version that
changes this metadata ABI, rebuild K modules and libraries that emit or consume
RTTI together; do not mix stale compiled artifacts with the updated runtime.

Testing `expired()` and then assuming the object is alive is incorrect:
another thread may release the last strong reference between those operations.
Use the result of `lock()` instead.

## Thread safety

Reference counting and weak promotion are atomic. Operations on distinct
handles in the same group may run concurrently, including copying, resetting,
destruction and promotion.

Concurrent access to one handle must be externally synchronised if any access
modifies that handle. Multiple read-only accesses to a stable handle are
permitted. Publication of a handle to another thread must itself be properly
synchronised.

These guarantees protect ownership, not the contents of `T`. Concurrent
mutation of `T` requires its own synchronisation.

Neither `useCount() == 1` nor `expired() == false` establishes exclusive access.
A successful `lock()` guarantees object lifetime until its returned strong
handle is released; it does not grant exclusive access to the object.

The object's destructor runs on whichever thread releases the final strong
reference. The managed object's destructor must not throw.

## Example

```k
class Value {
    public number : int;
    Value(n : int) : number(n) {}
}

example() : int {
    owner : Value! = new Value(42);
    first : Shared<Value>(owner);
    second : Shared<Value>(first);
    weak : Weak<Value>(first);

    moved : Shared<Value>(#second);
    first.reset();
    moved.reset();

    locked : Shared<Value> = weak.lock();
    if (!locked) {
        return 0; // Both strong handles were released; the object is gone.
    }
    return locked.get()->number;
}
```

Weak links can break ownership cycles: a parent may strongly own a child while
the child holds only a weak link to its parent. A cycle containing only strong
handles is not automatically collected.

### Const handles and const views

```k
class Base {
    public number : int;
    public Base() : number(42) {}
}
class Derived : public Base {
    public Derived() : Base() {}
}

exampleViews() {
    source : Shared<Derived>(new Derived());
    handle : const Shared<Derived>& = source;
    writable : Shared<Base>(handle);
    ++writable.get()->number; // The handle is const, not the object.

    readOnly : Shared<const Base>(handle);
    observed : Weak<const Base>(readOnly);
    locked : Shared<const Base> = observed.lock();
}
```

The following conversion is invalid, even while the object is alive:

```k
// Given readOnly : Shared<const Base>, dropping pointee const is an error.
invalid : Shared<Base>(readOnly);
```

## Complexity and scope

Handle bookkeeping is O(1); atomic promotion may retry under contention.
Copying, draining, swapping and locking do not allocate. Acquiring a non-null
owner allocates ownership metadata and may throw `OutOfMemory`.
Reference counts must fit in a positive `int`; exhausting that range is
diagnosed by the runtime and terminates execution rather than wrapping.

Custom deleters, `enable_shared_from_this` equivalents, aliasing constructors
and array specialisations are not provided.

## Migration from the intrusive implementation

The existing `acquire()`, `share()`, `get()`, `isNull()`, `useCount()` and
`reset()` methods remain available. `share()` now accepts a const source and
clears its destination when the source is empty, matching copy assignment.
Acquiring a null owner produces an empty handle without allocating metadata.

The handle representation has changed to a common erased control block plus an
adjusted typed view. Applications and libraries using `Shared<T>` or `Weak<T>`
must be rebuilt together; the old and new representations are not
ABI-compatible, and dependent modules compiled against the previous layout
must not be mixed with the new library.

## See also

- [Memory primitives](memory.md)
- [Exception types](exceptions.md)
- [Ownership and indirection types](../language/basic/types.md)
- [Memory, Storage, and Ownership guide](../../guides/memory.md)
