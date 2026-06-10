/*
 * K Language compiler — libkdi
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

#ifndef LIBKDI_DOC_HPP
#define LIBKDI_DOC_HPP

/**
 * @file kdi_doc.hpp
 *
 * Symbol documentation lookup and formatting helpers for KDI files.
 */

#include "kdi_file.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace kdi {

enum class kdi_doc_kind : uint8_t {
    namespace_,
    aggregate,
    enum_,
    union_,
    function,
    method,
    constructor,
    destructor,
    variable,
};

struct kdi_doc_symbol {
    kdi_doc_kind kind = kdi_doc_kind::function;
    std::string  name;
    std::string  fq_name;
    std::string  mangled_name;
    std::optional<kdi_doc_block>    block_doc;
    std::optional<kdi_doc_function> function_doc;
    std::vector<kdi_doc_symbol>      children;
};

std::string kdi_doc_kind_to_string(kdi_doc_kind kind);

std::vector<kdi_doc_symbol> kdi_find_doc_symbols(const kdi_file& file,
                                                 const std::string& symbol);

std::string kdi_format_doc_text(const kdi_doc_symbol& symbol,
                                bool list_children = false);
std::string kdi_format_doc_json(const kdi_doc_symbol& symbol,
                                bool list_children = false);

} // namespace kdi

#endif // LIBKDI_DOC_HPP
