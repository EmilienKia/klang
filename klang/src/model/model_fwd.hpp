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
#ifndef KLANG_MODEL_FWD_HPP
#define KLANG_MODEL_FWD_HPP
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "../lex/lexer.hpp"
#include "../common/common.hpp"
#include "type.hpp"
#include "import.hpp"
#include "template.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

namespace k::parse::ast {
struct ast_node;
struct function_decl;
struct parameter_spec;
struct aggregate_decl;
struct enum_decl;
struct annotation_def;
struct brace_init_list;
}

namespace k::model {
class constructor_invocation_expression;
class global_variable_definition;

class context;

class expression;
class statement;
class variable_statement;
class block;

class parameter;
class function;
class constructor;
class destructor;
class static_constructor;
class static_destructor;
class aggregate;
class structure;
class enumeration;
class klass;
class interface;
class annotation_type;
class ns;
class unit;

class global_tool_function;
class global_constructor_function;
class global_destructor_function;
// Forward declarations for imported model nodes (defined in imported.hpp).
class imported_function;
class imported_constructor;
class imported_destructor;
class imported_method;
class imported_variable;
class imported_aggregate;
class imported_structure;
class imported_klass;
class imported_interface;


namespace gen {
class symbol_resolver;
class aggregate_type_resolver;
class model_materializer;
class type_reference_resolver;
class declaration_generator;
class implementation_generator;
class init_order_resolver;
}

enum visibility {
    DEFAULT,
    PUBLIC,
    PROTECTED,
    PRIVATE
};

/**
 * A single entry in the vtable of a class.
 * Represents a virtual function slot: the function that occupies this slot
 * and the index within the vtable array (after the RTTI placeholder at index 0).
 */
struct vtable_entry {
    /** Slot index in the vtable (0 = first function slot, i.e. vtable[1] after RTTI). */
    size_t slot_index = 0;
    /**
     * The most-derived override of this virtual function in the class that owns the vtable.
     * After symbol resolution, this always points to the concrete implementation.
     */
    std::shared_ptr<function> func;
    /**
     * The "introducing" function (the first declaration of this virtual slot
     * in the inheritance hierarchy). Used for signature matching.
     */
    std::shared_ptr<function> introducing_func;
};

/**
 * This-adjustment thunk descriptor for a secondary vtable slot.
 *
 * When a derived class D inherits from multiple bases (B, C) each with their own
 * vtable, the secondary vtable for base C (embedded at a non-zero byte offset in D)
 * needs "thunk" function pointers that adjust 'this' from C* back to D* before
 * calling the real override.
 *
 * This struct describes one such thunk: the slot index, the real override function,
 * and the byte offset to subtract from 'this'.
 *
 * No LLVM types are stored here; they are computed by the generators from these
 * pure model values.
 */
struct thunk_info {
    /** Vtable slot index (0-based within the vtable entries, NOT counting the RTTI slot). */
    size_t slot_index = 0;
    /** The concrete (most-derived) override function to call after this-adjustment. */
    std::shared_ptr<function> real_func;
    /**
     * Byte offset to subtract from 'this' (a Base* pointing into D's layout) to
     * obtain the D* pointing to the start of D. Always positive when the base
     * subobject is at a non-zero offset.
     */
    ptrdiff_t this_adjustment = 0;
    /** True if a thunk is needed (this_adjustment != 0 AND real_func overrides an ancestor). */
    bool needs_thunk = false;
};

/**
 * Descriptor for a secondary vtable that a derived class must emit for one of its
 * non-primary base subobjects.
 *
 * A "secondary vtable" points to the vtable entries as seen from the perspective of
 * a base class embedded at a non-zero offset inside the derived class.  Its function
 * pointers may be this-adjustment thunks when the slot was overridden in the derived class.
 */
struct secondary_vtable_spec {
    /** The base class whose embedded subobject needs a secondary vtable. */
    std::shared_ptr<klass> base_class;
    /**
     * Byte offset of the base subobject within the derived class layout.
     * 0 means the base is at the start of the object — no adjustment needed (skip).
     */
    ptrdiff_t base_offset = 0;
    /** Per-slot thunk descriptors (indexed identically to base_class->get_vtable()->entries). */
    std::vector<thunk_info> slot_thunks;
};

/**
 * Annotation attached to a function_invocation_expression by type_reference_resolver
 * (Phase 3). Describes how the call should be dispatched at the call-site level.
 *
 * This is pure model data — no llvm::* types.  The code generator reads it to
 * decide between a direct LLVM call and a vtable-indirect dispatch.
 */
struct virtual_dispatch_info {
    /** Dispatch strategy chosen at resolution time. */
    enum class dispatch_kind {
        /** Direct (non-virtual) call: call the LLVM function directly. */
        DIRECT,
        /** Vtable dispatch through the static receiver type's vtable. */
        VTABLE,
        /** Indirect call through a function-reference variable (fp(args)). */
        INDIRECT,
        /** Indirect call through a member function pointer (obj.*mfp(args) or ptr->*mfp(args)). */
        INDIRECT_MEMBER,
    };

