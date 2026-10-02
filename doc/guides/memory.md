# Memory, Storage, and Ownership in K

K combines values with block-based lifetimes, six indirection forms, explicit
heap allocation, and standard-library handles for shared ownership and
uninitialized storage. This guide is a practical map for choosing between
those tools and keeping object lifetimes clear.

For normative type and allocation rules, see [Indirection Types](../spec/language/basic/types.md),
[Dynamic Allocation](../spec/language/memory/new-delete.md),
[Memory Primitives](../spec/stdlib/memory.md), and
[Shared and Weak](../spec/stdlib/shared.md).

## 1. Where values live

| Declaration or operation | Storage and lifetime | Cleanup |
|--------------------------|----------------------|---------|
| Local value, e.g. `item : Item;` | Automatic storage for the enclosing block; lifetime starts at its declaration and ends when control leaves that scope. | Destructible values are destroyed in reverse declaration order, including during exception unwinding. |
| Function parameter | Lives for the duration of the call. An owner parameter receives transferred ownership; observers do not. | Destructible parameter values are cleaned up when the call exits. |
| Namespace/global variable | Shared program-level storage for the module variable. It is not a fresh value per function call. | Do not depend on initialization or destruction order across globals or modules unless your program establishes it explicitly. |
| Static member variable | One variable associated with its aggregate, shared by all instances. It is not stored in each instance. | As for other program-level state, do not assume cross-module initialization or shutdown ordering. |
| Static local variable | Persists across calls; its initializer is evaluated the first time execution reaches the declaration. This is different from an ordinary local. | Do not treat it as block-lifetime storage. Avoid relying on undocumented initialization-thread-safety or shutdown-order guarantees. |
| `new T(args)` | Heap allocation plus construction; the result is a nullable `T!` owner. | Explicit `delete`, transfer to another owner, or automatic owner cleanup at scope exit. |
| `UniSlot<T>` / `MultiSlot<T>` | Explicit raw storage whose allocation and contained-object lifetimes are controlled separately. | The caller must track and destroy constructed objects; `MultiSlot` releases its raw buffer at scope exit. |

Ordinary local values do **not** have C++-style static-local duration. An
ordinary local object is cleaned up when its block exits. K also supports
explicit `static` local declarations, which persist across calls; use that
feature only when that longer lifetime is intended.

Global and static-member state is shared state, not thread-local state. The
language reference currently documents no source-level thread-local storage
specifier. Runtime implementation details such as TLS used internally by the
exception machinery do not make user `static` variables thread-local.

For automatic locals and owners, cleanup follows scope exit and exception
unwinding. The language reference does not promise a universal ordering for
initialization or destruction of globals across modules; avoid global
initialization dependencies where possible.

## 2. The six addressers

An *addresser* describes how a value is reached. The four observer addressers
do not own the object. `!` owns a dynamic allocation, while `#` grants a
consumer permission to drain resources from an existing object.

| Type suffix | Name | Nullable? | Binding can be reassigned? | Owns object? | Typical use |
|-------------|------|-----------|-----------------------------|--------------|-------------|
| `T&` | Reference | No | No | No | Required borrowed argument or alias to a value. |
| `T+` | Link | No | Yes | No | Rebindable, non-null observer. |
| `T?` | View | Yes | No | No | Nullable observer whose binding does not change. |
| `T*` | Pointer | Yes | Yes | No | Nullable, rebindable observer. |
| `T!` | Owner | Yes | Yes, by move | Yes | Sole lifetime owner of a dynamically allocated object. |
| `T#` | Drain | No | No | No | Non-null argument that may be consumed by the callee. |

Binding mutability and pointee constness are separate. A non-rebindable
`T&`/`T?` binding does not make a mutable `T` immutable. To prevent mutation
through an observer, qualify the pointee (`const T&`, `const T*`, etc.). A
`const Shared<T>` handle is also not a const pointee:
`Shared<T>::get()` returns `T*` even when the handle itself is const. Bind
that pointer to a `const T*` or `const T&` when access must be read-only.

### 2.1 Forming, borrowing, and dereferencing

`&` has two roles in the syntax: as a type suffix it declares a reference, and
as the unary address-of operator `&value` it forms a non-null `T+` link to an
addressable value. It does not allocate or transfer ownership.

