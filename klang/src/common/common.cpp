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


#include "common.hpp"

#include "../lex/lexer.hpp"
#include "../parse/ast.hpp"

#include <iostream>
#include <sstream>

namespace k {


bool name::operator == (const name& other) const {
    if( _root_prefix != other._root_prefix )
        return false;
    if( _identifiers.size() != other._identifiers.size() )
        return false;
    for(size_t i = 0; i < _identifiers.size(); ++i) {
        if(_identifiers[i] != other._identifiers[i])
            return false;
    }
    return true;
}

std::string name::to_string()const {
    std::ostringstream stm;
    stm << (_root_prefix ? "::" : "");
    if(_identifiers.empty()) {
        stm << "<<noidentifier>>";
    } else {
        stm << _identifiers.front();
        for(size_t i = 1; i < _identifiers.size(); ++i) {
            stm << "::" << _identifiers[i];
        }
    }
    return stm.str();
}

} // namespace k
