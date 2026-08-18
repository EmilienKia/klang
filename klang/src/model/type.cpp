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

#include "type.hpp"
#include "model.hpp"
#include "context.hpp"
#include "../common/tools.hpp"
#include "../parse/ast.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>

namespace k::model {

//
// Base type
//

type::type(std::shared_ptr<type> subtype, llvm::Type* llvm_type):
        subtype(subtype),
        _llvm_type(llvm_type)
{}

std::shared_ptr<type> type::get_subtype() const
{
    return subtype.lock();
}

bool type::is_primitive() const
{
    return subtype.use_count()!=0;
}

bool type::is_resolved() const
{
    return false;
}

bool type::contains_unresolved(const std::shared_ptr<type>& t) {
    if (!t) return false;
    if (std::dynamic_pointer_cast<unresolved_type>(t)) return true;
    return contains_unresolved(t->get_subtype());
}

std::shared_ptr<type> type::canonical(const std::shared_ptr<type>& t) {
    if (!t) return t;

    // Strip alias layers at this level (an alias may rename another alias).
    // The chain is bounded by the cycle detection performed at resolution time,
    // but a depth guard keeps a malformed model from looping here.
    std::shared_ptr<type> current = t;
    for (unsigned int depth = 0; depth < 64; ++depth) {
        auto alias = std::dynamic_pointer_cast<alias_type>(current);
        if (!alias) break;
        auto underlying = alias->get_underlying();
        if (!underlying) return current;
        current = underlying;
    }

    // A resolved unresolved_type may itself stand for an alias.
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(current)) {
        if (unres->is_resolved()) {
            auto resolved = canonical(unres->get_resolved());
            if (resolved != unres->get_resolved()) return resolved;
        }
        return current;
    }

    // Descend through indirection wrappers: an alias may be nested inside one
    // (e.g. 'identifier*'), and the wrapper must then be rebuilt over the
    // canonicalised inner type.
    auto sub = current->get_subtype();
    if (!sub) return current;

    auto canon_sub = canonical(sub);
    if (canon_sub == sub) return current;

    if (auto sized = std::dynamic_pointer_cast<sized_array_type>(current)) {
        return canon_sub->get_array(sized->get_size());
    }
    if (auto wrapper = make_pinned_wrapper(current, canon_sub)) {
        return wrapper;
    }
    return current;
}

std::shared_ptr<reference_type> type::get_reference()
{
    if(!reference) {
        reference = std::shared_ptr<reference_type>(new reference_type(shared_from_this()));
    }
    return reference;
}

std::shared_ptr<pointer_type> type::get_pointer()
{
    if(!pointer) {
        pointer = std::shared_ptr<pointer_type>(new pointer_type(shared_from_this()));
    }
    return pointer;
}

std::shared_ptr<link_type> type::get_link()
{
    if(!link) {
        link = std::shared_ptr<link_type>(new link_type(shared_from_this()));
    }
    return link;
}

std::shared_ptr<view_type> type::get_view()
{
    if(!view) {
        view = std::shared_ptr<view_type>(new view_type(shared_from_this()));
    }
    return view;
}

std::shared_ptr<owner_type> type::get_owner()
{
    if(!owner) {
        owner = std::shared_ptr<owner_type>(new owner_type(shared_from_this()));
    }
    return owner;
}

std::shared_ptr<drain_type> type::get_drain()
{
    if(!drain) {
        drain = std::shared_ptr<drain_type>(new drain_type(shared_from_this()));
    }
    return drain;
}

std::shared_ptr<const_type> type::get_const()
{
    if(!const_) {
        const_ = std::shared_ptr<const_type>(new const_type(shared_from_this()));
    }
    return const_;
}

std::shared_ptr<array_type> type::get_array()
{
    if(!array) {
        array = std::shared_ptr<array_type>(new array_type(shared_from_this()));
    }
    return array;
}

std::shared_ptr<sized_array_type> type::get_array(unsigned long size)
{
    return get_array()->with_size(size);
}

