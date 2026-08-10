# Lambdas

[← Index](../index.md) · [Functions](functions.md) · [Function References](function_references.md)

A *lambda expression* is an anonymous function defined inline as an expression. It may *capture* variables from its enclosing scope and evaluates to a callable value — concretely, a non-null reference `&(Params):Ret` bound to an implicit *closure* object.

Lambdas integrate smoothly with the rest of the type system: the closure type is an anonymous struct synthesised by the compiler, and the callable produced is a `&(Params):Ret` non-null function reference.

---

## Contents

1. [Syntax overview](#1-syntax-overview)
2. [Capture lists](#2-capture-lists)
3. [Return type](#3-return-type)
4. [Capture-free lambdas](#4-capture-free-lambdas)
5. [Bracket-less form](#5-bracket-less-form)
6. [`const` lambdas](#6-const-lambdas)
7. [Closure lifetime](#7-closure-lifetime)
8. [Lowering — how lambdas are compiled](#8-lowering--how-lambdas-are-compiled)
9. [Lambdas in special contexts](#9-lambdas-in-special-contexts)
10. [Lambdas inside templates](#10-lambdas-inside-templates)
11. [Examples](#11-examples)
12. [Grammar summary](#12-grammar-summary)

---

## 1. Syntax overview

```
LambdaExpr:
    [ 'const' ] [ CaptureList ] '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
```

**Parts:**

| Part | Optional? | Purpose |
|------|-----------|---------|
| `const` | yes | Marks operator() as `const`; writing through any by-reference capture is an error |
| `CaptureList` | yes | Explicit `[…]` list of variables to capture; if omitted, all used locals are captured by reference |
| `(ParameterList)` | no | Typed parameter list, identical to a regular function |
| `: TypeSpec` | yes | Explicit return type; deduced when absent |
| `BlockStatement` | no | Body of the lambda |

All parameters must be **explicitly typed** — K has no `auto` keyword.

**Result type:** every lambda expression evaluates to a `&(Params):Ret` (non-null callable reference). The return type `Ret` is determined by [§3](#3-return-type).

**Minimal example:**

```k
add : &(int,int):int = (a:int, b:int):int { return a + b; };
```

---

## 2. Capture lists

The capture list controls which enclosing variables become part of the closure and how they are captured.

### 2.1 Explicit capture list syntax

```
CaptureList:
    '[' ']'
  | '[' Capture { ',' Capture } ']'

Capture:
    Identifier                       -- capture by value (copy)
  | '&' Identifier                   -- capture by mutable reference (link T+)
  | 'const' '&' Identifier           -- capture by immutable reference
  | Identifier '=' Expression        -- init capture by value
  | '&' Identifier '=' Expression    -- init capture by reference to lvalue
  | 'this'                           -- capture enclosing object by reference (link Outer+)
  | '&' 'this'                       -- same as `this`
  | 'const' 'this'                   -- capture enclosing object by const reference
  | 'const' '&' 'this'               -- same as `const this`
```

### 2.2 Capture modes

| Spelling | Captured as | Mutable inside lambda? |
|----------|-------------|------------------------|
| `x` | copy of `x` at lambda-creation time | yes (copy is independent) |
| `&x` | link `T+` to `x` | yes |
| `const &x` | const reference to `x` | no |
| `x = expr` | copy of `expr` (evaluated at lambda site) | yes |
| `&x = expr` | link `T+` to lvalue `expr` | yes |
| `this` / `&this` | link `Outer+` to enclosing object | yes |
| `const this` / `const &this` | const reference to enclosing object | no |

### 2.3 Empty capture list `[]`

An explicit empty list `[]` means **no captures**. Any reference to a local variable from the enclosing scope inside the lambda body is a compile error. The resulting lambda has no closure storage.

### 2.4 Omitting the capture list

When the `[…]` delimiters are absent entirely, every free identifier in the lambda body that resolves to an enclosing local variable is **captured by reference** automatically. This is the *implicit capture-all-by-reference* mode.

This is equivalent to writing `&x` for each used variable individually, except the list is inferred automatically.

### 2.5 Default-capture syntax is not supported

The C++ shorthands `[&]`, `[=]`, and `[&, x]` are **not valid K syntax** and produce a parse error (`ERR_LAMBDA_BAD_CAPTURE_SYNTAX`). K requires either explicit captures or the bracket-omission form.

### 2.6 Init captures

An init capture evaluates an arbitrary expression at the point where the lambda is defined and stores the result in a new closure member:

```k
base : int = 40;
f : &(int):int = [b = base + 2](n:int):int { return b + n; };
// b is captured as 42, regardless of later changes to `base`
```

The init-capture name (`b` above) is a new variable that exists only inside the lambda body.

### 2.7 `this` capture in member functions

Inside a member function, `this` (or `&this`) captures the enclosing object as a mutable link `Outer+`. `const this` (or `const &this`) captures it as a const reference.

```k
class Adder {
    base : int;
    Adder(b:int) : base(b) {}
    getAdder() : &(int):int {
        return [this](x:int):int { return base + x; };
    }
}
```

The identifier `base` inside the lambda resolves to `this->base` through the captured link.

---

## 3. Return type

The return type of a lambda is determined in the following order of priority:

1. **Contextual deduction** — if the lambda is used in a context that expects a specific `&(Params):Ret` type (assignment target, function parameter, return statement), `Ret` is inferred from that context.
2. **Body deduction** — all `return` expressions must have the same type; that type becomes the return type. A lambda body with no `return` statements deduces `void`.
3. **Explicit annotation** — the `: TypeSpec` suffix after the parameter list always takes precedence and overrides both 1 and 2.

If deduction would be ambiguous (e.g. conflicting `return` types and no explicit annotation), the compiler reports an error requiring an explicit return type.

> **Current compiler limitation.** Destination-context deduction for capture-free lambdas is not fully reliable yet in all callable-binding paths. When a lambda is assigned to a callable and inference would otherwise depend on the target type, prefer spelling the return type explicitly.

```k
// Explicit return type
square : &(int):int = (n:int):int { return n * n; };

// Deduced from context (variable type declares :int)
counter : &():int = []() { return 0; };

// Deduced from body
identity : &(int):int = [](n:int) { return n; }; // return type: int (from body)
```

---

## 4. Capture-free lambdas

A lambda that ends up with **no captures** (either an explicit `[]` list or an omitted list where no enclosing locals are referenced) is a *capture-free lambda*:

- Its `operator()` is **static** — no `this` pointer is needed.
- The closure occupies **zero storage** — no anonymous struct is allocated.
- The resulting `&(Params):Ret` callable has a null context pointer (`ctx == null`).
- It is **safe to store indefinitely** — there is no closure to dangle.

```k
// Both forms produce an identical, capture-free callable
add1 : &(int,int):int = [](a:int, b:int):int { return a + b; };
add2 : &(int,int):int =   (a:int, b:int):int { return a + b; };
```

---

## 5. Bracket-less form

When the `[…]` delimiters are omitted, the lambda uses implicit all-by-reference capture. In expression position — where the grammar unambiguously resolves to a lambda — the brackets may also be dropped for capture-free lambdas that reference no enclosing locals, producing the most concise form:

```k
result : int = ((n:int):int { return n * 2; })(21);  // → 42
```

This is particularly useful for immediately-invoked lambdas and for passing simple callbacks as arguments:

```k
apply(f : &(int):int, v : int) : int { return f(v); }

x : int = apply((n:int):int { return n + 1; }, 41);  // → 42
```

---

## 6. `const` lambdas

Prefixing the lambda with `const` forces its `operator()` to be `const`:

```k
const [ CaptureList ] ( ParameterList ) [ : ReturnType ] BlockStatement
```

Inside a `const` lambda:

- Reading by-reference captures is permitted.
- **Writing** through any by-reference capture is a **compile error**.
- By-value captures may still be mutated (they are copies owned by the closure).

If `const` is **not** specified, the compiler automatically infers `const` on `operator()` when none of the by-reference captures are written to in the body.

```k
count : int = 0;

// Explicit const — writing to `count` is an error
readOnly : const &():int = const[&count]() { return count; };

// Attempting to write through a const lambda — compile error
bad : const &():void = const[&count]() {
    count = count + 1;  // ERROR: const lambda cannot write to by-reference capture
};
```

---

## 7. Closure lifetime

A lambda's closure is a local anonymous struct. Its lifetime rules mirror those of any other local variable:

- A lambda defined inside a function lives until the **end of the enclosing block** in which it was defined.
- Storing a capturing lambda's callable in a **longer-lived location** — a returned value, a member variable (of a different object), or a global — creates a dangling reference. The compiler emits `WARN_LAMBDA_ESCAPES_SCOPE` for statically detectable escapes.
- **Capture-free lambdas** hold no closure state and therefore **never dangle** — they may be stored anywhere freely.

```k
// Dangerous — returned callable references a local closure
makeCounter() : &():int {
    count : int = 0;
    return [&count]():int { return count; };  // WARN_LAMBDA_ESCAPES_SCOPE: closure escapes
}

// Safe — capture-free; no closure is created
makeAdder() : &(int,int):int {
    return (a:int, b:int):int { return a + b; };  // OK
}
```

See [§9](#9-lambdas-in-special-contexts) for member-variable and global-variable initializer contexts, which are safe.

---

## 8. Lowering — how lambdas are compiled

Internally, the compiler synthesises an anonymous struct (the *closure type*) for each lambda:

```
struct __lambda_<N> {
    // One member per captured variable
    <captured-members> ...;

    // Constructor initialised at lambda-creation site
    __lambda_<N>( <capture-params> ) : ... {}

    // The call operator — const if applicable
    operator()( <lambda-params> ) : <return-type> {
        <lambda-body>
    }
}
```

The lambda expression itself:

1. Allocates an instance of `__lambda_<N>` (stack-allocated in function scope).
2. Initialises its captured members from the enclosing scope.
3. Evaluates to a `&(Params):Ret` callable bound to that closure instance.

For capture-free lambdas, step 1 and 2 are elided entirely: the closure struct has no members and the resulting callable's context pointer is null.

This lowering is an implementation detail; K programs must not rely on the name or layout of the synthesised struct.

---

## 9. Lambdas in special contexts

### 9.1 Member-variable initializer

A lambda used to initialise a member variable gets a **hidden member** added to the enclosing class for its closure. The closure is destroyed when the enclosing object is destroyed — it does **not** dangle:

```k
class EventHandler {
    handler : &(int):void = [this](event:int):void {
        processEvent(event);
    };
    processEvent(e:int) : void { /* ... */ }
}
```

### 9.2 Global-variable initializer

A lambda used to initialise a global variable gets a **hidden global** for its closure. The closure lives for the duration of the program:

```k
globalProcessor : &(int):int = [](n:int):int { return n * 2; };
```

### 9.3 Immediately invoked lambda

A lambda may be called immediately at the point of definition:

```k
result : int = ([](n:int):int { return n * 2; })(21);  // → 42
```

The outer parentheses are required when the `[…]` form is used, to avoid a parse ambiguity between the subscript operator and the capture list.

### 9.4 Passed as an argument

A lambda may be passed directly to a function expecting a compatible callable type:

```k
apply(f : &(int):int, v : int) : int { return f(v); }

result : int = apply([](n:int):int { return n + 1; }, 41);  // → 42
```

The lambda's type must be compatible with the parameter type. Compatibility is checked the same way as any `&`-typed assignment.

---

## 10. Lambdas inside templates

A lambda defined inside a template function body is **re-instantiated** for each template instantiation. Each instantiation produces a distinct closure type:

```k
template<T>
applyTwice(f : &(T):T, x : T) : T {
    return f(f(x));
}
```

The closure struct synthesised for the lambda body of a particular instantiation of `applyTwice` is private to that instantiation. Two different instantiations (e.g. `applyTwice<int>` and `applyTwice<double>`) each have their own closure type, even if the lambda text is identical.

---

## 11. Examples

### Capture-free lambda — no closure object

```k
// The [] can be omitted entirely when no captures are needed
add : &(int,int):int = (a:int, b:int):int { return a + b; };
result : int = add(20, 22);  // → 42
```

### Capture by value

```k
x : int = 10;
addX : &(int):int = [x](n:int):int { return n + x; };
x = 999;                     // does not affect addX — x was copied at lambda site
result : int = addX(32);     // → 42
```

### Capture by mutable reference

```k
count : int = 0;
inc : &():void = [&count]() { count = count + 1; };
inc();
inc();
// count == 2
```

### `this` capture in a member function

```k
class Adder {
    base : int;
    Adder(b:int) : base(b) {}
    getAdder() : &(int):int {
        return [this](x:int):int { return base + x; };
    }
}

test() : int {
    a : Adder = Adder(40);
    adder : &(int):int = a.getAdder();
    return adder(2);  // → 42
}
```

### Immediately invoked lambda

```k
result : int = ([](n:int):int { return n * 2; })(21);  // → 42
```

### Passed as an argument

```k
apply(f : &(int):int, v : int) : int { return f(v); }

test() : int {
    return apply([](n:int):int { return n + 1; }, 41);  // → 42
}
```

### Init capture

```k
base : int = 40;
f : &(int):int = [b = base + 2](n:int):int { return b + n; };
// b is captured as 42 at lambda-creation time
result : int = f(0);  // → 42
```

### `const` lambda

```k
mutables : int = 0;
readOnly : const &(int):int = const[&mutables](n:int):int {
    // mutables = n;  // ERROR: const lambda cannot write to by-reference capture
    return mutables + n;
};
```

### Return type deduced from context

```k
counter : &():int = []() { return 0; };  // return type int deduced from variable type
```

### Implicit all-by-reference capture (bracket omission)

```k
a : int = 10;
b : int = 32;
sum : &():int = () { return a + b; };  // a and b captured by reference implicitly
result : int = sum();  // → 42
```

---

## 12. Grammar summary

```ebnf
LambdaExpr:
    [ 'const' ] [ CaptureList ] '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement

CaptureList:
    '[' ']'
  | '[' Capture { ',' Capture } ']'

Capture:
    Identifier                           -- capture by value
  | '&' Identifier                       -- capture by mutable reference
  | 'const' '&' Identifier               -- capture by const reference
  | Identifier '=' Expression            -- init capture by value
  | '&' Identifier '=' Expression        -- init capture by mutable reference to lvalue
  | 'this'                               -- capture enclosing object by reference
  | '&' 'this'                           -- same as 'this'
  | 'const' 'this'                       -- capture enclosing object by const reference
  | 'const' '&' 'this'                   -- same as 'const this'

-- The following are NOT valid capture forms (parse error ERR_LAMBDA_BAD_CAPTURE_SYNTAX):
--   [&]   [=]   [&, x]   [=, &x]   [auto ...]
```

**Type of a lambda expression:** `&(Params):Ret` — a non-null callable reference.

**Callable reference types:**

| Qualifier | Nullable | Rebindable |
|-----------|----------|------------|
| `&` | No (non-null) | No (immutable binding) |
| `*` | Yes | Yes |
| `?` | Yes | No |
| `+` | No | Yes |

Lambdas always evaluate to the non-null `&` form.

---

*See also:* [Function References](function_references.md) · [Functions](functions.md) · [Types](../basic/types.md) · [Expressions](../expressions/call.md)
