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

#include "compiler.hpp"

#include "config.h"
#include "common/path_lookup_file_resolver.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>
#include <unordered_set>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LegacyPassManager.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include "common/process.hpp"
#include "gen/resolvers.hpp"
#include "gen/generators.hpp"
#include "parse/ast_dump.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"
#include "model/tools/kdi_exporter.hpp"
#include "model/tools/kdi_importer.hpp"
#include "common/path_lookup_file_resolver.hpp"

#include <kdi.hpp>
#include "errors.hpp"

namespace k {

const source* compiler::source_for_position(const char* ptr) const {
    if (!ptr) return nullptr;
    for (const auto& src : _sources) {
        const char* begin = src.content.data();
        const char* end   = begin + src.content.size();
        if (ptr >= begin && ptr <= end) {
            return &src;
        }
    }
    return nullptr;
}

char_coord compiler::coordinates_from_pos(const k::char_pos& coord) const {
    if (auto* src = source_for_position(coord.pos)) {
        return src->get_coordinates(coord);
    }
    return char_coord::INVALID();
}

std::pair<char_coord,char_coord> compiler::coordinates_from_lex(const lex::lexeme& lex) const {
    if (lex.content.empty()) {
        return {char_coord::INVALID(), char_coord::INVALID()};
    }
    if (auto* src = source_for_position(&lex.content.front())) {
        return {src->get_coordinates({&lex.content.front()}), src->get_coordinates({&lex.content.back()})};
    }
    return {char_coord::INVALID(), char_coord::INVALID()};
}

std::optional<SourceLocation> compiler::get_source_location(const lex::any_lexeme& lexeme) const {
    return std::visit([&](const auto& lex) -> std::optional<SourceLocation> {
        using T = std::decay_t<decltype(lex)>;
        if constexpr (!std::is_base_of_v<k::lex::lexeme, T>) {
            return std::nullopt;
        } else {
            if (lex.content.empty()) {
                return std::nullopt;
            }
            const auto* src = source_for_position(&lex.content.front());
            if (!src) {
                return std::nullopt;
            }
            const auto coord = src->get_coordinates({&lex.content.front()});
            if (!coord) {
                return std::nullopt;
            }
            return SourceLocation{src->path, coord.line + 1, coord.col + 1};
        }
    }, lexeme);
}

static const char* severity_str[] = {
    "Trace  ",
    "Debug  ",
    "Info   ",
    "Warning",
    "Error  ",
    "Fatal  "
};

void compiler::report(const k::log::diagnostic& diag) {
    // Filter by log-level threshold
    if (diag.level < _log_level) return;

    // Filter by ignored diagnostic codes. Only ever applies to diagnostics
    // strictly below `error` severity (trace/debug/info/warning) that carry
    // a non-zero code — errors and fatals are never suppressible this way,
    // and code-less trace/debug messages (code == 0) never match.
    if (diag.level < log::diagnostic::severity::error
        && diag.code != 0
        && _ignored_diagnostic_codes.count(diag.code)) {
        return;
    }

    const unsigned int code = diag.code;
    const auto sev = (int)diag.level;
    const char* sev_str = severity_str[sev < 6 ? sev : 4];

    // Trace and debug messages use a simplified format (no source location, no error code)
    if (diag.level == log::diagnostic::severity::trace || diag.level == log::diagnostic::severity::debug) {
        std::string formatted = diag.message;
        if (!diag.args.empty()) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            for(const auto& arg : diag.args) store.push_back(arg);
            try { formatted = fmt::vformat(diag.message, store); } catch(...) {}
        }
        std::ostream& out = _log_stream ? *_log_stream : std::cout;
        out << sev_str << " : " << formatted << "\n";
        return;
    }

    // Resolve source location from the primary lexeme (pos), then range (start/end).
    // The resolved source file and coordinates are returned together.
    struct located {
        const source* src = nullptr;
        char_coord c1 = char_coord::INVALID();
        char_coord c2 = char_coord::INVALID();
    };

