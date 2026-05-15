# Template Parameter Pack Perfect Forwarding to Constructors — Implementation Plan

## Goal

Enable `UniSlot<T>::construct(Args...args)` to forward arbitrary arguments to `T`'s
constructor via template parameter pack perfect forwarding. This requires:

1. Member template functions (template methods inside template aggregates)
2. Pack forwarding to constructor invocations
3. Intrinsic codegen that uses deduced pack types to select the right constructor

---

## Current State Assessment

### What works today
- `template<typename...Ts> fun fwd(Ts...args) : int { return target(args...); }` ✅
  Free function template packs work end-to-end (parse → instantiate → codegen).
- Pack expansion into `function_invocation_expression` ✅
- Pack expansion into `constructor_invocation_expression` ✅ (in template_instantiator)
- `template<typename T> struct Foo { method() : T { ... } }` ✅
  Template struct members using outer T work.

### What's missing
1. **Member template functions** (a method with its own template params inside a template struct)
   — `template<typename T> struct S { template<typename...Args> construct(Args...args); }`
   — When `S<int>` is instantiated, `construct` must remain a template function (with its own
     `_tpl_info`) in the instantiated struct, but with `T` already substituted.
   — `clone_method` currently doesn't preserve `_tpl_info`.

2. **Invoking a member template function** with explicit template args
   — `slot.construct<int, float>(42, 3.14)` needs to:
     a. Identify `construct` as a template method.
     b. Instantiate it with `<int, float>`.
     c. Call the instantiated method.

3. **Intrinsic codegen for construct with args**
   — `emit_intrinsic_unislot_construct` needs to select the right constructor of `T` based
     on the actual argument types (not just zero-arg default ctor).

---

## Implementation Plan

### Step 1: Member template preservation during aggregate instantiation

**Files:** `klang/src/model/template_instantiator.cpp`

When `clone_method` clones a method from a template aggregate definition:
- If `src.get_tpl_info() != nullptr` (the method is itself a template):
  - Clone the `tpl_info` to the new function.
  - Apply the outer struct's type substitution to ANY type references in the
    cloned method's signature/body that refer to the outer's template params.
  - Keep the method's own template params as unresolved (they still need
    instantiation when called).
  - Mark the cloned function as `is_template() == true`.

**Key change:**
```cpp
void template_instantiator::clone_method(...) {
    // ...existing flag copy...
    
    // If the source method is itself a template, preserve that info
    if (src.get_tpl_info()) {
        auto* new_ti = new tpl_info(*src.get_tpl_info()); // deep copy
        new_func->set_tpl_info(std::unique_ptr<tpl_info>(new_ti));
    }
    
    populate_function_from_template(new_func, src, subst, val_subst);
}
```

**Test:**
```k
template<typename T>
struct Wrapper {
    _val : T;
    template<typename U>
    set(u : U) { _val = u; }    // member template
}
w : Wrapper<int>;
w.set<int>(42);
```

### Step 2: Member template invocation resolution

**Files:** `klang/src/gen/gen_expr_invocation.cpp`, `klang/src/gen/resolvers_type_ref.cpp`

When resolving a `function_invocation_expression` like `obj.method<Args...>(vals...)`:
- After finding the method, check if it's a template.
- If yes, instantiate it with the provided template arguments (same mechanism as
  free template function instantiation in `template_instantiator::instantiate_function_template`).
- The parent namespace for the instantiation is the owning aggregate.

**Key considerations:**
- Need to detect `member_of_object_expression` → `function_invocation_expression`
  chain where the callee carries template arguments.
- The instantiated method needs to be registered in the aggregate's function list.
- Parameters of the instantiated method carry the concrete types.

### Step 3: Pack forwarding in member template context

**Files:** `klang/src/model/template_instantiator.cpp`

When instantiating a member template with pack args (e.g. `construct<int, float>`):
- `build_pack_substitution_map` already extracts pack types from the args.
- `populate_function_from_template` expands pack parameters into concrete params.
- `expand_pack_expressions_in_block` replaces `args...` with `args_0, args_1, ...`

This should already work IF step 1 and 2 are properly done, since the mechanism
is the same as free function template instantiation.

**Test:**
```k
template<typename T>
struct Builder {
    template<typename...Args>
    build(Args...args) : T { return make<T>(args...); }
}
```

### Step 4: Intrinsic construct with argument forwarding

**Files:** `klang/src/gen/gen_intrinsics.cpp`

Modify `emit_intrinsic_unislot_construct`:
- Instead of always finding the zero-arg constructor, examine the `function`'s
  actual parameters (after pack expansion/instantiation).
- Use those parameter types to find the best matching constructor of T via
  `get_best_matching_constructor`.
- Forward all explicit parameter values to the constructor call.

**Current code (zero-arg only):**
```cpp
// Find the default constructor (zero args)
for (auto& ctor : target_struct->constructors()) {
    if (ctor->get_parameter_size() == 0) { default_ctor = ctor; break; }
}
_builder->CreateCall(ctor_it->second, {slot_ptr});
```

