//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "common.hpp"

#include "lexer.hpp"
#include "ast.hpp"

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
