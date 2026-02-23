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

#ifndef KLANG_LOGGER_HPP
#define KLANG_LOGGER_HPP

#include "common.hpp"
#include "../lex/lexemes.hpp"

#include <string>
#include <vector>
#include <optional>

namespace k::log {

// ---------------------------------------------------------------------------
// diagnostic
// ---------------------------------------------------------------------------

/**
 * A diagnostic message DTO.
 *
 * Holds a severity, an optional error code, a message with optional formatting
 * arguments (for i18n), optional source location (up to 3 lexemes: pos, start,
 * end), and an optional flat list of sub-diagnostics for additional context
 * (e.g. candidate declarations when resolving an ambiguous call).
 *
 * Severity levels:
 *  - info  : informational, no issue, compilation continues and produces output
 *  - warning : something suspicious, compilation continues and produces output
 *  - error : something is wrong, compilation continues to gather more errors
 *            but will NOT produce output
 *  - fatal : unrecoverable condition (e.g. internal compiler error, file not
 *            found), compilation stops immediately
 *
 * Source location:
 *  - pos   : primary point of interest (e.g. the offending token)
 *  - start : beginning of the highlighted range
 *  - end   : end of the highlighted range
 * All three are optional; a renderer may choose to underline [start,end] and
 * point an arrow at pos, falling back gracefully when some are absent.
 *
 * Sub-diagnostics (notes):
 * A flat list of additional contextual messages, each with its own message,
 * arguments and optional source position. They are never recursive.
 * Typical use: listing candidates for an ambiguous overload resolution.
 */
struct diagnostic {

    enum class severity {
        info,
        warning,
        error,
        fatal
    };

    /** Optional error code; 0 means no code. */
    unsigned int code = 0;

    severity level = severity::error;

    /**
     * Main message template.
     * May contain positional placeholders {0}, {1}, … referring to args.
     * Owned string — no lifecycle issues.
     */
    std::string message;

    /** Substitution arguments for placeholders in message. */
    std::vector<std::string> args;

    /** Primary source location (e.g. the offending token). */
    std::optional<k::lex::any_lexeme> pos;
    /** Start of the highlighted source range. */
    std::optional<k::lex::any_lexeme> start;
    /** End of the highlighted source range. */
    std::optional<k::lex::any_lexeme> end;

    /**
     * A sub-diagnostic providing additional context.
     * Not recursive: sub-diagnostics cannot have their own sub-diagnostics.
     */
    struct note {
        /** Note message template, may contain {0}, {1}, … placeholders. */
        std::string message;
        /** Substitution arguments. */
        std::vector<std::string> args;
        /** Optional source position for this note. */
        std::optional<k::lex::any_lexeme> pos;
    };

    /** Flat list of additional contextual notes. */
    std::vector<note> notes;

    // -------------------------------------------------------------------------
    // Factory helpers
    // -------------------------------------------------------------------------

    static diagnostic make(severity lvl, unsigned int code, std::string msg,
                           std::vector<std::string> args = {}) {
        diagnostic d;
        d.level   = lvl;
        d.code    = code;
        d.message = std::move(msg);
        d.args    = std::move(args);
        return d;
    }

    static diagnostic make_info(unsigned int code, std::string msg,
                                std::vector<std::string> args = {}) {
        return make(severity::info, code, std::move(msg), std::move(args));
    }
    static diagnostic make_warning(unsigned int code, std::string msg,
                                   std::vector<std::string> args = {}) {
        return make(severity::warning, code, std::move(msg), std::move(args));
    }
    static diagnostic make_error(unsigned int code, std::string msg,
                                 std::vector<std::string> args = {}) {
        return make(severity::error, code, std::move(msg), std::move(args));
    }
    static diagnostic make_fatal(unsigned int code, std::string msg,
                                 std::vector<std::string> args = {}) {
        return make(severity::fatal, code, std::move(msg), std::move(args));
    }

    // -------------------------------------------------------------------------
    // Builder-style mutators (fluent API)
    // -------------------------------------------------------------------------

    /** Set the primary source location (single token). */
    diagnostic& at(k::lex::any_lexeme p) {
        pos = std::move(p);
        return *this;
    }

