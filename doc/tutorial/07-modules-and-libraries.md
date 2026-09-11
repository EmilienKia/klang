# 7. Modules and Libraries

A K module is a named compilation unit. A module without `main` is a library;
`klangc` emits its native binary and a KDI descriptor, which describes the
library's public API to other K programs.

## Build a library

Create `geometry.k`:

```k
module geometry;

public squareArea(side : int) : int {
    return side * side;
}
```

Build a shared library and its KDI file:

```sh
klangc --dyn-lib geometry.k
```

The result is `libgeometry.so` and `libgeometry.kdi`. `--static-lib` produces
a static archive instead; passing both flags produces both library forms.

Namespace-level declarations are public by default. Mark an API `public`
explicitly when it improves readability, and use `private` for
implementation-only declarations. Public and protected declarations are
recorded in KDI metadata.

## Consume the library

Create `app.k` beside the generated files:

```k
module app;
import geometry;

main() : int {
    result : int = geometry::squareArea(9);
    k::io::stdout.println(result);
    return 0;
}
```

Compile and link it:

```sh
klangc -I . -L . -l geometry app.k -o app
```

`-I` supplies KDI search directories, `-L` supplies library search directories,
and `-l geometry` selects `libgeometry.so`. Imported names remain in their
module namespace, so calls use `geometry::squareArea` rather than an unqualified
name.

## Multi-file modules

One module may span several files. Pass them to a single `klangc` invocation:

```sh
klangc geometry-shapes.k geometry-math.k --dyn-lib
```

All files must declare the same module or omit the declaration after one file
establishes it. They share one top-level namespace and produce one output.

## Imports and deployment

An `import` must appear after `module` and before other declarations. The
compiler resolves the imported `.kdi` first, then links the matching native
library. For installed or non-local dependencies, use `-I`, `-L`, or the
colon-separated `KLANG_LIB_PATH` environment variable to make both artifacts
discoverable.

For a CMake project, see [Integrating K compilation into CMake
projects](../howtos/cmake-integration-k-projects.md).

**Next:** [Generics and advanced techniques](08-generics-and-advanced-techniques.md)
