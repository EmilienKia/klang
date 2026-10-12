# How-To: Integrating K Compilation into CMake Projects

This guide explains how to integrate the compilation of **K language libraries and executables** (`.k` source files) into CMake build systems, both **within the Klang repository** (in-tree) and in **external independent projects** (out-of-tree).

---

## 1. Preamble and Prerequisites for External Projects

For an external CMake project building K sources, assume that the Klang toolchain and standard library are installed and accessible on the build host:

- **Compiler executable:** `klangc` is installed in `PATH` (e.g. `/usr/local/bin/klangc` or `/usr/bin/klangc`).
- **KDI inspection tool:** `kditool` is installed in `PATH` (e.g. `/usr/local/bin/kditool`).
- **Standard runtime library:** `libk.so` and `libk.a` are in system library paths (e.g. `/usr/local/lib` or `/usr/lib`) or specified via `LD_LIBRARY_PATH`.
- **Standard KDI descriptors:** `libk.kdi` and `k.kdi` are in system KDI paths (e.g. `/usr/local/lib/kdi`, `/usr/lib/kdi`) or discoverable via the `KLANG_LIB_PATH` environment variable.
- **Proactor dependency:** Linux and `liburing` 2.5 or newer are mandatory.
  Shared runtime linkage requires the liburing shared library at execution.
  A consumer linking `libk.a` must also link liburing (`-l uring`) and the
  other native runtime dependencies; an archive does not carry `DT_NEEDED`.
- **Runtime FFI dependency:** `libffi` development files and pkg-config
  metadata are required when building Klang. Shared runtime consumers need
  the matching native shared dependencies at execution; archive consumers
  must supply libffi and the rest of the native dependency closure at link time.

These external examples assume a populated toolchain prefix, which may also
be staged by manually copying `klangc`, `kditool`, `libk.so`, `libk.a`,
`libk.kdi` and `k.kdi` from the build outputs. The repository has no CMake
install rules; automated installation support is unchanged by the proactor
migration, and `cmake --install` alone does not populate such a prefix.
Build-tree paths below remain usable independently of packaging.

If the toolchain is installed in a non-standard location (e.g. `/opt/klang`), ensure your environment points to it:
```bash
export PATH="/opt/klang/bin:$PATH"
export KLANG_LIB_PATH="/opt/klang/lib/kdi:/opt/klang/lib"
export LD_LIBRARY_PATH="/opt/klang/lib:$LD_LIBRARY_PATH"
```

### 1.2 Build-tree package metadata and static consumers

The build generates `cmake-build-debug/libk/libk/libk.pc`, colocated with
`libk.so`, `libk.a`, `libk.kdi` and `k.kdi`. It describes existing build
outputs; it does not add CMake installation rules. Its `libdir` and
`kdi_dir` are relative to `${pcfiledir}`, so copying these artifacts together
to another directory preserves their package paths.

```bash
export PKG_CONFIG_PATH="$PWD/cmake-build-debug/libk/libk${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
pkg-config --variable=libdir libk
pkg-config --variable=kdi_dir libk
pkg-config --libs libk
pkg-config --libs --static libk
```

Shared metadata provides `-L… -lk`. Static metadata additionally supplies
libffi, liburing, native thread, dynamic-loader and math dependencies, and
ICU dependencies only if ICU support was enabled when building the runtime.
Link native consumers with a **C++ driver**, such as `clang++`, even if
their own source is C: K exceptions use the C++ ABI runtime.

**`pkg-config --static` does not force static library selection.** A normal
`-lk` can still select `libk.so`. For a native CMake consumer, use the full
archive path and remove only the generic `-lk` from the ordered dependency
flags:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(K_RUNTIME REQUIRED libk)
pkg_get_variable(K_RUNTIME_LIBDIR libk libdir)
pkg_get_variable(K_RUNTIME_KDI_DIR libk kdi_dir)

set(K_RUNTIME_STATIC_DEPS ${K_RUNTIME_STATIC_LDFLAGS})
list(REMOVE_ITEM K_RUNTIME_STATIC_DEPS "-lk")