```k
struct Counter {
    value : int;
    Counter(v : int) : value(v) {}
}

read(counter : const Counter&) : int {
    return counter.value;
}

example() : int {
    counter : Counter(10);
    reference : Counter& = counter;
    link : Counter+ = &counter;
    nullableView : Counter? = &counter;
    nullablePointer : Counter* = &counter;

    link->value = 11;
    nullablePointer->value = 12;
    return read(reference) + nullableView->value; // 24
}
```

`*observer` dereferences a pointer, view, link, or owner and yields access to
the pointed-to object. Dereferencing nullable `T?` or `T*` inserts a runtime
null check; dereferencing non-null `T+` needs no null check. `->member` is the
member-access shorthand and performs the corresponding null check for nullable
observers. A reference is already transparent access to its object; it is not
dereferenced with `*`.

Typical binding rules:

- `&value` produces `T+`; it may widen to `T*` or `T?`.
- A reference must bind to an addressable lvalue and cannot be rebound.
- A link must be non-null; binding or rebinding it from a nullable source
  causes a runtime null check.
- A view is nullable but its binding cannot be changed after initialization.
- A pointer is nullable and can be rebound.
- An observer copied from an owner borrows the address; the owner remains
  responsible for deletion.

Pointee constness is enforced independently of whether the cast is implicit or
explicit. An explicit checked cast must not remove `const` from the pointed-to
type:

```k
module invalid_const_cast_example;

invalidConstCast() : void {
    const value : int = 7;
    source : const int* = &value;
    mutableView : int* = (int*) source; // ERROR: cast would discard pointee constness
}
```

Do not confuse a `const` handle binding with a const pointee. For example,
`const Shared<Record>` still returns `Record*` from `get()`; use a
`const Record*` or `const Record&` observer when the object must only be read.

### 2.2 Use `#` only when a consumer may drain

`#` is both the drain type suffix (`T#`) and the prefix drain expression
`#value`. A reference cannot implicitly become a drain: the call site must
use `#` to make the permission explicit.
Draining is not a blanket bitwise move and does not promise that every source
object becomes empty. The consumer's type-specific constructor or operation
determines what is transferred and what valid state remains in the source.

Examples of distinct postconditions:

- Moving an owner through assignment or an owner parameter transfers the
  allocation and sets the source owner to `null`. This is the owner's move
  behavior, not a promise that every `#` consumer empties its source.
- A successful drain of a `Shared<T>` handle transfers that handle and leaves
  the source handle empty.
- A mismatched checked converting drain of `Shared<U>` does not consume the
  source and leaves the destination empty.
- Draining a primitive is permitted but just copies the scalar.
- A user-defined drain constructor must leave the source object valid if it
  changes that object's state; the compiler does not reset arbitrary fields.

```k
struct Buffer {
    size : int;
    Buffer(n : int) : size(n) {}
    Buffer(other : Buffer#) : size(other.size) {
        other.size = 0;
    }
}

consume(buffer : Buffer#) : int {
    moved : Buffer(#buffer);
    return moved.size;
}

exampleDrain() : int {
    original : Buffer(7);
    return consume(#original); // Explicit permission to consume.
}
```

This is a **do not compile** example: passing a reference-like lvalue without
the explicit drain operator does not grant drain permission.

```k
module invalid_drain_example;

struct DrainBuffer {
    value : int;
    DrainBuffer(v : int) : value(v) {}
    DrainBuffer(other : DrainBuffer#) : value(other.value) {
        other.value = 0;
    }
}

consume(buffer : DrainBuffer#) : int {
    moved : DrainBuffer(#buffer);
    return moved.value;
}

invalidDrain() : int {
    original : DrainBuffer(7);
    return consume(original); // ERROR: reference-to-drain conversion is not implicit
}
```

## 3. Dynamic allocation and owners

`new T(args)` allocates and constructs one `T`, returning `T!`. The owner is
nullable, move-only, and solely responsible for the dynamic object's lifetime.
When a non-null owner leaves scope, K destroys the object and frees its
allocation. Exception unwinding performs the same cleanup. `delete owner`
performs that cleanup immediately and sets the owner to `null`; deleting a
null owner is a no-op.

