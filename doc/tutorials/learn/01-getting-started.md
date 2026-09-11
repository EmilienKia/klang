# 1. Getting Started

Klang contains the `klangc` compiler, the K standard library (`libk`), and KDI
metadata tooling. K programs are compiled ahead of time to native code through
LLVM.

## Build Klang

From a checkout of the repository, configure and build the debug tree:

```sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

The compiler is then available at `cmake-build-debug/klang/klangc`. On an
installed system, use `klangc` from `PATH` instead.

## Write a first program

Create `hello.k`:

```k
module hello;

main() : int {
    k::io::stdout.println("Hello, K!");
    return 0;
}
```

Compile and run it from the repository root:

```sh
./cmake-build-debug/klang/klangc hello.k -o hello
./hello
```

The program prints:

```text
Hello, K!
```

## Read the program

- `module hello;` places the declarations in the `hello` namespace. A module
  declaration must be first in the file.
- `main` is the program entry point. It can return `int` for the process exit
  status, or omit a return type to return zero on success.
- `k::io::stdout` is the standard output printer and `println` appends a
  newline.
- The base standard library module, `k`, is compiler-managed: non-`k` programs
  do not need `import k;`.

K uses `name : Type` declarations, rather than `Type name`. You will use this
form for variables, fields, and parameters throughout the tutorials.

## Try a calculation

Replace `main` with this version:

```k
main() : int {
    answer : int = 6 * 7;
    k::io::stdout.println(answer);
    return 0;
}
```

The explicit `int` makes the program's intent clear and enables compile-time
type checking. The next chapter covers values, declarations, and expressions
in more detail.

**Next:** [Values and control flow](02-values-and-control-flow.md)
