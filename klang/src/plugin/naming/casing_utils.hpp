/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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

#ifndef KLANG_PLUGIN_NAMING_CASING_UTILS_HPP
#define KLANG_PLUGIN_NAMING_CASING_UTILS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace k::plugin::naming {

/** Split an identifier into constituent words, preserving leading and trailing underscores. */
void split_words(std::string_view name,
                 std::string& prefix_underscores,
                 std::vector<std::string>& words,
                 std::string& suffix_underscores);

/** Convert an identifier to PascalCase (e.g. "point_2d" -> "Point2d", "my_class" -> "MyClass"). */
std::string to_pascal_case(std::string_view name);

/** Convert an identifier to camelCase (e.g. "My_Method" -> "myMethod", "my_var" -> "myVar"). */
std::string to_camel_case(std::string_view name);

/** Convert an identifier to SCREAMING_SNAKE_CASE (e.g. "max_size" -> "MAX_SIZE", "myConst" -> "MY_CONST"). */
std::string to_screaming_snake_case(std::string_view name);

/** Convert an identifier to snake_case / lower_case (e.g. "MyNamespace" -> "my_namespace"). */
std::string to_snake_case(std::string_view name);

} // namespace k::plugin::naming

#endif // KLANG_PLUGIN_NAMING_CASING_UTILS_HPP
