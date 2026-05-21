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
#include <string_view>

#include "expressions.hpp"
#include "model.hpp"
#include "../errors.hpp"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "../common/target_init.hpp"


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
    ::k::initialize_llvm_targets();

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
    _null_type = std::shared_ptr<k::model::null_type>(new k::model::null_type());
    _struct_types.clear();
    _unresolved.clear();
    _global_vars.clear();
    _functions.clear();
    _parameter_variables.clear();
    _function_this_variables.clear();
    _variables.clear();
    _string_pool.clear();
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

void context::add_enum(const std::string& name, std::shared_ptr<enum_type> et) {
    _enum_types.insert({name, et});
}

void context::materialise_opaque_struct_type(std::shared_ptr<struct_type> st_type) {
    if (st_type->is_resolved()) return; // already has an LLVM type
    // Create an opaque (body-less) LLVM StructType so that is_resolved() returns true.
    // The body can be set later (e.g. via resolve_struct_type) if needed.
    auto llvm_type = llvm::StructType::create(llvm_context(), st_type->name());
    auto default_const = llvm::ConstantAggregateZero::get(llvm_type);
    st_type->set_llvm_type({}, llvm_type, default_const);
}

void context::build_imported_struct_body(std::shared_ptr<struct_type> st_type) {
    auto st = st_type->get_struct();
    if (!st) {
        // No aggregate model — fallback to opaque
        materialise_opaque_struct_type(st_type);
        return;
    }

    std::vector<struct_type::field> fields;
    std::vector<llvm::Type*> llvm_types;

    // Walk children in insertion order, picking member_variable_definition entries.
    for (auto& child : st->get_children()) {
        auto var = std::dynamic_pointer_cast<member_variable_definition>(child);
        if (!var) continue;

        const std::string var_name = var->get_short_name();
        auto mtype = var->get_type();

        if (!mtype) {
            // Synthetic field without type (e.g. vptr placeholder) → opaque ptr
            llvm::Type* ptr_ty = llvm::PointerType::get(llvm_context(), 0);
            fields.emplace_back(struct_type::field{fields.size(), var_name, std::weak_ptr<k::model::type>{}});
            llvm_types.push_back(ptr_ty);
            continue;
        }

        // If the field type is itself an unresolved struct, ensure it has an LLVM type
        // (opaque at minimum) so that get_llvm_type() returns non-null.
        if (auto dep_st = std::dynamic_pointer_cast<struct_type>(mtype)) {
            if (!dep_st->is_resolved()) {
                materialise_opaque_struct_type(dep_st);
            }
        } else if (type::is_pointer(mtype) || type::is_reference(mtype)) {
            auto sub = mtype->get_subtype();
            if (sub) {
                if (auto dep_st = std::dynamic_pointer_cast<struct_type>(sub)) {
                    if (!dep_st->is_resolved()) {
                        materialise_opaque_struct_type(dep_st);
                    }
                }
            }
        }

        llvm::Type* llvm_ty = get_llvm_type(mtype);
        if (!llvm_ty) {
            // Cannot resolve — skip or use i8 placeholder
            llvm_ty = llvm::Type::getInt8Ty(llvm_context());
        }

        fields.emplace_back(struct_type::field{fields.size(), var_name, mtype});
        llvm_types.push_back(llvm_ty);
    }

    // If the struct_type already has a named LLVM opaque type, set its body.
    // Otherwise create a new StructType.
    if (st_type->is_resolved()) {
        // Already has an opaque LLVM StructType — set the body on it.
        auto* existing = llvm::dyn_cast_or_null<llvm::StructType>(st_type->get_llvm_type());
        if (existing && existing->isOpaque()) {
            existing->setBody(llvm::ArrayRef<llvm::Type*>(llvm_types));
            auto default_const = llvm::ConstantAggregateZero::get(existing);
            st_type->set_llvm_type(std::move(fields), existing, default_const);
            return;
        }
    }

    // Not yet resolved — create a new named StructType.
    auto* llvm_type = llvm::StructType::create(llvm_context(),
                                               llvm::ArrayRef<llvm::Type*>(llvm_types),
                                               st_type->name());
    auto* default_const = llvm::ConstantAggregateZero::get(llvm_type);
    st_type->set_llvm_type(std::move(fields), llvm_type, default_const);
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

    // Look for enum types
    if(auto it = _enum_types.find(type_name); it!=_enum_types.end()) {
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
        auto unres = create_unresolved(ident->name.to_name());
        if (!ident->template_args.empty()) {
            unres->_ast_template_args = ident->template_args;
            unres->_has_explicit_template_args = true;
        } else if (ident->has_explicit_template_args) {
            // Empty <> — no args but explicit template usage (all defaults)
            unres->_has_explicit_template_args = true;
        }
        return unres;
    } else if(auto kw = dynamic_cast<const k::parse::ast::keyword_type_specifier*>(&type_spec)) {
        return from_keyword(kw->keyword, kw->is_unsigned);
    } else if(auto ptr = dynamic_cast<const k::parse::ast::pointer_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*ptr->subtype);
        if(ptr->pointer_type==lex::operator_::AMPERSAND) {
            return subtype->get_reference();
        }
        // int[] is canonicalized to reference(array(int)) for stack/parameter use,
        // but for indirection types (T[]*, T[]+, T[]?) we need pointer/link/view(array(T)),
        // not pointer/link/view(ref(array(T))).  Unwrap the reference when it wraps
        // an unsized array, same as the owner case below.
        if (auto ref = std::dynamic_pointer_cast<reference_type>(subtype)) {
            if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                if (!arr->is_sized()) {
                    subtype = arr;
                }
            }
        }
        if(ptr->pointer_type==lex::operator_::STAR) {
            return subtype->get_pointer();
        } else if(ptr->pointer_type==lex::operator_::PLUS) {
            return subtype->get_link();
        } else if(ptr->pointer_type==lex::operator_::QUESTION_MARK) {
            return subtype->get_view();
        } else if(ptr->pointer_type==lex::operator_::HASH) {
            return subtype->get_drain();
        } else
            return {}; // Shall not happen
    } else if(auto own = dynamic_cast<const k::parse::ast::owner_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*own->subtype);
        // int[] is canonicalized to reference(array(int)) for stack/parameter use,
        // but for ownership (int[]!) we need owner(array(int)), not owner(ref(array(int))).
        // Unwrap the reference when it wraps an unsized array inside an owner.
        if (auto ref = std::dynamic_pointer_cast<reference_type>(subtype)) {
            if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                if (!arr->is_sized()) {
                    subtype = arr;
                }
            }
        }
        return subtype->get_owner();
    } else if(auto arr = dynamic_cast<const k::parse::ast::array_type_specifier*>(&type_spec)) {
        auto subtype = from_type_specifier(*arr->subtype);
        if(arr->lex_int) {
            return subtype->get_array(arr->lex_int->to_unsigned_int());
        } else {
            // int[] is a reference to an unsized array — identical to int[]&
            // Canonicalise immediately so that int[] and int[]& share the same type object.
            return subtype->get_array()->get_reference();
        }
    } else if(auto frt = dynamic_cast<const k::parse::ast::function_ref_type_specifier*>(&type_spec)) {
        // Determine ref kind from operator token
        function_reference_type::ref_kind rk = function_reference_type::ref_kind::pointer;
        if (frt->ref_op == lex::operator_::QUESTION_MARK) {
            rk = function_reference_type::ref_kind::view;
        } else if (frt->ref_op == lex::operator_::PLUS) {
            rk = function_reference_type::ref_kind::link;
        }

        // Resolve parameter types (may produce unresolved types — resolved later)
        std::vector<std::shared_ptr<type>> param_types;
        for (const auto& pt : frt->param_types) {
            auto t = from_type_specifier(*pt);
            if (!t) return {};
            param_types.push_back(t);
        }

        // Owner structure (for member function pointer): resolved lazily
        // We create an unresolved placeholder struct_type via create_unresolved if owner is set.
        // The real binding happens in type_reference_resolver.
        std::shared_ptr<unresolved_function_ref_type> result{
            new unresolved_function_ref_type(
                frt->owner.has_value() ? frt->owner->to_name() : k::name{},
                rk, param_types)};
        return result;
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
    } else if (std::holds_alternative<lex::string>(literal)) {
        const auto& s = literal.get<lex::string>();
        // TODO Decode escape sequences (only ASCII for now)
        auto str = std::get<std::string>(s.value());
        // +1 for null terminator
        size_t len = str.size() + 1;
        // String literals are static globals: their LLVM value is a pointer
        // to { i32, [N x i8] }, so the model type is const char[N]&.
        // Use const<char> as element type so it matches 'const char[]' parameter types.
        return from_type(primitive_type::CHAR)->get_const()->get_array(len)->get_reference();
    } else if (std::holds_alternative<lex::boolean>(literal)) {
        return from_type(primitive_type::BOOL);
    } else if (std::holds_alternative<lex::null>(literal)) {
        // null literal: return the dedicated null_type singleton.
        return _null_type;
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
    } else if (std::holds_alternative<lex::string>(literal)) {
        const auto &s = literal.get<lex::string>();
        // TODO Decode escape sequences (only raw ASCII content for now)
        auto str = std::get<std::string>(s.value());
        // Append null terminator
        str.push_back('\0');
        return get_or_create_string_literal(str);
    } else if (std::holds_alternative<lex::boolean>(literal)) {
        const auto& b = literal.get<lex::boolean>();
        if(std::get<bool>(b.value())) {
            return llvm::ConstantInt::getTrue(*_context);
        } else {
            return llvm::ConstantInt::getFalse(*_context);
        }
    } else if (std::holds_alternative<lex::null>(literal)) {
        // null literal: emit a null opaque pointer constant
        return llvm::ConstantPointerNull::get(llvm::PointerType::get(*_context, 0));
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
    } else if (std::holds_alternative<long>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(64, std::get<long>(value), true));
    } else if (std::holds_alternative<unsigned long>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(64, std::get<unsigned long>(value), false));
    } else if (std::holds_alternative<long long>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(64, std::get<long long>(value), true));
    } else if (std::holds_alternative<unsigned long long>(value)) {
        return llvm::ConstantInt::get(**this, llvm::APInt(64, std::get<unsigned long long>(value), false));
    } else if (std::holds_alternative<float>(value)) {
        return llvm::ConstantFP::get(llvm::Type::getFloatTy(**this), std::get<float>(value));
    } else if (std::holds_alternative<double>(value)) {
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(**this), std::get<double>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        // TODO Decode escape sequences (only raw ASCII content for now)
        auto str = std::get<std::string>(value);
        // Append null terminator
        str.push_back('\0');
        return get_or_create_string_literal(str);
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

llvm::Constant* context::get_or_create_string_literal(const std::string& content) {
    // content already includes the null terminator.
    // Deduplicate: return an existing global if we already emitted this string.
    auto it = _string_pool.find(content);
    if (it != _string_pool.end()) {
        return it->second;
    }

    auto& llvm_ctx = *_context;
    size_t N = content.size(); // includes null terminator

    // Build the K sized-array struct constant: { i32 size, [N x i8] data }
    auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
    auto* i8_ty  = llvm::Type::getInt8Ty(llvm_ctx);
    auto* arr_ty = llvm::ArrayType::get(i8_ty, N);
    auto* struct_ty = llvm::StructType::get(llvm_ctx, {i32_ty, arr_ty}, /*isPacked=*/false);

    // The size field stores the total number of bytes including the null terminator
    auto* size_const = llvm::ConstantInt::get(i32_ty, N, /*isSigned=*/false);
    // TODO Decode escape sequences — content is raw ASCII for now
    auto* data_const = llvm::ConstantDataArray::getString(llvm_ctx, llvm::StringRef(content.data(), N), /*AddNull=*/false);

    llvm::Constant* fields[] = {size_const, data_const};
    auto* init = llvm::ConstantStruct::get(struct_ty, fields);

    auto* gv = new llvm::GlobalVariable(
        *_module, struct_ty,
        /*isConstant=*/true,
        llvm::GlobalValue::PrivateLinkage,
        init, ".str");
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    _string_pool[content] = gv;
    return gv;
}


std::shared_ptr<unresolved_type> context::create_unresolved(const name& type_id) {
    std::shared_ptr<unresolved_type> res{new unresolved_type(type_id)};
    // Mark as template parameter placeholder if the name matches any active scope
    if (!_template_param_scopes.empty()) {
        auto id_str = type_id.to_string();
        for (auto it = _template_param_scopes.rbegin(); it != _template_param_scopes.rend(); ++it) {
            if (it->count(id_str)) {
                res->_is_template_param_placeholder = true;
                break;
            }
        }
    }
    _unresolved.push_back(res);
    return res;
}

std::shared_ptr<unresolved_type> context::create_unresolved(name&& type_id) {
    std::shared_ptr<unresolved_type> res{new unresolved_type(type_id)};
    if (!_template_param_scopes.empty()) {
        auto id_str = type_id.to_string();
        for (auto it = _template_param_scopes.rbegin(); it != _template_param_scopes.rend(); ++it) {
            if (it->count(id_str)) {
                res->_is_template_param_placeholder = true;
                break;
            }
        }
    }
    _unresolved.push_back(res);
    return res;
}

void context::push_template_param_scope(const std::unordered_set<std::string>& param_names) {
    _template_param_scopes.push_back(param_names);
}

void context::pop_template_param_scope() {
    if (!_template_param_scopes.empty()) {
        _template_param_scopes.pop_back();
    }
}

void context::resolve_struct_type(std::shared_ptr<struct_type> st_type,
                                   std::unordered_set<struct_type*>& in_progress) {
    auto throw_context_error = [](unsigned int code, const std::string& msg) -> void {
        throw context_resolution_error(k::log::diagnostic::make_error(code, msg));
    };

    if (st_type->is_resolved()) {
        auto* existing = llvm::dyn_cast_or_null<llvm::StructType>(st_type->get_llvm_type());
        if (existing && !existing->isOpaque() && st_type->fields_size() > 0) {
            return;
        }
    }

    // Skip struct_types whose owning aggregate lives inside a template definition.
    // Their member variable types may contain unresolved template parameters (e.g. T)
    // that will only be substituted during template instantiation.
    if (auto agg = st_type->get_struct()) {
        for (auto p = agg->parent<element>(); p; p = p->parent<element>()) {
            if (auto parent_agg = std::dynamic_pointer_cast<const aggregate>(p)) {
                if (parent_agg->is_template()) {
                    return;
                }
            }
        }
    }

    // Cycle detection
    if (in_progress.count(st_type.get())) {
        throw_context_error(
            static_cast<unsigned int>(k::diag::structure_diag::ERR_STRUCT_RECURSIVE_FORBIDDEN),
            "Cyclic dependency between struct types while resolving '" + st_type->name() +
            "'. Recursive fields are only supported through '*', '!' and '?'.");
    }
    in_progress.insert(st_type.get());
    struct in_progress_guard {
        std::unordered_set<struct_type*>& set;
        struct_type* st;
        ~in_progress_guard() { set.erase(st); }
    } guard{in_progress, st_type.get()};

    // Create (or reuse) a forward-declared LLVM struct type first, so legal
    // recursive fields (ptr/owner/view to this or in-progress structs) can map
    // to a concrete LLVM pointer type before the final body is known.
    auto* llvm_struct = llvm::dyn_cast_or_null<llvm::StructType>(st_type->get_llvm_type());
    if (!llvm_struct) {
        // Always create a fresh LLVM StructType — do NOT use getTypeByName() here.
        // Multiple model struct_types can share the same short name (e.g. a locally
        // defined 'Retention' and an imported 'k::annotations::Retention'). Using
        // getTypeByName would pick up the wrong one, leading to mismatched bodies.
        // LLVM automatically suffixes the name to keep it unique.
        llvm_struct = llvm::StructType::create(llvm_context(), st_type->name());
        st_type->set_llvm_type({}, llvm_struct, nullptr);
    }

    enum class recursive_field_policy {
        none,
        allowed,   // pointer/owner/view chain to in-progress struct
        forbidden  // by-value, reference, link, drain chain to in-progress struct
    };

    auto build_forbidden_cycle_message = [](const std::string& owner_name,
                                            std::string_view field_name,
                                            const std::shared_ptr<type>& field_type,
                                            const std::string& dep_name) {
        std::string message =
            "Forbidden recursive indirection in struct '" + owner_name + "', field '" + std::string(field_name) +
            "' (type '" + (field_type ? field_type->to_string() : std::string{"<null>"}) +
            "') targeting in-progress struct '" + dep_name +
            "'. Allowed recursive indirections are '*', '!' and '?' only.";
        return message;
    };

    auto classify_recursive_policy = [](const std::shared_ptr<type>& field_type,
                                        const struct_type* dep_struct) -> recursive_field_policy {
        auto walk = field_type;
        bool saw_allowed_indirection = false;
        bool saw_forbidden_indirection = false;

        while (walk) {
            if (type::is_const(walk) || type::is_array(walk)) {
                walk = walk->get_subtype();
                continue;
            }
            if (type::is_pointer(walk) || type::is_owner(walk) || type::is_view(walk)) {
                saw_allowed_indirection = true;
                walk = walk->get_subtype();
                continue;
            }
            if (type::is_reference(walk) || type::is_link(walk) || type::is_drain(walk)) {
                saw_forbidden_indirection = true;
                walk = walk->get_subtype();
                continue;
            }

            auto st = std::dynamic_pointer_cast<struct_type>(walk);
            if (!st || st.get() != dep_struct) {
                // Some unresolved wrappers can still carry the target struct id.
                auto unresolved = std::dynamic_pointer_cast<unresolved_type>(walk);
                if (!unresolved) {
                    return recursive_field_policy::none;
                }
                const auto& type_id = unresolved->type_id();
                if (type_id.empty() || type_id.back() != dep_struct->name()) {
                    return recursive_field_policy::none;
                }
            }
            if (saw_forbidden_indirection || !saw_allowed_indirection) {
                return recursive_field_policy::forbidden;
            }
            return recursive_field_policy::allowed;
        }

        return recursive_field_policy::none;
    };

    auto resolve_dep_struct = [&](const std::shared_ptr<struct_type>& dep_st,
                                  const std::shared_ptr<type>& field_type,
                                  std::string_view field_name) {
        if (!dep_st) return;
        if (in_progress.count(dep_st.get())) {
            auto policy = classify_recursive_policy(field_type, dep_st.get());
            if (policy == recursive_field_policy::allowed) {
                return;
            }
            throw_context_error(
                static_cast<unsigned int>(k::diag::structure_diag::ERR_STRUCT_RECURSIVE_FORBIDDEN),
                build_forbidden_cycle_message(st_type->name(), field_name, field_type, dep_st->name()));
        }
        resolve_struct_type(dep_st, in_progress);
    };

    auto st = st_type->get_struct();
    if (!st) {
        // struct_type with no owning aggregate (e.g. union types) — skip field iteration.
        // The LLVM body has already been set externally (or will be set later).
        return;
    }
    std::vector<struct_type::field> fields;
    std::vector<llvm::Type*> types;

    // Iterate over _children in insertion order (not _vars which is alphabetically sorted).
    // This ensures __vptr__ (injected at position 0) is first, followed by __base_X__ fields
    // (injected before regular members), and then user-declared members in declaration order.
    for (auto& child : st->get_children()) {
        auto var = std::dynamic_pointer_cast<member_variable_definition>(child);
        if (!var) continue;
        std::string var_name = var->get_short_name();
        auto type = var->get_type();

        // Special case: synthetic vptr fields (injected by symbol_resolver_process_class)
        // have no type yet — represent them as opaque pointers at LLVM level.
        if (!type) {
            llvm::Type* ptr_ty = llvm::PointerType::get(llvm_context(), 0);
            fields.emplace_back(struct_type::field{fields.size(), var_name, std::weak_ptr<k::model::type>{}});
            types.push_back(ptr_ty);
            continue;
        }

        // For pointer/reference/owner/link/view types whose subtype is a struct_type,
        // resolve the underlying struct first so get_llvm_type() on the wrapper succeeds.
        // This handles the __parent__ field (reference to outer struct) and owner fields.
        auto effective_type = type;
        if (type::is_pointer(type) || type::is_reference(type) || type::is_drain(type)
            || type::is_owner(type) || type::is_link(type) || type::is_view(type)) {
            if (type::contains_unresolved(type)) {
                // The pointer/reference chain contains stale unresolved_type
                // wrappers.  Re-resolve the entire type to get a clean chain.
                auto res_type = resolve_type(type);
                if (res_type) {
                    var->set_type(res_type);
                    effective_type = res_type;
                    // If the resolved type's leaf is a struct_type, resolve it.
                    auto walk = res_type;
                    while (walk) {
                        if (auto dep_st = std::dynamic_pointer_cast<struct_type>(walk)) {
                            resolve_dep_struct(dep_st, res_type, var_name);
                            break;
                        }
                        walk = walk->get_subtype();
                    }
                }
            } else {
                auto sub = type->get_subtype();
                if (sub) {
                    if (auto dep_st = std::dynamic_pointer_cast<struct_type>(sub)) {
                        resolve_dep_struct(dep_st, effective_type, var_name);
                    } else if (!sub->is_resolved()) {
                        // Try resolving the sub-type first
                        auto resolved_sub = resolve_type(sub);
                        if (resolved_sub) {
                            if (auto dep_st = std::dynamic_pointer_cast<struct_type>(resolved_sub)) {
                                resolve_dep_struct(dep_st, effective_type, var_name);
                            }
                        }
                    }
                }
                // The pointer/reference type is now implicitly resolved (subtype has LLVM type).
                effective_type = type;
            }
        } else if (!type->is_resolved() || type::contains_unresolved(type)) {
            // Not resolved, or contains stale unresolved_type wrappers that need
            // to be replaced with clean type chains.
            //
            // If the type is an unresolved template type (carries AST template args),
            // it should have been resolved by aggregate_type_resolver already.
            // If it hasn't, try resolve_type as a last resort; if still unresolved,
            // skip this struct — it will be resolved in a later pass.
            bool handled = false;
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(type)) {
                if (unres->has_template_args() && !unres->is_resolved()) {
                    // Try to resolve via the standard fallback
                    auto res_type = resolve_type(type);
                    if (res_type && type::is_resolved(res_type)) {
                        var->set_type(res_type);
                        effective_type = res_type;
                        // Walk through to find inner struct_type to resolve
                        auto walk = res_type;
                        while (walk) {
                            if (auto dep_st_type = std::dynamic_pointer_cast<struct_type>(walk)) {
                                resolve_dep_struct(dep_st_type, res_type, var_name);
                                break;
                            }
                            walk = walk->get_subtype();
                        }
                        handled = true;
                    } else {
                        // Still unresolved — bail out
                        return; // Will be resolved in a later pass
                    }
                }
            }
            if (!handled) {
            auto res_type = resolve_type(type);
            if (!res_type) {
                // Type not yet resolvable — defer to a later pass (e.g.
                // nested union types are only fully registered after
                // aggregate_type_resolver runs).
                return;
            }
            // If the resolved type is itself a struct_type not yet fully resolved,
            // recursively resolve it now (handles any declaration order).
            // Walk through wrapper types (const, view, array, pointer, etc.)
            // to find any inner struct_type that needs resolving.
            {
                auto walk = res_type;
                while (walk) {
                    if (auto dep_st_type = std::dynamic_pointer_cast<struct_type>(walk)) {
                        resolve_dep_struct(dep_st_type, res_type, var_name);
                        break;
                    }
                    walk = walk->get_subtype();
                }
            }
            var->set_type(res_type);
            effective_type = res_type;
            }
        } else if (auto dep_st_type = std::dynamic_pointer_cast<struct_type>(type)) {
            // Already a struct_type but may not have its LLVM type yet (e.g. forward reference).
            resolve_dep_struct(dep_st_type, type, var_name);
        }
        fields.emplace_back(fields.size(), var_name, effective_type);
        types.push_back(get_llvm_type(effective_type));
    }

    if (llvm_struct->isOpaque()) {
        llvm_struct->setBody(llvm::ArrayRef<llvm::Type*>(types));
    }
    auto default_const_value = llvm::ConstantAggregateZero::get(llvm_struct);
    st_type->set_llvm_type(std::move(fields), llvm_struct, default_const_value);

}

