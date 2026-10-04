# Klang

Klang is the compiler and runtime for **K**, a statically typed,
native-compiled systems programming language with C/C++-inspired syntax.

> **Status:** Working draft. K and its tooling are under active development.

## Overview

K combines explicit types, value-oriented aggregates, object-oriented
programming, deterministic resource management, templates, and compilation to
native code through LLVM. Its compiler driver is `klangc`; the repository also
contains `libk`, the K standard library, and `libkdi`, the metadata format and
tooling used for inter-module APIs.

```k
module hello;

main() : int {
    k::io::stdout().println("Hello, K!");
    return 0;
}
```

The base standard library is compiler-managed, so `k::io` is available without
an explicit `import k;`.

## Quick start

Configure and build the project:

```sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

Compile and run a K source file:

```sh
./cmake-build-debug/klang/klangc hello.k -o hello
./hello
```

To compile the included sample:

```sh
./cmake-build-debug/klang/klangc samples/fibo.k -o fibo
./fibo
```

`klangc` also produces object files (`-c`), shared libraries (`--dyn-lib`),
static libraries (`--static-lib`), and KDI interface descriptors for
libraries. See the [compiler manual](doc/man/klangc.md) and the
[CMake integration guide](doc/howtos/cmake-integration-k-projects.md) for
library builds, imports, linker paths, and external projects.

## Requirements

Building Klang requires:

- Linux and `liburing` 2.5 or newer
- CMake 3.17 or newer
- A C++20 compiler and a C/C++ linker toolchain
- LLVM development files discoverable by CMake
- `pkg-config` and `libcbor`
- Catch2, nlohmann_json, fmt, and Boost (components: program_options, system,
  filesystem)

`liburing` is a required build dependency; configuration fails without it or
on non-Linux platforms. Proactor initialization also checks the kernel's
`io_uring` capabilities and reports unavailable required data-plane operations
explicitly. Channel file and socket transport never falls back to blocking POSIX I/O.
Optional metadata/control operations (`open`, `statx`, `fsync`, `ftruncate`)
may instead use a bounded internal worker pool; this is not a transport fallback.

File, TCP/Unix stream and UDP/Unix datagram channels provide callback, Future
and Sync forms over the same proactor. Generic streams and synchronous file
streams remain separate. See the [I/O reference](doc/spec/stdlib/io-async.md)
and [network reference](doc/spec/stdlib/io-network.md), with examples in
[`file_channel_fully.k`](samples/file_channel_fully.k),
[`stream_channel_loopback.k`](samples/stream_channel_loopback.k) and
[`datagram_loopback.k`](samples/datagram_loopback.k). Application exit drains
I/O and callback cleanup before destroying Application fields on both normal
return and unwinding.

The [file and network I/O guide](doc/guides/io.md) explains callback, Future
and Sync usage with [nine runnable examples](samples/io/). Optional compilation:

```sh
cmake -S . -B cmake-build-debug -DKLANG_BUILD_SAMPLES=ON
cmake --build cmake-build-debug --target klang-io-samples
```

Samples are disabled by default. With the master option enabled,
`-DKLANG_BUILD_IO_SAMPLES=OFF` selects only the existing root samples.

## Documentation

- [Learn K](doc/tutorial/README.md) — progressive tutorials from a first
  program through modules and templates
- [Subsystem guides](doc/guides/) — in-depth technical guides (Date and Time, ...)
- [File and network I/O](doc/guides/io.md) — callback/Future/Sync walkthrough
- [Language reference](doc/spec/language/index.md) — complete language rules
  and grammar
- [Standard library reference](doc/spec/stdlib/index.md) — public `libk` APIs
- [How-to guides](doc/howtos/) — CMake integration and debugging
- [Compiler manual](doc/man/klangc.md) — `klangc` command-line options

## Repository layout

| Path | Contents |
|------|----------|
| `klang/` | K compiler implementation and compiler tests |
| `libk/` | K standard library and native runtime support |
| `libkdi/` | KDI descriptors, codecs, and tools |
| `doc/` | Tutorials, language and KDI specifications, manuals, and guides |
| `samples/` | Small K programs |

## Testing

After building, run the test suite from the build directory:

```sh
cd cmake-build-debug
ctest --output-on-failure
```

## Contributing

Contributions are welcome. Please keep changes focused, add regression tests
for behavioral fixes, and update the relevant documentation when changing K,
the standard library, or KDI behavior. Open an issue to discuss substantial
design changes before implementing them.

## License

Klang is licensed under the [Apache License, Version 2.0](LICENSE).