std::shared_ptr<const_type> type::make_pinned_const(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<const_type>(new const_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<reference_type> type::make_pinned_reference(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<reference_type>(new reference_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<pointer_type> type::make_pinned_pointer(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<pointer_type>(new pointer_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<link_type> type::make_pinned_link(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<link_type>(new link_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<view_type> type::make_pinned_view(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<view_type>(new view_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<owner_type> type::make_pinned_owner(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<owner_type>(new owner_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<drain_type> type::make_pinned_drain(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<drain_type>(new drain_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<array_type> type::make_pinned_array(const std::shared_ptr<type>& inner) {
    if (!inner) return nullptr;
    auto wrapper = std::shared_ptr<array_type>(new array_type(inner));
    wrapper->_pinned_subtype = inner;
    return wrapper;
}

std::shared_ptr<type> type::make_pinned_wrapper(
    const std::shared_ptr<type>& kind_of,
    const std::shared_ptr<type>& inner)
{
    if (!kind_of || !inner) {
        return nullptr;
    }

    if (is_reference(kind_of)) {
        return make_pinned_reference(inner);
    } else if (is_pointer(kind_of)) {
        return make_pinned_pointer(inner);
    } else if (is_link(kind_of)) {
        return make_pinned_link(inner);
    } else if (is_view(kind_of)) {
        return make_pinned_view(inner);
    } else if (is_owner(kind_of)) {
        return make_pinned_owner(inner);
    } else if (is_drain(kind_of)) {
        return make_pinned_drain(inner);
    } else if (is_const(kind_of)) {
        return make_pinned_const(inner);
    } else if (is_array(kind_of) && !is_sized_array(kind_of)) {
        return make_pinned_array(inner);
    } else {
        return nullptr;
    }
}

llvm::Type* type::get_llvm_type() const {
    return _llvm_type;
};

llvm::Constant* type::generate_default_value_initializer() const {
    return nullptr;
}


//
// Unresolved type
//

std::string unresolved_type::to_string() const {
    return "<<unresolved:" + _type_id.to_string() + ">>";
}

std::shared_ptr<type> unresolved_type::substitute_ast_type_spec(
    const k::parse::ast::type_specifier* spec,
    const std::unordered_map<std::string, std::shared_ptr<type>>& subst)
{
    if (!spec) return nullptr;

    if (auto ct = dynamic_cast<const k::parse::ast::const_type_specifier*>(spec)) {
        auto sub = substitute_ast_type_spec(ct->subtype.get(), subst);
        return sub ? type::make_pinned_const(sub) : nullptr;
    }
    if (auto ptr = dynamic_cast<const k::parse::ast::pointer_type_specifier*>(spec)) {
        auto sub = substitute_ast_type_spec(ptr->subtype.get(), subst);
        if (!sub) return nullptr;
        if (ptr->pointer_type == k::lex::operator_::AMPERSAND) return type::make_pinned_reference(sub);
        if (ptr->pointer_type == k::lex::operator_::STAR) return type::make_pinned_pointer(sub);
        if (ptr->pointer_type == k::lex::operator_::PLUS) return type::make_pinned_link(sub);
        if (ptr->pointer_type == k::lex::operator_::QUESTION_MARK) return type::make_pinned_view(sub);
        if (ptr->pointer_type == k::lex::operator_::HASH) return type::make_pinned_drain(sub);
        return nullptr;
    }
    if (auto own = dynamic_cast<const k::parse::ast::owner_type_specifier*>(spec)) {
        auto sub = substitute_ast_type_spec(own->subtype.get(), subst);
        return sub ? type::make_pinned_owner(sub) : nullptr;
    }
    if (auto arr = dynamic_cast<const k::parse::ast::array_type_specifier*>(spec)) {
        auto sub = substitute_ast_type_spec(arr->subtype.get(), subst);
        if (!sub) return nullptr;
        if (arr->lex_int) return sub->get_array(arr->lex_int->to_unsigned_int());
        return type::make_pinned_array(sub);
    }
    if (auto id_spec = dynamic_cast<const k::parse::ast::identified_type_specifier*>(spec)) {
        // A nested template reference (e.g. Box<T> in Pair<Box<T>, int>) is
        // substituted recursively, so that the enclosing substitution reaches
        // every parameter occurrence even where no instantiation scope makes
        // the parameter names visible (parameterised aliases).
        if (id_spec->has_explicit_template_args) {
            auto nested = std::shared_ptr<unresolved_type>(
                new unresolved_type(id_spec->name.to_name()));
            nested->_ast_template_args = id_spec->template_args;
            nested->_has_explicit_template_args = true;
            if (auto cloned = nested->clone_with_substituted_model_args(subst)) {
                return cloned;
            }
            return nested;
        }
        if (id_spec->name.size() == 1) {
            std::string arg_name{id_spec->name.names[0].content};
            auto sit = subst.find(arg_name);
            if (sit != subst.end() && sit->second) {
                return sit->second;
            }
        }
        return std::shared_ptr<unresolved_type>(new unresolved_type(id_spec->name.to_name()));
    }
    return nullptr;
}

std::shared_ptr<unresolved_type> unresolved_type::clone_with_substituted_model_args(
    const std::unordered_map<std::string, std::shared_ptr<type>>& subst) const
{
    if (_ast_template_args.empty() || subst.empty()) return nullptr;

    std::vector<std::shared_ptr<type>> model_args;
    model_args.reserve(_ast_template_args.size());
    bool any_substituted = false;

    for (const auto& ast_arg : _ast_template_args) {
        if (!ast_arg || !ast_arg->is_type() || !ast_arg->type_arg) {
            model_args.push_back(nullptr);
            continue;
        }
        auto sub = substitute_ast_type_spec(ast_arg->type_arg.get(), subst);
        if (sub) {
            model_args.push_back(sub);
            any_substituted = true;
        } else {
            model_args.push_back(nullptr);
        }
    }

    if (!any_substituted) return nullptr;

    // Create a new unresolved_type sharing the AST template args but with model overrides.
    auto new_ut = std::shared_ptr<unresolved_type>(new unresolved_type(_type_id));
    new_ut->_ast_template_args = _ast_template_args;
    new_ut->_has_explicit_template_args = _has_explicit_template_args;
    new_ut->_model_template_args = std::move(model_args);
    return new_ut;
}

//
// Null literal type
//

llvm::Type* null_type::get_llvm_type() const {
    // Opaque pointer — same representation as all other indirections.
    // _llvm_type is not cached because null_type has no subtype to anchor
    // a context; we return a fresh PointerType each time (LLVM deduplicates).
    // Note: this requires an LLVMContext, so callers must ensure the context is available.
    return nullptr; // will be provided by ConstantPointerNull at codegen time
}

llvm::Constant* null_type::generate_default_value_initializer() const {
    return nullptr;
}

//
// Unresolved function reference type
//

std::shared_ptr<unresolved_callable_type> unresolved_callable_type::make_shared(
    const k::name& owner_name,
    callable_type::addresser addr,
    const std::vector<std::shared_ptr<type>>& param_types,
    const std::shared_ptr<type>& return_type,
    const std::vector<std::shared_ptr<type>>& throws)
{
    return std::shared_ptr<unresolved_callable_type>(
        new unresolved_callable_type(owner_name, addr, param_types, return_type, throws));
}

std::string unresolved_callable_type::to_string() const {
    std::ostringstream stm;
    stm << "<<unresolved_fn_ref:";
    if (!_owner_name.empty()) stm << _owner_name.to_string() << "::";
    switch (_addresser) {
        case callable_type::addresser::none:      stm << "("; break;
        case callable_type::addresser::pointer:   stm << "*("; break;
        case callable_type::addresser::view:      stm << "?("; break;
        case callable_type::addresser::link:      stm << "+("; break;
        case callable_type::addresser::reference: stm << "&("; break;
    }
    for (size_t i = 0; i < _parameter_types.size(); ++i) {
        if (i > 0) stm << ", ";
        stm << _parameter_types[i]->to_string();
    }
    stm << ")>>";
    return stm.str();
}

//
// Primitive type
//

std::shared_ptr<primitive_type> primitive_type::make_shared(primitive_type::PRIMITIVE_TYPE type, bool is_unsigned, bool is_float, size_t size, llvm::Type* llvm_type) {
    return std::shared_ptr<primitive_type>(new primitive_type(type, is_unsigned, is_float, size, llvm_type));
}

bool primitive_type::is_resolved() const
{
    return true;
}

bool primitive_type::is_primitive() const
{
    return true;
}

std::string primitive_type::to_string()const {
    static std::map<primitive_type::PRIMITIVE_TYPE, std::string> type_names {
            {BOOL, "bool"},
            {BYTE, "byte"},
            {UNSIGNED_BYTE, "unsigned byte"},
            {CHAR,"char"},
            {SHORT, "short"},
            {UNSIGNED_SHORT, "unsigned short"},
            {INT, "int"},
            {UNSIGNED_INT, "unsigned int"},
            {LONG, "long"},
            {UNSIGNED_LONG, "unsigned long"},
            {LONG_LONG, "long long"},
            {UNSIGNED_LONG_LONG, "unsigned long long"},
            {FLOAT, "float"},
            {DOUBLE, "double"}
    };
    return type_names[_type];
}

llvm::Constant* primitive_type::generate_default_value_initializer() const {
    if (is_integer()) {
        return llvm::ConstantInt::get(get_llvm_type(), 0);
    } else if (is_float()) {
        return llvm::ConstantFP::get(get_llvm_type(), 0.0);
    } else if (is_boolean()) {
        return llvm::ConstantInt::getFalse(get_llvm_type());
    } // TODO handle other primitive types
    return nullptr;
}

//
// Pointer type
//
pointer_type::pointer_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool pointer_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

llvm::Type* pointer_type::get_llvm_type() const {
    auto sub = subtype.lock();
    if(_llvm_type==nullptr && sub && is_resolved()) {
        auto sub_llvm = sub->get_llvm_type();
        if (sub_llvm) {
            _llvm_type = llvm::PointerType::get(sub_llvm, 0 /*llvm::ADDRESS_SPACE_GENERIC*/);
        }
    }
    return _llvm_type;
}

std::string pointer_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "*";
    } else {
        return "<<nosub>>*";
    }
}


//
// Reference type
//
reference_type::reference_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool reference_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

/*
std::shared_ptr<reference_type> reference_type::get_reference() {
    return std::dynamic_pointer_cast<reference_type>(shared_from_this());
}
*/

llvm::Type* reference_type::get_llvm_type() const {
    auto sub = subtype.lock();
    if(_llvm_type==nullptr && sub && is_resolved()) {
        auto llvm_subtype = sub->get_llvm_type();
        if (!llvm_subtype) return nullptr;
        _llvm_type = llvm::PointerType::get(llvm_subtype, 0 /*llvm::ADDRESS_SPACE_GENERIC*/);
    }
    return _llvm_type;
}

std::string reference_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "&";
    } else {
        return "<<nosub>>&";
    }
}

//
// Link type (+)
//
link_type::link_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool link_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

llvm::Type* link_type::get_llvm_type() const {
    auto sub = subtype.lock();
    if(_llvm_type==nullptr && sub && is_resolved()) {
        auto sub_llvm = sub->get_llvm_type();
        if (sub_llvm) {
            _llvm_type = llvm::PointerType::get(sub_llvm, 0);
        }
    }
    return _llvm_type;
}

std::string link_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "+";
    } else {
        return "<<nosub>>+";
    }
}

//
// View type (?)
//
view_type::view_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool view_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

llvm::Type* view_type::get_llvm_type() const {
    auto sub = subtype.lock();
    if(_llvm_type==nullptr && sub && is_resolved()) {
        auto* sub_llvm = sub->get_llvm_type();
        if (!sub_llvm) return nullptr;
        _llvm_type = llvm::PointerType::get(sub_llvm, 0);
    }
    return _llvm_type;
}

std::string view_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "?";
    } else {
        return "<<nosub>>?";
    }
}

//
// Owner type (!) — owning pointer, unique ownership
//
owner_type::owner_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool owner_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

llvm::Type* owner_type::get_llvm_type() const {
    auto sub = subtype.lock();
    if(_llvm_type==nullptr && sub && is_resolved()) {
        auto sub_llvm = sub->get_llvm_type();
        if (sub_llvm) {
            _llvm_type = llvm::PointerType::get(sub_llvm, 0);
        }
    }
    return _llvm_type;
}

std::string owner_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "!";
    } else {
        return "<<nosub>>!";
    }
}

//
// Drain type (#) — drainable indirection (immutable binding, non-null)
//
drain_type::drain_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool drain_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

llvm::Type* drain_type::get_llvm_type() const {
    auto sub = subtype.lock();
    if(_llvm_type==nullptr && sub && is_resolved()) {
        auto sub_llvm = sub->get_llvm_type();
        if (sub_llvm) {
            _llvm_type = llvm::PointerType::get(sub_llvm, 0);
        }
    }
    return _llvm_type;
}

std::string drain_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "#";
    } else {
        return "<<nosub>>#";
    }
}

//
// Const type (const qualifier — compile-time only, no IR impact)
//
const_type::const_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool const_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

llvm::Type* const_type::get_llvm_type() const {
    // const is a compile-time qualifier only; delegate to the inner type.
    auto sub = subtype.lock();
    return sub ? sub->get_llvm_type() : nullptr;
}

std::string const_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return "const " + sub->to_string();
    } else {
        return "const <<nosub>>";
    }
}

