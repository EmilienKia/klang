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

std::string unresolved_function_ref_type::to_string() const {
    std::ostringstream stm;
    stm << "<<unresolved_fn_ref:";
    if (!_owner_name.empty()) stm << _owner_name.to_string() << "::";
    switch (_ref_kind) {
        case function_reference_type::ref_kind::pointer: stm << "*("; break;
        case function_reference_type::ref_kind::view:    stm << "?("; break;
        case function_reference_type::ref_kind::link:    stm << "+("; break;
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
            {CHAR,"char"},
            {SHORT, "short"},
            {UNSIGNED_SHORT, "unsigned short"},
            {INT, "int"},
            {UNSIGNED_INT, "unsigned int"},
            {LONG, "long"},
            {UNSIGNED_LONG, "unsigned long"},
            // TODO Add (unsigned) long long
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
    return subtype.lock()->is_resolved();
}

llvm::Type* pointer_type::get_llvm_type() const {
    if(_llvm_type==nullptr && is_resolved()) {
        _llvm_type = llvm::PointerType::get(subtype.lock()->get_llvm_type(), 0 /*llvm::ADDRESS_SPACE_GENERIC*/);
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
    return subtype.lock()->is_resolved();
}

/*
std::shared_ptr<reference_type> reference_type::get_reference() {
    return std::dynamic_pointer_cast<reference_type>(shared_from_this());
}
*/

llvm::Type* reference_type::get_llvm_type() const {
    if(_llvm_type==nullptr && is_resolved()) {
        auto llvm_subtype = subtype.lock()->get_llvm_type();
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
    return subtype.lock()->is_resolved();
}

llvm::Type* link_type::get_llvm_type() const {
    if(_llvm_type==nullptr && is_resolved()) {
        _llvm_type = llvm::PointerType::get(subtype.lock()->get_llvm_type(), 0);
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
    return subtype.lock()->is_resolved();
}

llvm::Type* view_type::get_llvm_type() const {
    if(_llvm_type==nullptr && is_resolved()) {
        auto* sub_llvm = subtype.lock()->get_llvm_type();
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
    return subtype.lock()->is_resolved();
}

llvm::Type* owner_type::get_llvm_type() const {
    if(_llvm_type==nullptr && is_resolved()) {
        _llvm_type = llvm::PointerType::get(subtype.lock()->get_llvm_type(), 0);
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
    return subtype.lock()->is_resolved();
}

llvm::Type* drain_type::get_llvm_type() const {
    if(_llvm_type==nullptr && is_resolved()) {
        _llvm_type = llvm::PointerType::get(subtype.lock()->get_llvm_type(), 0);
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
    return subtype.lock()->is_resolved();
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
    return subtype.lock()->is_resolved();
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
bool function_reference_type::is_resolved() const {
    if (_return_type && !_return_type->is_resolved()) return false;
    for (const auto& p : _parameter_types) {
        if (p && !p->is_resolved()) return false;
    }
    return true;
}

llvm::Type* function_reference_type::get_llvm_type() const {
    if (_llvm_type) return _llvm_type;
    // Build lazily: should have been built by the builder.
    return nullptr;
}

llvm::Constant* function_reference_type::generate_default_value_initializer() const {
    // A function reference variable is an opaque pointer; default to null pointer.
    if (!_llvm_type) return nullptr;
    if (auto* ptr_ty = llvm::dyn_cast<llvm::PointerType>(_llvm_type)) {
        return llvm::ConstantPointerNull::get(ptr_ty);
    }
    return nullptr;
}

std::string function_reference_type::to_string() const {
    std::ostringstream stm;
    switch (_ref_kind) {
        case ref_kind::pointer: stm << "*("; break;
        case ref_kind::view:    stm << "?("; break;
        case ref_kind::link:    stm << "+("; break;
    }
    for (size_t n = 0; n < _parameter_types.size(); ++n) {
        if (n > 0) stm << ", ";
        stm << _parameter_types[n]->to_string();
    }
    stm << ")";
    return stm.str();
}

bool function_reference_type::structurally_equal(const function_reference_type& other) const {
    if (_ref_kind != other._ref_kind) return false;
    if (_parameter_types.size() != other._parameter_types.size()) return false;
    // Compare return types
    if (_return_type != other._return_type) {
        if (!_return_type || !other._return_type) return false;
        // Simple pointer equality for now (types are cached)
        if (_return_type.get() != other._return_type.get()) return false;
    }
    for (size_t i = 0; i < _parameter_types.size(); ++i) {
        if (_parameter_types[i].get() != other._parameter_types[i].get()) return false;
    }
    return true;
}

llvm::Type* member_function_reference_type::get_llvm_type() const {
    if (_llvm_type) return _llvm_type;
    return nullptr;
}

std::string member_function_reference_type::to_string() const {
    std::ostringstream stm;
    if (_member_of) {
        stm << _member_of->get_short_name() << "::";
    }
    switch (_ref_kind) {
        case ref_kind::pointer: stm << "*("; break;
        case ref_kind::view:    stm << "?("; break;
        case ref_kind::link:    stm << "+("; break;
    }
    for (size_t n = 0; n < _parameter_types.size(); ++n) {
        if (n > 0) stm << ", ";
        stm << _parameter_types[n]->to_string();
    }
    stm << ")";
    return stm.str();
}

bool member_function_reference_type::structurally_equal(const member_function_reference_type& other) const {
    if (_member_of.get() != other._member_of.get()) return false;
    return function_reference_type::structurally_equal(other);
}

//
// Function reference type builder
//
function_reference_type_builder::function_reference_type_builder(const std::shared_ptr<context> &context):
    _context(context)
{}

std::shared_ptr<function_reference_type> function_reference_type_builder::build() const {
    std::vector<llvm::Type*> params;
    if (_member_of) {
        // First parameter is an implicit reference to the owner struct (the 'this' pointer).
        params.push_back(_member_of->get_struct_type()->get_reference()->get_llvm_type());
    }
    for (auto& param : _parameter_types) {
        params.push_back(_context->get_llvm_type(param));
    }
    llvm::Type* ret_type = _return_type
        ? _context->get_llvm_type(_return_type)
        : llvm::Type::getVoidTy(**_context);
    llvm::FunctionType* fn_type = llvm::FunctionType::get(ret_type, params, false);

    llvm::Type* ptr_type = llvm::PointerType::get(fn_type->getContext(), 0);

    if (_member_of) {
        auto fn_ref = std::shared_ptr<member_function_reference_type>(
            new member_function_reference_type(_member_of, _return_type, _parameter_types, _ref_kind, ptr_type));
        return fn_ref;
    } else {
        auto fn_ref = std::shared_ptr<function_reference_type>(
            new function_reference_type(_return_type, _parameter_types, _ref_kind, ptr_type));
        return fn_ref;
    }
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

        return t; // not a template param, keep as-is
    }

    // Wrapper types: substitute inner type and rebuild wrapper if changed
    auto inner = t->get_subtype();
    if (!inner) return t; // primitive or leaf — no substitution needed

    auto new_inner = substitute_type(inner, subst);
    if (new_inner == inner) return t; // unchanged

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
