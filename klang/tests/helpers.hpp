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

#ifndef KLANG_HELPERS_HPP
#define KLANG_HELPERS_HPP

#include <memory>
#include <string>

#include "../src/common/logger.hpp"
#include "../src/common/common.hpp"
#include "../src/common/process.hpp"


namespace k::model::gen {
class jit;
}

std::unique_ptr<k::model::gen::jit> gen_jit(std::string_view src, bool dump = false, bool optimize = true);

k::tools::exec_result build_and_exec(const std::string_view& src);

class test_logger : public k::log::logger {
 void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) override;;
 void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) override;
};



#endif //KLANG_HELPERS_HPP