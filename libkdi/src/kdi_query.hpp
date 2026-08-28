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

#ifndef LIBKDI_QUERY_HPP
#define LIBKDI_QUERY_HPP

#include "kdi_file.hpp"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace kdi {

struct kdi_symbol_row {
    std::string kind;
    std::string fq_name;
    std::string owner_fq_name;
    std::string name;
    std::string mangled_name;
    std::string signature;
};

std::string kdi_type_to_string(const kdi_type& type);

std::vector<kdi_symbol_row> kdi_list_symbols(const kdi_file& file,
                                             std::string_view query = {});

std::vector<kdi_symbol_row> kdi_list_aggregate_members(const kdi_file& file,
                                                       std::string_view aggregate);

void kdi_write_symbol_rows_tsv(const std::vector<kdi_symbol_row>& rows,
                               std::ostream& out,
                               bool include_header = false);

} // namespace kdi

#endif // LIBKDI_QUERY_HPP