```k
class Resource {
    public value : int;
    Resource(v : int) : value(v) {}
    ~Resource() {}
}

take(resource : Resource!) : int {
    return resource->value;
} // The parameter still owns and cleans up the resource here.

exampleOwner() : int {
    first : Resource! = new Resource(21);
    borrowed : Resource* = first;

    result : int = take(first); // Ownership moves into the parameter.
    // first is null; borrowed must no longer be used.
    return result;
}
```

Use an owner return type when ownership is intended to leave a function:

```k
makeResource(value : int) : Resource! {
    return new Resource(value);
}

useFactory() : int {
    resource : Resource! = makeResource(42);
    return resource->value;
}
```

An owner may be transferred to an owner parameter or return value. Assigning
one owner to another moves it and nulls the source; replacing a non-null
destination first releases its previous object. A pointer, link, view, or
reference made from an owner is only a borrow. It must not be used after the
managed object is destroyed. Moving an owner does not itself invalidate a
borrow, but its validity now depends on the new owner's lifetime; the emptied
source owner no longer protects it. K does not perform general borrow escape
analysis, so dangling observers remain a programmer error.

Never create a second owner from a borrowed address. In particular, do not
cast a `T*`/`T+`/`T?` into `T!` to make another owner: owner values are produced
by allocation or transferred from another owner. Duplicating ownership can
cause double destruction and invalid frees.

For class/interface objects, deleting through a base or interface owner uses
virtual destruction and reaches the allocated object's dynamic type, even
after an owner upcast has adjusted its stored view. A dynamic owner downcast is
different from a nullable observer cast: on mismatch, the source owner is
consumed and its object is deleted, while the destination remains null. Avoid
owner downcasts unless that destructive failure behavior is intended.

## 4. Shared ownership: `Shared<T>` and `Weak<T>`

Use `Shared<T>` when independently held handles must keep one dynamically
allocated class object alive together. Its converting constructors and
assignments can hold class/interface views; conversion changes the typed view,
not the shared ownership group or the original-type destruction callback.

Create a group by adopting an owner:

```k
class Record {
    public id : int;
    Record(value : int) : id(value) {}
}

exampleShared() : int {
    owner : Record! = new Record(42);
    first : Shared<Record>(owner); // Adopts; owner becomes null.
    second : Shared<Record>(first); // Retains the same object.
    weak : Weak<Record>(first);     // Observes without keeping Record alive.

    moved : Shared<Record>(#second); // Transfers this handle; second is empty.
    if (weak.expired()) {
        return -1;
    }
    first.reset();
    return moved.get()->id;
}
```

An empty default-constructed `Shared<T>` allocates no control block.
`Shared<T>(owner)` adopts the owner; a null owner produces an empty handle.
Copying a `Shared<T>` retains the same object. Draining a same-type handle
transfers that handle and empties the source.

### 4.1 Checked class and interface conversions

Construction or assignment from `Shared<U>` first adapts the source's typed
view. Compatible static upcasts are adjusted as needed; supported checked
class/interface conversions use RTTI and can cross-cast between sibling
interfaces. A successful conversion shares the original control block. A
mismatch produces an empty destination. There are no separate public
`cast<T>()` helper functions: use the converting constructor or assignment.

```k
interface Readable {
    read() : int;
}
interface Writable {
    write(value : int) : int;
}

class Buffer : public Readable, public Writable {
    public Buffer() {}
    public read() : int { return 1; }
    public write(value : int) : int { return value; }
}
class ReadOnly : public Readable {
    public ReadOnly() {}
    public read() : int { return 2; }
}

exampleSharedCast() : int {
    readable : Shared<Readable>(new Buffer());
    writable : Shared<Writable>(readable); // Successful sibling-interface cast.
    if (!writable) {
        return -1;
    }
    return writable.get()->write(3);
}
```

For a non-matching dynamic type, the converting constructor produces an empty
handle rather than throwing:

```k
exampleSharedMismatch() : bool {
    readable : Shared<Readable>(new ReadOnly());
    writable : Shared<Writable>(readable);
    return !writable; // true: ReadOnly has no Writable subobject.
}
```

Weak conversions use the same checked class/interface rules. Converting a
dynamic weak view temporarily promotes it while computing the adjusted view;
constructing a strong handle from a weak handle can throw if it is empty or
expired:

