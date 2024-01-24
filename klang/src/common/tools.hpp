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
