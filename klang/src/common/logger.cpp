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

#include "logger.hpp"

#include <iostream>
#include <sstream>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

namespace k::log {
//
// Logger
//

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message) {
    do_log(criticality, code, k::lex::char_coord{}, k::lex::char_coord{}, k::lex::char_coord{}, message, {});
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args) {
    do_log(criticality, code, k::lex::char_coord{}, k::lex::char_coord{}, k::lex::char_coord{}, message, args);
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& pos, const std::string_view& message) {
    do_log(criticality, code, {}, {}, pos, message, {});
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& pos, const std::string_view& message, const std::vector<std::string>& args) {
    do_log(criticality, code, {}, {}, pos, message, args);
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string_view& message) {
    do_log(criticality, code, start, end, {}, message, {});
}
void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string_view& message, const std::vector<std::string>& args) {
    do_log(criticality, code, start, end, {}, message, args);
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string_view& message) {
    do_log(criticality, code, start, end, pos, message, {});
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message) {
    do_log(criticality, code, k::lex::lexeme{}, k::lex::lexeme{}, pos, message, {});
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) {
    do_log(criticality, code, k::lex::lexeme{}, k::lex::lexeme{}, pos, message, args);
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message) {
    do_log(criticality, code, start, end, k::lex::lexeme{}, message, {});
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message, const std::vector<std::string>& args) {
    do_log(criticality, code, start, end, k::lex::lexeme{}, message, args);
}

void logger::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message) {
    do_log(criticality, code, start, end, pos, message, {});
}


//
// Logger relay
//

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), message);
}
void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), message, args);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& pos, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), pos, message);
}
void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& pos, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), pos, message, args);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), start, end, message);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), start, end, message, args);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), start, end, pos, message);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), start, end, pos, message, args);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), pos, message);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), pos, message, args);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), start, end, message);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), start, end, message, args);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message) {
    _log.do_log(criticality, with_flag(code), start, end, pos, message);
}

void logger_relay::do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) {
    _log.do_log(criticality, with_flag(code), start, end, pos, message, args);
}

} // k::log