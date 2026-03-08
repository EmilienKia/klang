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

#include "path_lookup_file_resolver.hpp"

#include <cstdlib>   // getenv
#include <sstream>

namespace k {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string
path_lookup_file_resolver::make_filename(const std::string& module_name,
                                         const std::string& extension)
{
    const std::string base = file_resolver::module_name_to_file_base(module_name);

    // Binary libs use the "lib" prefix convention (like GCC/Clang -l).
    if (extension == ".so" || extension == ".a") {
        return "lib" + base + extension;
    }
    // For .kdi (and any other extension) use just base + extension.
    return base + extension;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration API
// ─────────────────────────────────────────────────────────────────────────────

void path_lookup_file_resolver::add_explicit_path(
    const std::string& module_name,
    const std::filesystem::path& path)
{
    _explicit.emplace_back(module_name, path);
}

void path_lookup_file_resolver::add_search_dir(const std::filesystem::path& dir)
{
    _dirs.push_back(dir);
}

void path_lookup_file_resolver::add_dirs_from_env(const std::string& env_var_name)
{
    const char* raw = std::getenv(env_var_name.c_str());
    if (!raw || *raw == '\0') return;

#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif

    std::string value(raw);
    std::istringstream ss(value);
    std::string token;
    while (std::getline(ss, token, sep)) {
        if (!token.empty()) {
            _dirs.emplace_back(token);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve()
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::filesystem::path>
path_lookup_file_resolver::resolve(const std::string& module_name,
                                   const std::string& extension,
                                   const file_resolver* next) const
{
    // 1. Explicit paths (highest priority, e.g. -i / -l with full path)
    for (const auto& [mod, path] : _explicit) {
        if (mod == module_name) {
            if (std::filesystem::is_regular_file(path)) {
                return std::filesystem::absolute(path);
            }
        }
    }

    // 2. Directory search
    const std::string filename = make_filename(module_name, extension);
    for (const auto& dir : _dirs) {
        auto candidate = dir / filename;
        if (std::filesystem::is_regular_file(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }

    // 3. Delegate to next resolver in chain
    if (next) {
        return next->resolve(module_name, extension);
    }

    return std::nullopt;
}

} // namespace k