void context::resolve_types() {
    // Note: primitive types (and derivative) are always resolved.
    // Note: references, pointers and arrays depend on only from their subtypes.

    // Resolve structures in dependency order (recursive, handles any declaration order).
    std::unordered_set<struct_type*> in_progress;
    for (auto& [st_name, st_type] : _struct_types) {
        // Skip struct_types whose owning aggregate lives inside a template definition.
        if (auto agg = st_type->get_struct()) {
            bool inside_template = false;
            for (auto p = agg->parent<element>(); p; p = p->parent<element>()) {
                if (auto parent_agg = std::dynamic_pointer_cast<const aggregate>(p)) {
                    if (parent_agg->is_template()) {
                        inside_template = true;
                        break;
                    }
                }
            }
            if (inside_template) continue;
        }
        resolve_struct_type(st_type, in_progress);
    }
}

void context::init_module(const std::string& module_name) {
    _module = std::make_unique<llvm::Module>(module_name, *_context);
}

// ---------------------------------------------------------------------------
// intern_llvm_struct_from_def
// ---------------------------------------------------------------------------
// Parse a snippet like  '%Counter = type { ptr, %ICounter, i32 }'
// using the current LLVMContext.  Return the StructType* identified by the
// given type name, or nullptr on failure.
//
// Implementation: wrap the snippet in a minimal IR module text, parse it,
// then import the named StructType into _module (if not already present).
llvm::StructType*
context::intern_llvm_struct_from_def(const std::string& llvm_def,
                                     const std::string& type_name)
{
    if (llvm_def.empty() || type_name.empty()) return nullptr;

    // If already interned in this LLVMContext and NOT opaque, return directly.
    // If opaque (a forward-reference created when another type referenced this one
    // before it was fully defined), fall through to parse the body.
    if (auto* existing = llvm::StructType::getTypeByName(*_context, type_name)) {
        if (!existing->isOpaque()) return existing;
        // Fall through: need to set the body of this opaque type.
    }

    // Parse the snippet via a minimal IR module sharing *_context so that
    // all named struct types are interned into the same context.
    std::string ir = "; KDI import\n" + llvm_def + "\n";
    llvm::SMDiagnostic diag;
    auto buf = llvm::MemoryBuffer::getMemBuffer(ir, "<kdi-struct-def>");
    auto tmp = llvm::parseIR(buf->getMemBufferRef(), diag, *_context);
    if (!tmp) {
        // Parsing failed — create an opaque placeholder (or return existing opaque).
        auto* existing = llvm::StructType::getTypeByName(*_context, type_name);
        return existing ? existing : llvm::StructType::create(*_context, type_name);
    }

    // The named StructType is now interned in *_context (shared with _module).
    return llvm::StructType::getTypeByName(*_context, type_name);
}

