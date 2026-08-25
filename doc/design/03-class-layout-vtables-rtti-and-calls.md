# Class Layout, Vtables, RTTI & Call Synthesis

This document provides a comprehensive technical specification of **aggregate memory layouts**, **virtual method table (vtable) synthesis**, **Runtime Type Information (RTTI) structures**, and **call invocation mechanisms** in `klangc`.

---

## 1. Class Composition & Object Memory Layout

`klangc` models four aggregate categories under `k::model::aggregate`:

| Aggregate Kind | Keyword | Virtual Dispatch | RTTI | Default Member Visibility | Default Base Visibility |
|----------------|---------|------------------|------|---------------------------|-------------------------|
| `structure` | `struct` | No | No | `public` | `public` |
| `klass` | `class` | Yes | Yes (Slot 0) | `protected` (vars), `public` (fns) | `public` |
| `interface` | `interface`| Yes (Pure) | Yes (Slot 0) | `public` (pure methods) | `public` |
| `annotation_type` | `annotation`| No | Yes (Slot 0) | `public` | `public` |

---

### 1.1 Object Layout Models

#### 1. Plain Structures (`struct`)
Non-polymorphic. Contains only direct data members ordered by declaration sequence. No vptr is present.

```
struct Point {
    x : int;
    y : int;
}
```
**LLVM Struct Layout**: `%Point = type { i32, i32 }`

---

#### 2. Single Inheritance Classes (`class Derived : Base`)
Polymorphic classes carry a primary virtual pointer (`__vptr__`) at field offset 0. Base subobjects are inlined at the start of the layout, followed by derived class member fields.

```
class Animal {
    age : int;
    virtual speak();
}

class Dog : Animal {
    breed_id : int;
    override speak();
}
```

```
┌────────────────────────────────────────────────────────┐
│                   Dog Object Layout                    │
├─────────┬──────────────┬───────────────────────────────┤
│ Field 0 │ ptr __vptr__ │ Points to Dog Primary Vtable  │
│ Field 1 │ i32 age      │ Inherited from Animal         │
│ Field 2 │ i32 breed_id │ Declared in Dog               │
└─────────┴──────────────┴───────────────────────────────┘
```
**LLVM Struct Layout**: `%Dog = type { ptr, i32, i32 }`

---

#### 3. Multiple & Interface Inheritance (`class C : B1, B2, I1`)
When a class inherits from multiple base classes or interfaces:
- The **primary base** (first base class with a vtable) shares the object's primary `__vptr__` at offset 0.
- Each **secondary base** or **interface** is embedded as a distinct subobject at a non-zero byte offset and maintains its own secondary vptr (`__vptr_<BaseName>__`).

```
interface Drawable {
    draw();
}

class Shape {
    id : int;
    virtual area() : double;
}

class Circle : Shape, Drawable {
    radius : double;
    override area() : double;
    override draw();
}
```

```
┌────────────────────────────────────────────────────────┐
│                  Circle Object Layout                  │
├─────────┬───────────────────┬──────────────────────────┤
│ Field 0 │ ptr __vptr__      │ Circle Primary Vtable    │
│ Field 1 │ i32 id            │ (Shape subobject)        │
├─────────┼───────────────────┼──────────────────────────┤
│ Field 2 │ ptr __vptr_Draw__ │ Circle-as-Drawable Vtable│
├─────────┼───────────────────┼──────────────────────────┤
│ Field 3 │ double radius     │ (Circle member field)    │
└─────────┴───────────────────┴──────────────────────────┘
```

When invoking `Drawable::draw()` on a `Circle&` reference, the reference points directly to `Field 2` (the `Drawable` subobject).

---

#### 4. Virtual & Diamond Inheritance (`compute_virtual_bases`)
When multiple inheritance paths converge on a shared base (e.g. `D : B, C` where both `B` and `C` inherit `A`), `klass::compute_virtual_bases()` detects the diamond pattern:
- The shared virtual base `A` is marked `is_virtual = true`.
- The virtual base subobject is placed once at the very end of the most-derived class layout, preventing duplicate state.

---

#### 5. Non-Static Nested Classes (Inner Classes)
Non-static nested classes declare an implicit reference to their enclosing class instance. `klangc` injects a synthetic member variable `__parent__` at field 0 (or field 1 after the vptr), populated during inner class construction.

---

## 2. Virtual Method Tables (Vtables)

### 2.1 Vtable Structure & Layout

Every polymorphic aggregate with `has_vtable() == true` generates a global vtable constant in LLVM IR:

```
@_KTVN6module3DogE = linkonce_odr constant { ptr, ptr, ptr } {
    ptr @_KTRN6module3DogE,          ; Slot -1 (Byte Offset 0): Pointer to RTTI Class Descriptor
    ptr @_KFDN6module3Dog8destructE, ; Slot 0  (Byte Offset 8): Universal Virtual Destructor
    ptr @_KFMvN6module3Dog5speakE    ; Slot 1  (Byte Offset 16): Dog::speak()
}
```

