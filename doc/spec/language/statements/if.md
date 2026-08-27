# If Statement

[← Index](../index.md) · [Statements](statements.md)

The `if` statement conditionally executes a branch based on a boolean test expression.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Else-if chains](#3-else-if-chains)
4. [Examples](#4-examples)
5. [Link guard (soft-fail)](#5-link-guard-soft-fail)
6. [Condition-variable declaration (`if-let`)](#6-condition-variable-declaration-if-let)
7. [Condition-variable form with trailing test expression](#7-condition-variable-form-with-trailing-test-expression)
8. [Multi-variable form with trailing test expression (hard-fail)](#8-multi-variable-form-with-trailing-test-expression-hard-fail)
9. [Multi-variable soft-fail form (no trailing test expression)](#9-multi-variable-soft-fail-form-no-trailing-test-expression)
---
## 1. Syntax
### Grammar
```
IfElseStatement:
    'if' '(' Expression ')' Statement
    | 'if' '(' Expression ')' Statement 'else' Statement
    | 'if' '(' IfCondVarDecl ')' Statement
    | 'if' '(' IfCondVarDecl ')' Statement 'else' Statement
    | 'if' '(' IfCondVarDeclList ';' ConditionalExpr ')' Statement
    | 'if' '(' IfCondVarDeclList ';' ConditionalExpr ')' Statement 'else' Statement
    | 'if' '(' IfCondVarDeclList ')' Statement
    | 'if' '(' IfCondVarDeclList ')' Statement 'else' Statement

IfCondVarDeclList:
    IfCondVarDecl { ';' IfCondVarDecl }

IfCondVarDecl:
    { Specifier } Identifier ':' TypeSpec [ CondVarInitialiser ]

CondVarInitialiser:
    '=' ConditionalExpr
    | '(' [ ExpressionList ] ')'
    | BraceInitList
```
The test expression (or condition variable declaration) is enclosed in parentheses.  
Both the `then` branch and the `else` branch may be a single statement or a block statement.
The `if(var; test)` and `if(var1; var2; ...; test)` forms declare variables scoped to
the `if` statement and use a separate test expression for branching (see §7 and §8).
The `if(var1; var2; ...)` form without test expression uses soft-fail: addressor
variables are null-checked and union alternative accesses used during condition-variable
initialization may fail softly to `else` (see §9).
---
## 2. Semantics
1. The test expression is evaluated.
2. If the result is true (non-zero), the `then` branch is executed.
3. If the result is false (zero) and an `else` clause is present, the `else` branch is executed.
4. If the result is false and there is no `else`, execution continues after the `if` statement.
The test expression may be of any type convertible to `bool` (integers, pointers, etc.).
---
## 3. Else-if chains
An `else` clause may immediately contain another `if`, forming an else-if chain.  
There is no explicit `else if` keyword; this is simply nesting.
```k
if (cond1) {
    // branch 1
} else if (cond2) {
    // branch 2
} else {
    // default branch
}
```
The `else` always binds to the nearest preceding unmatched `if` (dangling-else rule).
---
## 4. Examples
### Simple if-then-else
```k
min(a: int, b: int) : int {
    if (a < b)
        return a;
    else
        return b;
}
```
### If-then-else with blocks
```k
max(a: int, b: int) : int {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}
```
### Else-if chain (fibonacci)
```k
fibo(i: unsigned short) : unsigned int {
    if (i == 0) return 1;
    else if (i == 1) return 1;
    return fibo(i - 1) + fibo(i - 2);
}
```
### Pointer guard
```k
assign(i: int, j: int) : int {
    p : int*;
    if (i < j) {
        p = &a;
    } else {
        p = &b;
    }
    *p += i + j;
    return *p;
}
```
---
## 5. Link guard (soft-fail)

When the condition expression of an `if` contains a **link assignment** (rebind)
from a nullable source (pointer `*`, view `?`, or owner `!`), or a **dynamic
downcast** to a link type (`+`), the usual fatal-trap-on-null behaviour is
replaced by a **soft-fail**:

1. If the source is null or the dynamic cast fails (RTTI mismatch), the
   assignment is skipped and the condition evaluates to `false`.
2. If an `else` clause is present, the `else` branch is executed.
3. If there is no `else`, execution continues after the `if` statement.

The link variable retains its previous value on the soft-fail path (the store
is skipped).

> **Note:** Outside of an `if` condition, assigning a null value to a link
> remains a fatal error (throws `NullAssignationError` or
> `NullCastError`).

### Link rebind guard
```k
test() : int {
    x : int = 42;
    lnk : int+ = &x;
    p : int* = null;
    if (lnk = p) {
        // entered only if p is non-null
        return *lnk;
    } else {
        // entered because p is null — lnk still points to x
        return 0;
    }
}
```

### Dynamic downcast guard
```k
class Base {
    public val : int;
    public Base(v : int) : val(v) {}
    public dummy() : int { return 0; }
}
class Derived : public Base {
    public extra : int;
    public Derived(v : int) : Base(v), extra(99) {}
    public get_extra() : int { return extra; }
}
class Other : public Base {
    public data : int;
    public Other(v : int) : Base(v), data(0) {}
}

get_extra_fn(d : Derived&) : int { return d.get_extra(); }

test() : int {
    o : Other(5);
    bp : Base* = &o;
    d : Derived(1);
    dlnk : Derived+ = &d;
    if (dlnk = (Derived+) bp) {
        // entered only if bp points to a Derived (RTTI match)
        return get_extra_fn(*dlnk);
    } else {
        // entered because bp points to Other, not Derived
        return -1;
    }
}
```

### Nesting
Nested `if` statements each have their own soft-fail destination.  An inner
`if` with a link guard soft-fails to its own `else` (or continuation) without
affecting the outer `if`.

---
## 6. Condition-variable declaration (`if-let`)

An `if` statement may declare a local variable as its condition instead of
providing an expression.  The variable declaration has exactly the same form as a
regular local variable declaration (`name : type = expr`, `name : type(args)`, or
`name : type {init}`), but is written inside the parentheses and is **not**
terminated by a semicolon.

```k
if (myvar : int = callSomething()) {
    // myvar is accessible here
} else {
    // myvar is accessible here too unless initialization soft-failed — see below
}
// myvar is NOT accessible here — it has been destroyed
```

### Boolean casting and soft-fail

The value of the declared variable determines which branch to take.  In the
classic if-let form, two runtime checks may convert the condition into a
**soft-fail** instead of throwing a `NullAssignationError` / `NullCastError`:

1. **Nullable addressor soft-fail**: when the declared variable is an addressor
   type and the initializer produces `null`.
2. **Union alternative soft-fail**: when the initializer explicitly accesses a
   union alternative (`u.alt`) and the union's active discriminant does not
   match that alternative.

On a soft-fail, the `else` branch is taken (or execution continues after the
`if` if there is no `else`) and the condition variable does not exist on that
path. For non-soft-failing cases, the variable value is cast to `bool`:

| Variable type                 | Behaviour                              |
|-------------------------------|----------------------------------------|
| Numeric primitive (`int`, `float`, …) | `!= 0` → then, `== 0` → else |
| Aggregate (struct/class)              | User-defined `operator() : bool` is called |
| Pointer `*`, owner `!`, view `?`      | `!= null` → then, `== null` → else (**soft-fail**: var not in else) |
| Reference `&`, link `+`              | non-null → then, null → else (**soft-fail**: var not in else) |

Union alternative mismatch is orthogonal to the declared variable type:

- `if (x : int = u.first)` soft-fails when `u` does not currently hold `first`.
- `if (r : S+ = &u.second)` soft-fails when `u` does not currently hold `second`.
- `if (p : T* = u.third)` first soft-fails on union mismatch; if the alternative
  matches, it then follows the regular pointer null-check rules.

### Scope and lifetime

The condition variable is local to the `if` statement:

- For **non-addressor types** (int, struct, etc.): the variable is **visible**
  in both the `then` and `else` branches.
- For **addressor types** (ref, link, ptr, view, owner): the variable is
  **only visible in the `then` branch** (soft-fail means it may be null/unset
  on the `else` path).
- It is **destroyed** at the end of each branch:
  - Destructors are called for aggregate types.
  - Memory is freed for owner types.
- Destruction also occurs on early exits (`return`, `break`, `continue`).
- After the `if` statement, the variable no longer exists.  A new variable with
  the same name may be declared.

### Addressor soft-fail

When the declared variable is an **addressor type** (reference `&`, link `+`,
pointer `*`, view `?`, or owner `!`) and its initialiser evaluates to `null`,
a **soft-fail** is triggered:

1. The variable initialisation is skipped or its value is null.
2. The `else` branch is taken (or execution continues after the `if` if there is
   no `else`).
3. The variable is **not visible** in the `else` branch — using it is a
   compile-time error.

```k
test() : int {
    p : int* = null;
    if (lnk : int+ = p) {
        // entered only if p is non-null — lnk is valid
        return *lnk;
    } else {
        // entered because p is null — lnk does NOT exist here
        return -1;
    }
}
```

### Union alternative soft-fail

When the initializer of a condition variable performs an explicit access to a
union alternative, the compiler inserts a runtime discriminant check.

If the union does **not** currently hold the requested alternative, the access
does **not** trap in classic if-let form. Instead:

1. The condition soft-fails.
2. The `else` branch is taken (or execution continues after the `if` if no
   `else` is present).
3. The condition variable is not created on that path.

```k
union U {
    first: int;
    second: long;
}

test() : int {
    u : U;
    u.second = 9;
    if (v : int = u.first) {
        return v;
    } else {
        return -1;   // entered because active alternative is 'second'
    }
}
```

This behaviour applies uniformly to all condition-variable forms (classic if-let,
multi-variable soft-fail, and forms with a trailing test expression).

### Nested `if-let`

Condition variable declarations can be nested.  Each `if` defines its own
scope, so there is no name collision:

```k
test() : int {
    if (a : int = 3) {
        if (b : int = 5) {
            return a + b;   // 8
        }
    }
    return 0;
}
```

---
## 7. Condition-variable form with trailing test expression

A condition-variable declaration may be followed by a semicolon and a separate
test expression. In this form, the variable is declared and initialized using
pattern-like soft-fail semantics. If binding succeeds, branching is determined
by the **test expression** — not by a boolean cast of the variable.

```k
if (myVar : MyStruct& = getSomething(); myVar.aTest()) {
    // entered when myVar binds successfully and myVar.aTest() returns true
} else {
    // entered when binding soft-fails or myVar.aTest() returns false
}
```

### Pattern-like soft-fail

When using the `if(var; test)` form, condition-variable initialization uses
pattern-like soft-fail semantics:

- If the condition variable is an addressor (ref `&`, link `+`, ptr `*`, view `?`,
  or owner `!`) and evaluates to `null` or dynamic downcast fails, it **soft-fails**:
  the trailing `test` expression is **not evaluated**, the variable is cleaned up,
  and execution branches directly to `else` (or continues past `if`).
- If the initializer accesses a union alternative (`u.alt`) whose active discriminant
  does not match, it **soft-fails**: the trailing `test` expression is **not evaluated**,
  and execution branches directly to `else` (or continues past `if`).
- If binding succeeds, the trailing `test` expression is evaluated to determine branching.

### Key differences with classic `if-let`

| Aspect                        | Classic if-let                  | `if(var; test)` form            |
|-------------------------------|----------------------------------|---------------------------------|
| Branch condition              | Boolean cast of variable value   | Separate test expression (if bound) |
| Ref/link soft-fail            | Yes — null → else branch         | Yes — null → else (skips test)  |
| Union access soft-fail        | Yes — alt mismatch → else branch | Yes — alt mismatch → else (skips test) |
| Pointer null soft-fail        | Yes — null → else branch         | Yes — null → else (skips test)  |
| Variable in else branch       | Not visible on soft-fail paths   | Not visible on soft-fail paths  |

### Examples

```k
// Variable declared, test determines branching
test() : int {
    if (x : int = 42; x > 0) {
        return x;       // entered because x > 0
    }
    return 0;
}

// Zero variable would be false in classic if-let, but separate test overrides
test2() : int {
    if (x : int = 0; true) {
        return 1;       // entered because test is true, despite x == 0
    }
    return -1;
}

// Reference variable with separate test — soft-fails if null, skips test
test3() : int {
    p : int* = null;
    if (p1 : int* = p; *p1 > 0) {
        return *p1;     // safe: *p1 > 0 is skipped because p is null
    } else {
        return -1;      // entered because p is null
    }
}
```

---
## 8. Multi-variable form with trailing test expression

The condition-variable form with a trailing test expression can be extended with
**multiple variable declarations**,
each separated by a semicolon. The last semicolon-separated element is always the
test expression that determines branching.

```k
if (v1 : T1 = e1; v2 : T2 = e2; v3 : T3 = e3; testExpr) {
    // all variables are accessible here
} else {
    // entered if any binding fails or testExpr is false
}
```

### Declaration order and short-circuit

Variables are declared **left to right**. Later declarations may reference
earlier ones. If any variable soft-fails (null-check failure or union alternative
mismatch), subsequent variable declarations and the trailing test expression are
**not evaluated** (short-circuit), and previously-initialized variables are
cleaned up before branching to `else`.

```k
if (a : int = 1; b : int = a + 1; c : int = b + 1; c == 3) {
    return a + b + c;   // 6
}
```

### All initialiser forms supported

Each variable declaration supports assignment (`= expr`), constructor
(`T(args)`), and brace initialisation (`T{...}`):

```k
if (x : int = 5; s : MyStruct{.a = x, .b = x + 1}; s.a + s.b > 10) {
    return s.a + s.b;
}
```

### Scope and lifetime

- All variables are scoped to the `if` statement.
- In the `then` branch, all variables are accessible and destroyed at the end of the block in reverse order.
- On soft-fail or when `testExpr` evaluates to `false`, all initialized variables are cleaned up before branching to `else`.
- After the `if` statement, none of the variables exist.

There is no soft-fail mechanism in this form.

### Scope and lifetime

- All variables are scoped to the `if` statement.
- All variables are accessible in both the `then` and `else` branches.
- Destructors are called for all variables at the end of each branch, in
  **reverse declaration order** (last declared is destroyed first).
- After the `if` statement, none of the variables exist.

### Test expression is mandatory

When multiple variables are declared, a test expression **must** follow the last
semicolon.  Omitting it is a parse error.  (For a single variable without a
separate test, use the classic if-let form instead.)

### Examples

```k
// Two variables, second references first
test() : int {
    if (a : int = 3; b : int = a + 1; b > 3) {
        return a + b;   // 7
    }
    return 0;
}

// Struct variables with destructors
test2() : int {
    if (s1 : S = makeS(10); s2 : S = makeS(s1.val + 5); s2.val > 10) {
        return s1.val + s2.val;   // 25
    }
    return 0;
}

// Nested multi-var if
test3() : int {
    if (a : int = 1; b : int = 2; a + b > 0) {
        if (c : int = a + b; d : int = c * 2; d == 6) {
            return d;   // 6
        }
    }
    return 0;
}
```

---
## 9. Multi-variable soft-fail form (no trailing test expression)

When multiple condition variables are declared **without** a trailing test
expression, the `if` statement uses **soft-fail** semantics. Two families of
runtime failures are converted into a branch-to-`else` (or continuation after
the `if` if there is no `else`):

- nullable addressor failure: an addressor variable (pointer `*`, reference `&`,
  link `+`, view `?`, owner `!`) becomes null or cannot be bound from a nullable source;
- union alternative mismatch: an initializer explicitly accesses a union
  alternative that is not currently active.

```k
if (p1 : Foo* = getPtr(); p2 : Bar* = p1->getBar(); p3 : Baz* = p2->getBaz()) {
    // all three are non-null
    use(p3);
} else {
    // at least one was null — no variables are visible here
}
```

### Behaviour summary

| Form | Soft-fail | Branch condition | Vars in else | Cleanup in else |
|---|---|---|---|---|
| `if(var)` single, non-addressor | no, except union alt mismatch | `bool(var)` | yes on success / **no** on union soft-fail | yes on success / **no** on soft-fail |
| `if(var)` single, addressor | **yes** | non-null after successful init | **no** | **no** |
| `if(var1; var2; ...)` multi, no test | **yes** | all initializers succeed; addressors also must be non-null | **no** | **no** |
| `if(var1; ...; test)` with test | **yes** | `test` (evaluated only if all bindings succeed) | **no** on soft-fail / cleaned up before else | **no** |

### Declaration order and short-circuit

Variables are declared **left to right**.  Later declarations may reference
earlier ones.  If a variable soft-fails (null-check failure or union alternative
mismatch), subsequent variable declarations are **not evaluated** (short-circuit),
preventing invalid chained accesses.

```k
// Safe chained dereference — if getPtr() returns null, getBar() is never called
if (a : Foo* = getPtr(); b : Bar* = a->getBar()) {
    use(b);
}
```

### Non-addressor variables

Non-addressor variables (integers, structs, etc.) are allowed and are **never**
null-checked.  They always succeed and proceed to the next variable.

```k
// x is an int — always succeeds. p is null-checked.
if (x : int = compute(); p : Foo* = getPtr(x)) {
    use(x, p);
}
```

### Variables not visible in else

In this form, **no variables** are visible in the `else` branch.  This is
because the soft-fail may have occurred at any point during the chain, and
variables after the failure point were never initialized.

### Cleanup

When a soft-fail occurs at variable `i`, all previously-initialized variables
(`0` through `i-1`, or `0` through `i` for pointer/view/owner types where
the null value was stored) are cleaned up in **reverse declaration order**
(destructors called, owners freed) before jumping to `else`.

In the `then` branch, all variables are cleaned up in reverse order at the
end of the block, as usual.

### Examples

```k
// Two pointers — second is null → enters else
test() : int {
    a : int = 10;
    if (p1 : int* = &a; p2 : int* = null) {
        return *p1 + *p2;
    } else {
        return -1;   // -1
    }
}

// Two links — first null → enters else
test2() : int {
    pn : int* = null;
    b : int = 20;
    pb : int* = &b;
    if (l1 : int+ = pn; l2 : int+ = pb) {
        return *l1 + *l2;
    } else {
        return -2;   // -2
    }
}

// Struct cleanup on soft-fail
test3() : int {
    g_dtor_count = 0;
    if (s : S = makeS(1); p : int* = null) {
        return 1;
    }
    // s's destructor was called during soft-fail cleanup
    return g_dtor_count;   // >= 1
}

// Union mismatch on second variable — soft-fails before evaluating later vars
union U {
    first: int;
    second: long;
}

test4() : int {
    u : U;
    u.first = 5;
    if (a : int = u.first; b : long = u.second) {
        return 1;
    } else {
        return 2;    // entered because 'second' is not active
    }
}
```

---
*See also:* [Statements](statements.md) · [While Statement](while.md) · [For Statement](for.md) · [Expressions](../expressions/expressions.md)