// ---------------------------------------------------------------------------
// intern_all_llvm_struct_defs
// ---------------------------------------------------------------------------
// Parse all type definitions contained in @p combined_ir in a single LLVM IR
// module so that forward-references between types (e.g. base/derived pairs)
// are resolved immediately.  All named StructTypes are therefore available in
// *_context after this call with their full bodies — no opaque placeholders
// will be left behind.
//
// This is the preferred way to import struct types from KDI files.
// intern_llvm_struct_from_def() can still be used afterwards for individual
// types that are not yet known (it will find them already interned if they
// appear in the combined blob).
void context::intern_all_llvm_struct_defs(const std::string& combined_ir) {
    if (combined_ir.empty()) return;

    std::string ir = "; KDI combined import\n" + combined_ir + "\n";
    llvm::SMDiagnostic diag;
    auto buf = llvm::MemoryBuffer::getMemBuffer(ir, "<kdi-combined-defs>");
    // parseIR uses *_context, so all named StructTypes are interned into it.
    auto tmp = llvm::parseIR(buf->getMemBufferRef(), diag, *_context);
    // We do not need the temporary module — the types are now in *_context.
    // Silently ignore parse failures: individual types will fall back to opaque
    // via intern_llvm_struct_from_def() if needed.
}

void context::attach_llvm_struct_type(std::shared_ptr<struct_type> st_type,
                                      llvm::StructType* llvm_st)
{
    if (!st_type || !llvm_st) return;
    auto* default_const = llvm::ConstantAggregateZero::get(llvm_st);
    std::vector<struct_type::field> no_fields;
    st_type->set_llvm_type(std::move(no_fields), llvm_st, default_const);
}

