# Callables & Lambdas — Internals, Synthesis & Lowering

This document describes the internal architecture, memory representation, type compatibility rules, binding synthesis, and code generation lowering for **first-class callables** and **lambdas** in `klangc`.

---

## 1. Callable Architecture & Fat Pointer Representation

In K, a **callable** represents a first-class function pointer or delegate. A callable type is specified by an addresser (`*`, `?`, `+`, `&`, `!`, `#`), an optional parameter list, and an optional return type:

```
// Syntax examples:
fp1 : *(int, int):int;   // Nullable pointer callable taking (int, int) returning int
fp2 : &(String):void;    // Non-null reference callable taking String
fp3 : +(double):double;  // Mutable non-null link callable
```

### 1.1 Runtime Memory Representation (`%__k.callable`)

Every callable value is lowered in LLVM IR as a **fat pointer** (two 64-bit pointers on 64-bit platforms):

```
%__k.callable = type { ptr, ptr }
```

```
┌────────────────────────────────────────────────────────┐
│               Callable Fat Reference                   │
├─────────┬──────────┬───────────────────────────────────┤
│ Field 0 │ ptr fn   │ Executable machine code pointer   │
├─────────┼──────────┼───────────────────────────────────┤
│ Field 1 │ ptr ctx  │ Receiver / Closure context pointer│
└─────────┴──────────┴───────────────────────────────────┘
```

- **`fn` (Function Pointer)**: Points directly to the target executable code (free function, static method, member method, or lambda body).
- **`ctx` (Context Pointer)**:
  - `null` for free functions and static methods.
  - Points to the receiver object (`this`) for bound member functions and functors.
  - Points to the closure environment struct (or captured variable) for capturing lambdas.

---

## 2. Callable Binding Variations (`callable_bind_expression`)

When a function, method, functor, or lambda is bound to a callable variable or passed as a callable argument, `klangc` generates a `k::model::callable_bind_expression` node.

```
                               callable_bind_expression
                                         │
    ┌────────────────────┬───────────────┴───────────────┬────────────────────┐
    ▼                    ▼                               ▼                    ▼
Free / Static       Bound Member                    Functor Object      Functional Interface
 { @fn, null }    { @fn, &receiver }               { @op(), &obj }      { vtable[slot], &iface }
```

### 2.1 Free Functions & Static Methods
Context-free targets require no receiver:
```
add(a : int, b : int) : int { return a + b; }

f : *(int, int):int = add;
```
**Lowering**:
```
%f = insertvalue %__k.callable { ptr @add, ptr null }, ptr @add, 0
```

---

### 2.2 Bound Member Methods (`obj.method` / `ptr->method`)
Binding a non-static member method captures the receiver object as `ctx`:
```
struct Counter {
    base : int;
    add(x : int) : int { return base + x; }
}

c : Counter;
f : &(int):int = c.add; // Binds Counter::add with ctx = &c
```
**Lowering**:
1. Evaluate receiver expression (`&c`).
2. If necessary, upcast receiver pointer to the declaring aggregate type.
3. Emit fat pointer `{ ptr @Counter_add, ptr %c_ptr }`.

---

### 2.3 Virtual Method Bindings
When binding a virtual method (`obj.virtualMethod`):
1. The compiler emits code to load the vtable slot dynamically from the receiver's `__vptr__`.
2. The fat pointer stores `{ ptr %loaded_vtable_fn, ptr %receiver_subobject_ptr }`.
3. Invocation automatically executes the dynamic override with the correct subobject `this` pointer.

---

### 2.4 Functor Objects (`operator()`)
Any aggregate type that declares an `operator()` can be bound directly to a callable:
```
struct Multiplier {
    factor : int;
    operator()(val : int) : int { return val * factor; }
}

m : Multiplier;
f : &(int):int = m; // Binds Multiplier::operator() with ctx = &m
```

---

### 2.5 Functional Interfaces (Single Abstract Method / SAM)
An interface (or abstract class) is **functional** if its vtable contains exactly **one** abstract method (the universal destructor at Slot 0 excluded).

```
interface Predicate {
    test(val : int) : bool;
}

filter(p : &(int):bool);

// Passing a Predicate reference to filter():
pred : Predicate&;
filter(pred); // Automatically binds pred's single abstract method vtable slot
```
- **Lowering**:
  - `fn` is loaded from the single abstract method's vtable slot on `pred`.
  - `ctx` is the unadjusted `pred` subobject pointer.

---

### 2.6 Null-Propagating Bindings
When binding a member method through a nullable pointer/view (`*` or `?`) to a nullable callable destination:
- If the receiver pointer is `null`, the binding evaluates to `{ null, null }` without triggering a null-dereference trap.
- If the destination is a non-null addresser (`&` or `+`), a null check is emitted, raising a `FatalError` if null.

---

## 3. Compatibility & Variance Rules (`gen_callable_compat.cpp`)

