//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>
//

#ifndef KLANG_TOOLS_HPP
#define KLANG_TOOLS_HPP

#include <functional>

namespace tools {



template<typename Map, typename K, typename F, typename T = typename Map::mapped_type, typename It = typename Map::iterator>
It compute_if_absent(Map& map, const K& key, F func) {
    It it = map.find(key);
    if ( it == map.end() ) {
        T val = func(key);
        return map.insert({key, val}).first;
    } else {
        return it;
    }
}



template<typename Map, typename K = std::function<typename Map::mapped_type()> , typename T = typename Map::mapped_type, typename It = typename Map::iterator>
It find_put_if_absent(Map& map, const K& key, T&& val) {
    It it = map.find(key);
    if ( it == map.end() ) {
        return map.insert({key, val}).first;
    } else {
        return it;
    }
}


template<typename Map, typename K, typename T = typename Map::mapped_type, typename It = typename Map::iterator>
T& get_or(Map& map, const K& key, T& fallback) {
    It it = map.find(key);
    if ( it != map.end() ) {
        return it.second;
    } else {
        return fallback;
    }
}




}

#endif //KLANG_TOOLS_HPP
