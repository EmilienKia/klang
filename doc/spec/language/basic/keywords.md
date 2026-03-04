# Keywords

[← Index](../index.md) · [Lexical Conventions](lexical.md)

Keywords are reserved identifiers with special syntactic meaning.
They may **not** be used as ordinary identifiers.

---

## Complete keyword list

| Keyword     | Description |
|-------------|-------------|
| `bool`      | Boolean primitive type |
| `byte`      | 8-bit signed integer type |
| `char`      | 8-bit signed character type (alias of `byte`) |
| `short`     | 16-bit signed integer type |
| `int`       | 32-bit signed integer type |
| `long`      | 64-bit signed integer type |
| `float`     | 32-bit IEEE 754 floating-point type |
| `double`    | 64-bit IEEE 754 floating-point type |
| `unsigned`  | Unsigned modifier for integer types |
| `struct`    | Structure type declaration |
| `class`     | Class type declaration (with automatic virtual dispatch) |
| `interface` | Interface declaration (implicitly abstract contract type) |
| `namespace` | Namespace declaration |
| `module`    | Module (compilation unit) name declaration |
| `import`    | Import declaration |
| `static`    | Static storage / static member modifier |
| `const`     | Constant qualifier: marks a variable or parameter as immutable after construction |
| `public`    | Public visibility modifier |
| `protected` | Protected visibility modifier |
| `private`   | Private visibility modifier |
| `abstract`  | Abstract modifier: prevents instantiation / requires override |
| `final`     | Final modifier: prevents further inheritance or overriding |
| `this`      | Reference to the current object inside a member function |
| `return`    | Return statement |
| `if`        | Conditional statement |
| `else`      | Alternative branch of an `if` statement |
| `while`     | Condition-controlled loop |
| `for`       | Counter-controlled loop |

### Grammar

```
Keyword: (one of)
    bool     byte     char     short    int      long
    float    double   unsigned
    struct   class    interface   namespace   module   import
    static   const    abstract   final
    public   protected   private
    this     return
    if       else     while    for
```

---

## Notes

- `struct`, `class`, and `interface` declare user-defined aggregate types. See [Structures](../structs/structs.md), [Classes and Virtuality](../structs/classes.md), and [Interfaces](../structs/interfaces.md).
- `abstract` is valid on a `class` or its methods to mark them as requiring overriding. On an `interface` or its methods it is accepted but redundant (warning).
- `final` prevents further inheritance or overriding. See the relevant sections in [Classes and Virtuality](../structs/classes.md).
- `const` marks a variable or parameter as immutable. See [Const-ness](types.md#12-const-ness) for the full specification.
- `public`, `protected`, and `private` are parsed as visibility declarations inside namespace and struct bodies; they end with a colon (e.g., `public:`).
- `unsigned` is used as a type modifier: `unsigned int`, `unsigned short`, etc. It is not a standalone type.
- All other keywords are used in declarations or statements as described in the relevant reference sections.

---

*See also:* [Lexical Conventions](lexical.md) · [Types](types.md) · [Statements Overview](../statements/statements.md)
