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

#include <unordered_set>

#include "expressions.hpp"
#include "model.hpp"
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

    reset();
}

std::unique_ptr<llvm::LLVMContext> context::move_llvm_context() {
    std::unique_ptr<llvm::LLVMContext> res = std::make_unique<llvm::LLVMContext>();
    std::swap(res, _context);
    reset();
    return res;
}

void context::reset() {
    _context = std::make_unique<llvm::LLVMContext>();
    _primitive_types.clear();
    _struct_types.clear();
    _unresolved.clear();
    _global_vars.clear();
    _functions.clear();
    _parameter_variables.clear();
    _function_this_variables.clear();
    _variables.clear();
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
    return from_string(is_unsigned ? (std::string("unsigned ") + std::string(kw.content)) : std::string(kw.content));
    // TODO find other types by name.
}


std::shared_ptr<type> context::from_type_specifier(const k::parse::ast::type_specifier& type_spec)
{
    if(auto ct = dynamic_cast<const k::parse::ast::const_type_specifier*>(&type_spec)) {
        auto inner = from_type_specifier(*ct->subtype);
        return inner ? inner->get_const() : std::shared_ptr<type>{};
    } else if(auto ident = dynamic_cast<const k::parse::ast::identified_type_specifier*>(&type_spec)) {
        return create_unresolved(ident->name.to_name());
    } else if(auto kw = dynamic_cast<const k::parse::ast::keyword_type_specifier*>(&type_spec)) {
        return from_keyword(kw->keyword);
    } else if(auto ptr = dynamic_cast<const k::parse::ast::pointer_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*ptr->subtype);
        if(ptr->pointer_type==lex::operator_::STAR) {
            return subtype->get_pointer();
        } else if(ptr->pointer_type==lex::operator_::AMPERSAND) {
            return subtype->get_reference();
        } else if(ptr->pointer_type==lex::operator_::TILDE) {
            return subtype->get_link();
        } else if(ptr->pointer_type==lex::operator_::CARET) {
            return subtype->get_pinned();
        } else
            return {}; // Shall not happen
    } else if(auto arr = dynamic_cast<const k::parse::ast::array_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*arr->subtype);
        if(arr->lex_int) {
            return subtype->get_array(arr->lex_int->to_unsigned_int());
        } else {
            // int[] is a reference to an unsized array — identical to int[]&
            // Canonicalise immediately so that int[] and int[]& share the same type object.
            return subtype->get_array()->get_reference();
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


llvm::Constant* context::get_llvm_constant_from_literal(const k::lex::any_literal &literal) {
    if (std::holds_alternative<lex::integer>(literal)) {
        const auto &i = literal.get<lex::integer>();
        auto val = llvm::APInt((unsigned) i.size, i.int_content(), (uint8_t) i.base);
        return llvm::ConstantInt::get(**this, val);
    } else if (std::holds_alternative<lex::float_num>(literal)) {
        const auto &f = literal.get<lex::float_num>();
        llvm::Type* type = f.size==lex::DOUBLE ?  llvm::Type::getDoubleTy(*_context) : llvm::Type::getFloatTy(*_context) ;
        llvm::APFloat val(type->getScalarType()->getFltSemantics(), f.float_content());
        return llvm::ConstantFP::get(type, val);
    } else if (std::holds_alternative<lex::character>(literal)) {
        const auto &c = literal.get<lex::character>();
        auto val = llvm::APInt(8, static_cast<uint64_t>(std::get<char>(c.value())));
        return llvm::ConstantInt::get(**this, val);
    } else if (std::holds_alternative<lex::boolean>(literal)) {
        const auto& b = literal.get<lex::boolean>();
        if(std::get<bool>(b.value())) {
            return llvm::ConstantInt::getTrue(*_context);
        } else {
            return llvm::ConstantInt::getFalse(*_context);
        }
    } else {
        // TODO handle other literal types
        return nullptr;
    }/**/
}

llvm::Constant* context::get_llvm_constant_from_value(const k::value_type &value) {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? llvm::ConstantInt::getTrue(**this) : llvm::ConstantInt::getFalse(**this);
    } else if (std::holds_alternative<char>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(8, std::get<char>(value), true));
    } else if (std::holds_alternative<unsigned char>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(8, std::get<unsigned char>(value), false));
    } else if (std::holds_alternative<short>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(16, std::get<short>(value), true));
    } else if (std::holds_alternative<unsigned short>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(16, std::get<unsigned short>(value), false));
    } else if (std::holds_alternative<int>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(32, std::get<int>(value), true));
    } else if (std::holds_alternative<unsigned int>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(32, std::get<unsigned int>(value), false));
    } else if (std::holds_alternative<long long>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(64, std::get<long long>(value), true));
    } else if (std::holds_alternative<unsigned long long>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(64, std::get<unsigned long long>(value), false));
    } else if (std::holds_alternative<float>(value)) {
        return llvm::ConstantFP::get(llvm::Type::getFloatTy(**this), std::get<float>(value));
    } else if (std::holds_alternative<double>(value)) {
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(**this), std::get<double>(value));
    } else {
        // TODO handle other literal types
        return nullptr;
    }
}