Because callables are dispatched through raw function pointers without intermediate wrapper overhead, `klangc` enforces **strict ABI compatibility**:

### 3.1 Strict Subtyping & Variance Rules

1. **Addresser Invariant**: The addresser category (`*`, `?`, `+`, `&`, `!`, `#`) must match exactly between source and target.
2. **Parameter Contravariance**: Parameter types must be contravariant (or identical).
3. **Return Type Covariance**: Return types must be covariant (or identical).
4. **Base Subobject Offset-0 Rule**:
   - An upcast from `Derived*` to `Base*` in return position (or `Base*` to `Derived*` in parameter position) is **only** permitted if `Base` resides at **byte offset 0** in `Derived`.
   - If a non-zero byte offset adjustment is needed, direct callable binding is rejected with `ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT` because an indirect call cannot adjust pointer registers.
5. **No Primitive Conversions**: Implicit integer widening or floating-point conversions are strictly forbidden across callable prototypes.
6. **Exception `throws` Subset Rule**:
   - The source function's declared `throws` set must be a strict subset of the destination callable's `throws` specification.
   - Violations trigger `ERR_CALLABLE_THROWS_NOT_SUBSET`.
7. **Nominal Typedef Preservation**: Strong aliases (`typedef`) maintain nominal identity and cannot be implicitly substituted without explicit conversion.

---

## 4. Lambda Compilation & Lowering

A **lambda expression** (`[](params) : Ret { body }`) is syntactic sugar for a synthetic function and an optional closure context.

```
auto f = [x = 10](y : int) : int {
    return x + y;
};
```

### 4.1 Frontend to Semantic Model Transformation (`model_builder.cpp`)

When `model_builder::visit_lambda_expression()` processes an AST lambda node:

```
[AST Lambda Expression]
    │
    ├── 1. Generate unique synthetic function name: "__lambda_<counter>"
    │
    ├── 2. Build AST Function Declaration:
    │      - Modifiers: 'private', 'static' (if inside class)
    │      - Parameter list, return type, and body block
    │
    ├── 3. Inject synthetic function into enclosing Namespace or Aggregate
    │
    ├── 4. Analyze Capture List:
    │      - By-Value captures: [val]
    │      - By-Reference captures: [&ref]
    │      - Init-Captures: [x = expr]
    │
    └── 5. Construct model::callable_bind_expression(kind::lambda)
```

### 4.2 Capture Lowering & Context Packaging

#### 1. Capture-Free Lambdas
- The synthetic function `@__lambda_1` takes only user-declared parameters.
- Emitted as a context-free callable: `{ ptr @__lambda_1, ptr null }`.

#### 2. Capturing Lambdas (Closures)
- `klangc` creates a synthetic anonymous struct holding captured fields:
  ```
  %__lambda_1_closure = type { i32 } ; Captured 'x'
  ```
- The synthetic lambda function receives the closure environment as its implicit `this` parameter:
  ```
  @__lambda_1(ptr %ctx, i32 %y) : i32 {
      %x_ptr = getelementptr %__lambda_1_closure, ptr %ctx, i32 0, i32 0
      %x = load i32, ptr %x_ptr
      %res = add i32 %x, %y
      ret i32 %res
  }
  ```
- The lambda expression allocates and initializes `%__lambda_1_closure` on the stack (or heap) and emits:
  ```
  %callable = { ptr @__lambda_1, ptr %closure_alloc }
  ```

---

## 5. Callable Invocation Lowering (`implementation_generator`)

When executing a callable call expression `f(arg1, arg2)`:

```
; 1. Load the callable fat struct
%callable_val = load %__k.callable, ptr %f_addr
%fn_ptr       = extractvalue %__k.callable %callable_val, 0
%ctx_ptr      = extractvalue %__k.callable %callable_val, 1

; 2. Null check (for nullable addressers * or ?)
%is_null = icmp eq ptr %fn_ptr, null
br i1 %is_null, label %null_trap, label %dispatch_block

null_trap:
    call void @__k_fatal_null_dereference()
    unreachable

dispatch_block:
; 3. Check if context pointer is present
%has_ctx = icmp ne ptr %ctx_ptr, null
br i1 %has_ctx, label %call_with_ctx, label %call_without_ctx

call_with_ctx:
    ; Invoke passing ctx as implicit first argument ('this')
    %res1 = call i32 %fn_ptr(ptr %ctx_ptr, i32 %arg1, i32 %arg2)
    br label %merge_block

call_without_ctx:
    ; Invoke as free function
    %res2 = call i32 %fn_ptr(i32 %arg1, i32 %arg2)
    br label %merge_block

merge_block:
    %result = phi i32 [ %res1, %call_with_ctx ], [ %res2, %call_without_ctx ]
```

### 5.1 Exception Unwinding Through Callables

If the callable prototype specifies exceptions (`throws ...`), the call instruction is lowered as an `invoke` instruction rather than a `call`, wiring into the standard LLVM exception landing pad and cleanup tables of the enclosing function.
