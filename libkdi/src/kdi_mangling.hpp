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

#ifndef LIBKDI_MANGLING_HPP
#define LIBKDI_MANGLING_HPP

#include "kdi_mangling.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace kdi {

struct kdi_mangling_error : std::invalid_argument {
    explicit kdi_mangling_error(const std::string& what) : std::invalid_argument(what) {}
};

std::string kdi_mangle_symbol(std::string_view readable);
std::string kdi_demangle_symbol(std::string_view mangled);

} // namespace kdi

#endif // LIBKDI_MANGLING_HPP
