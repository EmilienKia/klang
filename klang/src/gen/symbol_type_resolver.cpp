/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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
//
// Note: Last resolver log number: 0x30004
//

#include "symbol_type_resolver.hpp"

namespace k::model::gen {


//
// Exceptions
//
resolution_error::resolution_error(const std::string &arg) :
        runtime_error(arg)
{}

resolution_error::resolution_error(const char *string) :
        runtime_error(string)
{}

//
// Symabol and type resolver
//

void symbol_type_resolver::resolve()
{
    visit_unit(_unit);
}



std::shared_ptr<expression> symbol_type_resolver::adapt_type(const std::shared_ptr<expression>& expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(prim_src==prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;

}



} // k::model::gen
