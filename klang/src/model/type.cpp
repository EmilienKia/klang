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
    std::cerr << "Unsized array are not supported yet." << std::endl;
    return nullptr;
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
    return llvm::ArrayType::get(subtype.lock()->get_llvm_type(), get_size());
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

struct_type::struct_type(const std::string& name, std::weak_ptr<k::model::structure> st):
type(nullptr),
_name(name),
_struct(st)
{
}


struct_type::struct_type(const std::string& name, std::weak_ptr<structure> st, std::vector<field>&& fields, llvm::StructType* llvm_struct_type):
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

std::shared_ptr<structure> struct_type::get_struct() const {
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
    // NOTE: is it relevant to check subtypes resolution each time ?
    /*if(!_return_type->is_resolved()) {
        return false;
    }
    for(const auto& param_type : _parameter_types) {
        if(!param_type->is_resolved()) {
            return false;
        }
    }*/
    return true;
}

std::string function_reference_type::to_string() const {
    std::ostringstream stm;
    stm << "fn:((";
    for(size_t n=0; n<_parameter_types.size(); ++n) {
        if(n>0) {
            stm << ", ";
        }
        auto param_type = _parameter_types[n];
        stm << param_type->to_string();
    }
    stm << "):" ;
    stm << _return_type->to_string();
    stm << ")";
    return stm.str();
}

std::string member_function_reference_type::to_string() const {
    std::ostringstream stm;
    stm << "memfn:((" << _member_of->get_short_name() << ")(";
    for(size_t n=0; n<_parameter_types.size(); ++n) {
        if(n>0) {
            stm << ", ";
        }
        auto param_type = _parameter_types[n];
        stm << param_type->to_string();
    }
    stm << "):" ;
    stm << _return_type->to_string();
    stm << ")";
    return stm.str();
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
        params.push_back(_member_of->get_struct_type()->get_reference()->get_llvm_type());
    }
    for (auto& param : _parameter_types) {
        params.push_back(_context->get_llvm_type(param));
    }
    llvm::Type* ret_type = _return_type ? _context->get_llvm_type(_return_type) : llvm::Type::getVoidTy(**_context);
    llvm::FunctionType* fn_type = llvm::FunctionType::get(ret_type, params, false);

    std::shared_ptr<function_reference_type> fn_ref_type{new function_reference_type(_return_type, _parameter_types, fn_type)};
    return fn_ref_type;
}


} // k::model