**New code (forward all args):**
```cpp
// Collect argument values from the function's parameters (after 'this')
std::vector<llvm::Value*> ctor_args = {slot_ptr};
for (const auto& param : function.parameters()) {
    auto param_alloca_it = _context->_parameter_variables.find(param);
    if (param_alloca_it != _context->_parameter_variables.end()) {
        auto loaded = _builder->CreateLoad(
            _context->get_llvm_type(param->get_type()),
            param_alloca_it->second, param->get_short_name());
        ctor_args.push_back(loaded);
    }
}

// Find best matching constructor based on parameter types
std::vector<std::shared_ptr<type>> arg_types;
for (const auto& param : function.parameters()) {
    arg_types.push_back(param->get_type());
}
auto best_ctor = find_matching_ctor(target_struct, arg_types);
_builder->CreateCall(best_ctor_llvm, ctor_args);
```

### Step 5: Update UniSlot<T> source

**File:** `libk/libk/src/memory.k`

```k
template<typename T>
struct UniSlot {
    private:
    _slot : T;

    public:
    @annotations::Intrinsic("UniSlot::constructor")
    UniSlot();

    @annotations::Intrinsic("UniSlot::destructor")
    ~UniSlot();

    @annotations::Intrinsic("UniSlot::construct")
    template<typename...Args>
    construct(Args...args);

    @annotations::Intrinsic("UniSlot::destruct")
    destruct();

    get() : T& { return _slot; }
}
```

### Step 6: Tests

#### 6.1 Member template functions (prerequisite)
```k
// Test: member template method on a template struct
template<typename T>
struct Container {
    _val : T;
    template<typename U>
    setFrom(u : U) { _val = u; }
}
test() : int {
    c : Container<int>;
    c.setFrom<int>(42);
    return c._val;
}
```

#### 6.2 Member template with parameter packs
```k
// Test: member template with pack forwarding
template<typename T>
struct Factory {
    template<typename...Args>
    make(Args...args) : int { return target(args...); }
}
```

#### 6.3 UniSlot construct with arguments
```k
struct Point {
    x : int;
    y : int;
    Point(px : int, py : int) { x = px; y = py; }
}

test() : int {
    slot : UniSlot<Point>;
    slot.construct<int, int>(10, 20);
    return slot.get().x + slot.get().y;  // expect 30
}
```

#### 6.4 UniSlot construct zero-arg still works
```k
// Regression: zero-arg construct is just construct<>() or construct()
test() : int {
    slot : UniSlot<Widget>;
    slot.construct();  // calls Widget default ctor
    return slot.get().value;
}
```

---

## Execution Order (dependencies)

```
Step 1: clone_method preserves tpl_info
    │
    ├── Test: member template method basic
    │
Step 2: member template invocation resolution
    │
    ├── Test: member template call with explicit args
    │
Step 3: member template with packs (should work from 1+2)
    │
    ├── Test: member template pack forwarding
    │
Step 4: intrinsic construct with args
    │
    ├── Test: UniSlot construct with args
    │
Step 5: update memory.k
    │
Step 6: full test suite validation
```

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Member template functions never tested → unknown gaps in model_builder, symbol_resolver | Start with a minimal test and iterate |
| Parser may not handle `template<...>` before a member function inside a `struct` block | Check parser_declarations.cpp for nested template parsing |
| Mangling of member template instantiations may collide | Test with multiple instantiations of same method |
| `get_best_matching_constructor` needs arg types at codegen time | Types are available after pack expansion in instantiated method |
| Ambiguity: `slot.construct<int>(42)` — is `<int>` a template arg or comparison? | K already handles this for free functions with `<>` syntax |

---

## Alternative Approach (if member templates are too complex)

If implementing full member template support proves too large, a **simpler fallback**
for UniSlot specifically:

- Don't make `construct()` a template method.
- Instead, have the intrinsic codegen for `UniSlot::construct` **infer argument types
  from the call site** at resolution time.
- The `construct` method is declared with no explicit params; the intrinsic
  recognises the pattern and generates the forwarding call.
- This is more "intrinsic magic" but avoids the member template machinery entirely.

This fallback is semantically weaker (no type-checking at declaration time) but
much simpler to implement. It can serve as a bridge until full member templates land.

---

## Estimated Complexity

| Step | Effort | Risk |
|------|--------|------|
| Step 1: tpl_info preservation | Medium | Low — well-scoped change |
| Step 2: member template resolution | **High** | High — touches invocation resolution |
| Step 3: packs in member context | Low | Low — should work if 1+2 are correct |
| Step 4: intrinsic construct args | Low-Medium | Low — localised to gen_intrinsics.cpp |
| Step 5: memory.k update | Trivial | None |
| Step 6: tests | Medium | None |

**Total estimate:** Large feature (Steps 1-2 carry the bulk of the risk and work).
The alternative fallback approach would be Small-Medium effort.

