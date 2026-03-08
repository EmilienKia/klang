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

#ifndef KLANG_FILE_RESOLVER_HPP
#define KLANG_FILE_RESOLVER_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace k {

/**
 * Abstract interface for resolving a K module resource (a .kdi description
 * file or a .so/.a binary) to an absolute filesystem path.
 *
 * Resolvers can be chained: if this resolver cannot locate the resource it
 * calls resolve() on the *next* resolver (if any).  Chains are built with
 * file_resolver::chain().
 *
 * The canonical name-to-filename convention is:
 *   module name   "math::vec"
 *   extension     ".kdi"  →  filename  "math.vec.kdi"
 *   extension     ".so"   →  filename  "libmath.vec.so"
 *   extension     ".a"    →  filename  "libmath.vec.a"
 *
 * Each implementation is free to apply its own lookup strategy (directory
 * scan, explicit path map, environment variable, …).
 */
class file_resolver {
public:
    virtual ~file_resolver() = default;

    /**
     * Try to resolve a module name to an absolute filesystem path.
     *
     * @param module_name  K module name, e.g. "math::vec"
     * @param extension    File extension including the dot, e.g. ".kdi"
     * @param next         Next resolver in the chain, or nullptr.
     * @return             Absolute path if found, nullopt otherwise.
     */
    virtual std::optional<std::filesystem::path>
    resolve(const std::string& module_name,
            const std::string& extension,
            const file_resolver* next = nullptr) const = 0;

    /**
     * Build a chain: tries @p first, then @p second.
     * Returns a new resolver that owns both.
     */
    static std::unique_ptr<file_resolver>
    chain(std::unique_ptr<file_resolver> first,
          std::unique_ptr<file_resolver> second);

    /**
     * Convert a K module name (e.g. "math::vec") to its filename base
     * (e.g. "math.vec") by replacing "::" separators with ".".
     */
    static std::string module_name_to_file_base(const std::string& module_name);
};

} // namespace k

#endif // KLANG_FILE_RESOLVER_HPP

