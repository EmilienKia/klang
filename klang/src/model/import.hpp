/*
 * K Language compiler
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

#ifndef KLANG_IMPORT_HPP
#define KLANG_IMPORT_HPP

#include "../common/common.hpp"

#include <memory>
#include <string>

// Forward-declare kdi types to avoid a hard dependency on libkdi from model headers.
namespace kdi {
    struct kdi_file;
    struct kdi_function;
    struct kdi_variable;
    struct kdi_aggregate;
    struct kdi_enum;
    struct kdi_constructor;
    struct kdi_destructor;
    struct kdi_method;
} // namespace kdi

namespace k::model {

/**
 * Represents a single import declaration in a compilation unit, after the
 * model-builder phase has recorded it from the AST.
 *
 * Initially only the module_name is filled.  The kdi_importer pass (run
 * before symbol resolution) resolves the .kdi file on disk and fills in
 * resolved_kdi_path and kdi.  The 'used' flag is set to true by the symbol /
 * type resolvers when at least one symbol from this import is actually
 * referenced.
 */
struct imported_module {
    /// Qualified name of the module, e.g. {"math", "vec"}.
    k::name module_name;

    /// Absolute path to the .kdi file once resolved (empty before resolution).
    std::string resolved_kdi_path;

    /// Loaded KDI descriptor (null before resolution).
    std::shared_ptr<kdi::kdi_file> kdi;

    /// True once a symbol from this import has been used (for unused-import warning).
    bool used = false;

    /// True if this import was implicitly injected by the compiler (e.g. the
    /// base standard library "k").  Implicit imports are never flagged as
    /// unused and are always linked.
    bool implicit = false;
};

} // namespace k::model

#endif // KLANG_IMPORT_HPP

