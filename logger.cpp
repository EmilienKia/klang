//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "logger.hpp"

#include <iostream>
#include <sstream>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

namespace k {
namespace log {

void logger::info(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::info, code, coord, coord, message, args});
}

void logger::warning(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::warning, code, coord, coord, message, args});
}

void logger::error(unsigned int code, const k::lex::char_coord& coord, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::error, code, coord, coord, message, args});
}

void logger::info(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::info, code, start, end, message, args});
}

void logger::warning(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::warning, code, start, end, message, args});
}

void logger::error(unsigned int code, const k::lex::char_coord& start, const k::lex::char_coord& end, const std::string& message, const std::vector<std::string>& args) {
    push_back(log_entry{log_entry::CRITICALITY::error, code, start, end, message, args});
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
        fmt::print("{},{} - {} {:0>4X} : {}\n", entry.start.line, entry.start.col, criticality_str[entry.criticality], entry.code, msg);
    } else {
        fmt::print("{},{} - {} {:0>4X} : {}\n", entry.start.line, entry.start.col, criticality_str[entry.criticality], entry.code, entry.message);
    }
}

} // k
} // log