llvm::Constant* context::get_llvm_constant_from_value_expression(const value_expression& value) {
    if (value.is_literal()) {
        return get_llvm_constant_from_literal(value.any_literal());
    } else {
        return get_llvm_constant_from_value(value.get_value());
    }
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

void context::resolve_struct_type(std::shared_ptr<struct_type> st_type,
                                   std::unordered_set<struct_type*>& in_progress) {
    if (st_type->is_resolved()) return;

    // Cycle detection
    if (in_progress.count(st_type.get())) {
        throw std::runtime_error("Cyclic dependency between struct types: " + st_type->name());
    }
    in_progress.insert(st_type.get());

    auto st = st_type->get_struct();
    std::vector<struct_type::field> fields;
    std::vector<llvm::Type*> types;
    for (auto [var_name, var] : st->variables()) {
        auto type = var->get_type();

        // For pointer/reference types whose immediate subtype is a struct_type,
        // resolve the underlying struct first so get_llvm_type() on the pointer/ref succeeds.
        // This handles the __parent__ field (reference to outer struct).
        auto effective_type = type;
        if (type::is_pointer(type) || type::is_reference(type)) {
            auto sub = type->get_subtype();
            if (sub) {
                if (auto dep_st = std::dynamic_pointer_cast<struct_type>(sub)) {
                    resolve_struct_type(dep_st, in_progress);
                } else if (!sub->is_resolved()) {
                    // Try resolving the sub-type first
                    auto resolved_sub = resolve_type(sub);
                    if (resolved_sub) {
                        if (auto dep_st = std::dynamic_pointer_cast<struct_type>(resolved_sub)) {
                            resolve_struct_type(dep_st, in_progress);
                        }
                    }
                }
            }
            // The pointer/reference type is now implicitly resolved (subtype has LLVM type).
            effective_type = type;
        } else if (!type->is_resolved()) {
            // Not resolved, try to resolve it.
            auto res_type = resolve_type(type);
            if (!res_type) {
                throw std::runtime_error("Cannot resolve structure field type: " + type->to_string());
            }
            // If the resolved type is itself a struct_type not yet fully resolved,
            // recursively resolve it now (handles any declaration order).
            if (auto dep_st_type = std::dynamic_pointer_cast<struct_type>(res_type)) {
                resolve_struct_type(dep_st_type, in_progress);
            }
            var->set_type(res_type);
            effective_type = res_type;
        } else if (auto dep_st_type = std::dynamic_pointer_cast<struct_type>(type)) {
            // Already a struct_type but may not have its LLVM type yet (e.g. forward reference).
            resolve_struct_type(dep_st_type, in_progress);
        }
        fields.emplace_back(fields.size(), var_name, effective_type);
        types.push_back(get_llvm_type(effective_type));
    }
    auto llvm_type = llvm::StructType::create(llvm_context(), llvm::ArrayRef<llvm::Type*>(types), st_type->name());
    auto default_const_value = llvm::ConstantAggregateZero::get(llvm_type);
    st_type->set_llvm_type(std::move(fields), llvm_type, default_const_value);

    in_progress.erase(st_type.get());
}

void context::resolve_types() {
    // Note: primitive types (and derivative) are always resolved.
    // Note: references, pointers and arrays depend on only from their subtypes.

    // Resolve structures in dependency order (recursive, handles any declaration order).
    std::unordered_set<struct_type*> in_progress;
    for (auto& [st_name, st_type] : _struct_types) {
        resolve_struct_type(st_type, in_progress);
    }
}

void context::init_module(const std::string& module_name) {
    _module = std::make_unique<llvm::Module>(module_name, *_context);
}

std::shared_ptr<type> context::resolve_type(const std::shared_ptr<type>& type) {
    if (type->is_resolved()) {
        return type;
    } else if (type::is_pointer(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            // Not resolvable
            // TODO throw an exception
            std::cerr << "Error: cannot resolve pointer subtype." << std::endl;
            return nullptr;
        } else {
            return res->get_pointer();
        }
    } else if (type::is_reference(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            std::cerr << "Error: cannot resolve reference subtype." << std::endl;
            return nullptr;
        } else {
            return res->get_reference();
        }
    } else if (type::is_link(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            std::cerr << "Error: cannot resolve link subtype." << std::endl;
            return nullptr;
        } else {
            return res->get_link();
        }
    } else if (type::is_pinned(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            std::cerr << "Error: cannot resolve pinned subtype." << std::endl;
            return nullptr;
        } else {
            return res->get_pinned();
        }
    } else if (type::is_array(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            // Not resolvable
            // TODO throw an exception
            std::cerr << "Error: cannot resolve array subtype." << std::endl;
            return nullptr;
        } else {
            if (type::is_sized_array(type)) {
                auto sized_arr = std::dynamic_pointer_cast<sized_array_type>(type);
                return res->get_array(sized_arr->get_size());
            } else {
                // Unsized array: canonicalise to ref<array<T>> (same as int[]&)
                return res->get_array()->get_reference();
            }
        }
    } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(type)) {
        auto res = unres->get_resolved();
        if (res) {
            return res;
        } else {
            auto resolved_type = from_string(unres->type_id().to_string());
            // If from_string returned a struct_type, accept it even if its LLVM type is not
            // yet generated: resolve_struct_type will handle the LLVM materialisation
            // recursively. An unresolved_type remaining means the name was truly unknown.
            if (std::dynamic_pointer_cast<struct_type>(resolved_type)) {
                unres->resolve(resolved_type);
                return resolved_type;
            }
            if (!resolved_type->is_resolved()) {
                // TODO throw an exception
                std::cerr << "Error: cannot resolve type: " << unres->type_id().to_string() << std::endl;
                return nullptr;
            } else {
                unres->resolve(resolved_type);
                return resolved_type;
            }
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(type)) {
        // Already a struct_type (possibly not yet LLVM-resolved) — return it as-is.
        // The LLVM type will be set when resolve_struct_type runs.
        return st_type;
    } else {
        // Unknown type
        return nullptr;
    }
}

} // namespace k::model
