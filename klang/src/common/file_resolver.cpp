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

#include "file_resolver.hpp"

#include <algorithm>
#include <sstream>

namespace k {

// ─────────────────────────────────────────────────────────────────────────────
// Chained resolver (private implementation detail)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

class chained_resolver final : public file_resolver {
    std::unique_ptr<file_resolver> _first;
    std::unique_ptr<file_resolver> _second;
public:
    chained_resolver(std::unique_ptr<file_resolver> a,
                     std::unique_ptr<file_resolver> b)
        : _first(std::move(a)), _second(std::move(b)) {}

    std::optional<std::filesystem::path>
    resolve(const std::string& module_name,
            const std::string& extension,
            const file_resolver* /*next*/) const override
    {
        // Try first, with second as its next resolver.
        return _first->resolve(module_name, extension, _second.get());
    }
};

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// file_resolver static members
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<file_resolver>
file_resolver::chain(std::unique_ptr<file_resolver> first,
                     std::unique_ptr<file_resolver> second)
{
    return std::make_unique<chained_resolver>(std::move(first),
                                             std::move(second));
}

std::string file_resolver::module_name_to_file_base(const std::string& module_name)
{
    // Replace every "::" with "."
    std::string result;
    result.reserve(module_name.size());
    std::size_t pos = 0;
    while (pos < module_name.size()) {
        if (module_name[pos] == ':' &&
            pos + 1 < module_name.size() &&
            module_name[pos + 1] == ':')
        {
            result += '.';
            pos += 2;
        } else {
            result += module_name[pos++];
        }
    }
    return result;
}

} // namespace k

