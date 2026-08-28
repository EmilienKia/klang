/*
 * K Language standard library — symbol mangling C API
 *
 * Copyright 2026 Emilien Kia
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

#ifndef LIBK_SYMBOL_MANGLING_H
#define LIBK_SYMBOL_MANGLING_H

#ifdef __cplusplus
extern "C" {
#endif

char* __k_symbol_mangle(const char* readable);
char* __k_symbol_demangle(const char* mangled);
void __k_symbol_string_free(char* value);

#ifdef __cplusplus
}
#endif

#endif // LIBK_SYMBOL_MANGLING_H