void context::attach_llvm_struct_type(std::shared_ptr<struct_type> st_type,
                                      llvm::StructType* llvm_st,
                                      std::vector<struct_type::field> named_fields)
{
    if (!st_type || !llvm_st) return;
    auto* default_const = llvm::ConstantAggregateZero::get(llvm_st);
    st_type->set_llvm_type(std::move(named_fields), llvm_st, default_const);
}

llvm::Function*
context::declare_llvm_function_from_def(const std::string& llvm_def,
                                        const std::string& mangled_name)
{
    if (llvm_def.empty() || mangled_name.empty()) return nullptr;
    if (!_module) return nullptr;

    // If already declared in this module, return it.
    if (auto* existing = _module->getFunction(mangled_name))
        return existing;

    // Parse the snippet in a temporary module sharing *_context so that
    // struct types referenced in the prototype are resolved.
    std::string ir = "; KDI import\n" + llvm_def + "\n";
    llvm::SMDiagnostic diag;
    auto buf = llvm::MemoryBuffer::getMemBuffer(ir, "<kdi-fn-def>");
    auto tmp = llvm::parseIR(buf->getMemBufferRef(), diag, *_context);
    if (!tmp) return nullptr;

    auto* tmpFn = tmp->getFunction(mangled_name);
    if (!tmpFn) return nullptr;

    // Create/get the function in *_module with the same type and ExternalLinkage.
    auto* fn = llvm::Function::Create(
        tmpFn->getFunctionType(),
        llvm::GlobalValue::ExternalLinkage,
        mangled_name,
        *_module);
    return fn;
}