    dispatch_kind kind = dispatch_kind::DIRECT;

    /**
     * Vtable slot index (0-based within vtable entries, not counting the RTTI slot).
     * Valid only when kind == VTABLE; -1 for DIRECT.
     */
    int slot_index = -1;

    /**
     * The klass whose vtable should be used for the dispatch lookup.
     * This is the *static* receiver type at the call site (e.g. the type of `b` in
     * `b.speak()` where b : Animal&). Non-null when kind == VTABLE and the receiver
     * is a locally-defined klass.
     */
    std::shared_ptr<klass> dispatch_class;

    /**
     * Imported aggregate (imported_klass / imported_interface) to use for vtable
     * dispatch when the receiver is an imported type (neither inherits from klass).
     * Non-null when kind == VTABLE and dispatch_class is null.
     */
    std::shared_ptr<aggregate> imported_dispatch_agg;

    /**
     * Optional this-adjustment offset (bytes) to apply BEFORE loading the vptr.
     * Non-zero when the receiver is a secondary-base reference (e.g. a C& pointing
     * into a D object that embeds C at offset > 0).
     * 0 for primary-base dispatch (no adjustment needed before vptr load).
     */
    ptrdiff_t this_adjustment = 0;
};

/**
 * Complete vtable layout for a class.
 * Each class (or virtual base) has its own vtable descriptor.
 * For single inheritance, there is one vtable_layout per class.
 * For multiple / diamond inheritance, a derived class may have multiple
 * vtable_layouts (one per primary vtable + one per each non-primary base path).
 */
struct vtable_layout {
    /** All virtual function slots in declaration order. */
    std::vector<vtable_entry> entries;

    /**
     * Secondary vtable specifications computed by model_materializer.
     * One entry per non-primary base class with a vtable embedded at non-zero offset.
     * Empty for classes with no multiple inheritance.
     */
    std::vector<secondary_vtable_spec> secondary_vtables;

    /** LLVM global variable holding the vtable constant (set during declaration generation). */
    llvm::GlobalVariable* llvm_global = nullptr;

    /** LLVM struct type for the vtable: { ptr (RTTI), [N x ptr] } (set during type resolution). */
    llvm::StructType* llvm_type = nullptr;

    /**
     * LLVM global variable holding the RTTI constant for this class.
     * The RTTI global is a genuine ::k::Class instance with layout:
     *   { ptr __vptr__ (Class vtable), ptr __vptr_TypeInfo__ (Class secondary vtable), ptr name (short name) }
     * The 'typeid' is the address of this global, valid across dynamic modules.
     * Set during declaration generation. May be non-null even if llvm_global is null
     * (e.g. for abstract classes that have no emitted vtable global).
     */
    llvm::GlobalVariable* llvm_rtti_global = nullptr;

    /**
     * Mangled symbol name for the vtable global (set for imported types from KDI).
     * Used to lazily create an external declaration when llvm_global is first needed.
     */
    std::string vtable_symbol;

    /**
     * Mangled symbol name for the RTTI global (set for imported types from KDI).
     * Used to lazily create an external declaration when llvm_rtti_global is first needed.
     */
    std::string rtti_symbol;

    /** Total number of slots (entries.size()). */
    size_t slot_count() const { return entries.size(); }
};



class model_visitor;
class template_instantiator;

} // namespace k::model

#endif //KLANG_MODEL_FWD_HPP