```
┌────────────────────────────────────────────────────────┐
│                     Vtable Layout                      │
├─────────┬──────────────┬───────────────────────────────┤
│ Slot -1 │ ptr RTTI     │ Pointer to RTTI Descriptor    │
├─────────┼──────────────┼───────────────────────────────┤
│ Slot 0  │ ptr dtor     │ Universal Virtual Destructor  │
├─────────┼──────────────┼───────────────────────────────┤
│ Slot 1  │ ptr method1  │ First virtual method          │
├─────────┼──────────────┼───────────────────────────────┤
│ Slot N  │ ptr methodN  │ N-th virtual method           │
└─────────┴──────────────┴───────────────────────────────┘
```

### 2.2 The Universal Destructor Slot (Slot 0)

In K, every polymorphic class and interface implicitly inherits `::k::Object`.
1. `::k::Object` seeds **Slot 0** with its virtual destructor (`~Object`).
2. Every derived class destructor automatically overrides Slot 0.
3. This guarantees that destroying an object through **any** base pointer or interface reference routes directly to the most-derived destructor without needing dynamic type inspection.

### 2.3 Secondary Vtables & Adjustor Thunks

When a derived class `Circle` overrides a method `draw()` declared in secondary base `Drawable` (which resides at byte offset `+16` inside `Circle`):
1. A secondary vtable is created for `Circle-as-Drawable`.
2. Direct invocation of `Circle::draw(Circle* this)` expects `this` to point to the start of `Circle` (`offset 0`).
3. Calling through `Drawable*` passes a pointer at `offset +16`.
4. `klangc` generates an **Adjustor Thunk** function (`thunk_info`):
   - Subtracts 16 bytes from the incoming `this` pointer: `this_adj = GEP i8, this_ptr, -16`.
   - Tail-calls `Circle::draw(this_adj, args...)`.
5. The secondary vtable slot stores the pointer to the adjustor thunk.

```
Caller (has Drawable*) ──► [Secondary Vtable Slot] ──► [Adjustor Thunk]
                                                             │
                                                             ▼ (adjusts this by -16)
                                                      [Circle::draw]
```

### 2.4 Linkage & COMDAT Deduplication
Vtables for template instantiations (e.g. `Vector<int>`) are emitted with `linkonce_odr` linkage and attached to an `llvm::Comdat` group keyed by the mangled vtable name. The linker guarantees that only a single instance of the vtable exists across all compilation units.

---

## 3. Runtime Type Information (RTTI)

K provides a rich, reflection-ready RTTI metadata hierarchy defined in `libk/src/rtti.k` and emitted by `klangc`.

### 3.1 RTTI Metadata Hierarchy

```
                   ┌───────────────────────────┐
                   │   const interface TypeInfo│
                   │        (getName())        │
                   └─────────────▲─────────────┘
                                 │
                   ┌─────────────┴─────────────┐
                   │const interface            │
                   │      AggregateType        │
                   │(getFullName, getBases,    │
                   │ getNested, getVisibility) │
                   └─────────────▲─────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         │                       │                       │
┌────────┴────────┐     ┌────────┴────────┐     ┌────────┴────────┐
│const class Class│     │  const class    │     │  const class    │
│ (getFunctions,  │     │    Interface    │     │ AnnotationType  │
│ getConstructors)│     │  (getFunctions) │     │(getAnnotations) │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

### 3.2 RTTI Structure Emission

For every class `MyClass`, the compiler emits an immutable global structure `@_KTR...`:

```
@_KTRN8mymodule7MyClassE = constant %"k::Class" {
    ptr @_KTVN1k5ClassE,                   ; Class vptr
    ptr @_KTVN1k13AggregateTypeE,          ; AggregateType vptr
    ptr @_KTVN1k8TypeInfoE,               ; TypeInfo vptr
    ptr @_KTVN1k6ObjectE,                 ; Object vptr
    ptr @.str.name,                       ; "MyClass"
    ptr @.str.fullname,                   ; "::mymodule::MyClass"
    ptr @_KTR_bases_array,                ; Array of base TypeInfo descriptors
    ptr @_KTR_nested_array,               ; Array of nested TypeInfo descriptors
    ptr @_KTR_enclosing_type,             ; Enclosing type descriptor (or null)
    i32 0,                                ; Flags (Visibility, static modifier)
    ptr @_KTR_annotations_array,          ; Attached annotations array
    ptr @_KTR_functions_array,            ; Member Function descriptors array
    ptr @_KTR_constructors_array          ; Constructor descriptors array
}
```

### 3.3 Dynamic Casting & Type Matching (`is` / `as`)

1. **Extracting Runtime Class (`__k_object_get_class`)**:
   ```c
   void* __k_object_get_class(void* obj) {
       void** vptr = *(void***)obj; // Dereference field 0 (vptr)
       return vptr[0];              // Vtable slot 0 is RTTI Class pointer
   }
   ```
2. **`is` Operator (Type Test)**:
   - Evaluates whether object's dynamic type is identical to or derived from target type.
   - Compares RTTI descriptor pointers directly or walks the `getBases()` descriptor tree.
3. **`as` Operator (Safe Downcast)**:
   - Performs dynamic type verification.
   - If dynamic type matches target, computes necessary base-subobject pointer adjustment and returns the cast pointer.
   - If match fails, returns `null` for nullable pointer/view targets or throws `ClassCastException` for reference targets.

### 3.4 Exception Catching Integration

Exception dispatch uses per-thread TLS slots in `libk/src/rtti.c`:
- `__k_thrown_typeinfo_addr()`: Points to the RTTI descriptor of the currently thrown exception.
- `__k_thrown_typeinfo_chain_addr()`: Points to the inheritance descriptor chain.
- Inside landing pads, `klangc` emits checks matching the active exception against each `catch (e : ExceptionType)` clause using RTTI metadata before branching to the handler.

---

## 4. Call Invocation & Synthesis Mechanisms

### 4.1 Direct (Static) Function Calls
Used for global functions, static methods, and non-virtual member functions.
```
; K Source: calc(10, 20)
%res = call i32 @_KFN4calcE(i32 10, i32 20)