```k
exampleWeakCast(source : const Weak<Readable>&) : int
    throws(BadWeakReferenceException) {
    writable : Weak<Writable>(source);
    strong : Shared<Writable>(source);
    if (!strong) {
        return 0; // Live object, but no matching Writable subobject.
    }
    return strong.get()->write(1);
}
```

The same checked adaptation applies to converting copy assignment. A mismatch
clears the destination but leaves the source handle unchanged. A converting
drain consumes the source only when adaptation succeeds; on mismatch it clears
the destination and preserves the source. This distinction is useful when the
source must remain available after a failed cast.

### 4.2 Weak references and promotion

`Weak<T>` observes the shared control block without extending the object's
lifetime. A weak handle cannot be dereferenced or used to obtain a raw object
pointer. Use `lock()` for a race-safe, non-throwing promotion attempt:

```k
exampleWeakPromotion(weak : const Weak<Record>&) : int {
    locked : Shared<Record> = weak.lock();
    if (locked) {
        // The object remains alive while locked is a strong handle.
        return locked.get()->id;
    }
    return 0; // Empty or expired.
}
```

Checking `weak.expired()` and then assuming the object is still alive is
unsafe: another thread may release the final strong handle immediately after
the check. Use the result of `lock()` instead. Constructing `Shared<T>` from
an empty or expired `Weak<T>` throws `BadWeakReferenceException`; `lock()`
returns empty instead.

Converting `Weak<U>` to `Weak<T>` uses the same type adaptation rules. A
dynamic weak conversion temporarily promotes the source while computing the
adjusted view; an expired object is never inspected. A live mismatch gives an
empty converted weak handle. Promoting a live but type-mismatched weak view to
`Shared<T>` yields empty; an empty or expired input follows the throwing
`BadWeakReferenceException` contract.

Use weak back-references to break ownership cycles. A cycle made only of
strong `Shared` handles is not collected automatically:

```text
Parent ──Shared──► Child
   ▲                 │
   └──────Weak───────┘
```

### 4.3 Concurrency and constness

Strong and weak reference counts, and weak-to-strong promotion, are atomic.
Operations on distinct handles can run concurrently. Concurrent mutation of
one handle itself requires external synchronization, as does concurrent access
to mutable data in the managed object. Atomic ownership does not make the
object's fields thread-safe.

`useCount()` reports an atomic snapshot, not a lock or an exclusivity
guarantee. `useCount() == 1` does not establish that no other thread can
acquire a handle. Similarly, `expired()` is not a substitute for `lock()`.
The last strong handle destroys the object on whichever thread releases it;
the destructor must not throw.

## 5. Explicit storage: `UniSlot<T>` and `MultiSlot<T>`

**Use `UniSlot<T>` and `MultiSlot<T>` with caution, and only to implement
specialized memory containers.** They are not recommended for ordinary
application-level memory management. Prefer normal values, owners, existing
containers, or shared ownership unless you are implementing a container that
requires explicit control over storage and object lifetimes.

Slots separate storage for an object from the object's constructed lifetime.
They are low-level building blocks, not smart pointers or automatic-lifetime
wrappers for the contained `T` values. `MultiSlot` releases its raw buffer at
scope exit, but neither slot type automatically destroys contained values.

| Type | Storage | Construction API | Destruction API | Does its own destructor clean up `T`? |
|------|---------|------------------|-----------------|----------------------------------------|
| `UniSlot<T>` | Embedded storage for one `T`; no separate heap allocation. | `construct(args...)` | `destruct()` | No. |
| `MultiSlot<T>` | Heap buffer with capacity for multiple `T` values. | `construct(index, args...)` | `destruct(index)` | No. |

### 5.1 `UniSlot<T>`: one manually managed object

`UniSlot<T>` embeds the storage inside the slot. Its constructor prepares that
storage but does not construct a live `T`; its destructor is a no-op with
respect to the contained `T`. There is no occupancy flag, so track the state
yourself.

```k
struct Point {
    x : int;
    y : int;
    Point(px : int, py : int) : x(px), y(py) {}
    ~Point() {}
}

exampleUniSlot() : int {
    slot : UniSlot<Point>;
    slot.construct<int, int>(3, 4);
    result : int = slot.get().x + slot.get().y;
    slot.destruct();
    return result;
}
```

Only call `get()` after a successful construction, and call `destruct()` once
for each live object. Constructing twice without destroying, or destroying
unconstructed storage, is undefined behavior. Leaving scope does not invoke
the contained object's destructor for you.