    /** Set the start of the highlighted source range. */
    diagnostic& from(k::lex::any_lexeme s) {
        start = std::move(s);
        return *this;
    }

    /** Set the end of the highlighted source range. */
    diagnostic& to(k::lex::any_lexeme e) {
        end = std::move(e);
        return *this;
    }

    /** Set both start and end of the highlighted source range. */
    diagnostic& range(k::lex::any_lexeme s, k::lex::any_lexeme e) {
        start = std::move(s);
        end   = std::move(e);
        return *this;
    }

    /**
     * Add a contextual note.
     * @param msg  Note message template (may contain {0},{1},… placeholders).
     * @param a    Substitution arguments.
     * @param p    Optional source position for this note.
     */
    diagnostic& add_note(std::string msg,
                         std::vector<std::string> a = {},
                         std::optional<k::lex::any_lexeme> p = std::nullopt) {
        notes.push_back({std::move(msg), std::move(a), std::move(p)});
        return *this;
    }
};


// ---------------------------------------------------------------------------
// Exception hierarchy
// ---------------------------------------------------------------------------

/**
 * Format a diagnostic message by substituting {0}, {1}, … placeholders with
 * the corresponding args.  Falls back to the raw message if substitution fails
 * (e.g. mismatched placeholder count) or if there are no args.
 *
 * This intentionally does NOT include any lexeme / source-location decoration
 * — it is meant for exception::what() which should be plain text.
 */
std::string format_diagnostic_message(const diagnostic& diag);

/**
 * Base class for all compiler-generated exceptions.
 *
 * Holds the full diagnostic (severity, code, message, args, optional source
 * locations) so that catch sites have access to structured data rather than
 * just a bare string.  what() returns the formatted message (placeholders
 * substituted with args) with no source-location decoration.
 *
 * Derived classes (parsing_error, resolution_error, generation_error) exist
 * only to allow catch sites to distinguish the compilation phase.
 */
class compiler_error : public std::runtime_error {
protected:
    diagnostic _diag;

    /** Build the what()-string from the diagnostic at construction time. */
    static std::string build_what(const diagnostic& d) {
        return format_diagnostic_message(d);
    }

public:
    explicit compiler_error(diagnostic diag)
        : std::runtime_error(build_what(diag)), _diag(std::move(diag)) {}

    /** The full structured diagnostic. */
    const diagnostic& get_diagnostic() const noexcept { return _diag; }
};

// ---------------------------------------------------------------------------
// logger
// ---------------------------------------------------------------------------

/**
 * Abstract base logger.
 *
 * The single pure-virtual method is report(diagnostic).
 * All other helpers build a diagnostic and forward to report().
 *
 * Source location can be provided as:
 *  - a single k::lex::any_lexeme  (→ pos)
 *  - two k::lex::any_lexeme       (→ start + end)
 *  - three k::lex::any_lexeme     (→ start + end + pos)
 *  - a k::lex::opt_any_lexeme     (→ pos, may be empty)
 * This replaces the former lexeme_logger helper class.
 */
class logger {
public:
    virtual ~logger() = default;

    /** Core posting method. Implementations decide what to do per severity. */
    virtual void report(const diagnostic& diag) = 0;

    // -----------------------------------------------------------------------
    // Convenience helpers — no location
    // -----------------------------------------------------------------------

