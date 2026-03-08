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

#ifndef KLANG_IMPORTED_HPP
#define KLANG_IMPORTED_HPP

/**
 * @file imported.hpp
 *
 * Model nodes for entities imported from external K modules (.kdi files).
 *
 * All "imported_*" classes live here, separate from model.hpp so that the
 * main model file stays focused on the compiled unit and is not polluted by
 * import-specific details.
 *
 * Hierarchy:
 *   imported_function      — global/namespace-level function (no body)
 *   imported_constructor   — aggregate constructor (no body)
 *   imported_destructor    — aggregate destructor  (no body)
 *   imported_method        — member method         (no body)
 *   imported_variable      — global/static variable (no initialiser)
 *   imported_aggregate     — abstract base for imported struct/class/interface
 *   imported_structure     — imported struct
 *   imported_klass         — imported class
 *   imported_interface     — imported interface
 */

#include "model.hpp"

#include <kdi.hpp>

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Imported function (global / namespace-level)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A function imported from an external module.
 *
 * Represents a K function declared in a .kdi file — it has a signature
 * (return type + parameters) and a fixed mangled name from the KDI, but NO
 * body (get_block() returns nullptr).  It lives as a child of the unit's root
 * namespace (or a synthetic sub-namespace) and participates in the normal
 * symbol-resolution flow.
 *
 * The LLVM generator emits it as a `declare` (ExternalLinkage, no BasicBlock).
 * Created and owned by unit::get_or_create_imported_function().
 */
class imported_function : public function {
protected:
    friend class unit;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class gen::implementation_generator;

    const kdi::kdi_function* _kdi_function = nullptr;

    imported_function(std::shared_ptr<element> parent, const kdi::kdi_function* kdi_fn);
    void update_mangled_name() override;

public:
    static std::shared_ptr<imported_function>
    make_shared(std::shared_ptr<element> parent, const kdi::kdi_function* kdi_fn);

    void accept(model_visitor& visitor) override;
    const kdi::kdi_function* get_kdi_function() const { return _kdi_function; }
    bool is_external() const { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Imported constructor
// ─────────────────────────────────────────────────────────────────────────────

/** A constructor imported from an external module (C1 + C2 mangled names, no body). */
class imported_constructor : public constructor {
protected:
    friend class unit;
    const kdi::kdi_constructor* _kdi_ctor = nullptr;

    imported_constructor(std::shared_ptr<aggregate> parent, const kdi::kdi_constructor* kdi_ctor);
    void update_mangled_name() override;

public:
    static std::shared_ptr<imported_constructor>
    make_shared(std::shared_ptr<aggregate> parent, const kdi::kdi_constructor* kdi_ctor);

    void accept(model_visitor& visitor) override;
    const kdi::kdi_constructor* get_kdi_constructor() const { return _kdi_ctor; }
    const std::string& get_c2_mangled_name() const;
    bool is_external() const { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Imported destructor
// ─────────────────────────────────────────────────────────────────────────────

/** A destructor imported from an external module (D1 + D2 mangled names, no body). */
class imported_destructor : public destructor {
protected:
    friend class unit;
    const kdi::kdi_destructor* _kdi_dtor = nullptr;

    imported_destructor(std::shared_ptr<aggregate> parent, const kdi::kdi_destructor* kdi_dtor);
    void update_mangled_name() override;

public:
    static std::shared_ptr<imported_destructor>
    make_shared(std::shared_ptr<aggregate> parent, const kdi::kdi_destructor* kdi_dtor);

    void accept(model_visitor& visitor) override;
    const kdi::kdi_destructor* get_kdi_destructor() const { return _kdi_dtor; }
    const std::string& get_d2_mangled_name() const;
    bool is_external() const { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Imported method
// ─────────────────────────────────────────────────────────────────────────────

/** A member method imported from an external module (no body). */
class imported_method : public function {
protected:
    friend class unit;
    const kdi::kdi_method* _kdi_method = nullptr;

    imported_method(std::shared_ptr<element> parent, const kdi::kdi_method* kdi_m);
    void update_mangled_name() override;

public:
    static std::shared_ptr<imported_method>
    make_shared(std::shared_ptr<element> parent, const kdi::kdi_method* kdi_m);

    void accept(model_visitor& visitor) override;
    const kdi::kdi_method* get_kdi_method() const { return _kdi_method; }
    bool is_external() const { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Imported aggregate (struct / class / interface)
// ─────────────────────────────────────────────────────────────────────────────

class imported_aggregate : public aggregate {
protected:
    friend class unit;
    const kdi::kdi_aggregate* _kdi_aggregate = nullptr;

    imported_aggregate(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;
    const kdi::kdi_aggregate* get_kdi_aggregate() const { return _kdi_aggregate; }
    bool is_external() const { return true; }
    bool is_class() const override { return false; }
    bool has_vtable() const override { return false; }
};

class imported_structure : public imported_aggregate {
protected:
    friend class unit;
    imported_structure(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
public:
    static std::shared_ptr<imported_structure>
    make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
    void accept(model_visitor& visitor) override;
    bool is_class() const override { return false; }
};

class imported_klass : public imported_aggregate {
protected:
    friend class unit;
    bool _has_vtable = false;
    imported_klass(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
public:
    static std::shared_ptr<imported_klass>
    make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
    void accept(model_visitor& visitor) override;
    bool is_class() const override { return true; }
    bool has_vtable() const override { return _has_vtable; }
};

class imported_interface : public imported_aggregate {
protected:
    friend class unit;
    imported_interface(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
public:
    static std::shared_ptr<imported_interface>
    make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg);
    void accept(model_visitor& visitor) override;
    bool is_class() const override { return true; }
    bool has_vtable() const override { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Imported variable
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A global or static variable imported from an external module.
 * Inherits from global_variable_definition; no initialiser.
 * The LLVM generator emits an ExternalLinkage GlobalVariable with no initialiser.
 */
class imported_variable : public global_variable_definition {
protected:
    friend class unit;
    const kdi::kdi_variable* _kdi_variable = nullptr;

    imported_variable(std::shared_ptr<variable_holder> parent, const kdi::kdi_variable* kdi_var);
    void update_mangled_name() override;

public:
    static std::shared_ptr<imported_variable>
    make_shared(std::shared_ptr<variable_holder> parent, const kdi::kdi_variable* kdi_var);

    void accept(model_visitor& visitor) override;
    const kdi::kdi_variable* get_kdi_variable() const { return _kdi_variable; }
    bool is_external() const { return true; }
};

} // namespace k::model

#endif // KLANG_IMPORTED_HPP