//
// Array type
//

array_type::array_type(std::shared_ptr<type> subtype) :
    type(subtype)
{}

bool array_type::is_resolved() const
{
    auto sub = subtype.lock();
    return sub ? sub->is_resolved() : false;
}

bool array_type::is_sized() const {
    return false;
}

std::shared_ptr<sized_array_type> array_type::with_size(unsigned long size) {
    return tools::compute_if_absent(_sized_types, size,
                    [&](unsigned long sz){return std::shared_ptr<sized_array_type>{new sized_array_type(std::weak_ptr<array_type>(std::dynamic_pointer_cast<array_type>(this->shared_from_this())), sz)};}
            )->second;
}

llvm::Type* array_type::get_llvm_type() const {
    // Unsized array: { i32, [0 x T] } — trailing-array struct.
    // The [0 x T] is a zero-length array, used for GEP-based element access
    // when the actual size is only known at runtime.
    if (_llvm_type == nullptr) {
        auto* elem_llvm = subtype.lock()->get_llvm_type();
        if (!elem_llvm) return nullptr;
        auto& ctx = elem_llvm->getContext();
        auto* i32_t  = llvm::Type::getInt32Ty(ctx);
        auto* data_t = llvm::ArrayType::get(elem_llvm, 0);
        _llvm_type = llvm::StructType::get(ctx, {i32_t, data_t}, /*isPacked=*/false);
    }
    return _llvm_type;
}