    void info (unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(code, msg, args));
    }
    void warn (unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(code, msg, args));
    }
    void error(unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(code, msg, args));
    }
    void fatal(unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(code, msg, args));
    }

    // -----------------------------------------------------------------------
    // Convenience helpers — single lexeme (pos)
    // -----------------------------------------------------------------------

    void info (unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(code, msg, args).at(pos));
    }
    void warn (unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(code, msg, args).at(pos));
    }
    void error(unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(code, msg, args).at(pos));
    }
    void fatal(unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(code, msg, args).at(pos));
    }

    // -----------------------------------------------------------------------
    // Convenience helpers — optional lexeme (pos may be absent)
    // -----------------------------------------------------------------------

    void info (unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_info(code, msg, args);
        if(pos) d.at(*pos);
        report(d);
    }
    void warn (unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_warning(code, msg, args);
        if(pos) d.at(*pos);
        report(d);
    }
    void error(unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_error(code, msg, args);
        if(pos) d.at(*pos);
        report(d);
    }
    void fatal(unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_fatal(code, msg, args);
        if(pos) d.at(*pos);
        report(d);
    }

    // -----------------------------------------------------------------------
    // Convenience helpers — two lexemes (start + end)
    // -----------------------------------------------------------------------

    void info (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(code, msg, args).range(start, end));
    }
    void warn (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(code, msg, args).range(start, end));
    }
    void error(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(code, msg, args).range(start, end));
    }
    void fatal(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(code, msg, args).range(start, end));
    }

    // -----------------------------------------------------------------------
    // Convenience helpers — three lexemes (start + end + pos)
    // -----------------------------------------------------------------------

    void info (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(code, msg, args).range(start, end).at(pos));
    }
    void warn (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(code, msg, args).range(start, end).at(pos));
    }
    void error(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(code, msg, args).range(start, end).at(pos));
    }
    void fatal(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(code, msg, args).range(start, end).at(pos));
    }

};

// ---------------------------------------------------------------------------
// logger_relay — forwards all diagnostics to a parent logger, optionally
// ORing a class-flag into the error code.
// ---------------------------------------------------------------------------
class logger_relay : public logger {
protected:
    logger& _log;
    unsigned int _class_flag = 0;

    unsigned int with_flag(unsigned int code) const {
        return code != 0 ? (_class_flag | code) : 0;
    }

public:
    logger_relay(logger& log, unsigned int class_flag = 0)
        : _log(log), _class_flag(class_flag) {}

    ~logger_relay() override = default;

    void report(const diagnostic& diag) override {
        if(diag.code == 0 || _class_flag == 0) {
            _log.report(diag);
        } else {
            diagnostic flagged = diag;
            flagged.code = with_flag(diag.code);
            _log.report(flagged);
        }
    }

    // -----------------------------------------------------------------------
    // Helpers that build a diagnostic with the class-flagged code and forward.
    // -----------------------------------------------------------------------

    void info (unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(with_flag(code), msg, args));
    }
    void warn (unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(with_flag(code), msg, args));
    }
    void error(unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(with_flag(code), msg, args));
    }
    void fatal(unsigned int code, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(with_flag(code), msg, args));
    }

    void info (unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(with_flag(code), msg, args).at(pos));
    }
    void warn (unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(with_flag(code), msg, args).at(pos));
    }
    void error(unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(with_flag(code), msg, args).at(pos));
    }
    void fatal(unsigned int code, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(with_flag(code), msg, args).at(pos));
    }

    void info (unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_info(with_flag(code), msg, args);
        if(pos) d.at(*pos);
        report(d);
    }
    void warn (unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_warning(with_flag(code), msg, args);
        if(pos) d.at(*pos);
        report(d);
    }
    void error(unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_error(with_flag(code), msg, args);
        if(pos) d.at(*pos);
        report(d);
    }
    void fatal(unsigned int code, const k::lex::opt_any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        auto d = diagnostic::make_fatal(with_flag(code), msg, args);
        if(pos) d.at(*pos);
        report(d);
    }

    void info (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(with_flag(code), msg, args).range(start, end));
    }
    void warn (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(with_flag(code), msg, args).range(start, end));
    }
    void error(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(with_flag(code), msg, args).range(start, end));
    }
    void fatal(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(with_flag(code), msg, args).range(start, end));
    }

    void info (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_info(with_flag(code), msg, args).range(start, end).at(pos));
    }
    void warn (unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_warning(with_flag(code), msg, args).range(start, end).at(pos));
    }
    void error(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_error(with_flag(code), msg, args).range(start, end).at(pos));
    }
    void fatal(unsigned int code, const k::lex::any_lexeme& start, const k::lex::any_lexeme& end, const k::lex::any_lexeme& pos, const std::string& msg, const std::vector<std::string>& args = {}) {
        report(diagnostic::make_fatal(with_flag(code), msg, args).range(start, end).at(pos));
    }
};

} // namespace k::log

#endif // KLANG_LOGGER_HPP
