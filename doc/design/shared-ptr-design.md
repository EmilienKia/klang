# Shared<T> — Feasibility Study and Design Proposal

> **Date:** 2026-05-17  
> **Status:** Proposal  
> **Target:** `libk/libk/src/shared.k`

---

## 1. Feasibility Study

### 1.1 Objective

Implement a `Shared<T>` template class in the K standard library that provides
reference-counted shared ownership semantics, equivalent to C++ `std::shared_ptr<T>`.

### 1.2 Core Requirements

| # | Requirement | Feasibility |
|---|-------------|-------------|
| 1 | Multiple `Shared<T>` instances share ownership of one T object | ✓ (see §1.3) |
| 2 | Null/empty constructor | ✓ trivial |
| 3 | From-owner constructor (drains a `T!`) | ✓ trivial |
| 4 | Copy constructor (share with existing `Shared<T>`) | ✓ (see §1.3) |
| 5 | Destructor: decrement count, free if last | ✓ (see §1.3) |
| 6 | `get()` method returning `T&` or `T*` | ✓ trivial |
| 7 | No `@Intrinsic`, no compiler-specific code | ✓ (see §1.3) |
| 8 | Dereference operator (`operator *`) | ⚠ Currently, only binary/unary prefix `*` is in the overloadable set, but it takes 0 params for unary member — **depends on compiler support for unary `*`** (see §1.5) |

### 1.3 The Ownership Challenge

K's `T!` owner type enforces **exclusive** ownership with automatic deletion on scope
exit. This creates a fundamental tension with shared ownership:

- **Moving** a `T!` transfers ownership (source ← null).
- **Assigning null** to a `T!` **destroys** the pointed-to object.
- There is **no way** to "abandon" a `T!` without destroying the object.

Therefore, if a struct holds `_ptr : T!` and that struct is destroyed while T must
remain alive (because other `Shared<T>` instances still reference it), the K runtime
will auto-delete T — breaking the shared semantics.

### 1.4 Feasible Approaches

#### Approach A: Intrusive Doubly-Linked List (Pure K) ✅ RECOMMENDED

All `Shared<T>` instances forming a sharing group are linked in a doubly-linked
list. Exactly **one** node in the chain holds the `SharedBlock<T>!` owner
(which in turn owns the managed `T!`). On destruction:

- If the dying node holds the `!` owner AND has neighbors: **move** ownership to
  a neighbor before unlinking.
- If the dying node is the **last** in the chain (no neighbors): **delete** the
  block, which auto-deletes T.
- If the dying node does NOT hold the owner: just unlink.

