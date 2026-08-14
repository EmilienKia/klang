# How-To: Integrating K Compilation into CMake Projects

This guide explains how to integrate the compilation of **K language libraries and executables** (`.k` source files) into CMake build systems, both **within the Klang repository** (in-tree) and in **external independent projects** (out-of-tree).

---

## 1. Preamble and Prerequisites for External Projects

For an external CMake project building K sources, assume that the Klang toolchain and standard library are installed and accessible on the build host:

- **Compiler executable:** `klangc` is installed in `PATH` (e.g. `/usr/local/bin/klangc` or `/usr/bin/klangc`).
- **KDI inspection tool:** `kditool` is installed in `PATH` (e.g. `/usr/local/bin/kditool`).
- **Standard runtime library:** `libk.so` and `libk.a` are in system library paths (e.g. `/usr/local/lib` or `/usr/lib`) or specified via `LD_LIBRARY_PATH`.
- **Standard KDI descriptors:** `libk.kdi` and `k.kdi` are in system KDI paths (e.g. `/usr/local/lib/kdi`, `/usr/lib/kdi`) or discoverable via the `KLANG_LIB_PATH` environment variable.

If the toolchain is installed in a non-standard location (e.g. `/opt/klang`), ensure your environment points to it:
```bash
export PATH="/opt/klang/bin:$PATH"
export KLANG_LIB_PATH="/opt/klang/lib/kdi:/opt/klang/lib"
export LD_LIBRARY_PATH="/opt/klang/lib:$LD_LIBRARY_PATH"
```

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