llvm::StructType* array_type::get_llvm_struct_type() const {
    return llvm::dyn_cast_or_null<llvm::StructType>(get_llvm_type());
}

llvm::ArrayType* array_type::get_llvm_data_array_type() const {
    auto* st = get_llvm_struct_type();
    if (!st) return nullptr;
    return llvm::dyn_cast_or_null<llvm::ArrayType>(st->getElementType(FIELD_DATA));
}

std::string array_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "[]";
    } else {
        return "<<nosub>>[]";
    }
}

//
// Sized array type
//

sized_array_type::sized_array_type(std::weak_ptr<array_type> unsized_array_type, unsigned long size) :
    array_type(unsized_array_type.lock()->get_subtype()),
    _unsized_array_type(unsized_array_type),
    size(size)
{}

unsigned long sized_array_type::get_size() const {
    return size;
}

bool sized_array_type::is_sized() const {
    return true;
}

std::shared_ptr<array_type> sized_array_type::get_unsized() const {
    return _unsized_array_type.lock();
}

std::shared_ptr<sized_array_type> sized_array_type::with_size(unsigned long size) {
    return _unsized_array_type.lock()->with_size(size);
}

llvm::Type* sized_array_type::get_llvm_type() const {
    if (_llvm_type == nullptr) {
        auto* elem_llvm = subtype.lock()->get_llvm_type();
        if (!elem_llvm) return nullptr;
        auto& ctx = elem_llvm->getContext();
        // Field 0: i32  (element count / capacity)
        // Field 1: [N x T]  (element data)
        auto* i32_t    = llvm::Type::getInt32Ty(ctx);
        auto* data_t   = llvm::ArrayType::get(elem_llvm, size);
        _llvm_type = llvm::StructType::get(ctx, {i32_t, data_t}, /*isPacked=*/false);
    }
    return _llvm_type;
}

