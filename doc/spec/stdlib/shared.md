# Shared<T> and Weak<T> — Atomic Shared Ownership

> **Module:** `k`  
> **Source:** `libk/libk/src/shared.k`  
> **Status:** Working Draft — 2026

## Overview

`Shared<T>` shares ownership of a dynamically allocated class object.
`Weak<T>` observes the same ownership group without extending the object's
lifetime. Both templates require a class type `T`.

Copying either handle does not copy `T`. Draining a handle with `#` transfers
its participation in the group and leaves the source empty.

The last strong handle destroys the object exactly once. Weak handles may
outlive the object; they do not delay its destruction. An expired group must
not be resurrected, even if another group subsequently manages a new object.

## Shared<T>

```k
template<class T>
final struct Shared {
    Shared();
    Shared(ptr : T!);
    Shared(other : const Shared<T>&);
    Shared(other : Shared<T>#);
    Shared(other : const Weak<T>&) throws(BadWeakReferenceException);
    ~Shared();

    operator =(other : const Shared<T>&) : Shared<T>&;
    operator =(other : Shared<T>#) : Shared<T>&;
    operator =(ptr : T!) : Shared<T>&;

    acquire(ptr : T!);
    share(other : const Shared<T>&);
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
`T` const: `const Shared<T>` still returns `T*`.

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
    ~Weak();

    operator =(other : const Shared<T>&) : Weak<T>&;
    operator =(other : const Weak<T>&) : Weak<T>&;
    operator =(other : Weak<T>#) : Weak<T>&;

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
| Copy construction or assignment | Observe the same group, including when it has already expired. |
| Drain construction or assignment | Transfer the observation and empty the source. Self-draining is a no-op. |
| `useCount()` | Return a snapshot of the strong count, or zero if empty. |
| `expired()` | Return true if empty or if the strong count is zero. |
| `lock()` | Atomically return a new strong handle if the object is alive; otherwise return an empty `Shared<T>`. Failure is a normal result and does not throw. |
| `reset()` | Release the observation and leave an empty handle. |
| `swap(other)` | Exchange observations without changing reference counts. |

`Weak<T>` deliberately has no raw-object accessor, boolean conversion, or
dereference operation. Access must first acquire a strong handle through
`lock()` or the checked `Shared<T>` constructor.

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

## Complexity and scope

Handle bookkeeping is O(1); atomic promotion may retry under contention.
Copying, draining, swapping and locking do not allocate. Acquiring a non-null
owner allocates ownership metadata and may throw `OutOfMemory`.
Reference counts must fit in a positive `int`; exhausting that range is
diagnosed by the runtime and terminates execution rather than wrapping.

Custom deleters, `enable_shared_from_this` equivalents, aliasing constructors,
array specialisations and conversions between different template arguments
are not provided.

## Migration from the intrusive implementation

The existing `acquire()`, `share()`, `get()`, `isNull()`, `useCount()` and
`reset()` methods remain available. `share()` now accepts a const source and
clears its destination when the source is empty, matching copy assignment.
Acquiring a null owner produces an empty handle without allocating metadata.

The handle representation has changed. Applications and libraries using
`Shared<T>` must be rebuilt together; the old and new representations are not
ABI-compatible.

## See also

- [Memory primitives](memory.md)
- [Exception types](exceptions.md)
- [Ownership and indirection types](../language/basic/types.md)