add_executable(native_host src/native_host.cpp)
target_link_libraries(native_host PRIVATE
    "${K_RUNTIME_LIBDIR}/libk.a"
    ${K_RUNTIME_STATIC_DEPS}
)
```

Keeping the archive before its dependencies matters for linkers that resolve
archives from left to right. Shared consumers can instead use
`pkg_check_modules(K_RUNTIME REQUIRED IMPORTED_TARGET libk)` and
`PkgConfig::K_RUNTIME`. Runtime shared-library discovery still needs the
appropriate loader path or RPATH.

For `klangc`, use `-I <kdi_dir>` for metadata and `-L <libdir>` for binaries.
The compiler automatically imports and links module `k`. A static consumer
must select a directory containing `libk.a` and `k.kdi` **but no `libk.so`**,
and put it first in the KDI search order as well as in `-L`. Run from a
directory without another `libk.so`, because the current directory is
searched before `-I` directories; use `--no-lib-path-env` when an inherited
`KLANG_LIB_PATH` could introduce a different runtime.

```bash
klangc --no-lib-path-env -I /path/to/static-runtime -L /path/to/static-runtime \
    main.k native_helper.o -l ffi -l uring -l pthread -l dl -l m -o myapp
```

This example lists the basic native dependencies. Also pass the configured
dependency library directories and any additional libraries reported by
`pkg-config --libs --static libk`, including ICU when enabled. The compiler's
`-l` option accepts library names or library paths, **not arbitrary linker
options**: use `-l pthread` for thread linkage rather than `-l -pthread`.
If metadata contains other native driver options, use a separate C++ final
link step rather than silently discarding them. A full archive path in an
extra `-l` does not disable the compiler's automatic base `-lk`; isolating
the runtime directory avoids a mixed shared/static link.

If runtime FFI looks up a native helper through `Module::lookupSymbol()` on
the application module returned by `applicationModule()`, also pass
`--export-symbol <exact-native-name>` to `klangc`. On GNU/Linux ELF
executables this exports that selected native name for dynamic lookup; it
does not infer a C signature or export every helper. This is unnecessary
for normal direct calls through `@ffi::Extern("C")`. See the
[compiler manual](../man/klangc.md) and [FFI reference](../spec/stdlib/ffi.md).

### Package-metadata relocation limitations

Use the package name with `pkg-config --with-path=<directory> libk` rather
than querying an absolute `.pc` filename: some pkgconf versions split
package queries even when the filename is supplied as one argument.

Package-metadata relocation is tested in directories without whitespace or
shell quote characters. In pkgconf 1.8.1, `${pcfiledir}` expansion can escape
spaces even in `--variable` output, while an apostrophe can cause unquoted
`Libs` fragments to disappear. Quoting `${libdir}` in the `.pc` file instead
can double-escape already escaped spaces. Neither quoting the command-line
argument nor decoding emitted flags once repairs these tool-side errors.
Keep the package directory free of these characters when relying on this
tool version; arbitrary package paths are not guaranteed. Direct compiler
argument handling is tested separately with spaces and apostrophes, including
relocated native shared/archive runtime paths and archive-only K consumer
source, object and executable paths.

---

## 2. In-Tree Integration (Inside the Klang Repository)

When adding a K library or executable inside the Klang repository itself (e.g. in `samples/` or `libk/`), use CMake target generator expressions to reference `klangc` and the build-tree stdlib paths.

### 2.1 In-Tree K Library Example

```cmake
# Building a shared + static library inside klang repo
set(MYLIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/math.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.k
)
set(MYLIB_SO  ${CMAKE_CURRENT_BINARY_DIR}/libmylib.so)
set(MYLIB_A   ${CMAKE_CURRENT_BINARY_DIR}/libmylib.a)
set(MYLIB_KDI ${CMAKE_CURRENT_BINARY_DIR}/libmylib.kdi)

