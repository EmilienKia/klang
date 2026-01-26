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
class unit_llvm_ir_gen;
class unit_llvm_jit;
}

class unit;
class context;
}

class compiler {
protected:
    k::log::logger _log;
    k::parse::parser _parser;
    std::shared_ptr<k::parse::ast::unit> _ast_unit;
    std::shared_ptr<model::context> _context;
    std::shared_ptr<model::unit> _unit;

    std::unique_ptr<model::gen::unit_llvm_ir_gen> _gen;

    llvm::TargetMachine* _target;

    void process_gen(bool optimize = true, bool dump = true);

public:
    compiler(llvm::TargetMachine* target = nullptr);

    std::shared_ptr<model::unit> get_unit() {
        return _unit;
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


    void compile(std::string_view src, bool optimize = true, bool dump = false);

    std::unique_ptr<k::model::gen::unit_llvm_jit> to_jit();

    bool gen_object_file(const std::string& output_file);

protected:
    void find_elements_from(const name& name, const std::shared_ptr<model::element>& element, std::vector<std::shared_ptr<model::element>>& res) const;
};

} // k
#endif //KLANG_COMPILER_HPP