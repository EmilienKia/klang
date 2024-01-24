/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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

#include "../lex/lexer.hpp"

#include <vector>

namespace k {
namespace log {

struct log_entry {

    enum CRITICALITY {
        info,
        warning,
        error
    } criticality;

    unsigned int code;

    k::lex::char_coord start, end, pos;

    std::string message;

    std::vector<std::string> args;
};

class logger : public std::vector<log_entry> {
public:
    typedef std::vector<log_entry> parent_t;

    using parent_t::parent_t;

    void info(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args = {});
    void warning(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args = {});
    void error(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args = {});

    void info(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args = {});
    void warning(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args = {});
    void error(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args = {});

    void info(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string& message, const std::vector<std::string>& args = {});
    void warning(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string& message, const std::vector<std::string>& args = {});
    void error(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const k::lex::char_coord& pos, const std::string& message, const std::vector<std::string>& args = {});

    void print() const;

protected:
    static void print(const log_entry& entry);
};

} // k
} // log

#endif //KLANG_LOGGER_HPP
