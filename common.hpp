//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_COMMON_HPP
#define KLANG_COMMON_HPP

#include <string>
#include <vector>

namespace k {


class name
{
protected:
    bool _root_prefix = false;
    std::vector<std::string> _identifiers;

public:
    name() =default;

    name(const std::string& name) :
            _root_prefix(false), _identifiers({name}) {}

    name(bool root_prefix, const std::string& name) :
            _root_prefix(root_prefix), _identifiers({name}) {}

    name(bool root_prefix, const std::vector<std::string>& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(bool root_prefix, std::vector<std::string>&& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(bool root_prefix, const std::initializer_list<std::string>& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(const name&) = default;
    name(name&&) = default;

    name& operator=(const name&) = default;
    name& operator=(name&&) = default;

    bool has_root_prefix()const {
        return _root_prefix;
    }

    size_t size() const {
        return _identifiers.size();
    }

    const std::string& at(size_t index) const {
        return _identifiers.at(index);
    }

    const std::string& operator[] (size_t index) const {
        return _identifiers[index];
    }

    bool operator == (const name& other) const;

    std::string to_string()const;

    operator std::string () const{
        return to_string();
    }
};




} // namespace k
#endif //KLANG_COMMON_HPP
