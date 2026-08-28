/*
 * K Language compiler — kdi tool
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

/**
 * @file main.cpp
 *
 * kditool — K Description Interface utility.
 *
 * Usage:
 *   kditool dump       <file.kdi>           Print a human-readable description.
 *   kditool validate   <file.kdi>           Validate a KDI file against the schema.
 *   kditool json-dump  <file.kdi>           Dump a KDI file as JSON to stdout.
 *   kditool doc [--json] [--list-children] <file.kdi> <symbol>
 *                                           Print the in-code documentation for one symbol.
 *   kditool docgen    [--md] [--html] <file.kdi> [destination-directory]
 *                                           Generate documentation tree (Markdown and/or HTML).
 *                                           --md / --markdown : generate Markdown output.
 *                                           --html / --static-html: generate static HTML output.
 *                                           If neither flag is given, both formats are generated.
 *   kditool to-json    <file.kdi>           Convert a .kdi (CBOR) file to .kdi.json.
 *   kditool to-cbor    <file.kdi.json>      Convert a .kdi.json file to .kdi (CBOR).
 *   kditool mangle     <readable-symbol>    Convert a readable K symbol to a mangled name.
 *   kditool demangle   <mangled-symbol>     Convert a mangled K symbol to a readable name.
 *   kditool help                           Display this help message.
 *
 * Exit codes:
 *   0  — success / file is valid
 *   1  — validation error(s) found
 *   2  — I/O or parse error
 *   3  — usage error
 */

#include "kdi.hpp"
#include "kdi_doc.hpp"
#include "kdi_docgen.hpp"
#include "kdi_dump.hpp"
#include "kdi_json.hpp"
#include "kdi_mangling.hpp"
#include "kdi_symbols.hpp"

#include <boost/program_options.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace po = boost::program_options;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog,
                        const po::options_description& global_opts)
{
    std::cout
        << "Usage: " << prog << " <command> [options] <file> [<symbol|binary|destination-directory>]\n\n"
        << "Commands:\n"
        << "  dump      <file.kdi>                   Dump a KDI file in human-readable form\n"
        << "  validate  <file.kdi>                   Validate a KDI file (schema v"
        << kdi::KDI_SCHEMA_MAJOR << "." << kdi::KDI_SCHEMA_MINOR << ")\n"
        << "  json-dump <file.kdi>                   Dump a KDI file as JSON to stdout\n"
        << "  doc       [--json] [--list-children] <file.kdi> <symbol>\n"
        << "                                          Show documentation for one symbol\n"
        << "  docgen    [--md] [--html] <file.kdi> [destination-directory]\n"
        << "                                          Generate documentation (Markdown and/or HTML).\n"
        << "                                          --md / --markdown   : Markdown output only.\n"
        << "                                          --html / --static-html: HTML output only.\n"
        << "                                          Default (no flag): generate both.\n"
        << "  to-json   <file.kdi>                   Convert .kdi (CBOR) -> .kdi.json\n"
        << "  to-cbor   <file.kdi.json>              Convert .kdi.json -> .kdi (CBOR)\n"
        << "  mangle    <readable-symbol>            Convert a readable K symbol to a mangled name\n"
        << "  demangle  <mangled-symbol>             Convert a mangled K symbol to a readable name\n"
        << "  check-symbols <file.kdi> <binary>      Verify that all symbols declared in\n"
        << "                                          the KDI are present in the binary\n"
        << "  help                                    Show this help\n\n"
        << global_opts << "\n";
}

static void print_doc_ambiguity(const std::string& symbol,
                                const std::vector<kdi::kdi_doc_symbol>& matches)
{
    std::cerr << "Ambiguous symbol '" << symbol << "': " << matches.size()
              << " candidate(s)\n";
    for (const auto& match : matches) {
        std::cerr << "  [" << kdi::kdi_doc_kind_to_string(match.kind) << "] "
                  << (match.fq_name.empty() ? match.name : match.fq_name);
        if (!match.mangled_name.empty())
            std::cerr << "  // " << match.mangled_name;
        std::cerr << "\n";
    }
}