**Pros:**
- Pure K. No FFI helper file needed for the shared mechanism itself.
- No runtime allocation for a control block — the chain IS the sharing metadata.
- Proven pattern (similar to Boost's `intrusive_ptr` with linked-list policy).
- Works within K's ownership rules: ownership transfers between nodes.

**Cons:**
- Each `Shared<T>` instance costs 2 extra pointers (`_prev`, `_next`) = 16 bytes overhead.
- Slightly more complex destructor/copy logic.
- Thread-unsafe (acceptable for Phase 1 — same as `std::shared_ptr` in single-threaded C++).

#### Approach B: FFI Control Block (C Helper)

A small C helper file (`shared.c`) provides `malloc`/`free` for a control block
holding a `{int refCount}`. The managed T is still a `T!` in K, but the control
block's refcount determines WHEN to delete.

**Fundamental problem:** The Shared that originally holds `T!` may be destroyed
before others. At that point, K auto-deletes T (because `T!` goes out of scope).
There is no way to prevent this without ALSO using the linked-list trick to transfer
`T!` ownership between Shared instances.

**Conclusion:** Approach B alone doesn't solve the problem. It would still need the
ownership-transfer mechanism from Approach A, making the FFI refcount redundant
(the linked-list already implicitly tracks "count" via list membership).

#### Approach C: Control Block WITH T! Inside (FFI + linked list hybrid)

Use a K struct `SharedBlock<T>` that holds `_ptr: T!`, allocated with `new`. One
Shared owns the block (`SharedBlock<T>!`); others observe it (`SharedBlock<T>*`).
Ownership of the block is transferred via linked-list on destruction.

This is essentially Approach A with a named "block" struct. Slightly cleaner
separation of concerns: the block encapsulates T ownership, the chain manages
block ownership.

### 1.5 Dereference Operator Feasibility

From the K spec, overloadable unary operators include: `+` `-` `~` `!` `++_` `--_`
`_++` `_--`. The unary `*` (dereference) is listed in §8.3 as "produces `T&`" for
indirection types. However, it is **not** in the overloadable operator list (§10.7).

**Conclusion:** The dereference operator `*` is NOT currently overloadable in K.
We will provide `get()` as the primary accessor. A future language extension could
add `operator *` support for smart pointers.

### 1.6 Language Features Required (All Already Implemented)

| Feature | Status | Used For |
|---------|--------|----------|
| Templates | ✓ | `Shared<T>` parametric type |
| Struct with constructor/destructor | ✓ | Lifecycle management |
| Owner (`T!`) fields | ✓ | Block owns T |
| Pointer (`T*`) fields | ✓ | Observer references |
| Self-referencing pointers (`Shared<T>*`) | ✓ | Linked-list nodes (same pattern as `LinkedListNode`) |
| `operator =` overloading | ✓ | Assignment semantics |
| `new` / `delete` | ✓ | Block allocation |
| `&this` (address-of this) | ⚠ | Needs verification — used to get `Shared<T>*` to self |
| Mutable parameter (`T+`) | ✓ | Copy constructor mutates source's linked-list |
| `return this` | ✓ | Assignment operator returns reference |

### 1.7 Risk: `&this`

The expression `&this` is not used anywhere in the current codebase. If the compiler
doesn't support it, we can work around it by passing the address explicitly:
- In the copy constructor, the source links us in and provides its `&`address.
- Alternatively, use a helper method that takes `self : Shared<T>+` as explicit param.

**Mitigation:** If `&this` doesn't work, use an explicit two-step initialization
pattern (construct empty, then call `linkTo(other)`).

---

## 2. Design Proposal

### 2.1 Data Layout

```k
template<typename T>
struct SharedBlock {
    _ptr : T!;        // owns the managed object
}

template<typename T>
struct Shared {
    private:
    _block : SharedBlock<T>!;    // non-null in exactly ONE node of the chain
    _obs   : T*;                 // cached observer (avoids null-check on _block)
    _prev  : Shared<T>*;         // intrusive doubly-linked list
    _next  : Shared<T>*;
}
```

**Memory footprint per `Shared<T>`:** 4 pointers = 32 bytes (64-bit).  
**SharedBlock:** 1 pointer = 8 bytes (allocated once per sharing group).

### 2.2 Invariants

1. If `_obs == null` → the Shared is empty (null state). `_block`, `_prev`, `_next` are all null.
2. If `_obs != null` → the Shared is active. `_obs` points to the managed T.
3. In any active sharing group, **exactly one** node has `_block != null` (the owner).
4. All active nodes form a doubly-linked list. The list may have 1..N nodes.
5. When the list has exactly 1 node, that node holds `_block != null` (it's both
   the sole user and the owner).

### 2.3 Public API

```k
template<typename T>
struct Shared {
    public:
    /** Create an empty (null) shared pointer. */
    Shared();

    /** Create a shared pointer from an owner. Drains ownership from ptr.
     *  After this call, ptr is null and the Shared owns the object. */
    Shared(ptr : T!);

    /** Create a shared pointer that shares ownership with other.
     *  Increments the sharing group by one. */
    Shared(other : Shared<T>+);

    /** Destructor. Decrements the sharing group. If this is the last
     *  active Shared, destroys the managed object. */
    ~Shared();

    /** Return a pointer to the managed object (null if empty). */
    const get() : T* ;

    /** Return true if this Shared is empty (manages no object). */
    const isNull() : bool;

    /** Return the number of Shared instances sharing this object.
     *  Returns 0 if empty. O(n) traversal. */
    const useCount() : int;

    /** Release this Shared's participation in the group. After this call,
     *  the Shared is empty. If it was the last reference, T is destroyed. */
    reset();

    /** Replace the managed object. Equivalent to reset() + construct from owner. */
    reset(ptr : T!);

    /** Assignment: share with the source. Releases current, joins source's group. */
    operator =(other : Shared<T>+) : Shared<T>&;
}
```

### 2.4 Constructor/Destructor Semantics

#### `Shared()` — Null constructor
```k
Shared() {
    _block = null;
    _obs = null;
    _prev = null;
    _next = null;
}
```

#### `Shared(ptr : T!)` — From-owner constructor
```k
Shared(ptr : T!) {
    _obs = ptr;                     // observer copy BEFORE move
    _block = new SharedBlock<T>();
    _block._ptr = ptr;              // move T! into block
    _prev = null;
    _next = null;
}
```

#### `Shared(other : Shared<T>+)` — Copy/share constructor
```k
Shared(other : Shared<T>+) {
    _obs = other._obs;
    _block = null;                   // not the owner of the block
    // Insert self AFTER other in the chain
    _prev = &other;                  // link to other
    _next = other._next;
    if (_next != null) {
        _next->_prev = &this;        // update old next's prev
    }
    other._next = &this;             // other now points to us
}
```

#### `~Shared()` — Destructor
```k
~Shared() {
    if (_obs == null) return;        // empty, nothing to do

    // Unlink self from the chain
    if (_prev != null) {
        _prev->_next = _next;
    }
    if (_next != null) {
        _next->_prev = _prev;
    }

    // Transfer block ownership if needed
    if (_block != null) {
        if (_next != null) {
            _next->_block = _block;  // move owner to next neighbor
        } else if (_prev != null) {
            _prev->_block = _block;  // move owner to prev neighbor
        } else {
            // No neighbors — we are the last. Delete the block (and T).
            delete _block;
        }
    }
}
```

### 2.5 Assignment Operator

```k
operator =(other : Shared<T>+) : Shared<T>& {
    // Bail out if self-assignment (same group, same T)
    if (_obs == other._obs) return this;

    // Release current participation
    reset();

    // Join other's group
    _obs = other._obs;
    _prev = &other;
    _next = other._next;
    if (_next != null) {
        _next->_prev = &this;
    }
    other._next = &this;

    return this;
}
```

### 2.6 Accessor Methods

```k
const get() : T* {
    return _obs;
}

const isNull() : bool {
    return _obs == null;
}

const useCount() : int {
    if (_obs == null) return 0;
    count : int = 1;
    // Walk backward
    p : Shared<T>* = _prev;
    while (p != null) {
        count = count + 1;
        p = p->_prev;
    }
    // Walk forward
    n : Shared<T>* = _next;
    while (n != null) {
        count = count + 1;
        n = n->_next;
    }
    return count;
}
```

### 2.7 `reset()` Method

```k
reset() {
    if (_obs == null) return;

    // Unlink
    if (_prev != null) {
        _prev->_next = _next;
    }
    if (_next != null) {
        _next->_prev = _prev;
    }

    // Transfer block ownership
    if (_block != null) {
        if (_next != null) {
            _next->_block = _block;
        } else if (_prev != null) {
            _prev->_block = _block;
        } else {
            delete _block;
        }
    }

    // Clear self
    _block = null;
    _obs = null;
    _prev = null;
    _next = null;
}

reset(ptr : T!) {
    reset();
    _obs = ptr;
    _block = new SharedBlock<T>();
    _block._ptr = ptr;
}
```

---

## 3. Potential Issues and Mitigations

### 3.1 `&this` Support

If the compiler doesn't support `&this`, we can restructure using a helper:

```k
private linkAfter(other : Shared<T>+, self : Shared<T>+) {
    _prev = &other;
    _next = other._next;
    if (_next != null) {
        _next->_prev = &self;
    }
    other._next = &self;
}
```

The caller would provide `&variable` explicitly. But in constructors, `this` is the
only way to get a pointer to self. **This is the primary technical risk.**

Fallback: if `&this` isn't supported, we could use a two-phase approach where
linking requires calling a method post-construction that takes an explicit `Shared<T>+` self.

### 3.2 `_block = null` Destroys T?

Setting `_block = null` on a `SharedBlock<T>!` field will auto-destroy the block
(and thus T). However, in our design, `_block` is null in ALL copies except one.
When we write `_block = null` in `reset()`, the `_block` was already null for non-owner
nodes, so it's a no-op. For the owner node, we only set `_block = null` AFTER
transferring ownership (`_next->_block = _block` which is a MOVE, setting source to null
without destroy).

Actually: `_next->_block = _block` is an owner-to-owner assignment = MOVE. The source
(`_block`) becomes null after the move. The destination gets ownership. Then when we
set `_block = null` afterward, it's already null. ✓ Correct.

### 3.3 Move Semantics Through Pointer Dereference

`_next->_block = _block` assigns through a pointer dereference (`->`) to a `T!` field.
This should trigger K's owner move semantics. This pattern is used in `LinkedList<T>`
(e.g., `prev->_next = node;` where `_next` is a `LinkedListNode!`). ✓ Confirmed by
existing code.

### 3.4 Thread Safety

Phase 1 is explicitly single-threaded. The linked-list approach is NOT thread-safe.
A future `AtomicShared<T>` could use an FFI-based atomic refcount combined with
a mutex-protected linked list, or a lock-free design.

### 3.5 Self-Referencing Template

`Shared<T>` contains `_prev : Shared<T>*` and `_next : Shared<T>*` — this is a
self-referencing template struct. The pattern is already proven with `LinkedListNode`
which has `_next : LinkedListNode!`. ✓ Supported.

---

## 4. Implementation Plan

### Phase 1: Core Implementation

| Step | Description | Files |
|------|-------------|-------|
| 1 | Verify `&this` works (write a minimal test) | `klang/tests/test-gen-*.cpp` |
| 2 | Create `shared.k` with `SharedBlock<T>` + `Shared<T>` | `libk/libk/src/shared.k` |
| 3 | Implement null + from-owner constructors | `shared.k` |
| 4 | Implement copy constructor (with linked-list insertion) | `shared.k` |
| 5 | Implement destructor (unlink + ownership transfer) | `shared.k` |
| 6 | Implement `get()`, `isNull()` | `shared.k` |
| 7 | Implement `reset()` (both overloads) | `shared.k` |
| 8 | Implement `operator =` | `shared.k` |
| 9 | Implement `useCount()` | `shared.k` |
| 10 | Register `shared.k` in `libk/libk/CMakeLists.txt` | `CMakeLists.txt` |

### Phase 2: Tests

| Step | Description | Files |
|------|-------------|-------|
| 11 | Create test file | `libk/libk/tests/test-shared.cpp` |
| 12 | Register test in CMakeLists.txt | `libk/libk/CMakeLists.txt` |

### Phase 3 (Future): Enhancements

| Step | Description |
|------|-------------|
| A | `WeakShared<T>` (non-owning observer with validity check) |
| B | `makeShared<T>(args...)` factory function |
| C | Language-level `operator *` support for smart pointers |
| D | Thread-safe variant |

---

## 5. Test Plan

### 5.1 Unit Tests (JIT-based)

All tests via `jit_k()` helper (same pattern as `test-list.cpp`).

| # | Test Case | Assertion |
|---|-----------|-----------|
| 1 | `Shared<int>` null constructor → `isNull() == true` | `get() == null` |
| 2 | `Shared<int>` from owner → `isNull() == false`, `*get() == value` | Correct value |
| 3 | Copy constructor → `useCount() == 2` | Both get() return same address |
| 4 | Destructor of copy → `useCount()` back to 1 | Object still alive |
| 5 | Destructor of last → object destroyed (test via side-effect) | Side-effect triggered |
| 6 | `reset()` on middle of chain → chain integrity preserved | Others still valid |
| 7 | `reset(newPtr)` → old object freed if last, new object managed | Correct values |
| 8 | `operator =` → proper release of old + join new group | Correct counts |
| 9 | Self-assignment → no-op | No crash, same state |
| 10 | Multiple copies (3+) → proper chain management | useCount() == N |
| 11 | Original creator destroyed first → object survives in copies | Copies still valid |
| 12 | Last copy destroyed → object destroyed | Destructor called |

### 5.2 End-to-End Tests (build_and_exec)

| # | Test Case | Validation |
|---|-----------|------------|
| 13 | Shared<T> with a class type (test destructor side-effect) | Exit code encodes destructor call |
| 14 | Shared<T> passed as function parameter | Shared semantics preserved across call |
| 15 | Shared<T> returned from function | Shared semantics preserved |

### 5.3 Error/Edge Cases

| # | Test Case | Expected |
|---|-----------|----------|
| 16 | `get()` on null Shared | Returns null pointer |
| 17 | `reset()` on null Shared | No-op, no crash |
| 18 | Double reset | No crash |
| 19 | `useCount()` on null | Returns 0 |

---

## 6. Alternatives Considered and Rejected

| Alternative | Reason for Rejection |
|-------------|---------------------|
| FFI refcount only (no linked list) | Cannot solve the T! auto-delete problem |
| Single owner + observers (non-shared) | Not shared_ptr semantics |
| Generic (`generic<class T>`) instead of template | Generics require `class` constraint and can't hold `T` by value in the block; templates give more flexibility |
| Store T* (raw pointer, no ownership) | Would require manual deallocation from C, defeating the purpose |

---

## 7. Decision

**Recommended approach: Linked-list (Approach A), implemented as a `template<typename T> struct`.**

**Critical prerequisite:** Verify compiler support for `&this` (Step 1).

If `&this` is not supported, the fallback is to add a small C helper file
(`shared.c`) that provides alloc/free for a refcount-only control block, combined
with the ownership-transfer trick for the `T!` — effectively a hybrid of A and B.
But this should only be needed if the pure-K approach hits a compiler limitation.

