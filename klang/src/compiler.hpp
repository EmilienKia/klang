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
#ifndef KLANG_COMPILER_HPP
#define KLANG_COMPILER_HPP
#include <string_view>

#include "common/logger.hpp"
#include "parse/parser.hpp"

namespace llvm {
class TargetMachine;
}

namespace k {
namespace model {
class element;

namespace gen {
class implementation_generator;
class jit;
}

class unit;
class context;
}

class compiler : protected log::logger,  public std::enable_shared_from_this<compiler> {
protected:
    static bool _compiler_class_init;

    source _source;

    std::shared_ptr<k::parse::ast::unit> _ast_unit;
    std::shared_ptr<model::context> _context;
    std::shared_ptr<model::unit> _model_unit;

    llvm::TargetMachine* _target;

    /** Set to true when a fatal compilation error occurs (e.g. overload collision).
     *  to_jit() returns nullptr when this flag is set. */
    bool _has_compilation_error = false;

    void process_generation(bool optimize = true, bool dump = true);

    compiler(llvm::TargetMachine* target = nullptr);

public:
    static void initialize();

    static std::shared_ptr<compiler> create(llvm::TargetMachine* target_machine = nullptr);

    std::shared_ptr<model::unit> get_unit() {
        return _model_unit;
    }

    std::shared_ptr<const model::unit> get_unit() const {
        return _model_unit;
    }

    /**
     * Try to find elements recursively by their name.
     * If the name is absolute (starting by the "::" prefix, the lookup is done including the root namespace of the module,
     * or directly look at imported dependencies (importing dependencies is not implemented yet).
     * If the name is relative:
     * - First look at the members of the root namespace of the module
     * - Then if the name corresponds to the root namespace of the module, look into the module
     * - Then Look at the imported modules (not implemented yet)
     * @param name Name of element to look for
     * @return List of elements with the corresponding name
     */
    std::vector<std::shared_ptr<model::element>> find_elements(const name& name) const;
    std::vector<std::shared_ptr<model::element>> find_elements(const std::string& name) const {
        return find_elements(name::from(name));
    }

    /**
     * Try to find an element from its name and return its fully mangled name.
     * Works only when only one element with this exact name exists.
     * Only callable elements are considered (methods or global (or static) variable).
     * @param name Name of element to look for
     * @return Mangled name of found element
     * @throw std::runtime_exception If multiple elements with same name exists or not found element.
     */
    std::string get_element_mangled_name(const name& name) const;
    std::string get_element_mangled_name(const std::string& name) const {
        return get_element_mangled_name(name::from(name));
    }


    void parse_source(const std::string_view& path, const std::string_view& src, bool optimize = true, bool dump = false);

    bool has_main_method() const;

    void dump_gen_code();
    bool verify_gen_code();
    void optimize_gen_code();

    std::unique_ptr<k::model::gen::jit> to_jit(bool init_runtime = true);

    bool gen_object_file(const std::string& output_file);

    bool gen_executable(const std::string& output_file);

    void print_logs();

protected:
    void find_elements_from(const name& name, const std::shared_ptr<model::element>& element, std::vector<std::shared_ptr<model::element>>& res) const;

    char_coord coordinates_from_pos(const k::char_pos& coord) const;
    std::pair<char_coord,char_coord> coordinates_from_lex(const lex::lexeme& lex) const;

    void log_message(k::log::log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args);
    void log_message(k::log::log_entry::CRITICALITY criticality, unsigned int code, char_coord pos, const std::string_view& message, const std::vector<std::string>& args);
    void log_message(k::log::log_entry::CRITICALITY criticality, const std::string_view& message, const std::vector<std::string>& args);
    void log_message(k::log::log_entry::CRITICALITY criticality, char_coord pos, const std::string_view& message, const std::vector<std::string>& args);

    void log_source_line(char_coord pos);
    void log_source_line(unsigned int line, unsigned int col);

    void log_source_line(char_coord start, char_coord end);
    void log_source_line(unsigned int line, unsigned int start, unsigned int end);
    void log_source_lines(unsigned int line_start, unsigned int start, unsigned int line_end, unsigned int end);

    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args) override;
    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) override;
    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const std::string_view& message, const std::vector<std::string>& args);
    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) override;



    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args);
    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message, const std::vector<std::string>& args);
    void do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) override;


};

} // k
#endif //KLANG_COMPILER_HPP