If `T`'s constructor throws a checked exception, the intrinsic translates it
to `ConstructionException` (a `FatalError`); other fatal errors propagate.
Handle any construction failure before treating the slot as occupied.

### 5.2 `MultiSlot<T>`: manually managed elements in a buffer

`MultiSlot<T>` holds a heap buffer and a capacity. Use `allocate(capacity)` to
obtain storage, explicitly construct each occupied index, destroy every live
element, then call `deallocate()` if you want to release the buffer before
scope exit. Call `allocate()` only when the slot has no buffer (initially or
after `deallocate()`); it does not first release an existing allocation.
`getCapacity()` does not tell you how many elements are constructed; keep that
state separately.

```k
struct Item {
    value : int;
    Item(v : int) : value(v) {}
    ~Item() {}
}

exampleMultiSlot() : int {
    slots : MultiSlot<Item>;
    slots.allocate(2);
    slots.construct(0, 10);
    slots.construct(1, 20);

    result : int = slots.get(0).value + slots.get(1).value;
    slots.destruct(1);
    slots.destruct(0);
    slots.deallocate();
    return result;
}
```

`MultiSlot`'s own destructor frees its backing buffer, but does not destroy
any live elements. Destroy every constructed element before calling
`deallocate()` or leaving scope, or element destructors will be skipped.
`deallocate()` frees the buffer and resets its pointer and capacity to empty.
An out-of-range index, `get()` on unconstructed storage, duplicate
construction, or destruction of an unconstructed element is undefined
behavior. Allocation failure is reported as `OutOfMemory`.

`reallocate(newCapacity)` changes the buffer allocation and preserves raw
memory contents where the allocator does. It does not track object occupancy
or perform element moves/destruction; do not shrink past live elements or
assume that raw-byte preservation alone proves an object remains valid after
relocation. Maintain the live-index set yourself.

A `MultiSlot` may be drained to another `MultiSlot<T>`; this transfers the
buffer and capacity and leaves the source empty. It does not walk the buffer
or destroy its elements during transfer. Move assignment first frees the
destination's existing raw buffer, so destroy its live elements before
assigning into it. The destination inherits the source's obligation to destroy
all live values before releasing the buffer.

Prefer a normal local value when one value is enough and ordinary block
cleanup is desired. Use `new` plus an owner for one independently allocated
object, `Shared<T>` when ownership must be shared, and slots only when a
specialized memory container requires explicit control over object
construction and destruction. The examples above illustrate that low-level
implementation work, not a recommended pattern for ordinary application code.

## 6. Choosing the right relationship

| Need | Choose | What to watch |
|------|--------|---------------|
| Required, non-null borrow that cannot be rebound | `T&` | The referent must outlive the reference. |
| Required, non-null observer that can be rebound | `T+` | Null sources are checked; it still does not own. |
| Optional borrow with a fixed binding | `T?` | Check or safely handle null before access. |
| Optional observer that can be rebound | `T*` | Check for null; do not let it outlive the owner. |
| One owner for a heap object | `T!` | Transfers on assignment/parameter passing; scope exit cleans up. |
| Shared lifetime | `Shared<T>` | Break cycles with `Weak<T>`; synchronize object access separately. |
| Observe a shared object without extending its lifetime | `Weak<T>` | Use `lock()` and keep the returned strong handle while accessing. |
| Implementing a specialized memory container with manually controlled object states | `UniSlot<T>` / `MultiSlot<T>` | Use with caution and only for this low-level purpose; track construction explicitly, since slot destruction does not clean up stored values. |
| Transfer the resources of a particular value to a consumer | `T#` and `#value` | The consumer defines the source's postcondition; it may not become empty. |

Common failure modes are dangling observers after owner scope exit, two
independent owners for one allocation, forgetting manual slot destruction,
assuming `Shared` makes object state thread-safe, and strong-reference cycles
that keep one another alive.

## See also

- [Language types and conversions](../spec/language/basic/types.md)
- [Dynamic allocation and owners](../spec/language/memory/new-delete.md)
- [UniSlot and MultiSlot API](../spec/stdlib/memory.md)
- [Shared and Weak API](../spec/stdlib/shared.md)
- [File system guide](file-system.md)
- [Date and time guide](time.md)
