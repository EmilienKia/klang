/*
 * K Language compiler
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
#ifndef KLANG_LLVM_TARGET_TRIPLE_HPP
#define KLANG_LLVM_TARGET_TRIPLE_HPP

#include <llvm/Config/llvm-config.h>
#include <llvm/TargetParser/Triple.h>

namespace k {

// LLVM 21 changed target-machine and module APIs from strings to Triple.
#if LLVM_VERSION_MAJOR >= 21
inline const llvm::Triple& llvm_target_triple_arg(const llvm::Triple& triple) {
    return triple;
}
#else
inline llvm::StringRef llvm_target_triple_arg(const llvm::Triple& triple) {
    return triple.getTriple();
}
#endif

} // namespace k

#endif // KLANG_LLVM_TARGET_TRIPLE_HPP
