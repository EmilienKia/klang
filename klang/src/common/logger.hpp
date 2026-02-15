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

#include <vector>

namespace k::log {


struct log_entry {

    enum CRITICALITY {
        info,
        warning,
        error
    } criticality;

    unsigned int code;

    k::char_pos start, end, pos;

    std::string message;

    std::vector<std::string> args;
};


class logger {
public:
    virtual ~logger() = default;

    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args);

    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& pos, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args);

    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const std::string_view& message, const std::vector<std::string>& args);

    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) = 0;


    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args);

    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message, const std::vector<std::string>& args);

    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message);
    virtual void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) = 0;




    //
    // Message only
    //

    void info(const std::string_view& msg) { do_log(log_entry::info, 0, msg); }
    void warn(const std::string_view& msg) { do_log(log_entry::warning, 0, msg); }
    void error(const std::string_view& msg) { do_log(log_entry::error, 0, msg); }

    void info(unsigned int code, const std::string_view& msg) { do_log(log_entry::info, code, msg); }
    void warn(unsigned int code, const std::string_view& msg) { do_log(log_entry::warning, code, msg); }
    void error(unsigned int code, const std::string_view& msg) { do_log(log_entry::error, code, msg); }

    void info(const std::string_view& msg, const std::vector<std::string>& args) { do_log(log_entry::info, 0, msg, args); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args) { do_log(log_entry::warning, 0, msg, args); }
    void error(const std::string_view& msg, const std::vector<std::string>& args) { do_log(log_entry::error, 0, msg, args); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args) { do_log(log_entry::info, code, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args) { do_log(log_entry::warning, code, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args) { do_log(log_entry::error, code, msg, args); }

    //
    // Message and unique position
    //

    void info(const std::string_view& msg, const k::char_pos& pos) { do_log(log_entry::info, 0, pos, msg); }
    void warn(const std::string_view& msg, const k::char_pos& pos) { do_log(log_entry::warning, 0, pos, msg); }
    void error(const std::string_view& msg, const k::char_pos& pos) { do_log(log_entry::error, 0, pos, msg); }

    void info(const std::string_view& msg, const k::lex::lexeme& pos) { do_log(log_entry::info, 0, pos, msg); }
    void warn(const std::string_view& msg, const k::lex::lexeme& pos) { do_log(log_entry::warning, 0, pos, msg); }
    void error(const std::string_view& msg, const k::lex::lexeme& pos) { do_log(log_entry::error, 0, pos, msg); }

    void info(unsigned int code, const std::string_view& msg, const k::char_pos& pos) { do_log(log_entry::info, code, pos, msg); }
    void warn(unsigned int code, const std::string_view& msg, const k::char_pos& pos) { do_log(log_entry::warning, code, pos, msg); }
    void error(unsigned int code, const std::string_view& msg, const k::char_pos& pos) { do_log(log_entry::error, code, pos, msg); }

    void info(unsigned int code, const std::string_view& msg, const k::lex::lexeme& pos) { do_log(log_entry::info, code, pos, msg); }
    void warn(unsigned int code, const std::string_view& msg, const k::lex::lexeme& pos) { do_log(log_entry::warning, code, pos, msg); }
    void error(unsigned int code, const std::string_view& msg, const k::lex::lexeme& pos) { do_log(log_entry::error, code, pos, msg); }

    void info(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& pos) { do_log(log_entry::info, 0, pos, msg); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& pos) { do_log(log_entry::warning, 0, pos, msg); }
    void error(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& pos) { do_log(log_entry::error, 0, pos, msg); }

    void info(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& pos) { do_log(log_entry::info, 0, pos, msg); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& pos) { do_log(log_entry::warning, 0, pos, msg); }
    void error(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& pos) { do_log(log_entry::error, 0, pos, msg); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& pos) { do_log(log_entry::info, code, pos, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& pos) { do_log(log_entry::warning, code, pos, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& pos) { do_log(log_entry::error, code, pos, msg, args); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& pos) { do_log(log_entry::info, code, pos, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& pos) { do_log(log_entry::warning, code, pos, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& pos) { do_log(log_entry::error, code, pos, msg, args); }

    //
    // Message and two positions (begin + end)
    //

    void info(const std::string_view& msg, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::info, 0, start, end, msg); }
    void warn(const std::string_view& msg, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::warning, 0, start, end, msg); }
    void error(const std::string_view& msg, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::error, 0, start, end, msg); }

    void info(const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::info, 0, start, end, msg); }
    void warn(const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::warning, 0, start, end, msg); }
    void error(const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::error, 0, start, end, msg); }

    void info(unsigned int code, const std::string_view& msg, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::info, code, start, end, msg); }
    void warn(unsigned int code, const std::string_view& msg, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::warning, code, start, end, msg); }
    void error(unsigned int code, const std::string_view& msg, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::error, code, start, end, msg); }

    void info(unsigned int code, const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::info, code, start, end, msg); }
    void warn(unsigned int code, const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::warning, code, start, end, msg); }
    void error(unsigned int code, const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::error, code, start, end, msg); }

    void info(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::info, 0, start, end, msg, args); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::warning, 0, start, end, msg, args); }
    void error(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::error, 0, start, end, msg, args); }

    void info(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::info, 0, start, end, msg, args); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::warning, 0, start, end, msg, args); }
    void error(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::error, 0, start, end, msg, args); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::info, code, start, end, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::warning, code, start, end, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end) { do_log(log_entry::error, code, start, end, msg, args); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::info, code, start, end, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::warning, code, start, end, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end) { do_log(log_entry::error, code, start, end, msg, args); }

    //
    // Message and three positions (begin + end + pos)
    //

    void info(const std::string_view& msg, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::info, 0, start, end, pos, msg); }
    void warn(const std::string_view& msg, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::warning, 0, start, end, pos, msg); }
    void error(const std::string_view& msg, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::error, 0, start, end, pos, msg); }

    void info(const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::info, 0, start, end, pos, msg); }
    void warn(const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::warning, 0, start, end, pos, msg); }
    void error(const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::error, 0, start, end, pos, msg); }

    void info(unsigned int code, const std::string_view& msg, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::info, code, start, end, pos, msg); }
    void warn(unsigned int code, const std::string_view& msg, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::warning, code, start, end, pos, msg); }
    void error(unsigned int code, const std::string_view& msg, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::error, code, start, end, pos, msg); }

    void info(unsigned int code, const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::info, code, start, end, pos, msg); }
    void warn(unsigned int code, const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::warning, code, start, end, pos, msg); }
    void error(unsigned int code, const std::string_view& msg, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::error, code, start, end, pos, msg); }

    void info(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::info, 0, start, end, pos, msg, args); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::warning, 0, start, end, pos, msg, args); }
    void error(const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::error, 0, start, end, pos, msg, args); }

    void info(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::info, 0, start, end, pos, msg, args); }
    void warn(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::warning, 0, start, end, pos, msg, args); }
    void error(const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::error, 0, start, end, pos, msg, args); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::info, code, start, end, pos, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::warning, code, start, end, pos, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos) { do_log(log_entry::error, code, start, end, pos, msg, args); }

    void info(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::info, code, start, end, pos, msg, args); }
    void warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::warning, code, start, end, pos, msg, args); }
    void error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos) { do_log(log_entry::error, code, start, end, pos, msg, args); }

};

class logger_relay : public logger {
protected:
    logger& _log;
    unsigned int _class_flag = 0;

private:
    unsigned int with_flag(unsigned int code)const {
        return code!=0 ? _class_flag|code : 0;
    }

public:
    logger_relay(logger& log, unsigned int class_flag = 0) : _log(log), _class_flag(class_flag) {}
    ~logger_relay() override = default;

    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args) override;

    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& pos, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) override;

    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const std::string_view& message, const std::vector<std::string>& args) override;

    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) override;


    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) override;

    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message, const std::vector<std::string>& args) override;

    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message) override;
    void do_log(log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) override;

};


} // k::log

#endif //KLANG_LOGGER_HPP