    auto lex_to_located = [&](const k::lex::any_lexeme& lex) -> located {
        return std::visit([&](const auto& l) -> located {
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_base_of_v<k::lex::lexeme, T>) {
                if (!l.content.empty()) {
                    auto* s = source_for_position(&l.content.front());
                    if (s) {
                        return { s,
                                 s->get_coordinates({&l.content.front()}),
                                 s->get_coordinates({&l.content.back()}) };
                    }
                }
            }
            return {};
        }, lex);
    };

    // Format message
    std::string formatted = diag.message;
    if (!diag.args.empty()) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for(const auto& arg : diag.args) store.push_back(arg);
        try { formatted = fmt::vformat(diag.message, store); } catch(...) {}
    }

    // Determine primary display coord and source file
    located primary;
    if (diag.pos) {
        primary = lex_to_located(*diag.pos);
    } else if (diag.start) {
        primary = lex_to_located(*diag.start);
    }

    const std::string& diag_path = primary.src ? primary.src->path : (_sources.empty() ? "" : _sources.front().path);

    // Print main message
    if (primary.c1) {
        fmt::print("{}:{}:{}: {} {:0>5X} : {}\n",
            diag_path, primary.c1.line, primary.c1.col,
            sev_str, code, formatted);
    } else {
        fmt::print("{}: {} {:0>5X} : {}\n",
            diag_path, sev_str, code, formatted);
    }

    // Print source excerpt
    auto log_excerpt = [&](const located& loc_start, const located& loc_end) {
        if (!loc_start.src) return;
        if (loc_start.c1 && loc_end.c2) {
            log_source_line(*loc_start.src, loc_start.c1, loc_end.c2);
        } else if (loc_start.c1) {
            log_source_line(*loc_start.src, loc_start.c1);
        }
    };

    if (diag.start && diag.end) {
        auto ls = lex_to_located(*diag.start);
        auto le = lex_to_located(*diag.end);
        log_excerpt(ls, le);
    } else if (diag.pos) {
        auto lp = lex_to_located(*diag.pos);
        if (lp.src && lp.c1 && lp.c2) log_source_line(*lp.src, lp.c1, lp.c2);
        else if (lp.src && lp.c1)      log_source_line(*lp.src, lp.c1);
    } else if (diag.start) {
        auto ls = lex_to_located(*diag.start);
        if (ls.src && ls.c1) log_source_line(*ls.src, ls.c1, ls.c2);
    }

    // Print notes
    for (const auto& note : diag.notes) {
        std::string note_msg = note.message;
        if (!note.args.empty()) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            for(const auto& arg : note.args) store.push_back(arg);
            try { note_msg = fmt::vformat(note.message, store); } catch(...) {}
        }
        if (note.pos) {
            auto nl = lex_to_located(*note.pos);
            const std::string& note_path = nl.src ? nl.src->path : diag_path;
            if (nl.c1) {
                fmt::print("{}:{}:{}: note: {}\n", note_path, nl.c1.line, nl.c1.col, note_msg);
                if (nl.src) log_source_line(*nl.src, nl.c1);
            } else {
                fmt::print("{}: note: {}\n", note_path, note_msg);
            }
        } else {
            fmt::print("{}: note: {}\n", diag_path, note_msg);
        }
    }
}

void compiler::print_logs() {
    // Logs are printed in real-time by report(). Nothing to do here.
}


void compiler::log_source_line(const source& src, unsigned int line, unsigned int col) {
    auto txt = src.get_line(line);
    fmt::print("{:>5d} | {}", line, txt);
    fmt::print("      | {}^", std::string(col, ' ') );
    if (txt.empty() || (txt.back()!='\r' && txt.back()!='\n')) {
        fmt::print("\n");
    }
}

void compiler::log_source_line(const source& src, unsigned int line, unsigned int start, unsigned int end) {
    if (end<start) {
        log_source_line(src, line, end, start);
    } else {
        auto txt = src.get_line(line);
        fmt::print("{:>5d} | {}", line, txt);
        if (start == end) {
            fmt::print("      | {}^", std::string(start, ' ') );
        } else {
            fmt::print("      | {}^{}", std::string(start, ' '), std::string(end-start-1, '~') );
        }
        if (txt.empty() || (txt.back()!='\r' && txt.back()!='\n')) {
            fmt::print("\n");
        }
    }
}

void compiler::log_source_lines(const source& src, unsigned int line_start, unsigned int start, unsigned int line_end, unsigned int end) {
    if (line_end<line_start) {
        log_source_lines(src, line_end, end, line_start, start);
    } else {
        auto line1 = src.get_line(line_start);
        fmt::print("{:>5d} | {}", line_start, line1);
        fmt::print("      | {}^{}", std::string(start, ' '), std::string(line1.size()-start-1, '~') );
        if (line_end > line_start + 1) {
            fmt::print("  ... |");
        }
        if (line1.empty() || (line1.back()!='\r' && line1.back()!='\n')) {
            fmt::print("\n");
        }
        auto line2 = src.get_line(line_end);
        fmt::print("{:>5d} | {}", line_end, line2);
        if (end==0) {
            fmt::print("      | ^");
        } else {
            fmt::print("      | {}^", std::string(end-1, '~') );
        }
        if (line2.empty() || (line2.back()!='\r' && line2.back()!='\n')) {
            fmt::print("\n");
        }
    }
}

void compiler::log_source_line(const source& src, char_coord pos) {
    log_source_line(src, pos.line, pos.col);
}

void compiler::log_source_line(const source& src, char_coord start, char_coord end) {
    if (start.line==end.line) {
        log_source_line(src, start.line, start.col, end.col);
    } else {
        log_source_lines(src, start.line, start.col, end.line, end.col);
    }
}





} // k
