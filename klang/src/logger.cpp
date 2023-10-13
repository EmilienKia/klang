/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

namespace k {
namespace log {

void logger::info(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::info, code, coord, coord, coord, message, args});
}

void logger::warning(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::warning, code, coord, coord, coord, message, args});
}

void logger::error(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::error, code, coord, coord, coord, message, args});
}

void logger::info(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::info, code, start, end, start, message, args});
}

void logger::warning(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::warning, code, start, end, start, message, args});
}

void logger::error(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::error, code, start, end, start, message, args});
}

void logger::info(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::info, code, start, end, pos, message, args});
}

void logger::warning(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::warning, code, start, end, pos, message, args});
}

void logger::error(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::error, code, start, end, pos, message, args});
}


void logger::print() const {
    for(const auto& log : *this) {
        print(log);
    }
}

void logger::print(const log_entry& entry) {
    static const char* criticality_str[] = {
            "Info   ",
            "Warning",
            "Error  "
    };

    if(entry.args.size()>0) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for(const auto& arg : entry.args) {
            store.push_back(arg);
        }
        std::string msg = fmt::vformat(entry.message, store);
        fmt::print("{},{} - {} {:0>5X} : {}\n", entry.start.line, entry.start.col, criticality_str[entry.criticality], entry.code, msg);
    } else {
        fmt::print("{},{} - {} {:0>5X} : {}\n", entry.start.line, entry.start.col, criticality_str[entry.criticality], entry.code, entry.message);
    }
}

} // k
} // log