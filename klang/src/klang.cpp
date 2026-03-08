/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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

#include <filesystem>
#include <iostream>

#include <string_view>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

#include <boost/program_options.hpp>

#include "compiler.hpp"
#include "config.h"

#include "common/logger.hpp"
#include "common/path_lookup_file_resolver.hpp"
#include <kdi.hpp>
#include "parse/parser.hpp"
#include "parse/ast_dump.hpp"
#include "model/model.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"
#include "gen/resolvers.hpp"
#include "gen/generators.hpp"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

namespace po = boost::program_options;

static std::string read_text_file_content(const std::string& path) {
    std::ostringstream ss;
    std::ifstream input_file(path);
    if (!input_file.is_open()) {
        std::cerr << "Could not open the file '" << path << "'" << std::endl;
        exit(EXIT_FAILURE);
    }
    ss << input_file.rdbuf();
    return ss.str();
}

int main(int argc, const char** argv) {

    std::string output_file;
    std::vector<std::string> input_files;
    std::string target_triple;
    std::string raw_ir_file;
    std::string opt_ir_file;
    std::vector<std::string> kdi_dirs;      // -I <dir>
    std::vector<std::string> kdi_files;     // -i <file.kdi>
    std::vector<std::string> lib_dirs;      // -L <dir>
    std::vector<std::string> lib_files;     // -l <name-or-path>
    std::string lib_path_env;               // --lib-path-env

    k::compiler::initialize();

    po::options_description cli_gobal_options("Global options");
    cli_gobal_options.add_options()
            ("help,h", "Display this information.")
            ("version,v", "Display version information.")
            ("compile,c", "Compile the source files, but do not link")
            ("output,o", po::value<std::string>(&output_file), "Place the output into <arg> file.")
            ("input-file", po::value<std::vector<std::string>>(&input_files), "input file")
            ("dyn-lib",    "Produce a shared library (.so) instead of an executable")
            ("static-lib", "Produce a static library (.a) instead of an executable")
            ("emit-raw-ir", "Export LLVM IR text after code generation (before optimisation)")
            ("raw-ir-file", po::value<std::string>(&raw_ir_file)->implicit_value(""),
                            "Write raw IR to <arg> file (implies --emit-raw-ir; omit value or use - for stdout)")
            ("emit-opt-ir", "Export LLVM IR text after optimisation")
            ("opt-ir-file", po::value<std::string>(&opt_ir_file)->implicit_value(""),
                            "Write optimised IR to <arg> file (implies --emit-opt-ir; omit value or use - for stdout)")
            ("emit-kdi-json", "Also write a .kdi.json alongside the .kdi file when producing a library")
            ("no-emit-kdi",   "Suppress .kdi generation when producing a library")
            // ── Import / link search path options ───────────────────────────
            ("include-path,I",
                po::value<std::vector<std::string>>(&kdi_dirs)->composing(),
                "Add <arg> to the list of directories searched for .kdi files "
                "(and .so/.a when -L is not specified). May be repeated.")
            ("include-kdi,i",
                po::value<std::vector<std::string>>(&kdi_files)->composing(),
                "Explicitly specify a .kdi file for an imported module "
                "(format: <module-name>=<path> or just <path> when the module "
                "name is embedded in the file's header). May be repeated.")
            ("lib-path,L",
                po::value<std::vector<std::string>>(&lib_dirs)->composing(),
                "Add <arg> to the list of directories searched for .so/.a "
                "binary libraries. May be repeated.")
            ("lib,l",
                po::value<std::vector<std::string>>(&lib_files)->composing(),
                "Specify a library binary to link against. Accepts either a "
                "short name (e.g. math.vec → libmath.vec.so) or a full/relative "
                "path to a .so or .a file. May be repeated.")
            ("lib-path-env",
                po::value<std::string>(&lib_path_env),
                "Override the name of the environment variable used to pass "
                "additional KDI/library search directories "
                "(default: " KLANG_LIB_PATH_ENV_VAR "). "
                "The value is a colon-separated list of directories.")
            ("no-lib-path-env",
                "Disable the environment-variable-based search path entirely.")
            ("enforce-ns-collision",
                "Reject compilation if the root namespace of the unit being "
                "compiled collides with the root namespace of any imported module.")
            ;

    po::options_description cli_target_options("Target options");
    cli_target_options.add_options()
            ("print-effective-triple", "Print the effective target triple")
            ("print-target-triple", "Print the normalized target triple")
            ("print-targets", "Print the registered targets")
            ("target", po::value<std::string>(&target_triple), "Generate code for the given target")
            ;

    po::options_description cmdline_options;
    cmdline_options.add(cli_gobal_options).add(cli_target_options);

    po::positional_options_description p;
    p.add("input-file", -1);

    po::variables_map vm;
    auto parser = po::command_line_parser(argc, argv)
            .options(cmdline_options)
            .positional(p)
            .allow_unregistered()
            .run();
    po::store(parser, vm);
    po::notify(vm);

    std::vector<std::string> unrecognized = po::collect_unrecognized(parser.options, po::exclude_positional);
    if(!unrecognized.empty()) {
        std::cout << "Unrecognized option : " << unrecognized[0] << std::endl << std::endl;
    }

    if(vm.count("help") || !unrecognized.empty()) {
        std::cout << "Usage: klangc [options] input-file..." << std::endl;
        std::cout << cmdline_options << std::endl;
        return 1;
    }

    if(!vm.count("target")) {
        target_triple = llvm::sys::getDefaultTargetTriple();
    }

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if(!target) {
        std::cerr << "Problem to find target: " << error << std::endl;
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions target_options;
    // Use PIC relocation model so that object files are compatible with both
    // PIE executables and shared libraries.
    std::optional<llvm::Reloc::Model> reloc_model = llvm::Reloc::PIC_;
    auto target_machine = target->createTargetMachine(
            target_triple, cpu,
            features,
            target_options,
            reloc_model);


    if(vm.count("version")) {
        std::cout << "klangc - K lang compiler " << PROJECT_VER << std::endl;
        std::cout << "Target: " << target_machine->getTargetTriple().getTriple() << std::endl;
        return 2;
    }

    if(vm.count("print-targets")) {
        llvm::TargetRegistry::printRegisteredTargetsForVersion(llvm::outs());
        // TODO make it prettier.
        //for(auto target : llvm::TargetRegistry::targets()) {
        //    std::cout << target.getName() << " - " << target.getBackendName() << " : " << target.getShortDescription() << std::endl;
        //}
        return 3;
    }

    if(vm.count("print-target-triple") || vm.count("print-effective-triple")) {
        std::cout << "Target: " << target_machine->getTargetTriple().getTriple() << std::endl;
        return 4;
    }


    if(input_files.empty()) {
        std::cerr << "No input file." << std::endl;
        return -1;
    }

    if(input_files.size() > 1) {
        std::cout << "klangc is supporting only one input file yet. Additional files will be ignored." << std::endl;
    }

    std::cout << "Parsing : " << input_files[0] << std::endl;
    std::string source = read_text_file_content(input_files[0]);

    try {

        auto compiler = k::compiler::create(target_machine);

        // Build and apply IR export options
        k::IrOutputOptions ir_opts;
        ir_opts.emit_raw_ir   = vm.count("emit-raw-ir") > 0 || vm.count("raw-ir-file") > 0;
        ir_opts.raw_ir_file   = (raw_ir_file == "-") ? "" : raw_ir_file;
        ir_opts.emit_opt_ir   = vm.count("emit-opt-ir") > 0 || vm.count("opt-ir-file") > 0;
        ir_opts.opt_ir_file   = (opt_ir_file == "-") ? "" : opt_ir_file;
        ir_opts.emit_kdi_json = vm.count("emit-kdi-json") > 0;
        ir_opts.no_emit_kdi   = vm.count("no-emit-kdi")   > 0;
        compiler->set_ir_output_options(ir_opts);

        // ── Build the file resolver ─────────────────────────────────────────
        {
            auto resolver = std::make_shared<k::path_lookup_file_resolver>();

            // 1. Explicit .kdi files (-i module=path or -i path)
            for (const auto& spec : kdi_files) {
                auto eq = spec.find('=');
                if (eq != std::string::npos) {
                    // "module::name=/path/to/file.kdi"
                    resolver->add_explicit_path(spec.substr(0, eq),
                                               spec.substr(eq + 1));
                } else {
                    // bare path — read module_name from the .kdi header
                    try {
                        auto kf = kdi::kdi_read_cbor_file(spec);
                        resolver->add_explicit_path(kf.header.module_name, spec);
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: -i '" << spec
                                  << "': cannot read KDI header: " << e.what()
                                  << " — ignored." << std::endl;
                    }
                }
            }

            // 2. Current directory (always first in the search order)
            resolver->add_search_dir(std::filesystem::current_path());

            // 3. Explicit -I directories
            for (const auto& d : kdi_dirs) {
                resolver->add_search_dir(d);
            }

            // 4. Environment variable (unless --no-lib-path-env)
            if (vm.count("no-lib-path-env") == 0) {
                const std::string env_name = lib_path_env.empty()
                    ? std::string(KLANG_LIB_PATH_ENV_VAR)
                    : lib_path_env;
                resolver->add_dirs_from_env(env_name);
            }

            // 5. System directories (configured by CMake via config.h)
#if defined(KLANG_LIBRARY_ARCHITECTURE) && !defined(_WIN32)
            const std::string arch(KLANG_LIBRARY_ARCHITECTURE);
            if (!arch.empty()) {
                resolver->add_search_dir("/usr/local/lib/kdi");
                resolver->add_search_dir("/usr/lib/kdi");
                resolver->add_search_dir("/usr/lib/" + arch + "/kdi");
            } else
#endif
            {
                resolver->add_search_dir("/usr/local/lib/kdi");
                resolver->add_search_dir("/usr/lib/kdi");
            }

            compiler->set_file_resolver(resolver);
        }

        // ── Enforce-ns-collision flag ───────────────────────────────────────
        if (vm.count("enforce-ns-collision") > 0) {
            compiler->set_enforce_ns_collision(true);
        }

        // Pre-resolve IR file names from the expected output path so that
        // process_generation() (called inside parse_source) can use them.
        if (ir_opts.emit_raw_ir || ir_opts.emit_opt_ir) {
            std::string effective_output = output_file;
            if (effective_output.empty()) {
                if (vm.count("compile")) {
                    effective_output = std::filesystem::path(input_files[0]).replace_extension(".o").string();
                } else if (vm.count("dyn-lib") || vm.count("static-lib")) {
                    // We don't yet know the module name, leave empty — will be
                    // resolved again after parse_source() by the gen_* methods.
                } else {
                    effective_output = std::filesystem::path(input_files[0]).replace_extension("").string();
                }
            }
            if (!effective_output.empty()) {
                compiler->resolve_ir_filenames(effective_output);
            }
        }

        compiler->parse_source(input_files[0], source, true, false);

        const bool want_compile    = vm.count("compile")     > 0;
        const bool want_dyn_lib    = vm.count("dyn-lib")     > 0;
        const bool want_static_lib = vm.count("static-lib")  > 0;
        const bool has_main        = compiler->has_main_method();

        if (want_compile) {
            // -c : just emit a native object file, no linking
            if (output_file.empty()) {
                output_file = std::filesystem::path(input_files[0]).replace_extension(".o").string();
            }
            return compiler->gen_object_file(output_file) ? 0 : -1;

        } else if (want_dyn_lib && want_static_lib) {
            // Both --dyn-lib and --static-lib : single compilation pass, two outputs.
            // -o is silently ignored here (names are derived automatically).
            if (!output_file.empty()) {
                std::cerr << "Warning: -o is ignored when both --dyn-lib and --static-lib are specified." << std::endl;
            }
            if (has_main) {
                std::cerr << "Warning: module defines a 'main' function but a library output was requested (--dyn-lib --static-lib); 'main' will be included in the library but not used as an entry point." << std::endl;
            }
            return compiler->gen_libraries("", "") ? 0 : -1;

        } else if (want_dyn_lib) {
            // --dyn-lib only
            if (has_main) {
                std::cerr << "Warning: module defines a 'main' function but a shared library output was requested (--dyn-lib); 'main' will be included in the library but not used as an entry point." << std::endl;
            }
            return compiler->gen_shared_library(output_file) ? 0 : -1;

        } else if (want_static_lib) {
            // --static-lib only
            if (has_main) {
                std::cerr << "Warning: module defines a 'main' function but a static library output was requested (--static-lib); 'main' will be included in the archive but not used as an entry point." << std::endl;
            }
            return compiler->gen_static_library(output_file) ? 0 : -1;

        } else if (!has_main) {
            // No explicit flag, no main() → auto: produce a shared library
            return compiler->gen_shared_library(output_file) ? 0 : -1;

        } else {
            // Default: compile and link into an executable
            return compiler->gen_executable(output_file);
        }


    } catch(const k::log::compiler_error&) {
        // Diagnostic was already reported via the compiler's logger before the throw.
        return -1;
    } catch(const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return -1;
    } catch(...) {
        std::cerr << "Unknown error." << std::endl;
        return -1;
    }

    return 0;
}