std::shared_ptr<type> context::resolve_type(const std::shared_ptr<type>& type) {
    if (type->is_resolved() && !type::contains_unresolved(type)) {
        return type;
    }
    // A resolved unresolved_type: return the target directly.
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(type)) {
        auto res = unres->get_resolved();
        if (res) return res;
    }
    if (type::is_const(type)) {
        // const_type wrapping an unresolved inner type (e.g. const(unresolved(Point)))
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            return res->get_const();
        }
    } else if (type::is_pointer(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            // Unsized arrays are canonicalised to ref<array<T>> by the array branch
            // of resolve_type, but inside a pointer we want pointer(array(T)), not
            // pointer(ref<array<T>>).  Unwrap the spurious reference layer.
            if (auto ref = std::dynamic_pointer_cast<reference_type>(res)) {
                if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                    if (!arr->is_sized()) {
                        res = arr;
                    }
                }
            }
            return res->get_pointer();
        }
    } else if (type::is_reference(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            // Unsized arrays are canonicalised to ref<array<T>> by the array branch
            // of resolve_type.  When the outer type is already a reference, the
            // canonicalisation has already provided the reference we need — return as-is
            // to avoid a spurious double-reference.
            if (auto ref = std::dynamic_pointer_cast<reference_type>(res)) {
                if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                    if (!arr->is_sized()) {
                        return res;
                    }
                }
            }
            return res->get_reference();
        }
    } else if (type::is_link(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            // Unsized arrays are canonicalised to ref<array<T>> by the array branch
            // of resolve_type, but inside a link we want link(array(T)), not
            // link(ref<array<T>>).  Unwrap the spurious reference layer.
            if (auto ref = std::dynamic_pointer_cast<reference_type>(res)) {
                if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                    if (!arr->is_sized()) {
                        res = arr;
                    }
                }
            }
            return res->get_link();
        }
    } else if (type::is_view(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            // Unsized arrays are canonicalised to ref<array<T>> by the array branch
            // of resolve_type, but inside a view we want view(array(T)), not
            // view(ref<array<T>>).  Unwrap the spurious reference layer.
            if (auto ref = std::dynamic_pointer_cast<reference_type>(res)) {
                if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                    if (!arr->is_sized()) {
                        res = arr;
                    }
                }
            }
            return res->get_view();
        }
    } else if (type::is_owner(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            // Unsized arrays are canonicalised to ref<array<T>> by the array branch
            // of resolve_type, but inside an owner we want owner(array(T)), not
            // owner(ref<array<T>>).  Unwrap the spurious reference layer.
            if (auto ref = std::dynamic_pointer_cast<reference_type>(res)) {
                if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                    if (!arr->is_sized()) {
                        res = arr;
                    }
                }
            }
            return res->get_owner();
        }
    } else if (type::is_drain(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
            return nullptr;
        } else {
            return res->get_drain();
        }
    } else if (type::is_array(type)) {
        auto res = resolve_type(type->get_subtype());
        if (!res) {
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
            // Template parameter placeholders (e.g. "T" inside a template definition)
            // are expected to remain unresolved until instantiation — no diagnostic.
            if (unres->is_template_param_placeholder()) {
                return nullptr;
            }
            // Types with AST template args (e.g. "Box<int>") are handled by the
            // resolver's template instantiation path — no diagnostic here.
            if (unres->has_template_args()) {
                return nullptr;
            }
            auto resolved_type = from_string(unres->type_id().to_string());
            // If from_string returned a struct_type, accept it even if its LLVM type is not
            // yet generated: resolve_struct_type will handle the LLVM materialisation
            // recursively. An unresolved_type remaining means the name was truly unknown.
            if (std::dynamic_pointer_cast<struct_type>(resolved_type)) {
                unres->resolve(resolved_type);
                return resolved_type;
            }
            // Similarly, if it resolved to an enum_type, accept it.
            if (std::dynamic_pointer_cast<enum_type>(resolved_type)) {
                unres->resolve(resolved_type);
                return resolved_type;
            }
            if (!resolved_type->is_resolved()) {
                // The type name is truly unknown — return nullptr.
                // Proper error diagnostics are emitted by the resolver passes.
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
