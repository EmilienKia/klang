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
#ifndef KLANG_TARGET_INIT_HPP
#define KLANG_TARGET_INIT_HPP

//
// Selective LLVM target initialisation.
//
// Only x86/x64, AArch64 and ARM (32-bit) are supported.
// Declaring the C-linkage initialisers explicitly avoids linking the
// InitializeAll* inline functions, which reference every backend compiled
// into the system LLVM installation.
//

extern "C" {
    // x86 / x86-64
    void LLVMInitializeX86TargetInfo();
    void LLVMInitializeX86Target();
    void LLVMInitializeX86TargetMC();
    void LLVMInitializeX86AsmPrinter();
    void LLVMInitializeX86AsmParser();

    // AArch64 (ARM 64-bit)
    void LLVMInitializeAArch64TargetInfo();
    void LLVMInitializeAArch64Target();
    void LLVMInitializeAArch64TargetMC();
    void LLVMInitializeAArch64AsmPrinter();
    void LLVMInitializeAArch64AsmParser();

    // ARM (32-bit)
    void LLVMInitializeARMTargetInfo();
    void LLVMInitializeARMTarget();
    void LLVMInitializeARMTargetMC();
    void LLVMInitializeARMAsmPrinter();
    void LLVMInitializeARMAsmParser();
} // extern "C"

namespace k {

/// Initialise all supported LLVM targets (x86, AArch64, ARM).
/// Safe to call multiple times — each LLVMInitialize* function is idempotent.
inline void initialize_llvm_targets()
{
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();

    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmPrinter();
    LLVMInitializeARMAsmParser();
}

} // namespace k

#endif // KLANG_TARGET_INIT_HPP