llvm::StructType* sized_array_type::get_llvm_struct_type() const {
    return llvm::dyn_cast_or_null<llvm::StructType>(get_llvm_type());
}

llvm::ArrayType* sized_array_type::get_llvm_data_array_type() const {
    auto* st = get_llvm_struct_type();
    if (!st) return nullptr;
    return llvm::dyn_cast_or_null<llvm::ArrayType>(st->getElementType(FIELD_DATA));
}

llvm::Constant* sized_array_type::generate_default_value_initializer() const {
    auto* st = get_llvm_struct_type();
    if (!st) return nullptr;
    auto& ctx = st->getContext();
    // { i32 N, zeroinitializer }
    auto* size_val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx),
                                            static_cast<uint64_t>(size), /*isSigned=*/false);
    auto* data_zero = llvm::ConstantAggregateZero::get(st->getElementType(FIELD_DATA));
    return llvm::ConstantStruct::get(st, {size_val, data_zero});
}

std::string sized_array_type::to_string() const {
    auto sub = subtype.lock();
    std::ostringstream stm;
    if(sub) {
        stm << sub->to_string();
    } else {
        stm << "<<nosub>>";
    }
    stm << '[' << size << ']';
    return stm.str();
}

//
// Structure type builder
//
struct_type_builder::struct_type_builder(std::shared_ptr<context> context) :
    _context(context)
{
}

void struct_type_builder::append_field(const std::string& name, std::shared_ptr<type> type) {
    _fields.push_back(struct_type::field{
        .index = _fields.size(),
        .name = name,
        .field_type = type
        });
}

std::shared_ptr<struct_type> struct_type_builder::build() {
    std::vector<llvm::Type*> types;
    for(size_t idx=0; idx<_fields.size(); ++idx) {
        auto& field = _fields[idx];
        auto type = field.field_type.lock();
        types.push_back(_context->get_llvm_type(type));
    }

    llvm::StructType* st = llvm::StructType::create(**_context, llvm::ArrayRef<llvm::Type*>(types), _name);
    std::shared_ptr<struct_type> st_type{new struct_type(_name, _struct, std::move(_fields), st)}; 
    _context->add_struct(st_type);

    return st_type;
}


//
// Structure type
//

struct_type::struct_type(const std::string& name, std::weak_ptr<k::model::aggregate> st):
type(nullptr),
_name(name),
_struct(st)
{
}


struct_type::struct_type(const std::string& name, std::weak_ptr<aggregate> st, std::vector<field>&& fields, llvm::StructType* llvm_struct_type):
type(llvm_struct_type),
_name(name),
_fields(fields),
_struct(st)
{
}

