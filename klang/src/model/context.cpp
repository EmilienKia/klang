/*
 * K Language compiler
 *
 * Copyright 2024 Emilien Kia
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

#include "context.hpp"

#include "llvm/IR/Type.h"
#include "llvm/Support/TargetSelect.h"


namespace k::model {


//
// Context
//

std::shared_ptr<context> context::create()
{
    return std::shared_ptr<context>{new context()};
}

context::context()
{
    // TODO initialize them only once
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();

    _context = std::make_unique<llvm::LLVMContext>();
    init();
}

std::unique_ptr<llvm::LLVMContext> context::move_llvm_context() {
    std::unique_ptr<llvm::LLVMContext> res = std::make_unique<llvm::LLVMContext>();
    std::swap(res, _context);
    init();
    return res;
}

void context::init() {
    init_primitive_types();
}

void context::init_primitive_types() {
    _primitive_types.insert(std::initializer_list<std::map<primitive_type::PRIMITIVE_TYPE, std::shared_ptr<primitive_type>>::value_type>{
        {primitive_type::BOOL, primitive_type::make_shared(primitive_type::BOOL, true, false, 1, llvm::Type::getInt1Ty(**this))},
        {primitive_type::BYTE, primitive_type::make_shared(primitive_type::BYTE, true, false, 1*8, llvm::Type::getInt8Ty(**this))},
        {primitive_type::CHAR, primitive_type::make_shared(primitive_type::CHAR, false, false, 1*8, llvm::Type::getInt8Ty(**this))},
        {primitive_type::SHORT, primitive_type::make_shared(primitive_type::SHORT, false, false, 2*8, llvm::Type::getInt16Ty(**this))},
        {primitive_type::UNSIGNED_SHORT, primitive_type::make_shared(primitive_type::UNSIGNED_SHORT, true, false, 2*8, llvm::Type::getInt16Ty(**this))},
        {primitive_type::INT, primitive_type::make_shared(primitive_type::INT, false, false, 4*8, llvm::Type::getInt32Ty(**this))},
        {primitive_type::UNSIGNED_INT, primitive_type::make_shared(primitive_type::UNSIGNED_INT, true, false, 4*8, llvm::Type::getInt32Ty(**this))},
        {primitive_type::LONG, primitive_type::make_shared(primitive_type::LONG, false, false, 8*8, llvm::Type::getInt64Ty(**this))},
        {primitive_type::UNSIGNED_LONG, primitive_type::make_shared(primitive_type::UNSIGNED_LONG, true, false, 8*8, llvm::Type::getInt64Ty(**this))},
        // TODO Add 128 bits integers
        {primitive_type::FLOAT, primitive_type::make_shared(primitive_type::FLOAT, false, true, 4*8, llvm::Type::getFloatTy(**this))},
        {primitive_type::DOUBLE, primitive_type::make_shared(primitive_type::DOUBLE, false, true, 8*8, llvm::Type::getDoubleTy(**this))}
    });
};

void context::add_struct(std::shared_ptr<struct_type> st_type) {
    _struct_types.insert({st_type->name(), st_type});
}

std::shared_ptr<primitive_type> context::from_type(primitive_type::PRIMITIVE_TYPE type){
    return _primitive_types[type];
}

std::shared_ptr<type> context::from_string(const std::string& type_name) {
    // Look for primitive type
    static std::map<std::string, primitive_type::PRIMITIVE_TYPE> type_map {
            {"bool", primitive_type::BOOL},
            {"byte", primitive_type::BYTE},
            {"char", primitive_type::CHAR},
            {"unsigned char", primitive_type::UNSIGNED_CHAR},
            {"short", primitive_type::SHORT},
            {"unsigned short", primitive_type::UNSIGNED_SHORT},
            {"int", primitive_type::INT},
            {"unsigned int", primitive_type::UNSIGNED_INT},
            {"long", primitive_type::LONG},
            {"unsigned long", primitive_type::UNSIGNED_LONG},
            // TODO Add (unsigned) long long
            {"float", primitive_type::FLOAT},
            {"double", primitive_type::DOUBLE}
    };        
    if(auto it = type_map.find(type_name); it!=type_map.end()) {
        return _primitive_types[it->second];
    }

    // TODO Look at namespaces

    // Look for structures
    if(auto it = _struct_types.find(type_name); it!=_struct_types.end()) {
        return it->second;
    }

    // TODO find other types by name.
    return create_unresolved(name(type_name));
}

std::shared_ptr<type> context::from_keyword(const lex::keyword& kw, bool is_unsigned) {
    return from_string(is_unsigned ? ("unsigned " + kw.content) : kw.content);
    // TODO find other types by name.
}


std::shared_ptr<type> context::from_type_specifier(const k::parse::ast::type_specifier& type_spec)
{
    if(auto ident = dynamic_cast<const k::parse::ast::identified_type_specifier*>(&type_spec)) {
        return create_unresolved(ident->name.to_name());
    } else if(auto kw = dynamic_cast<const k::parse::ast::keyword_type_specifier*>(&type_spec)) {
        return from_keyword(kw->keyword);
    } else if(auto ptr = dynamic_cast<const k::parse::ast::pointer_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*ptr->subtype);
        if(ptr->pointer_type==lex::operator_::STAR) {
            return subtype->get_pointer();
        } else if(ptr->pointer_type==lex::operator_::AMPERSAND) {
            return subtype->get_reference();
        } else
            return {}; // Shall not happen
    } else if(auto arr = dynamic_cast<const k::parse::ast::array_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*arr->subtype);
        if(arr->lex_int) {
            return subtype->get_array(arr->lex_int->to_unsigned_int());
        } else {
            return subtype->get_array();
        }
    } else {
        return {};
    }
}


std::shared_ptr<type> context::from_literal(const k::lex::any_literal &literal) {
    if (std::holds_alternative<lex::integer>(literal)) {
        auto lit = literal.get<lex::integer>();
        switch (lit.size) {
            case k::lex::BYTE:
                return from_type(lit.unsigned_num ? primitive_type::BYTE : primitive_type::CHAR);
            case k::lex::SHORT:
                return from_type(
                        lit.unsigned_num ? primitive_type::UNSIGNED_SHORT : primitive_type::SHORT);
            case k::lex::INT:
                return from_type(lit.unsigned_num ? primitive_type::UNSIGNED_INT : primitive_type::INT);
            case k::lex::LONG:
                return from_type(
                        lit.unsigned_num ? primitive_type::UNSIGNED_LONG : primitive_type::LONG);
            default:
                // TODO Add (unsigned) long long and bigint
                return {};
        }
    } else if (std::holds_alternative<lex::float_num>(literal)) {
        auto lit = literal.get<lex::float_num>();
        switch (lit.size) {
            case k::lex::FLOAT:
                return from_type(primitive_type::FLOAT);
            case k::lex::DOUBLE:
                return from_type(primitive_type::DOUBLE);
            default:
                // TODO Add other floating point types
                return {};
        }
    } else if (std::holds_alternative<lex::character>(literal)) {
        return from_type(primitive_type::CHAR);
    } else if (std::holds_alternative<lex::boolean>(literal)) {
        return from_type(primitive_type::BOOL);
    } else {
        // TODO handle other literal types
        return nullptr;
    }
}

llvm::Type* context::get_llvm_type(const std::shared_ptr<type>& type) {
    return type ? type->get_llvm_type() : nullptr;
}


std::shared_ptr<unresolved_type> context::create_unresolved(const name& type_id) {
    std::shared_ptr<unresolved_type> res{new unresolved_type(type_id)};
    _unresolved.push_back(res);
    return res;
}

std::shared_ptr<unresolved_type> context::create_unresolved(name&& type_id) {
    std::shared_ptr<unresolved_type> res{new unresolved_type(type_id)};
    _unresolved.push_back(res);
    return res;
}


void context::resolve_types() {
    
// TODO HERE !!!

}


} // namespace k::model
