# Klang Compiler Architecture & Design Documents

This directory contains in-depth technical design specifications for the **Klang reference compiler (`klangc`)** and the **K runtime model**.

---

## Documents Directory

1. **[01. Klang Compiler Architecture & Design](01-compiler-architecture.md)**
   - High-level compiler architecture and pipeline phases (Phase 0 to Phase 8).
   - AST vs. Semantic Model boundary.
   - LLVM integration, optimization, JIT, and artifact generation (executable, shared/static library, KDI).
   - Error handling, diagnostics, and CLI driver structure.

2. **[02. Symbol & Type Resolution](02-symbol-and-type-resolution.md)**
   - Multi-pass resolution architecture (Pass A, B, C, D).
   - Scope tree hierarchy and lookup algorithms (`scope_lookup`).
   - Symbol binding, aggregate structure resolution, and type inference.
   - Override verification, template instantiation interactions, and exception contracts.

3. **[03. Class Layout, Vtables, RTTI & Call Synthesis](03-class-layout-vtables-rtti-and-calls.md)**
   - Aggregate memory layouts (struct, class, interface, annotation, diamond virtual inheritance).
   - Virtual method tables (vtables), universal destructor slot (Slot 0), secondary vtables, and adjustor thunks.
   - Runtime Type Information (RTTI) metadata hierarchy and dynamic type operations (`is`, `as`).
   - Call synthesis mechanisms (direct, virtual, interface, constructor C1/C2, destructor D1/D2, exception unwinding).

4. **[04. Callables & Lambdas](04-callables-and-lambdas.md)**
   - First-class callable architecture and fat pointer representation (`%__k.callable = { ptr fn, ptr ctx }`).
   - Binding synthesis variants (free, static, member, virtual, functor, functional interface, null-propagating).
   - Strict ABI compatibility, parameter contravariance, return covariance, and `throws` set subset rules.
   - Lambda compilation, capture lowering, closure context packaging, and invocation lowering.

5. **[05. Templates, Instantiation & Argument Deduction](05-template-system-and-deduction.md)**
   - Dual-model template architecture (monomorphized templates vs. uniform synthesis generics).
   - Semantic model representation (`tpl_info`, parameter descriptors, argument descriptors).
   - Model-level instantiation engine (`template_instantiator`), substitution maps, and COMDAT deduplication.
   - Automated template argument deduction pattern matching across scalars, indirections, composites, arrays, callables, and packs.
   - SFINAE overload resolution integration, non-template preference tie-breaking, and dependent return type materialization.
   - Cross-module KDI template serialization and origin tracking.

6. **[06. Unified Call Syntax (UCS) & Member Invocation Resolution](06-unified-call-syntax-resolution.md)**
   - Unified Call Syntax mechanics for member and free function invocations (`obj.func(...)`).
   - Candidate collection pipeline across aggregate hierarchy, caller scopes, `using namespace`, and imported modules.
   - Scope isolation rules preventing unwanted member function leakage.
   - Deduction, specialization, and instantiation of free and member template functions under UCS.
   - Overload resolution scoring, argument adaptation, and code generation lowering.
