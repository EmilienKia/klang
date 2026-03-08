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

#ifndef KLANG_PATH_LOOKUP_FILE_RESOLVER_HPP
#define KLANG_PATH_LOOKUP_FILE_RESOLVER_HPP

#include "file_resolver.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace k {

/**
 * A file_resolver that searches a list of directories for a file whose name
 * is derived from the module name and the requested extension.
 *
 * The filename convention follows file_resolver::module_name_to_file_base():
 *   module "math::vec", extension ".kdi"  →  "math.vec.kdi"
 *   module "math::vec", extension ".so"   →  "libmath.vec.so"
 *   module "math::vec", extension ".a"    →  "libmath.vec.a"
 *
 * The default search order (configurable) is:
 *   1. Explicit paths added with add_explicit_path() (checked before dirs)
 *   2. Directories in the search-path list (add_search_dir())
 *   3. The next resolver in the chain, if set via resolve(…, next)
 *
 * Typically klangc builds one instance per resource type (.kdi / .so) and
 * chains them together.
 *
 * Thread-safety: the object is immutable after configuration; concurrent calls
 * to resolve() are safe.
 */
class path_lookup_file_resolver : public file_resolver {
public:
    /**
     * Default-construct an empty resolver (no directories, no env var).
     */
    path_lookup_file_resolver() = default;

    // ── Configuration API (call before resolve()) ─────────────────────────

    /**
     * Register an explicit (module_name → absolute_path) mapping.
     * These are checked first, before any directory search.
     * Corresponds to the CLI option  -i <file.kdi>  or  -l <file.so>.
     *
     * @param module_name  K module name, e.g. "math::vec"
     * @param path         Absolute (or resolvable) path to the file.
     */
    void add_explicit_path(const std::string& module_name,
                           const std::filesystem::path& path);

    /**
     * Append a directory to the ordered search list.
     * Directories are tried in insertion order.
     * Corresponds to the CLI option  -I <dir>  (for .kdi)
     *                           or   -L <dir>  (for .so/.a).
     */
    void add_search_dir(const std::filesystem::path& dir);

    /**
     * Read an environment variable whose value is a colon-separated (UNIX)
     * or semicolon-separated (Windows) list of directories, and append all
     * of them to the search list.
     *
     * Call this method to honour the KLANG_LIB_PATH environment variable
     * (or a user-supplied override).  Has no effect if the variable is unset
     * or empty.
     *
     * @param env_var_name  Name of the environment variable to read.
     */
    void add_dirs_from_env(const std::string& env_var_name);

    // ── file_resolver interface ───────────────────────────────────────────

    std::optional<std::filesystem::path>
    resolve(const std::string& module_name,
            const std::string& extension,
            const file_resolver* next = nullptr) const override;

    // ── Introspection (used by compiler::build_import_link_args) ─────────

    /**
     * Return the ordered list of search directories registered via
     * add_search_dir() and add_dirs_from_env().
     * Used by the compiler to build -L flags for the final clang invocation.
     */
    const std::vector<std::filesystem::path>& get_lib_search_dirs() const {
        return _dirs;
    }

private:
    /// explicit module_name → path mappings (highest priority)
    std::vector<std::pair<std::string, std::filesystem::path>> _explicit;

    /// Ordered list of directories to search
    std::vector<std::filesystem::path> _dirs;

    /// Build the expected filename for a module + extension.
    static std::string make_filename(const std::string& module_name,
                                     const std::string& extension);
};

} // namespace k

#endif // KLANG_PATH_LOOKUP_FILE_RESOLVER_HPP

