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

#include "kdi_symbols.hpp"
#include "kdi_file.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  define ACCESS(f,m) ::_access(f,m)
#  define PATH_SEP ';'
#else
#  include <unistd.h>
#  define ACCESS(f,m) ::access(f,m)
#  define PATH_SEP ':'
#endif

namespace kdi {

// ─────────────────────────────────────────────────────────────────────────────
// Internal: search for an executable on PATH
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Search for @p exe_name in the directories listed in the PATH environment
 * variable.  Returns the full path if found (executable bit set), empty string
 * otherwise.
 */
static std::string find_on_path(const std::string& exe_name) {
    // If it already looks like an absolute / relative path, check directly.
    if (exe_name.find('/') != std::string::npos
#ifdef _WIN32
        || exe_name.find('\\') != std::string::npos
#endif
    ) {
        return ACCESS(exe_name.c_str(), 1) == 0 ? exe_name : std::string{};
    }

    const char* raw_path = std::getenv("PATH");
    if (!raw_path) return {};

    std::string path_env(raw_path);
    std::string::size_type start = 0;
    while (start <= path_env.size()) {
        auto end = path_env.find(PATH_SEP, start);
        if (end == std::string::npos) end = path_env.size();
        std::string dir = path_env.substr(start, end - start);
        if (!dir.empty()) {
            std::string candidate = dir + "/" + exe_name;
            if (ACCESS(candidate.c_str(), 1) == 0)
                return candidate;
        }
        start = end + 1;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_collect_binary_symbols
// ─────────────────────────────────────────────────────────────────────────────

std::set<std::string> kdi_collect_binary_symbols(const std::string& binary_path) {
    // Verify that 'nm' is available on PATH before attempting to invoke it.
    static const std::string nm_name = "nm";
    static const std::string nm_path = find_on_path(nm_name);
    if (nm_path.empty())
        throw kdi_symbol_error(
            "kdi_collect_binary_symbols: required tool 'nm' not found on PATH");

    // Use nm with:
    //   --defined-only : only symbols that have a definition in this file
    //   --extern-only  : only global (externally visible) symbols
    //   --format=posix : one symbol per line: "name type value size"
    //                    the first field is always the symbol name
    std::string cmd = nm_path
                      + " --defined-only --extern-only --format=posix -- "
                      + binary_path + " 2>&1";

    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe)
        throw kdi_symbol_error("kdi_collect_binary_symbols: cannot invoke nm");

    std::set<std::string> symbols;
    std::array<char, 512> buf{};
    std::string line;

    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        line = buf.data();
        // Strip trailing newline
        if (!line.empty() && line.back() == '\n') line.pop_back();

        // Skip nm diagnostic lines (start with "nm:")
        if (line.rfind("nm:", 0) == 0) continue;

        // nm --format=posix: "symbol_name type value size"
        // Extract the first token (symbol name)
        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string sym = line.substr(0, sp);
        if (!sym.empty())
            symbols.insert(std::move(sym));
    }

    int rc = ::pclose(pipe);
    if (rc != 0) {
        throw kdi_symbol_error(
            "kdi_collect_binary_symbols: nm failed (exit " +
            std::to_string(rc) + ") for '" + binary_path + "'");
    }

    return symbols;
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol collector: walk all declared mangled names in a kdi_file
// ─────────────────────────────────────────────────────────────────────────────

struct sym_entry {
    std::string mangled;
    std::string context;
};

static void collect_function(const kdi_function& fn,
                              const std::string& ns_ctx,
                              std::vector<sym_entry>& out) {
    if (!fn.mangled_name.empty())
        out.push_back({fn.mangled_name, ns_ctx + fn.fq_name});
}

static void collect_variable(const kdi_variable& v,
                              const std::string& ns_ctx,
                              std::vector<sym_entry>& out) {
    if (!v.mangled_name.empty())
        out.push_back({v.mangled_name, ns_ctx + v.fq_name});
}

static void collect_aggregate(const kdi_aggregate& agg,
                               std::vector<sym_entry>& out);

static void collect_namespace(const kdi_namespace& ns,
                               std::vector<sym_entry>& out) {
    for (auto& fn : ns.functions)
        collect_function(fn, "", out);
    for (auto& v : ns.variables)
        collect_variable(v, "", out);
    for (auto& agg : ns.aggregates)
        collect_aggregate(agg, out);
    for (auto& sub : ns.namespaces)
        collect_namespace(sub, out);
}

static void collect_method(const kdi_method& m,
                            const std::string& agg_fq,
                            std::vector<sym_entry>& out) {
    if (m.is_abstract) return;                      // abstract → no symbol
    if (!m.mangled_name.empty())
        out.push_back({m.mangled_name, agg_fq + "::" + m.name});
}

static void collect_aggregate(const kdi_aggregate& agg,
                               std::vector<sym_entry>& out) {
    const std::string ctx = agg.fq_name;

    // Constructors (C1 + C2 variants)
    for (auto& ctor : agg.constructors) {
        if (!ctor.mangled_name.empty())
            out.push_back({ctor.mangled_name, ctx + "::[ctor C1]"});
        if (!ctor.mangled_name_c2.empty()
            && ctor.mangled_name_c2 != ctor.mangled_name)
            out.push_back({ctor.mangled_name_c2, ctx + "::[ctor C2]"});
    }

    // Destructor (D1 + D2 variants)
    if (agg.destructor) {
        auto& dtor = *agg.destructor;
        if (!dtor.is_compiler_generated) {
            if (!dtor.mangled_name.empty())
                out.push_back({dtor.mangled_name, ctx + "::[dtor D1]"});
            if (!dtor.mangled_name_d2.empty()
                && dtor.mangled_name_d2 != dtor.mangled_name)
                out.push_back({dtor.mangled_name_d2, ctx + "::[dtor D2]"});
        }
    }

    // Methods
    for (auto& m : agg.methods)
        collect_method(m, ctx, out);

    // Static member variables
    for (auto& v : agg.static_vars)
        collect_variable(v, "", out);

    // Layout members (only the ones with a mangled name, i.e. public/protected)
    for (auto& lf : agg.layout) {
        if (auto* lm = std::get_if<kdi_layout_member>(&lf)) {
            if (!lm->mangled_name.empty())
                out.push_back({lm->mangled_name, lm->fq_name});
        }
    }

    // Vtable symbols
    if (agg.vtable) {
        auto& vt = *agg.vtable;
        if (!vt.vtable_symbol.empty())
            out.push_back({vt.vtable_symbol, ctx + " [vtable]"});
        if (!vt.rtti_symbol.empty())
            out.push_back({vt.rtti_symbol, ctx + " [rtti]"});
    }

    // Nested aggregates
    for (auto& nested : agg.nested)
        collect_aggregate(nested, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_check_symbols
// ─────────────────────────────────────────────────────────────────────────────

kdi_symbol_check_result kdi_check_symbols(const kdi_file& file,
                                          const std::set<std::string>& binary_symbols)
{
    // Collect all declared mangled names from the KDI
    std::vector<sym_entry> declared;
    collect_namespace(file.unit.root_ns, declared);

    kdi_symbol_check_result result;
    for (auto& entry : declared) {
        if (binary_symbols.find(entry.mangled) == binary_symbols.end())
            result.missing.push_back({entry.mangled, entry.context});
    }
    return result;
}

kdi_symbol_check_result kdi_check_symbols(const kdi_file& file,
                                          const std::string& binary_path)
{
    auto syms = kdi_collect_binary_symbols(binary_path);
    return kdi_check_symbols(file, syms);
}

} // namespace kdi


