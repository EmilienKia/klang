/*
 * K Language crypto standard library test helpers
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

#include "crypto_test_helpers.hpp"

#include <dlfcn.h>
#include <iostream>
#include <string>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined"
#endif
#ifndef LIBK_CRYPTO_KDI_DIR
#error "LIBK_CRYPTO_KDI_DIR must be defined"
#endif
#ifndef LIBK_CRYPTO_LIB_DIR
#error "LIBK_CRYPTO_LIB_DIR must be defined"
#endif

namespace {

bool ensure_libk_crypto_loaded() {
    static void* handle = nullptr;
    if (handle) return true;
    if (!ensure_libk_loaded()) return false;

    const std::string path = std::string(LIBK_CRYPTO_LIB_DIR) + "/libk.crypto.so";
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "ensure_libk_crypto_loaded: cannot load " << path
                  << ": " << dlerror() << std::endl;
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<k::model::gen::jit> gen_jit_with_crypto(std::string_view source) {
    if (!ensure_libk_crypto_loaded()) return nullptr;

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(LIBK_KDI_DIR);
    resolver->add_search_dir(LIBK_CRYPTO_KDI_DIR);
    comp->set_file_resolver(resolver);

    try {
        comp->parse_source("crypto-test.k", source);
        return comp->to_jit();
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected error during crypto test compilation: "
                  << ex.what() << std::endl;
        return nullptr;
    }
}