bool struct_type::is_resolved() const
{
    /*
    for(const auto& field : _fields) {
        auto field_type = field.field_type.lock();
        if(!field_type->is_resolved()) {
            return false;
        }
    }
    return true;
*/
    return get_llvm_type() != nullptr;
}

std::string struct_type::to_string() const {
    return "struct:" + _name;
}

std::shared_ptr<aggregate> struct_type::get_struct() const {
    return _struct.lock();
}

void struct_type::set_llvm_type(std::vector<field>&& fields, llvm::StructType* llvm_struct_type, llvm::Constant* default_init_constant) {
    _fields = fields;
    _llvm_type = llvm_struct_type;
    _default_init_constant = default_init_constant;
}


bool struct_type::has_member(const std::string& name) const {
    for(const auto& field : _fields) {
        if(field.name == name) {
            return true;
        }
    }
    return false;
}

std::optional<struct_type::field> struct_type::get_member(const std::string& name) const {
    for(const auto& field : _fields) {
        if(field.name == name) {
            return field;
        }
    }
    return std::nullopt;
}

llvm::Constant* struct_type::generate_default_value_initializer() const {
    return _default_init_constant!=nullptr ? _default_init_constant : llvm::ConstantAggregateZero::get(get_llvm_type());
}


//
// Function reference type
//
bool callable_type::is_resolved() const {
    if (_return_type && !_return_type->is_resolved()) return false;
    for (const auto& p : _parameter_types) {
        if (p && !p->is_resolved()) return false;
    }
    for (const auto& t : _throws) {
        if (t && !t->is_resolved()) return false;
    }
    return true;
}

llvm::Type* callable_type::get_llvm_type() const {
    // A fat callable is always %__k.callable = { ptr, ptr }; the named struct is
    // interned in the context and installed at build time.
    return _llvm_type;
}

llvm::Constant* callable_type::generate_default_value_initializer() const {
    // Only nullable addressers (* and ?) have a default value: { null, null }.
    // A + or & callable must be explicitly initialised.
    if (!is_nullable()) return nullptr;
    auto* st = llvm::dyn_cast_or_null<llvm::StructType>(_llvm_type);
    if (!st) return nullptr;
    return llvm::ConstantAggregateZero::get(st);
}

std::string callable_type::to_string() const {
    std::ostringstream stm;
    switch (_addresser) {
        case addresser::none:      break;
        case addresser::pointer:   stm << "*"; break;
        case addresser::view:      stm << "?"; break;
        case addresser::link:      stm << "+"; break;
        case addresser::reference: stm << "&"; break;
    }
    stm << "(";
    for (size_t n = 0; n < _parameter_types.size(); ++n) {
        if (n > 0) stm << ", ";
        stm << (_parameter_types[n] ? _parameter_types[n]->to_string() : "<null>");
    }
    stm << ")";
    if (_return_type) stm << ":" << _return_type->to_string();
    if (!_throws.empty()) {
        stm << " throws ";
        for (size_t n = 0; n < _throws.size(); ++n) {
            if (n > 0) stm << ", ";
            stm << (_throws[n] ? _throws[n]->to_string() : "<null>");
        }
    }
    return stm.str();
}

std::shared_ptr<callable_type> callable_type::make_like(
    const callable_type& proto,
    const std::shared_ptr<type>& ret,
    const std::vector<std::shared_ptr<type>>& params,
    const std::vector<std::shared_ptr<type>>& throws)
{
    if (auto mem = dynamic_cast<const member_function_reference_type*>(&proto)) {
        auto res = std::shared_ptr<member_function_reference_type>(
            new member_function_reference_type(mem->get_member_of(), ret, params,
                                               proto._addresser, proto._llvm_type));
        res->set_throws(throws);
        return res;
    }
    auto res = std::shared_ptr<callable_type>(
        new callable_type(ret, params, proto._addresser, proto._llvm_type));
    res->set_throws(throws);
    return res;
}

bool callable_type::signature_equal(const callable_type& other) const {
    if (_parameter_types.size() != other._parameter_types.size()) return false;
    if (_throws.size() != other._throws.size()) return false;
    if (!!_return_type != !!other._return_type) return false;
    if (_return_type && !type::are_equal(_return_type, other._return_type)) return false;
    for (size_t i = 0; i < _parameter_types.size(); ++i) {
        if (!type::are_equal(_parameter_types[i], other._parameter_types[i])) return false;
    }
    for (size_t i = 0; i < _throws.size(); ++i) {
        if (!type::are_equal(_throws[i], other._throws[i])) return false;
    }
    return true;
}

bool callable_type::structurally_equal(const callable_type& other) const {
    if (_addresser != other._addresser) return false;
    return signature_equal(other);
}

