# Compiler Diagnostics (Selected)

This page documents high-signal diagnostics that are commonly triggered by
newer language constructs.

## Template-qualified scope references in expressions

The expression parser accepts these forms:

```k
Type<T>::member(...)
ns::Type<T>::member(...)
::ns::Type<T>::member(...)
```

When the sequence after `::` is malformed, the parser reports an error at the
exact token position (file, line, column) using the current lexeme.

### Typical parser message

- **Message text**: `Qualified identifier expect an identifier after intermediate "::"`
- **Trigger**: a `::` separator is not followed by a valid identifier segment.

### Examples

```k
// Invalid: missing member name after ::
return ::k::Expected<int, int>::;

// Invalid: non-identifier token after ::
return Type<int>::(42);
```

### Fix guidance

- Ensure each `::` is followed by a valid identifier segment.
- Keep template arguments on the qualifier type, then continue with `::member`.
- Valid references include:

```k
return Box<int>::make(42);
return math::Box<int>::make(42);
return ::k::Expected<int, int>::expected(42);
```

## Assignment operator return conventions

Assignment operators may return any type. The compiler recommends returning a
reference to the owning type so that assignments can be chained:

```k
template<typename T>
struct Holder {
    value : T;
    operator =(input : T) : Holder<T>& {
        value = input;
        return this;
    }
}
```

- **`0060F`**: an assignment operator has no return type (including `void`).
- **`00610`**: an assignment operator returns a type other than a reference to
  its owning type.

These warnings apply to concrete functions, including unused non-template
operators. Dependent member-template declarations are checked only when
instantiated, after their return types have been resolved. Deleted operators
are not checked. A concrete operator is checked once during type resolution,
even when its owning aggregate is revisited.

Messages use source-level operator and template type names, for example:

```text
Assignment operator 'operator=' in 'Holder<int>' returns 'int' instead of 'Holder<int>&'; returning a reference to the owning type is recommended for chaining
```

This is a convention warning, not a requirement to change ownership, copy,
conversion, or drain-assignment behavior.
