//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_LOGGER_HPP
#define KLANG_LOGGER_HPP

#include "lexer.hpp"

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

    k::lex::char_coord start, end;

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

    void print() const;

protected:
    static void print(const log_entry& entry);
};

} // k
} // log

#endif //KLANG_LOGGER_HPP