llvm::Type* member_function_reference_type::get_llvm_type() const {
    if (_llvm_type) return _llvm_type;
    return nullptr;
}

llvm::Constant* member_function_reference_type::generate_default_value_initializer() const {
    // An unbound member function reference stays a bare opaque function pointer.
    if (auto* ptr_ty = llvm::dyn_cast_or_null<llvm::PointerType>(_llvm_type)) {
        return llvm::ConstantPointerNull::get(ptr_ty);
    }
    return nullptr;
}

std::string member_function_reference_type::to_string() const {
    std::ostringstream stm;
    if (_member_of) {
        stm << _member_of->get_short_name() << "::";
    }
    switch (_addresser) {
        case addresser::none:      break;
        case addresser::pointer:   stm << "*"; break;
        case addresser::view:      stm << "?"; break;
        case addresser::link:      stm << "+"; break;
        case addresser::reference: stm << "&"; break;
    }
    stm << "(";
    for (size_t n = 0; n < _parameter_types.size(); ++n) {
        if (n > 0) stm << ", ";
        stm << _parameter_types[n]->to_string();
    }
    stm << ")";
    if (_return_type) stm << ":" << _return_type->to_string();
    return stm.str();
}

bool member_function_reference_type::structurally_equal(const member_function_reference_type& other) const {
    if (_member_of.get() != other._member_of.get()) return false;
    return callable_type::structurally_equal(other);
}

//
// Function reference type builder
//
callable_type_builder::callable_type_builder(const std::shared_ptr<context> &context):
    _context(context)
{}

std::shared_ptr<callable_type> callable_type_builder::build() const {
    if (_member_of) {
        // An unbound member function reference keeps the historical bare function
        // pointer representation: the owner reference is its implicit first parameter.
        std::vector<llvm::Type*> params;
        params.push_back(_member_of->get_struct_type()->get_reference()->get_llvm_type());
        for (auto& param : _parameter_types) {
            params.push_back(_context->get_llvm_type(param));
        }
        llvm::Type* ret_type = _return_type
            ? _context->get_llvm_type(_return_type)
            : llvm::Type::getVoidTy(**_context);
        llvm::FunctionType* fn_type = llvm::FunctionType::get(ret_type, params, false);
        llvm::Type* ptr_type = llvm::PointerType::get(fn_type->getContext(), 0);
        auto fn_ref = std::shared_ptr<member_function_reference_type>(
            new member_function_reference_type(_member_of, _return_type, _parameter_types, _addresser, ptr_type));
        fn_ref->set_throws(_throws);
        return fn_ref;
    }
    // A fat callable is always %__k.callable = { ptr, ptr } and is interned on the
    // nominal identity of its components.
    return _context->get_callable_type(_return_type, _parameter_types, _addresser, _throws);
}

//
// Enum type
//

llvm::Type* enum_type::get_llvm_type() const {
    if (_underlying_type) {
        return _underlying_type->get_llvm_type();
    }
    return nullptr;
}

bool enum_type::is_object_backed() const {
    auto en = _enumeration.lock();
    return en && en->is_object_backed();
}

std::shared_ptr<struct_type> enum_type::get_object_type() const {
    auto en = _enumeration.lock();
    return en ? en->get_object_type() : nullptr;
}

llvm::Constant* enum_type::generate_default_value_initializer() const {
    if (_underlying_type) {
        // Use the enum's default entry value (not just zero)
        auto en = _enumeration.lock();
        if (en && !en->entries().empty()) {
            auto def_entry = en->get_default_entry();
            return llvm::ConstantInt::get(_underlying_type->get_llvm_type(),
                                          static_cast<uint64_t>(def_entry.value),
                                          /*isSigned=*/def_entry.value < 0);
        }
        return _underlying_type->generate_default_value_initializer();
    }
    return nullptr;
}

std::string alias_type::to_string() const {
    if (auto al = _alias.lock()) {
        return al->get_short_name();
    }
    return _fq_name.empty() ? "<alias>" : _fq_name;
}

std::string enum_type::to_string() const {
    auto e = _enumeration.lock();
    if (e) {
        return "enum " + e->get_short_name();
    }
    return "enum <unknown>";
}