; K Source: obj.non_virtual_method(42)
%res = call i32 @_KFMvN6MyClass3fooE(ptr %obj_ptr, i32 42)
```

---

### 4.2 Virtual Method Calls

When calling a virtual method through a polymorphic reference or pointer:

```
; K Source: animal.speak()  (where speak() is at vtable slot 1)
%vptr_addr = getelementptr inbounds %Animal, ptr %animal_ptr, i32 0, i32 0
%vptr      = load ptr, ptr %vptr_addr
%slot_addr = getelementptr inbounds ptr, ptr %vptr, i64 2 ; Slot -1=RTTI, 0=dtor, 1=speak (index 2)
%fn_ptr    = load ptr, ptr %slot_addr
call void %fn_ptr(ptr %animal_ptr)
```

---

### 4.3 Secondary Base & Interface Method Calls

When calling an interface method on a variable statically typed as an interface:
1. The pointer `iface_ptr` already points directly to the interface subobject.
2. Load the secondary vptr from `iface_ptr`.
3. Load the target function pointer from the vtable slot (which points to an adjustor thunk).
4. Call the thunk passing `iface_ptr`. The thunk adjusts the pointer to the derived object and branches to the implementation.

---

### 4.4 Constructor Synthesis (C1 vs. C2)

`klangc` generates two constructor variants for every class:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   C1: Complete Object Constructor                      │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Set primary and all secondary vptrs to the MOST-DERIVED vtables.    │
│ 2. Invoke C2 constructors of direct base classes.                      │
│ 3. Execute member field default initializers.                          │
│ 4. Execute user-defined constructor body statements.                   │
└────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────┐
│                   C2: Base Subobject Constructor                       │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Set vptrs to intermediate base vtables.                             │
│ 2. Invoke C2 constructors of its own base classes.                     │
│ 3. Execute member field default initializers.                          │
│ 4. Execute user-defined constructor body statements.                   │
└────────────────────────────────────────────────────────────────────────┘
```

---

### 4.5 Destructor Synthesis (D1 vs. D2)

`klangc` generates two destructor variants:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   D1: Complete Object Destructor                       │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Reset vptrs to the most-derived class vtable.                       │
│ 2. Execute user-defined destructor body statements.                    │
│ 3. Invoke D2 destructors of member fields (in reverse declaration order│
│ 4. Invoke D2 destructors of direct base classes (in reverse order).    │
└────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────┐
│                   D2: Base Subobject Destructor                        │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Reset vptrs to intermediate base class vtables.                     │
│ 2. Execute user-defined destructor body statements.                    │
│ 3. Invoke D2 destructors of member fields (reverse order).             │
│ 4. Invoke D2 destructors of its own base classes (reverse order).      │
└────────────────────────────────────────────────────────────────────────┘
```

#### Virtual Destructor Calls (`emit_virtual_destructor_call`)
Invoked when destroying an object via an owner pointer or interface reference (`delete obj` or owner scope exit):
1. Loads vptr from `field 0`.
2. GEPs byte offset `8` (Slot 0, immediately following RTTI).
3. Loads destructor function pointer and calls `fn(this_ptr)`.

---

### 4.6 Exception Handling & Unwinding Lowering

Calls to functions that may throw are emitted using `llvm::InvokeInst` rather than `llvm::CallInst`:

```
%res = invoke i32 @might_throw(ptr %ctx)
        to label %normal_cont unwind label %landing_pad

normal_cont:
    ; Normal execution path

landing_pad:
    %lp = landingpad { ptr, i32 }
            cleanup
            catch ptr @_KTRN1k9ExceptionE
    ; 1. Run local variable destructors (reverse order)
    ; 2. Match caught exception type against thrown RTTI
    ; 3. If matched, jump to catch block
    ; 4. If unmatched, run cleanup and resume unwinding via 'resume'
```
