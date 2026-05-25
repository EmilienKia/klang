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