// ═══════════════════════════════════════════════════════════════════════════
// Type substitution (template instantiation support)
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<type> substitute_type(
    const std::shared_ptr<type>& t,
    const type_substitution_map& subst)
{
    if (!t) return nullptr;

    // Leaf: unresolved_type → look up in substitution map
    if (auto ut = std::dynamic_pointer_cast<unresolved_type>(t)) {
        const auto& tid = ut->type_id();

        auto it = subst.find(tid.to_string());
        if (it != subst.end()) return it->second;

        if (tid.has_root_prefix()) {
            it = subst.find(tid.without_root_prefix().to_string());
            if (it != subst.end()) return it->second;
        }

        if (!tid.empty()) {
            it = subst.find(tid.back());
            if (it != subst.end()) return it->second;
        }

        // Not a template parameter itself, but it may be a template reference whose
        // own arguments contain template parameters (e.g. "InputStream<T>").  In that
        // case rebuild the unresolved_type with the arguments substituted so that the
        // later template-instantiation passes resolve it to the concrete type
        // (e.g. "InputStream<byte>").  Without this, "InputStream<T>*" member/param
        // types keep a dangling 'T' and never resolve.
        if (ut->has_template_args()) {
            if (auto substituted = ut->clone_with_substituted_model_args(subst)) {
                return substituted;
            }
        }

        return t; // not a template param, keep as-is
    }

    // Callable types: substitute the return type, every parameter type and every
    // declared exception type. A callable is not a plain wrapper, so it must be
    // handled before the generic get_subtype() path.
    if (auto ct = std::dynamic_pointer_cast<callable_type>(t)) {
        bool changed = false;
        auto new_ret = substitute_type(ct->get_return_type(), subst);
        if (new_ret != ct->get_return_type()) changed = true;
        std::vector<std::shared_ptr<type>> new_params;
        new_params.reserve(ct->get_parameter_types().size());
        for (const auto& p : ct->get_parameter_types()) {
            auto np = substitute_type(p, subst);
            if (np != p) changed = true;
            new_params.push_back(np);
        }
        std::vector<std::shared_ptr<type>> new_throws;
        new_throws.reserve(ct->get_throws().size());
        for (const auto& th : ct->get_throws()) {
            auto nt = substitute_type(th, subst);
            if (nt != th) changed = true;
            new_throws.push_back(nt);
        }
        if (!changed) return t;
        return callable_type::make_like(*ct, new_ret, new_params, new_throws);
    }
    if (auto uct = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
        bool changed = false;
        auto new_ret = substitute_type(uct->get_return_type(), subst);
        if (new_ret != uct->get_return_type()) changed = true;
        std::vector<std::shared_ptr<type>> new_params;
        new_params.reserve(uct->parameter_types().size());
        for (const auto& p : uct->parameter_types()) {
            auto np = substitute_type(p, subst);
            if (np != p) changed = true;
            new_params.push_back(np);
        }
        std::vector<std::shared_ptr<type>> new_throws;
        new_throws.reserve(uct->get_throws().size());
        for (const auto& th : uct->get_throws()) {
            auto nt = substitute_type(th, subst);
            if (nt != th) changed = true;
            new_throws.push_back(nt);
        }
        if (!changed) return t;
        return unresolved_callable_type::make_shared(
            uct->owner_name(), uct->get_addresser(), new_params, new_ret, new_throws);
    }

    // Wrapper types: substitute inner type and rebuild wrapper if changed
    auto inner = t->get_subtype();
    if (!inner) return t; // primitive or leaf — no substitution needed

    auto new_inner = substitute_type(inner, subst);
    if (new_inner == inner) return t; // unchanged

    // When the substituted inner type is a freshly cloned, otherwise-unowned node
    // (an unresolved template reference rebuilt by clone_with_substituted_model_args,
    // or a nested pinned wrapper), the standard cached wrappers (get_reference/…) would
    // only weak-reference it, leaving the subtype dangling once this function returns.
    // Build a pinned wrapper that strongly owns such an inner instead.
    bool inner_needs_pinning = new_inner->is_pinned();
    if (!inner_needs_pinning) {
        if (auto ut = std::dynamic_pointer_cast<unresolved_type>(new_inner)) {
            inner_needs_pinning = ut->has_template_args();
        }
    }
    if (inner_needs_pinning) {
        if (auto pinned = type::make_pinned_wrapper(t, new_inner)) {
            return pinned;
        }
    }

    // Rebuild the wrapper on the substituted inner type
    if (type::is_reference(t))   return new_inner->get_reference();
    if (type::is_pointer(t))     return new_inner->get_pointer();
    if (type::is_link(t))        return new_inner->get_link();
    if (type::is_view(t))        return new_inner->get_view();
    if (type::is_owner(t))       return new_inner->get_owner();
    if (type::is_drain(t))       return new_inner->get_drain();
    if (type::is_const(t))       return new_inner->get_const();
    if (type::is_array(t)) {
        if (type::is_sized_array(t)) {
            auto sa = std::dynamic_pointer_cast<sized_array_type>(t);
            return new_inner->get_array(sa->get_size());
        }
        return new_inner->get_array();
    }

    // Unknown wrapper — return as-is
    return t;
}


} // k::model
