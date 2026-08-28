/*
 * K Language standard library — native C symbol mangling tests
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <catch2/catch_all.hpp>

#include <dlfcn.h>

#include <string>

#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined -- check CMakeLists.txt"
#endif

namespace {

template<typename T>
T load_symbol(void* handle, const char* name) {
    dlerror();
    void* sym = dlsym(handle, name);
    INFO(dlerror());
    REQUIRE(sym != nullptr);
    return reinterpret_cast<T>(sym);
}

} // namespace

TEST_CASE("libk: exported C symbol mangling API", "[libk][mangling][native]") {
    const std::string path = std::string(LIBK_LIB_DIR) + "/libk.so";
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(handle != nullptr);

    using translate_fn = char* (*)(const char*);
    using free_fn = void (*)(char*);

    auto mangle = load_symbol<translate_fn>(handle, "__k_symbol_mangle");
    auto demangle = load_symbol<translate_fn>(handle, "__k_symbol_demangle");
    auto free_string = load_symbol<free_fn>(handle, "__k_symbol_string_free");

    char* mangled = mangle("function math::add(int, int)");
    REQUIRE(mangled != nullptr);
    REQUIRE(std::string(mangled) == "_KFN4math3addEii");

    char* readable = demangle("_KFN4math3addEii");
    REQUIRE(readable != nullptr);
    REQUIRE(std::string(readable) == "function math::add(int, int)");

    free_string(readable);
    free_string(mangled);
}