add_custom_command(
    OUTPUT  ${MYLIB_SO} ${MYLIB_A} ${MYLIB_KDI}
    COMMAND $<TARGET_FILE:klangc>
            --dyn-lib --static-lib
            --module-name mylib
            -I ${KLANG_STDLIB_KDI_DIR}
            -L ${KLANG_STDLIB_LIB_DIR}
            ${MYLIB_SOURCES}
    DEPENDS klangc libk_stdlib ${MYLIB_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling K library: mylib"
    VERBATIM
)

# Target alias for dependency management
add_custom_target(mylib ALL
    DEPENDS ${MYLIB_SO} ${MYLIB_A} ${MYLIB_KDI}
)
```

### 2.2 In-Tree K Executable Example

```cmake
set(APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.k)
set(APP_BIN ${CMAKE_CURRENT_BINARY_DIR}/myapp)

add_custom_command(
    OUTPUT  ${APP_BIN}
    COMMAND $<TARGET_FILE:klangc>
            -g
            -I ${KLANG_STDLIB_KDI_DIR}
            -I ${CMAKE_CURRENT_BINARY_DIR}
            -L ${KLANG_STDLIB_LIB_DIR}
            -L ${CMAKE_CURRENT_BINARY_DIR}
            ${APP_SOURCES}
            -l mylib
            -o ${APP_BIN}
    DEPENDS klangc libk_stdlib mylib ${APP_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling K executable: myapp"
    VERBATIM
)

add_custom_target(myapp ALL
    DEPENDS ${APP_BIN}
)
```

---

## 3. Out-of-Tree Integration (External Independent Project)

In an external CMake project, define reusable CMake helper functions (`add_k_library` and `add_k_executable`) using `find_program(KLANGC_EXECUTABLE klangc REQUIRED)`.

### 3.1 Reusable CMake Helper Module (`KlangAddTargets.cmake`)

Create a helper file `cmake/KlangAddTargets.cmake`:

```cmake
# cmake/KlangAddTargets.cmake
find_program(KLANGC_EXECUTABLE klangc REQUIRED
    DOC "Path to the klangc compiler"
)

# ---------------------------------------------------------------------------
# add_k_library(<target_name>
#     MODULE_NAME <name>
#     SOURCES <file1.k> ...
#     [TYPE SHARED|STATIC|BOTH]      (default: BOTH)
#     [KDI_INCLUDES <dir1> ...]
#     [LIB_DIRS <dir1> ...]
#     [LINK_LIBS <lib1> ...]
#     [OBJECTS <file1.o> ...]
# )
# ---------------------------------------------------------------------------
function(add_k_library TARGET_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        ""
        "MODULE_NAME;TYPE"
        "SOURCES;KDI_INCLUDES;LIB_DIRS;LINK_LIBS;OBJECTS;DEPENDS"
    )

    if(NOT ARG_MODULE_NAME)
        set(ARG_MODULE_NAME "${TARGET_NAME}")
    endif()

    if(NOT ARG_TYPE)
        set(ARG_TYPE "BOTH")
    endif()

    set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    set(LIB_SO  "${OUT_DIR}/lib${ARG_MODULE_NAME}.so")
    set(LIB_A   "${OUT_DIR}/lib${ARG_MODULE_NAME}.a")
    set(LIB_KDI "${OUT_DIR}/lib${ARG_MODULE_NAME}.kdi")

    set(KLANG_FLAGS "")
    set(OUTPUT_FILES "")

    if(ARG_TYPE STREQUAL "SHARED")
        list(APPEND KLANG_FLAGS "--dyn-lib")
        set(OUTPUT_FILES ${LIB_SO} ${LIB_KDI})
    elseif(ARG_TYPE STREQUAL "STATIC")
        list(APPEND KLANG_FLAGS "--static-lib")
        set(OUTPUT_FILES ${LIB_A} ${LIB_KDI})
    else()
        list(APPEND KLANG_FLAGS "--dyn-lib" "--static-lib")
        set(OUTPUT_FILES ${LIB_SO} ${LIB_A} ${LIB_KDI})
    endif()

    list(APPEND KLANG_FLAGS "--module-name" "${ARG_MODULE_NAME}")

    foreach(inc ${ARG_KDI_INCLUDES})
        list(APPEND KLANG_FLAGS "-I" "${inc}")
    endforeach()

    foreach(libdir ${ARG_LIB_DIRS})
        list(APPEND KLANG_FLAGS "-L" "${libdir}")
    endforeach()

    foreach(lib ${ARG_LINK_LIBS})
        list(APPEND KLANG_FLAGS "-l" "${lib}")
    endforeach()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND KLANG_FLAGS "-g")
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT_FILES}
        COMMAND ${KLANGC_EXECUTABLE}
                ${KLANG_FLAGS}
                ${ARG_SOURCES}
                ${ARG_OBJECTS}
        DEPENDS ${ARG_SOURCES} ${ARG_OBJECTS} ${ARG_DEPENDS}
        WORKING_DIRECTORY ${OUT_DIR}
        COMMENT "Compiling K library ${TARGET_NAME} (${ARG_MODULE_NAME})"
        VERBATIM
    )

    add_custom_target(${TARGET_NAME} ALL
        DEPENDS ${OUTPUT_FILES}
    )