static int cmd_doc(const std::string& path,
                   const std::string& symbol,
                   bool json_output,
                   bool list_children)
{
    try {
        auto file = kdi::kdi_read_cbor_file(path);
        auto matches = kdi::kdi_find_doc_symbols(file, symbol);
        if (matches.empty()) {
            std::cerr << "Not found: '" << symbol << "' in '" << path << "'\n";
            return 1;
        }
        if (matches.size() > 1) {
            print_doc_ambiguity(symbol, matches);
            return 1;
        }

        if (json_output)
            std::cout << kdi::kdi_format_doc_json(matches[0], list_children) << "\n";
        else
            std::cout << kdi::kdi_format_doc_text(matches[0], list_children);
        return 0;
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

static int cmd_docgen(const std::string& kdi_path,
                      const std::string& destination_dir,
                      bool gen_markdown,
                      bool gen_html)
{
    try {
        auto file = kdi::kdi_read_cbor_file(kdi_path);
        std::string error_message;
        if (gen_markdown) {
            if (!kdi::kdi_generate_markdown_doc(file, destination_dir, &error_message)) {
                std::cerr << "Markdown docgen error: " << error_message << "\n";
                return 2;
            }
        }
        if (gen_html) {
            if (!kdi::kdi_generate_html_doc(file, destination_dir, &error_message)) {
                std::cerr << "HTML docgen error: " << error_message << "\n";
                return 2;
            }
        }
        return 0;
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Command implementations
// ─────────────────────────────────────────────────────────────────────────────

static int cmd_dump(const std::string& path) {
    try {
        auto file = kdi::kdi_read_cbor_file(path);
        kdi::kdi_dump(file, std::cout);
        return 0;
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

static int cmd_validate(const std::string& path) {
    try {
        auto file   = kdi::kdi_read_cbor_file(path);
        auto result = kdi::kdi_validate(file);
        if (result.is_valid()) {
            std::cout << "OK: " << path << " is valid (schema "
                      << file.header.schema_major << "."
                      << file.header.schema_minor << ")\n";
            return 0;
        }
        std::cerr << "INVALID: " << result.errors.size()
                  << " error(s) in " << path << "\n";
        for (auto& e : result.errors)
            std::cerr << "  [" << e.path << "] " << e.message << "\n";
        return 1;
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

static int cmd_json_dump(const std::string& path) {
    try {
        auto file = kdi::kdi_read_cbor_file(path);
        kdi::kdi_write_json(file, std::cout);
        std::cout << "\n";
        return 0;
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

static int cmd_to_json(const std::string& path) {
    try {
        auto file     = kdi::kdi_read_cbor_file(path);
        auto out_path = path + ".json";
        if (!kdi::kdi_write_json_file(file, out_path)) {
            std::cerr << "Error: cannot write '" << out_path << "'\n";
            return 2;
        }
        std::cout << "Written: " << out_path << "\n";
        return 0;
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

static int cmd_check_symbols(const std::string& kdi_path,
                              const std::string& binary_path) {
    // Load KDI
    kdi::kdi_file file;
    try {
        file = kdi::kdi_read_cbor_file(kdi_path);
    } catch (const kdi::kdi_parse_error& e) {
        std::cerr << "KDI parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error reading KDI: " << e.what() << "\n";
        return 2;
    }

    // Collect binary symbols and check
    kdi::kdi_symbol_check_result result;
    try {
        result = kdi::kdi_check_symbols(file, binary_path);
    } catch (const kdi::kdi_symbol_error& e) {
        std::cerr << "Symbol read error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    if (result.is_ok()) {
        std::cout << "OK: all symbols from '" << kdi_path
                  << "' are present in '" << binary_path << "'\n";
        return 0;
    }

    std::cerr << "MISSING: " << result.missing.size()
              << " symbol(s) declared in '" << kdi_path
              << "' not found in '" << binary_path << "'\n";
    for (auto& m : result.missing)
        std::cerr << "  [" << m.context << "] " << m.mangled_name << "\n";
    return 1;
}

static int cmd_to_cbor(const std::string& path) {
    try {
        auto file = kdi::kdi_read_json_file(path);
        // Strip trailing ".json" if present
        std::string out_path = path;
        if (out_path.size() > 5
            && out_path.substr(out_path.size() - 5) == ".json")
            out_path = out_path.substr(0, out_path.size() - 5);
        else
            out_path += ".kdi";
        if (!kdi::kdi_write_cbor_file(file, out_path)) {
            std::cerr << "Error: cannot write '" << out_path << "'\n";
            return 2;
        }

        static int cmd_mangle(const std::string& readable) {
            try {
                std::cout << kdi::kdi_mangle_symbol(readable) << "\n";
                return 0;
            } catch (const kdi::kdi_mangling_error& e) {
                std::cerr << "Mangling error: " << e.what() << "\n";
                return 1;
            }
        }

        static int cmd_demangle(const std::string& mangled) {
            try {
                std::cout << kdi::kdi_demangle_symbol(mangled) << "\n";
                return 0;
            } catch (const kdi::kdi_mangling_error& e) {
                std::cerr << "Mangling error: " << e.what() << "\n";
                return 1;
            }
        }
        std::cout << "Written: " << out_path << "\n";
        return 0;
    } catch (const kdi::kdi_json_error& e) {
        std::cerr << "JSON error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    // ── Global / top-level options ──────────────────────────────────────────
    po::options_description global_opts("Global options");
    global_opts.add_options()
        ("help,h",    "Display this help message and exit")
        ("version,v", "Display version information and exit")
        ("json",      "Output JSON for the doc command")
        ("list-children", "List direct child symbols for the doc command")
        ("md,markdown",   "docgen: generate Markdown documentation")
        ("html,static-html", "docgen: generate static HTML documentation")
        ("command",   po::value<std::string>(), "Command to execute")
        ("file",      po::value<std::string>(), "KDI file or symbol to process")
        ("subject",   po::value<std::string>(), "Symbol for doc, destination for docgen, or binary for check-symbols")
        ;

    po::positional_options_description pos;
    pos.add("command", 1);
    pos.add("file",    1);
    pos.add("subject",  1);

    po::variables_map vm;
    try {
        auto parsed = po::command_line_parser(argc, argv)
                          .options(global_opts)
                          .positional(pos)
                          .allow_unregistered()
                          .run();
        po::store(parsed, vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Option error: " << e.what() << "\n";
        print_usage(argv[0], global_opts);
        return 3;
    }

    // ── --version (check before requiring a command) ───────────────────────
    if (vm.count("version")) {
        std::cout << "kditool — KDI utility  (schema v"
                  << kdi::KDI_SCHEMA_MAJOR << "."
                  << kdi::KDI_SCHEMA_MINOR << ")\n";
        return 0;
    }

    // ── --help / missing command ────────────────────────────────────────────
    if (vm.count("help") || !vm.count("command")) {
        print_usage(argv[0], global_opts);
        return vm.count("help") ? 0 : 3;
    }

    const std::string command = vm["command"].as<std::string>();

    // ── help command ────────────────────────────────────────────────────────
    if (command == "help") {
        print_usage(argv[0], global_opts);
        return 0;
    }

    // ── Commands requiring a file argument ──────────────────────────────────
    if (command == "mangle" || command == "demangle") {
        if (!vm.count("file")) {
            std::cerr << "Error: command '" << command << "' requires a <symbol> argument.\n";
            print_usage(argv[0], global_opts);
            return 3;
        }
        return command == "mangle"
            ? cmd_mangle(vm["file"].as<std::string>())
            : cmd_demangle(vm["file"].as<std::string>());
    }

    static const std::vector<std::string> file_commands =
        {"dump", "validate", "json-dump", "doc", "docgen", "to-json", "to-cbor", "check-symbols"};

    bool is_file_cmd = false;
    for (auto& c : file_commands) if (c == command) { is_file_cmd = true; break; }

    if (!is_file_cmd) {
        std::cerr << "Unknown command: '" << command << "'\n";
        print_usage(argv[0], global_opts);
        return 3;
    }

    if (!vm.count("file")) {
        std::cerr << "Error: command '" << command << "' requires a <file> argument.\n";
        print_usage(argv[0], global_opts);
        return 3;
    }

    const std::string file = vm["file"].as<std::string>();

    if (command == "doc") {
        if (!vm.count("subject")) {
            std::cerr << "Error: 'doc' requires a <symbol> argument.\n";
            print_usage(argv[0], global_opts);
            return 3;
        }
        return cmd_doc(file,
                   vm["subject"].as<std::string>(),
                   vm.count("json") != 0,
                   vm.count("list-children") != 0);
    }

    if (command == "docgen") {
        const std::string destination = vm.count("subject")
                                            ? vm["subject"].as<std::string>()
                                            : ".";
        const bool want_md   = vm.count("md") != 0 || vm.count("markdown") != 0;
        const bool want_html = vm.count("html") != 0 || vm.count("static-html") != 0;
        // Default: generate both when no format flag is given
        const bool gen_md   = want_md   || (!want_md && !want_html);
        const bool gen_html = want_html || (!want_md && !want_html);
        return cmd_docgen(file, destination, gen_md, gen_html);
    }

    // check-symbols also requires a <binary> argument
    if (command == "check-symbols") {
        if (!vm.count("subject")) {
            std::cerr << "Error: 'check-symbols' requires a <binary> argument.\n";
            print_usage(argv[0], global_opts);
            return 3;
        }
        return cmd_check_symbols(file, vm["subject"].as<std::string>());
    }

    if (command == "dump")       return cmd_dump(file);
    if (command == "validate")   return cmd_validate(file);
    if (command == "json-dump")  return cmd_json_dump(file);
    if (command == "to-json")    return cmd_to_json(file);
    if (command == "to-cbor")    return cmd_to_cbor(file);

    // Should never reach here
    std::cerr << "Internal error: unhandled command '" << command << "'\n";
    return 3;
}