endfunction()

# ---------------------------------------------------------------------------
# add_k_executable(<target_name>
#     SOURCES <file1.k> ...
#     [OUTPUT_NAME <name>]
#     [KDI_INCLUDES <dir1> ...]
#     [LIB_DIRS <dir1> ...]
#     [LINK_LIBS <lib1> ...]
#     [OBJECTS <file1.o> ...]
#     [DEPENDS <target1> ...]
# )
# ---------------------------------------------------------------------------
function(add_k_executable TARGET_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        ""
        "OUTPUT_NAME"
        "SOURCES;KDI_INCLUDES;LIB_DIRS;LINK_LIBS;OBJECTS;DEPENDS"
    )

    if(NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME "${TARGET_NAME}")
    endif()

    set(OUT_BIN "${CMAKE_CURRENT_BINARY_DIR}/${ARG_OUTPUT_NAME}")
    set(KLANG_FLAGS "")

    foreach(inc ${ARG_KDI_INCLUDES})
        list(APPEND KLANG_FLAGS "-I" "${inc}")
    endforeach()

    foreach(libdir ${ARG_LIB_DIRS})
        list(APPEND KLANG_FLAGS "-L" "${libdir}")
    endforeach()

    foreach(lib ${ARG_LINK_LIBS})
        list(APPEND KLANG_FLAGS "-l" "${lib}")
    endforeach()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND KLANG_FLAGS "-g")
    endif()

    list(APPEND KLANG_FLAGS "-o" "${OUT_BIN}")

    add_custom_command(
        OUTPUT ${OUT_BIN}
        COMMAND ${KLANGC_EXECUTABLE}
                ${KLANG_FLAGS}
                ${ARG_SOURCES}
                ${ARG_OBJECTS}
        DEPENDS ${ARG_SOURCES} ${ARG_OBJECTS} ${ARG_DEPENDS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Compiling K executable ${TARGET_NAME}"
        VERBATIM
    )

    add_custom_target(${TARGET_NAME} ALL
        DEPENDS ${OUT_BIN}
    )
endfunction()
```

---

## 4. Complete External `CMakeLists.txt` Project Example

Here is a full working example of a project containing a C helper object, a K shared library, and a K executable consuming that library.

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_k_project LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include the custom Klang build functions defined above
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KlangAddTargets)

# 1. Compile native C helper to a .o file
set(C_HELPER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/native_helper.c")
set(C_HELPER_OBJ "${CMAKE_CURRENT_BINARY_DIR}/native_helper.o")

add_custom_command(
    OUTPUT  ${C_HELPER_OBJ}
    COMMAND ${CMAKE_C_COMPILER} -c -fPIC ${C_HELPER_SRC} -o ${C_HELPER_OBJ}
    DEPENDS ${C_HELPER_SRC}
    COMMENT "Compiling native C helper: native_helper.c"
    VERBATIM
)

# 2. Build K Library: mylib (module "mylib")
add_k_library(mylib
    MODULE_NAME "mylib"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/math.k"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms.k"
    OBJECTS
        ${C_HELPER_OBJ}
    TYPE SHARED
)

# 3. Build K Executable: myapp (imports mylib)
add_k_executable(myapp
    OUTPUT_NAME "myapp"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/main.k"
    KDI_INCLUDES
        "${CMAKE_CURRENT_BINARY_DIR}"
    LIB_DIRS
        "${CMAKE_CURRENT_BINARY_DIR}"
    LINK_LIBS
        "mylib"
    DEPENDS
        mylib
)
```

---

## 5. Summary of Best Practices

1. **Always pass `-g` in Debug configurations:** `klangc -g` generates full DWARF 5 symbols for line-stepping and variable inspection.
2. **Include `.o` files directly:** Use `klangc` to bundle pre-compiled C/C++ objects (`.o`) into K shared/static libraries or executables without separate linker invocations.
3. **Link directory resolution:** Provide `-I <path_to_kdi>` and `-L <path_to_so_or_a>` so `klangc` can find both the interface metadata (`.kdi`) and the binary object code during import resolution.
4. **Environment Variables:** In CI or containerized builds, set `KLANG_LIB_PATH` to simplify transitive `.kdi` lookups across submodules.
