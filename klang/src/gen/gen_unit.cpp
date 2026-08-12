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
//
// Note: Last gen_unit log number: 0x0035
//
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"

#include "../model/imported.hpp"
#include "../model/expressions.hpp"
#include "../model/mangler.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/IR/GlobalAlias.h>

#include <queue>
#include <unordered_map>
#include "../errors.hpp"

namespace k::model::gen {

//
// Named element
//

void symbol_resolver::visit_named_element(named_element& named) {
    // Assign fully qualified name if not already assigned, and compute the mangled name accordingly
    if (named.get_fq_name().empty()) {
        if (named.get_short_name().empty()) {
            // TODO correctly handle unnamed elements
        } else {
            auto elem = dynamic_cast<element*>(&named);
            if (elem) {
                named.assign_name(elem->ancestor<named_element>()->get_name().with_back(named.get_short_name()));
            }
        }
    }
}

//
// Unit
// Note : Global constructor and destructor method objects are always created but generated and registered only if needed.
// But Global main method is
//

// is_k_object() moved to gen_helpers.hpp (shared with gen_class.cpp's
// universal destructor vtable slot seeding).

/// Return true if the given aggregate is ::k::Annotation (the root annotation base).
static bool is_k_annotation(const aggregate& agg) {
    if (agg.get_short_name() != "Annotation") return false;
    auto parent_ns = agg.parent<ns>();
    if (!parent_ns) return false;
    if (parent_ns->get_short_name() != "k") return false;
    return true;
}

/// Check if a type name is available locally or via imports.
static bool is_type_available(unit& unit, const std::string& type_name) {
    auto root_ns = unit.get_root_namespace();
    if (root_ns->get_short_name() == "k") {
        if (root_ns->get_aggregate(type_name)) return true;
    } else if (auto k_ns = root_ns->get_child_namespace("k")) {
        if (k_ns->get_aggregate(type_name)) return true;
    }
    k::name qname{false, {"k", type_name}};
    if (unit.find_imported_type(qname)) return true;
    return false;
}

// ── Phase 4: ::k::Application abstract-chain helpers ─────────────────────────
//
// A "chain" is the sequence of classes from the first (outermost) abstract
// class deriving directly/transitively from ::k::Application (but not
// ::k::Application itself), down to the final concrete `class Application`.
// See gen_unit.cpp Pre-pass 1b for the validation algorithm using these.

/// The shape of a 'main' overload, matched against the 4 standard signatures.
enum class std_main_sig { NONE_STD, VOID_NOARGS, INT_NOARGS, VOID_ARGS, INT_ARGS };

/// Classify a 'main' function's signature: one of the 4 standard shapes, or
/// NONE_STD for any other ("custom") shape.
static std_main_sig classify_main_signature(const std::shared_ptr<function>& f) {
    if (!f) return std_main_sig::NONE_STD;
    size_t n = f->get_parameter_size();
    bool has_ret = f->has_return_type();
    if (n == 0) {
        return has_ret ? std_main_sig::INT_NOARGS : std_main_sig::VOID_NOARGS;
    }
    if (n == 1) {
        auto p = f->parameters().front();
        if (!p || !p->get_type()) return std_main_sig::NONE_STD;
        auto pt = p->get_type();
        auto no_ref = type::is_reference(pt) ? pt->get_subtype() : pt;
        auto nc = type::remove_const(no_ref);
        if (!type::is_array(nc)) return std_main_sig::NONE_STD;
        return has_ret ? std_main_sig::INT_ARGS : std_main_sig::VOID_ARGS;
    }
    return std_main_sig::NONE_STD;
}

/// True if two 'main' functions share the exact same parameter/return shape
/// (used to match a delegation target's abstract declaration against its
/// override in a subsequent class of the chain).
static bool same_main_shape(const std::shared_ptr<function>& a, const std::shared_ptr<function>& b) {
    if (!a || !b) return false;
    if (a->has_return_type() != b->has_return_type()) return false;
    if (a->has_return_type()) {
        auto ra = a->get_return_type(), rb = b->get_return_type();
        if (!ra || !rb || ra->to_string() != rb->to_string()) return false;
    }
    if (a->get_parameter_size() != b->get_parameter_size()) return false;
    for (size_t i = 0; i < a->get_parameter_size(); ++i) {
        auto pa = a->parameters()[i], pb = b->parameters()[i];
        if (!pa || !pb) return false;
        auto ta = pa->get_type(), tb = pb->get_type();
        if (!ta || !tb || ta->to_string() != tb->to_string()) return false;
    }
    return true;
}

/// A function is "implemented" (has a real body) iff it is neither deleted
/// nor abstract. (A well-formed program cannot have any other reason for a
/// function to lack a body.)
static bool main_is_implemented(const std::shared_ptr<function>& f) {
    return f && !f->is_deleted() && !f->is_abstract_func();
}

/// Build the ::k::Application-derived chain for `final_class`, ordered from
/// the outermost abstract class (nearest to ::k::Application, exclusive) to
/// `final_class` itself (last element). Returns {final_class} alone if there
/// is no intermediate abstract class between it and ::k::Application.
static std::vector<std::shared_ptr<klass>> build_application_chain(
    const std::shared_ptr<klass>& final_class,
    const std::shared_ptr<aggregate>& k_application)
{
    std::vector<std::shared_ptr<klass>> chain;
    std::shared_ptr<klass> cur = final_class;
    std::unordered_set<klass*> visited;
    while (cur && cur.get() != k_application.get() && visited.insert(cur.get()).second) {
        chain.push_back(cur);
        std::shared_ptr<klass> next;
        for (auto& bs : cur->get_bases()) {
            if (!bs.base) continue;
            if (bs.base.get() == k_application.get()) {
                // Direct base is ::k::Application itself: `cur` is the top of the chain.
                next = nullptr;
                break;
            }
            if (auto bk = std::dynamic_pointer_cast<klass>(bs.base)) {
                if (bk->is_derived_from(k_application)) {
                    next = bk;
                    break;
                }
            }
        }
        cur = next;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

/// Human-readable rendering of a main signature for diagnostics.
static std::string main_sig_to_string(const std::shared_ptr<function>& f) {
    if (!f) return "main(...)";
    std::string s = "main(";
    bool first = true;
    for (auto& p : f->parameters()) {
        if (!first) s += ", ";
        first = false;
        s += p && p->get_type() ? p->get_type()->to_string() : "?";
    }
    s += ")";
    if (f->has_return_type() && f->get_return_type()) s += " : " + f->get_return_type()->to_string();
    return s;
}

/**
 * Visit the compilation unit during symbol resolution.
 *
 * Steps:
 *   1. Process import statements: load KDI files and resolve imported modules.
 *   2. Visit the root namespace (recursively resolves all symbols).
 *   3. Visit global constructor, destructor, and main functions.
 */
void symbol_resolver::visit_unit(unit& unit)
{
    trace("[symbol_resolver::visit_unit] begin");
    // Step 1: Process import statements: load KDI files and resolve imported modules
    auto root_ns = _unit.get_root_namespace();

    // ── Pre-pass 0: implicit Object inheritance ─────────────────────────────────
    // Every class OR interface that has no declared base classes (and is not
    // ::k::Object itself) implicitly (and virtually) inherits from ::k::Object.
    // We inject that base before any resolution so that the rest of the
    // pipeline sees it as a normal base.
    //
    // Interfaces are included (not just classes) so that ::k::Object — and in
    // particular its virtual destructor at vtable slot 0 — is reachable via the
    // primary vtable inheritance chain from ANY root type, including abstract
    // interfaces with no by-value representation (e.g. ::k::Iterator<T>,
    // ::k::Sequence<T>). Since K's inheritance is always virtual, a class that
    // implements several such interfaces still gets exactly one shared Object
    // sub-object (existing virtual-base deduplication handles this).
    //
    // The injection only triggers when k::Object is actually reachable:
    //   - the current compilation unit defines it (module k), or
    //   - it is available through an imported module (import k;).
    // In standalone test compilations that do not import k, the injection is skipped.
    {
        bool object_available = is_type_available(_unit, "Object");

        if (object_available) {
            std::function<void(const std::vector<std::shared_ptr<element>>&)> inject_implicit_object;
            inject_implicit_object = [&](const std::vector<std::shared_ptr<element>>& children) {
                for (auto& child : children) {
                    if (auto kl = std::dynamic_pointer_cast<klass>(child)) {
                        // Both classes and interfaces get implicit Object inheritance
                        // when they have no declared base of their own.
                        if (!kl->has_bases() && !is_k_object(*kl)) {
                            kl->add_base("Object", PUBLIC);
                        }
                        // Recurse into nested aggregates
                        inject_implicit_object(kl->get_children());
                    } else if (auto st = std::dynamic_pointer_cast<aggregate>(child)) {
                        inject_implicit_object(st->get_children());
                    } else if (auto nspace = std::dynamic_pointer_cast<ns>(child)) {
                        inject_implicit_object(nspace->get_children());
                    }
                }
            };
            inject_implicit_object(root_ns->get_children());
        }
    }

    // ── Pre-pass 0b: implicit Annotation inheritance ─────────────────────────────
    // Every annotation_type that has no declared base classes (and is not
    // ::k::Annotation itself) implicitly inherits from ::k::Annotation.
    {
        bool annotation_available = is_type_available(_unit, "Annotation");

        if (annotation_available) {
            std::function<void(const std::vector<std::shared_ptr<element>>&)> inject_implicit_annotation;
            inject_implicit_annotation = [&](const std::vector<std::shared_ptr<element>>& children) {
                for (auto& child : children) {
                    if (auto ann = std::dynamic_pointer_cast<annotation_type>(child)) {
                        if (!ann->has_bases() && !is_k_annotation(*ann)) {
                            ann->add_base("Annotation", PUBLIC);
                        }
                        inject_implicit_annotation(ann->get_children());
                    } else if (auto st = std::dynamic_pointer_cast<aggregate>(child)) {
                        inject_implicit_annotation(st->get_children());
                    } else if (auto nspace = std::dynamic_pointer_cast<ns>(child)) {
                        inject_implicit_annotation(nspace->get_children());
                    }
                }
            };
            inject_implicit_annotation(root_ns->get_children());
        }
    }

    // ── Pre-pass 0c: Application class synthesis / detection ─────────────────────
    // Only for non-k modules where ::k::Application is available:
    {
        // Skip if this IS module k (Application is defined here, not consumed)
        bool is_module_k = (root_ns->get_short_name() == "k");
        bool app_base_available = !is_module_k && is_type_available(_unit, "Application");

        if (app_base_available) {
            auto existing_app = std::dynamic_pointer_cast<klass>(root_ns->get_aggregate("Application"));
            auto main_funcs   = root_ns->get_functions("main");

            if (existing_app) {
                // Phase 3: user explicitly declared class Application in this module.
                _unit._application_class            = existing_app;
                _unit._application_class_synthesized = false;
            } else if (!main_funcs.empty()) {
                // Phase 2b: synthesise a private Application class and move all main
                // overloads into it.
                auto app_class = root_ns->define_class("Application");
                app_class->set_visibility(PRIVATE);
                app_class->add_base("k::Application", PUBLIC);

                for (auto& mfn : main_funcs) {
                    root_ns->remove_function(mfn);
                    element::set_parent(app_class, mfn);
                    app_class->add_existing_function(mfn);
                }

                _unit._application_class            = app_class;
                _unit._application_class_synthesized = true;
            }
        }
    }

    // ── Pre-pass 1: resolve base names for diamond detection ────────────────────
    // We need to call compute_virtual_bases() BEFORE visit_namespace() so that
    // is_virtual is set correctly before sub-objects are injected.
    // This pre-pass only sets base_spec::base pointers (name resolution),
    // without performing any injection or other work.
    {
        std::function<void(const std::vector<std::shared_ptr<element>>&)> prepass;
        prepass = [&](const std::vector<std::shared_ptr<element>>& children) {
            for (auto& child : children) {
                if (auto st = std::dynamic_pointer_cast<aggregate>(child)) {
                    // Resolve nested aggregates first (depth-first, matches visit_structure order)
                    prepass(st->get_children());
                    // Resolve base names for this aggregate
                    for (auto& bs : st->get_bases_mutable()) {
                        if (!bs.base) { // not yet resolved
                            // Skip template base names (e.g. "Collection<T>") —
                            // they require type resolution and will be handled in visit_structure.
                            if (bs.raw_name.find('<') != std::string::npos) continue;
                            // Use the qualified/imported-aware lookup so that diamond
                            // detection sees bases declared with a namespace-qualified
                            // name (e.g. "k::Object") or reachable only through an
                            // imported KDI module — not just simple local names.
                            auto base_st = scope_lookup::lookup_structure_or_import(
                                _unit, _context, st->shared_as<element>(), bs.raw_name);
                            if (base_st) bs.base = base_st;
                            // Errors (not found, final, cross-type) will be caught properly in visit_structure
                        }
                    }
                } else if (auto nspace = std::dynamic_pointer_cast<ns>(child)) {
                    prepass(nspace->get_children());
                }
            }
        };
        prepass(_unit.get_root_namespace()->get_children());

        // Now compute virtual bases (diamond detection) on the fully pre-resolved graph
        std::vector<std::shared_ptr<aggregate>> all_structs;
        std::function<void(const std::vector<std::shared_ptr<element>>&)> collect;
        collect = [&](const std::vector<std::shared_ptr<element>>& children) {
            for (auto& child : children) {
                if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
                    all_structs.push_back(agg);
                    collect(agg->get_children());
                } else if (auto nspace = std::dynamic_pointer_cast<ns>(child)) {
                    collect(nspace->get_children());
                }
            }
        };
        collect(_unit.get_root_namespace()->get_children());
        klass::compute_virtual_bases(all_structs);

        // ── Generic-level template diamond detection ─────────────────────────
        // compute_virtual_bases() above only sees resolved (non-'<') base edges,
        // so diamonds that exist purely within a template hierarchy — e.g.
        // Vector<T> : IndexedCollection<T>, MutableCollection<T> both deriving
        // Collection<T> — are invisible to it. Detect them here by walking base
        // raw_names among the generic template definitions and mark the matching
        // generic base_spec edges virtual, so every future instantiation is born
        // (via clone in template_instantiator) with the correct is_virtual flags
        // BEFORE any on-demand layout materialisation. Only class/interface
        // templates participate (structs never use virtual bases).
        {
            auto simple_name = [](const std::string& raw) -> std::string {
                std::string r = raw;
                if (auto lt = r.find('<'); lt != std::string::npos) r = r.substr(0, lt);
                while (!r.empty() && r.back() == ' ') r.pop_back();
                if (auto cc = r.rfind("::"); cc != std::string::npos) r = r.substr(cc + 2);
                return r;
            };
            std::unordered_map<std::string, aggregate*> tpl_by_name;
            for (auto& agg : all_structs) {
                if (!agg->is_template()) continue;
                if (!(agg->is_class() || agg->is_interface())) continue;
                tpl_by_name[agg->get_short_name()] = agg.get();
            }
            auto lookup = [&](const std::string& raw) -> aggregate* {
                auto it = tpl_by_name.find(simple_name(raw));
                return it != tpl_by_name.end() ? it->second : nullptr;
            };
            std::function<void(aggregate*, std::unordered_map<aggregate*,int>&,
                               std::unordered_set<aggregate*>&)> count_bases;
            count_bases = [&](aggregate* cur, std::unordered_map<aggregate*,int>& counts,
                              std::unordered_set<aggregate*>& on_path) {
                if (on_path.count(cur)) return; // guard against inheritance cycles
                on_path.insert(cur);
                for (auto& bs : cur->get_bases()) {
                    aggregate* base = lookup(bs.raw_name);
                    if (!base) continue;
                    counts[base]++;
                    count_bases(base, counts, on_path);
                }
                on_path.erase(cur);
            };
            std::function<void(aggregate*, const std::unordered_set<aggregate*>&,
                               std::unordered_set<aggregate*>&)> mark_edges;
            mark_edges = [&](aggregate* cur, const std::unordered_set<aggregate*>& diamonds,
                             std::unordered_set<aggregate*>& visited) {
                if (visited.count(cur)) return;
                visited.insert(cur);
                for (auto& bs : cur->get_bases_mutable()) {
                    aggregate* base = lookup(bs.raw_name);
                    if (!base) continue;
                    if (diamonds.count(base)) {
                        bs.is_virtual = true;
                    } else {
                        mark_edges(base, diamonds, visited);
                    }
                }
            };
            for (auto& agg : all_structs) {
                if (!agg->is_template()) continue;
                if (!(agg->is_class() || agg->is_interface())) continue;
                std::unordered_map<aggregate*,int> counts;
                std::unordered_set<aggregate*> on_path;
                count_bases(agg.get(), counts, on_path);
                std::unordered_set<aggregate*> diamonds;
                for (auto& [b, c] : counts) if (c > 1) diamonds.insert(b);
                if (diamonds.empty()) continue;
                std::unordered_set<aggregate*> visited;
                mark_edges(agg.get(), diamonds, visited);
            }
        }
    }

    // ── Pre-pass 1b: Application class validation (Phase 3) ──────────────────
    // Runs after Pre-pass 1 has resolved base_spec::base pointers, so
    // is_derived_from() below sees the fully-resolved base graph.
    if (_unit._application_class && !_unit._application_class_synthesized) {
        auto app_class = _unit._application_class;
        lex::opt_any_lexeme app_lexeme;
        if (auto ast_ad = app_class->get_ast_aggregate_decl()) app_lexeme = lex::any_lexeme{ast_ad->name};

        if (app_class->is_abstract()) {
            auto d = k::log::diagnostic::make_error(
                static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_MUST_NOT_BE_ABSTRACT),
                "'class Application' must not be abstract: it is instantiated directly as the "
                "application's entry-point object", {});
            if (app_lexeme) d.at(*app_lexeme);
            logger_relay::report(d);
            throw resolution_error(std::move(d));
        }

        auto k_application = std::dynamic_pointer_cast<aggregate>(
            scope_lookup::lookup_structure_or_import(_unit, _context, app_class->shared_as<element>(), "k::Application"));
        if (!k_application || !app_class->is_derived_from(k_application)) {
            auto d = k::log::diagnostic::make_error(
                static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_MUST_EXTEND_K_APPLICATION),
                "'class Application' must (directly or transitively) extend '::k::Application'", {});
            if (app_lexeme) d.at(*app_lexeme);
            logger_relay::report(d);
            throw resolution_error(std::move(d));
        }

        // ── Phase 4: walk the ::k::Application abstract-class chain ──────────
        // Build the chain from the outermost abstract class deriving from
        // ::k::Application (exclusive) down to the final concrete `app_class`.
        auto chain = build_application_chain(app_class, k_application);
        // build_application_chain always includes at least app_class itself.

        auto lexeme_of = [](const std::shared_ptr<klass>& k) -> lex::opt_any_lexeme {
            lex::opt_any_lexeme lx;
            if (auto ast_ad = k->get_ast_aggregate_decl()) lx = lex::any_lexeme{ast_ad->name};
            return lx;
        };

        auto raise = [&](unsigned code, const lex::opt_any_lexeme& lx,
                          const std::string& msg, const std::vector<std::string>& args = {}) {
            auto d = k::log::diagnostic::make_error(code, msg, args);
            if (lx) d.at(*lx);
            logger_relay::report(d);
            throw resolution_error(std::move(d));
        };

        std::shared_ptr<function> chain_entry_main;   // topmost declared main (codegen target)
        std::shared_ptr<function> required_main;      // signature still needed at current chain position
        bool decided = false;

        for (size_t i = 0; i < chain.size(); ++i) {
            auto& level = chain[i];
            bool is_final_level = (i + 1 == chain.size());
            auto own_mains = level->get_functions("main");

            if (!decided) {
                if (own_mains.empty()) continue; // pure pass-through, keep looking

                std::vector<std::shared_ptr<function>> stds, customs;
                for (auto& f : own_mains) {
                    if (classify_main_signature(f) != std_main_sig::NONE_STD) stds.push_back(f);
                    else customs.push_back(f);
                }
                std::vector<std::shared_ptr<function>> active_stds;
                for (auto& f : stds) if (!f->is_deleted()) active_stds.push_back(f);

                if (active_stds.size() != 1) {
                    raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_BAD_ACTIVE_MAIN_COUNT),
                        lexeme_of(level),
                        "class '{}' must leave exactly one of the four standard 'main' signatures "
                        "non-deleted to decide the application entry point; found {}",
                        {level->get_short_name(), std::to_string(active_stds.size())});
                }
                auto entry_std = active_stds.front();
                chain_entry_main = entry_std;

                if (main_is_implemented(entry_std)) {
                    // Delegating implementation: must pair with exactly one custom abstract main.
                    if (customs.size() != 1) {
                        raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_BAD_DELEGATE_COUNT),
                            lexeme_of(level),
                            "class '{}' implements '{}' as a delegating entry point, so it must also "
                            "declare exactly one custom abstract 'main' to delegate to; found {}",
                            {level->get_short_name(), main_sig_to_string(entry_std), std::to_string(customs.size())});
                    }
                    auto delegate = customs.front();
                    if (delegate->is_deleted() || !delegate->is_abstract_func()) {
                        raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_DELEGATE_NOT_ABSTRACT),
                            lexeme_of(level),
                            "class '{}': the custom 'main' delegation target '{}' must be declared 'abstract' "
                            "(no body, not deleted)",
                            {level->get_short_name(), main_sig_to_string(delegate)});
                    }
                    required_main = delegate;
                } else {
                    // entry_std left abstract: it becomes the required override for the next level.
                    if (!customs.empty()) {
                        raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_UNEXPECTED_MAIN),
                            lexeme_of(level),
                            "class '{}' leaves '{}' abstract (non-delegating); it must not also declare "
                            "custom 'main' overload(s)",
                            {level->get_short_name(), main_sig_to_string(entry_std)});
                    }
                    required_main = entry_std;
                }
                decided = true;
                if (is_final_level && !main_is_implemented(required_main)) {
                    raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_FINAL_MAIN_NOT_IMPLEMENTED),
                        app_lexeme,
                        "'class Application' must implement the required entry-point method '{}'",
                        {main_sig_to_string(required_main)});
                }
                if (is_final_level) { required_main = nullptr; }
                continue;
            }

            // required_main is set: look for its override at this level.
            std::shared_ptr<function> matching;
            std::vector<std::shared_ptr<function>> others;
            for (auto& f : own_mains) {
                if (same_main_shape(f, required_main)) matching = f;
                else others.push_back(f);
            }
            if (!matching) {
                if (!others.empty()) {
                    raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_UNEXPECTED_MAIN),
                        lexeme_of(level),
                        "class '{}' declares 'main' overload(s) that do not match the entry signature "
                        "'{}' required by an outer class in the ::k::Application chain",
                        {level->get_short_name(), main_sig_to_string(required_main)});
                }
                if (is_final_level) {
                    raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_FINAL_MAIN_NOT_IMPLEMENTED),
                        app_lexeme,
                        "'class Application' must implement the required entry-point method '{}'",
                        {main_sig_to_string(required_main)});
                }
                continue; // pass-through
            }
            if (matching->is_deleted()) {
                raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_REQUIRED_MAIN_DELETED),
                    lexeme_of(level),
                    "class '{}' marks the required entry-point method '{}' as deleted; it cannot be "
                    "deleted once selected by an outer class",
                    {level->get_short_name(), main_sig_to_string(required_main)});
            }
            if (!main_is_implemented(matching)) {
                if (!others.empty()) {
                    raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_UNEXPECTED_MAIN),
                        lexeme_of(level),
                        "class '{}' re-declares '{}' abstract; it must not also declare custom 'main' overload(s)",
                        {level->get_short_name(), main_sig_to_string(matching)});
                }
                if (is_final_level) {
                    raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_FINAL_MAIN_NOT_IMPLEMENTED),
                        app_lexeme,
                        "'class Application' must implement the required entry-point method '{}'",
                        {main_sig_to_string(required_main)});
                }
                continue; // still abstract, keep looking further down
            }
            // matching is implemented.
            if (is_final_level) {
                required_main = nullptr; // resolved
                continue;
            }
            // Intermediate class implements it: must further delegate via exactly one new abstract main.
            if (others.size() != 1) {
                raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_BAD_DELEGATE_COUNT),
                    lexeme_of(level),
                    "class '{}' implements '{}' as a delegating entry point, so it must also declare "
                    "exactly one new custom abstract 'main' to continue the chain; found {}",
                    {level->get_short_name(), main_sig_to_string(matching), std::to_string(others.size())});
            }
            auto new_delegate = others.front();
            if (new_delegate->is_deleted() || !new_delegate->is_abstract_func()) {
                raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_CHAIN_DELEGATE_NOT_ABSTRACT),
                    lexeme_of(level),
                    "class '{}': the custom 'main' delegation target '{}' must be declared 'abstract' "
                    "(no body, not deleted)",
                    {level->get_short_name(), main_sig_to_string(new_delegate)});
            }
            required_main = new_delegate;
        }

        if (!decided) {
            raise(static_cast<unsigned int>(k::diag::application_diag::ERR_APPLICATION_NO_USABLE_MAIN),
                app_lexeme,
                "'class Application' must declare exactly one usable 'main' method", {});
        }

        _unit._application_entry_main = chain_entry_main;
        // The entry call must go through virtual dispatch iff the topmost
        // declared entry-point 'main' is itself abstract (its real body lives
        // in a subclass further down the chain). If it has a body directly
        // (the "delegating implementation" case), a direct call is correct:
        // the body's own internal calls to the abstract delegate already
        // dispatch virtually per normal language semantics.
        _unit._application_entry_main_is_virtual = !main_is_implemented(chain_entry_main);
    }

    // Step 2: Visit the root namespace (recursively resolves all symbols)
    visit_namespace(*_unit.get_root_namespace());

    // Step 3: Visit global constructor, destructor, and main functions
    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());
}

void type_reference_resolver::visit_unit(unit& unit)
{
    trace("[type_reference_resolver::visit_unit] begin");
    visit_namespace(*_unit.get_root_namespace());

    // Compute unified initialization/finalization order over all static constructors
    // and global variables, resolving cross-dependencies.
    init_order_resolver order_resolver(_log, _context, _unit);
    order_resolver.resolve();

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    std::shared_ptr<function> main_fn = unit.get_root_namespace()->get_function("main");
    if (!main_fn && unit._application_class) {
        // main() may have been moved into the (synthesised or user-declared)
        // Application class — look it up there instead.
        // Phase 4: if the Application class sits atop an abstract
        // ::k::Application chain, use the chain-derived entry point (the
        // topmost declared standard 'main' overload) instead of blindly
        // picking the first function named "main" directly on the final
        // class (which, in a chain, is very likely a different, non-entry
        // overload).
        main_fn = unit._application_entry_main
            ? unit._application_entry_main
            : unit._application_class->get_function("main");
    }
    if (main_fn) {
        if (auto main_func = unit.generate_main_function(main_fn)) {
            visit_global_main_function(*main_func);
        }
    }
}

/**
 * First-pass IR generation: emit all declarations (globals, functions, vtables).
 *
 * Steps:
 *   1. Set LLVM module metadata (target triple, data layout).
 *   2. Visit root namespace to emit all function/variable declarations.
 *   3. Emit vtable stubs for polymorphic classes.
 *   4. Emit redirect aliases for forwarded functions.
 *   5. Emit global constructor/destructor/main function declarations.
 */
void declaration_generator::visit_unit(unit &unit) {
    trace("[declaration_generator::visit_unit] begin");
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (unit._global_main_func) {
        visit_global_main_function(*unit._global_main_func);
    }

    // Step 1: Set LLVM module metadata (target triple, data layout)
    // ── Emit LLVM declarations for all imported entities ──────────────────
    // Strategy: if the entity has a non-empty llvm_def we parse it directly
    // via context::declare_llvm_function_from_def — this guarantees ABI
    // fidelity regardless of how the K type system maps to LLVM types.
    // Fallback to the old visit_function() path when llvm_def is absent.

    auto declare_imported_fn = [&](const std::shared_ptr<function>& fn,
                                   const std::string& llvm_def,
                                   const std::string& mangled) {
        if (_context->lookup_llvm_function(fn)) return; // already declared
        if (!llvm_def.empty() && !mangled.empty()) {
            auto* llvm_fn = _context->declare_llvm_function_from_def(llvm_def, mangled);
            if (llvm_fn) {
                _context->_functions[fn] = llvm_fn;
                return;
            }
        }
        // Fallback: derive signature from K types (old path)
        fn->accept(*this);
    };

    // imported_function
    for (const auto& [mangled, fn] : unit.get_imported_functions()) {
        const auto* kfn = fn->get_kdi_function();
        declare_imported_fn(fn,
            kfn ? kfn->llvm_def : "",
            kfn ? kfn->mangled_name : mangled);
    }

    // imported_aggregate: constructors, destructor, methods
    for (const auto& [fq, agg] : unit.get_imported_aggregates()) {
        const auto* kdi_agg = agg->get_kdi_aggregate();

        // Constructors
        for (size_t i = 0; i < agg->constructors().size(); ++i) {
            const auto& ic = agg->constructors()[i];
            std::string def, mng;
            if (kdi_agg && i < kdi_agg->constructors.size()) {
                def = kdi_agg->constructors[i].llvm_def;
                mng = kdi_agg->constructors[i].mangled_name;
            }
            declare_imported_fn(ic, def, mng);
        }
        // Destructor
        if (auto id = agg->get_destructor()) {
            std::string def, mng;
            if (kdi_agg && kdi_agg->destructor.has_value()) {
                def = kdi_agg->destructor->llvm_def;
                mng = kdi_agg->destructor->mangled_name;
            }
            declare_imported_fn(id, def, mng);
        }
        // Methods
        size_t method_idx = 0;
        for (const auto& child : agg->get_children()) {
            if (auto im = std::dynamic_pointer_cast<imported_method>(child)) {
                const kdi::kdi_method* km = im->get_kdi_method();
                declare_imported_fn(im,
                    km ? km->llvm_def : "",
                    km ? km->mangled_name : "");
                ++method_idx;
            }
        }
    }

    // imported_variable — emit ExternalLinkage GlobalVariable with no initialiser
    for (const auto& [mangled, var] : unit.get_imported_variables()) {
        if (!var || !var->get_type()) continue;
        auto llvm_type = _context->get_llvm_type(var->get_type());
        if (!llvm_type) continue;
        if (_context->_module->getGlobalVariable(var->get_mangled_name())) continue;
        auto* gv = new llvm::GlobalVariable(
            *_context->_module,
            llvm_type,
            /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            /*Initializer=*/nullptr,
            var->get_mangled_name());
        _context->_global_vars.insert({var, gv});
    }

    // Step 2: Visit root namespace to emit all function/variable declarations
    // ── Emit GlobalAlias for redirected functions ─────────────────────────
    // After all normal function declarations are emitted, create LLVM
    // GlobalAlias entries for redirected functions pointing to their
    // resolved targets.
    emit_redirect_aliases(*_unit.get_root_namespace());

    // Step 3: Emit vtable stubs for polymorphic classes
    // ── Emit Unit RTTI global (::k::Unit instance) ──────────────────────────
    // Only emit when libk is available (Unit is a class from module k).
    // Layout: { ptr __vptr__, ptr __vptr_Object__, ptr name, ptr fullName, ptr functions }
    // K implicitly adds Object as a base class for all classes, so Unit
    // has an Object sub-object at field 1.
    // All fields except name and fullName are null placeholders; patched in
    // implementation_generator::visit_unit.
    {
        bool has_libk = _unit.find_import(k::name("k")) != nullptr;
        if (!has_libk) {
            for (const auto& tdep : _unit.get_transitive_kdis()) {
                if (tdep && tdep->header.module_name == "k") { has_libk = true; break; }
            }
        }
        if (has_libk) {
        llvm::LLVMContext& llvm_ctx = **_context;
        llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
        llvm::Type* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);

        // Step 4: Emit redirect aliases for forwarded functions
        std::string unit_rtti_struct_name = "__rtti_unit_" + _unit.get_unit_name().to_string() + "__";
        std::vector<llvm::Type*> unit_rtti_fields = {
            ptr_ty,     // __vptr__
            ptr_ty,     // __vptr_Object__ (Object base sub-object)
            ptr_ty,     // name
            ptr_ty,     // fullName
            ptr_ty      // functions
        };
        llvm::StructType* unit_rtti_llvm_type = llvm::StructType::create(
            llvm_ctx, unit_rtti_fields, unit_rtti_struct_name);

        // Step 5: Emit global constructor/destructor/main function declarations
        std::string unit_rtti_name = mangler::mangle_rtti_unit(_unit.get_unit_name());

        // Helper: emit a K-sized-array string constant { i32 size, [N x i8] data }.
        auto make_name_gv = [&](const std::string& str, const std::string& suffix) -> llvm::Constant* {
            uint32_t len = static_cast<uint32_t>(str.size() + 1);
            // char is UTF-32: emit the name as [N x i32] code points (ASCII identifiers).
            std::vector<uint32_t> name_u32;
            for (unsigned char name_ch : str) name_u32.push_back(name_ch);
            name_u32.push_back(0); // null terminator
            llvm::Constant* str_data = llvm::ConstantDataArray::get(llvm_ctx, name_u32);
            llvm::StructType* str_struct_ty = llvm::StructType::get(
                llvm_ctx, {i32_ty, str_data->getType()}, /*isPacked=*/false);
            llvm::Constant* str_struct_init = llvm::ConstantStruct::get(
                str_struct_ty,
                {llvm::ConstantInt::get(i32_ty, len), str_data});
            auto* gv = new llvm::GlobalVariable(
                _context->module(), str_struct_ty,
                true, llvm::GlobalValue::PrivateLinkage,
                str_struct_init, unit_rtti_name + suffix);
            gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            return gv;
        };

        std::string unit_short_name = _unit.get_unit_name().to_string();
        std::string unit_fq_name = "::" + unit_short_name;

        llvm::Constant* name_cstr = make_name_gv(unit_short_name, "_name");
        llvm::Constant* fullname_cstr = make_name_gv(unit_fq_name, "_fullname");

        llvm::Constant* null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
        std::vector<llvm::Constant*> unit_rtti_init = {
            null_ptr,       // field 0: __vptr__         → patched later
            null_ptr,       // field 1: __vptr_Object__  (null — no Object dispatch needed)
            name_cstr,      // field 2: name
            fullname_cstr,  // field 3: fullName
            null_ptr        // field 4: functions        → patched later
        };
        llvm::Constant* unit_rtti_const = llvm::ConstantStruct::get(unit_rtti_llvm_type, unit_rtti_init);
        new llvm::GlobalVariable(
            _context->module(), unit_rtti_llvm_type,
            /*isConstant=*/false,  // mutable for patching, made constant after
            llvm::GlobalValue::ExternalLinkage,
            unit_rtti_const, unit_rtti_name);
        }  // if (has_libk)
    }
}

void declaration_generator::emit_redirect_aliases(ns& nspc) {
    for (auto& child : nspc.get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            emit_redirect_alias(*fn);
        } else if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
            emit_redirect_aliases_from_aggregate(*agg);
        } else if (auto child_ns = std::dynamic_pointer_cast<ns>(child)) {
            emit_redirect_aliases(*child_ns);
        }
    }
}

void declaration_generator::emit_redirect_aliases_from_aggregate(aggregate& agg) {
    for (auto& fn : agg.functions()) {
        emit_redirect_alias(*fn);
    }
    // Recurse into nested aggregates
    for (auto& [name, nested] : agg.aggregates()) {
        emit_redirect_aliases_from_aggregate(*nested);
    }
}

void declaration_generator::emit_redirect_alias(function& fn) {
    if (!fn.is_redirected()) return;
    auto target = fn.get_redirect_target();
    if (!target) return;

    // Find the LLVM function for the target
    auto target_it = _context->_functions.find(target);
    if (target_it == _context->_functions.end()) {
        // Target not yet declared — shouldn't happen after full declaration pass
        return;
    }
    llvm::Function* target_llvm = target_it->second;

    // Create GlobalAlias with the redirector's mangled name pointing to the target
    auto* alias = llvm::GlobalAlias::create(
        target_llvm->getValueType(),
        target_llvm->getAddressSpace(),
        llvm::GlobalValue::ExternalLinkage,
        fn.get_mangled_name(),
        target_llvm,
        _context->_module.get());

    // Register in the context so the alias can be found by symbol name
    _context->_functions.insert({fn.shared_as<k::model::function>(), target_llvm});
    (void)alias; // alias is owned by the module
}

/**
 * Second-pass IR generation: emit all function implementations and global initializers.
 *
 * Steps:
 *   1. Visit root namespace to emit all function bodies.
 *   2. Fill vtables with resolved function pointers.
 *   3. Emit global variable initializers.
 *   4. Emit global constructor, destructor, and main function bodies.
 */
void implementation_generator::visit_unit(unit &unit) {
    trace("[implementation_generator::visit_unit] begin");
    // Step 1: Visit root namespace to emit all function bodies
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (unit._global_main_func) {
        visit_global_main_function(*unit._global_main_func);
    }

    // ── Patch Unit RTTI global with Function descriptors ────────────────────
    // Collect all public non-member functions from the unit, emit Function RTTI
    // globals for each, and patch the Unit RTTI global.
    {
        std::string unit_rtti_name = mangler::mangle_rtti_unit(_unit.get_unit_name());
        auto* unit_rtti_gv = _context->module().getNamedGlobal(unit_rtti_name);
        if (unit_rtti_gv) {
            llvm::LLVMContext& llvm_ctx = **_context;
            llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
            llvm::Type* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
            llvm::Constant* null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));

            // Look up ::k::Unit vtable symbol
            std::string unit_vtable_name = "_KTVN1k4UnitE";
            llvm::Constant* unit_vt = _context->module().getNamedGlobal(unit_vtable_name);
            if (!unit_vt) {
                bool has_libk = _unit.find_import(k::name("k")) != nullptr;
                if (!has_libk) {
                    for (const auto& tdep : _unit.get_transitive_kdis()) {
                        if (tdep && tdep->header.module_name == "k") { has_libk = true; break; }
                    }
                }
                if (has_libk) {
                    unit_vt = new llvm::GlobalVariable(
                        _context->module(), ptr_ty,
                        true, llvm::GlobalValue::ExternalLinkage,
                        nullptr, unit_vtable_name);
                }
            }

            // Look up ::k::Function vtable symbol
            std::string func_vtable_name = "_KTVN1k8FunctionE";
            llvm::Constant* func_vt = _context->module().getNamedGlobal(func_vtable_name);
            if (!func_vt) {
                bool has_libk = _unit.find_import(k::name("k")) != nullptr;
                if (!has_libk) {
                    for (const auto& tdep : _unit.get_transitive_kdis()) {
                        if (tdep && tdep->header.module_name == "k") { has_libk = true; break; }
                    }
                }
                if (has_libk) {
                    func_vt = new llvm::GlobalVariable(
                        _context->module(), ptr_ty,
                        true, llvm::GlobalValue::ExternalLinkage,
                        nullptr, func_vtable_name);
                }
            }

            llvm::Constant* func_vt_or_null = func_vt ? func_vt : null_ptr;

            // Look up ::k::Parameter vtable symbol
            std::string param_vtable_name = "_KTVN1k9ParameterE";
            llvm::Constant* param_vt = _context->module().getNamedGlobal(param_vtable_name);
            if (!param_vt) {
                bool has_libk = _unit.find_import(k::name("k")) != nullptr;
                if (!has_libk) {
                    for (const auto& tdep : _unit.get_transitive_kdis()) {
                        if (tdep && tdep->header.module_name == "k") { has_libk = true; break; }
                    }
                }
                if (has_libk) {
                    param_vt = new llvm::GlobalVariable(
                        _context->module(), ptr_ty,
                        true, llvm::GlobalValue::ExternalLinkage,
                        nullptr, param_vtable_name);
                }
            }
            llvm::Constant* param_vt_or_null = param_vt ? param_vt : null_ptr;

            // Helper: emit a Parameter RTTI global for a single parameter
            auto make_param_rtti = [&](const std::shared_ptr<k::model::parameter>& param,
                                       const std::string& prefix, size_t idx) -> llvm::Constant* {
                std::string param_name_str = param->get_short_name();
                uint32_t len = static_cast<uint32_t>(param_name_str.size() + 1);
                // char is UTF-32: emit the name as [N x i32] code points (ASCII identifiers).
            std::vector<uint32_t> name_u32;
            for (unsigned char name_ch : param_name_str) name_u32.push_back(name_ch);
            name_u32.push_back(0); // null terminator
            llvm::Constant* str_data = llvm::ConstantDataArray::get(llvm_ctx, name_u32);
                llvm::StructType* str_struct_ty = llvm::StructType::get(
                    llvm_ctx, {i32_ty, str_data->getType()}, /*isPacked=*/false);
                llvm::Constant* str_struct_init = llvm::ConstantStruct::get(
                    str_struct_ty,
                    {llvm::ConstantInt::get(i32_ty, len), str_data});
                auto* name_gv = new llvm::GlobalVariable(
                    _context->module(), str_struct_ty,
                    true, llvm::GlobalValue::PrivateLinkage,
                    str_struct_init, prefix + "_param" + std::to_string(idx) + "_name");
                name_gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

                // Parameter struct: { ptr vptr, ptr vptr_Object, ptr name, ptr annotations }
                llvm::StructType* param_rtti_type = llvm::StructType::get(
                    llvm_ctx, {ptr_ty, ptr_ty, ptr_ty, ptr_ty}, /*isPacked=*/false);
                std::vector<llvm::Constant*> param_init = {
                    param_vt_or_null,  // __vptr__ (Parameter primary vtable)
                    null_ptr,          // __vptr_Object__ (null)
                    name_gv,           // name
                    null_ptr           // annotations (TODO: materialize when build_annotation_instance_constant is a shared utility)
                };
                llvm::Constant* param_const = llvm::ConstantStruct::get(param_rtti_type, param_init);
                auto* param_gv = new llvm::GlobalVariable(
                    _context->module(), param_rtti_type,
                    /*isConstant=*/true,
                    llvm::GlobalValue::PrivateLinkage,
                    param_const, prefix + "_param" + std::to_string(idx));
                param_gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                return param_gv;
            };

            // Helper: emit a Function RTTI global for a free function
            auto make_func_rtti = [&](const std::shared_ptr<k::model::function>& fn) -> llvm::Constant* {
                std::string fn_rtti_name = mangler::mangle_rtti_function(fn->get_name());

                auto make_name_gv_fn = [&](const std::string& str, const std::string& suffix) -> llvm::Constant* {
                    uint32_t len = static_cast<uint32_t>(str.size() + 1);
                    // char is UTF-32: emit the name as [N x i32] code points (ASCII identifiers).
            std::vector<uint32_t> name_u32;
            for (unsigned char name_ch : str) name_u32.push_back(name_ch);
            name_u32.push_back(0); // null terminator
            llvm::Constant* str_data = llvm::ConstantDataArray::get(llvm_ctx, name_u32);
                    llvm::StructType* str_struct_ty = llvm::StructType::get(
                        llvm_ctx, {i32_ty, str_data->getType()}, /*isPacked=*/false);
                    llvm::Constant* str_struct_init = llvm::ConstantStruct::get(
                        str_struct_ty,
                        {llvm::ConstantInt::get(i32_ty, len), str_data});
                    auto* gv = new llvm::GlobalVariable(
                        _context->module(), str_struct_ty,
                        true, llvm::GlobalValue::PrivateLinkage,
                        str_struct_init, fn_rtti_name + suffix);
                    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                    return gv;
                };

                llvm::Constant* fn_name_gv    = make_name_gv_fn(fn->get_short_name(), "_name");
                llvm::Constant* fn_fullname_gv = make_name_gv_fn(fn->get_fq_name(), "_fullname");

                // Flags: bits 0-1 = visibility (always 0=PUBLIC here), bit 2 = is_static, bit 3 = is_member (0)
                uint32_t fn_flags = 0;  // PUBLIC, not member
                if (fn->is_static()) fn_flags |= 4;
                // is_member = false for free functions (bit 3 = 0)

                // Build parameter RTTI array
                std::vector<llvm::Constant*> fn_param_ptrs;
                {
                    size_t idx = 0;
                    for (auto& p : fn->parameters()) {
                        if (!p) continue;
                        fn_param_ptrs.push_back(make_param_rtti(p, fn_rtti_name, idx));
                        ++idx;
                    }
                }
                llvm::Constant* fn_params_gv = null_ptr;
                if (!fn_param_ptrs.empty()) {
                    uint32_t count = static_cast<uint32_t>(fn_param_ptrs.size());
                    llvm::ArrayType* arr_ty = llvm::ArrayType::get(ptr_ty, count);
                    llvm::StructType* karr_ty = llvm::StructType::get(llvm_ctx, {i32_ty, arr_ty}, /*isPacked=*/false);
                    llvm::Constant* arr_data = llvm::ConstantArray::get(arr_ty, fn_param_ptrs);
                    llvm::Constant* karr_init = llvm::ConstantStruct::get(karr_ty, {
                        llvm::ConstantInt::get(i32_ty, count), arr_data
                    });
                    auto* gv = new llvm::GlobalVariable(
                        _context->module(), karr_ty,
                        true, llvm::GlobalValue::PrivateLinkage,
                        karr_init, fn_rtti_name + "_parameters");
                    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                    fn_params_gv = gv;
                }

                // Build the Function struct: { ptr vptr, ptr vptr_Object, ptr name, ptr fullName, ptr owner, i32 flags, ptr annotations, ptr parameters }
                // K implicitly adds Object as a base class for all classes, so Function
                // has an Object sub-object at field 1 (containing the Object vptr).
                // Note: annotation materialization for free functions will be added when
                // build_annotation_instance_constant is refactored into a shared utility.
                llvm::StructType* fn_rtti_type = llvm::StructType::get(
                    llvm_ctx, {ptr_ty, ptr_ty, ptr_ty, ptr_ty, ptr_ty, i32_ty, ptr_ty, ptr_ty}, /*isPacked=*/false);
                std::vector<llvm::Constant*> fn_init = {
                    func_vt_or_null,                       // __vptr__ (Function primary vtable)
                    null_ptr,                              // __vptr_Object__ (Object sub-object; null — no Object dispatch needed)
                    fn_name_gv,                            // name
                    fn_fullname_gv,                        // fullName
                    null_ptr,                              // owner (null for free functions)
                    llvm::ConstantInt::get(i32_ty, fn_flags),  // flags
                    null_ptr,                              // annotations (TODO: materialize for free functions)
                    fn_params_gv                           // parameters
                };
                llvm::Constant* fn_const = llvm::ConstantStruct::get(fn_rtti_type, fn_init);
                auto* fn_gv = new llvm::GlobalVariable(
                    _context->module(), fn_rtti_type,
                    /*isConstant=*/true,
                    // Private: free-function RTTI reflection descriptors are referenced
                    // only by baked pointers in this module's unit RTTI (never looked up
                    // by mangled name). Module-local linkage avoids strong-symbol
                    // collisions when modules that re-emit the same descriptors are
                    // statically linked (mirrors the member-function descriptors).
                    llvm::GlobalValue::PrivateLinkage,
                    fn_const, fn_rtti_name);
                fn_gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                return fn_gv;
            };

            // Step 2: Fill vtables with resolved function pointers
            // Recursively collect public non-member functions from namespaces
            std::vector<llvm::Constant*> fn_ptrs;
            std::function<void(const k::model::ns&)> collect_ns_functions;
            collect_ns_functions = [&](const k::model::ns& nspc) {
                for (auto& fn : const_cast<k::model::ns&>(nspc).functions()) {
                    if (!fn) continue;
                    if (fn->get_visibility() != PUBLIC) continue;
                    if (fn->is_compiler_generated()) continue;
                    // Only non-member functions (defined at namespace level, not inside aggregates)
                    if (fn->is_member()) continue;

                    // Step 3: Emit global variable initializers
                    llvm::Constant* fn_gv = make_func_rtti(fn);
                    if (fn_gv) fn_ptrs.push_back(fn_gv);
                }
                // Recurse into child namespaces
                for (auto& child : const_cast<k::model::ns&>(nspc).get_children()) {
                    if (auto child_ns = std::dynamic_pointer_cast<k::model::ns>(child)) {
                        collect_ns_functions(*child_ns);
                    }
                }
            };
            collect_ns_functions(*_unit.get_root_namespace());

            // Step 4: Emit global constructor, destructor, and main function bodies
            // Build K-array of function pointers
            llvm::Constant* functions_gv = null_ptr;
            if (!fn_ptrs.empty()) {
                uint32_t count = static_cast<uint32_t>(fn_ptrs.size());
                llvm::ArrayType* arr_ty = llvm::ArrayType::get(ptr_ty, count);
                llvm::StructType* karr_ty = llvm::StructType::get(llvm_ctx, {i32_ty, arr_ty}, /*isPacked=*/false);
                llvm::Constant* arr_data = llvm::ConstantArray::get(arr_ty, fn_ptrs);
                llvm::Constant* karr_init = llvm::ConstantStruct::get(karr_ty, {
                    llvm::ConstantInt::get(i32_ty, count), arr_data
                });
                auto* gv = new llvm::GlobalVariable(
                    _context->module(), karr_ty,
                    true, llvm::GlobalValue::PrivateLinkage,
                    karr_init, unit_rtti_name + "_functions");
                gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                functions_gv = gv;
            }

            // Patch the Unit RTTI global
            auto* rtti_type = llvm::cast<llvm::StructType>(unit_rtti_gv->getValueType());
            auto* old_init = unit_rtti_gv->getInitializer();
            auto* old_struct = llvm::cast<llvm::ConstantStruct>(old_init);

            llvm::Constant* vptr_field = unit_vt ? unit_vt : old_struct->getOperand(0);

            std::vector<llvm::Constant*> new_unit_rtti_init = {
                vptr_field,                    // field 0: __vptr__
                old_struct->getOperand(1),     // field 1: __vptr_Object__ (keep null)
                old_struct->getOperand(2),     // field 2: name (keep as-is)
                old_struct->getOperand(3),     // field 3: fullName (keep as-is)
                functions_gv                   // field 4: functions
            };

            unit_rtti_gv->setInitializer(llvm::ConstantStruct::get(rtti_type, new_unit_rtti_init));
            unit_rtti_gv->setConstant(true);
        }
    }
}

//
// Namespace
//

void symbol_resolver::visit_namespace(ns& ns)
{
    trace("[symbol_resolver::visit_namespace] '{}'", {ns.get_short_name()});
    if (ns.get_fq_name().empty()) {
        if (ns.is_root()) {
            // Root namespace
            // Should not happen, supposed to be handled at model construction level
            if (ns.get_name().empty()) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F001), std::nullopt,
                    "Internal error: root namespace has no name at code generation stage; "
                    "this should not happen and indicates a compiler bug");
            } else {
                ns.assign_name(ns.get_name().with_root_prefix());
            }
        } else {
            if (ns.get_short_name().empty()) {
                // TODO correctly handle unnamed namespaces
            } else {
                ns.assign_name(ns.parent<model::ns>()->get_name().with_back(ns.get_short_name()));
            }
        }
    }

    check_alias_declarations(ns, ns);

    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        ns.get_children()[i]->accept(*this);
    }

}

//
// signature_resolver — namespace-level entry points
// (element-specific methods are in gen_struct.cpp, gen_class.cpp, gen_function.cpp)
//

void signature_resolver::resolve_signatures(ns& ns) {
    visit_namespace(ns);
}

void signature_resolver::visit_namespace(ns& ns) {
    // Only visit aggregate children — free functions don't need the
    // signature pre-pass because they don't cause cross-type forward references.
    //
    // Exception: when the namespace declares aliases, free-function signatures
    // are pre-resolved too, so that overloads differing only by a typedef are
    // diagnosed before any call site is resolved (which would otherwise report
    // a plain ambiguity instead of the dedicated diagnostic).
    const bool visit_free_functions = !ns.get_aliases().empty();
    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        auto& child = ns.get_children()[i];
        if (std::dynamic_pointer_cast<aggregate>(child)
            || (visit_free_functions && std::dynamic_pointer_cast<function>(child))) {
            child->accept(*this);
        }
    }
}

/**
 * Visit a namespace during type resolution.
 *
 * Steps:
 *   1. Run signature_resolver pre-pass on all aggregates in this namespace.
 *   2. Visit all children (namespaces, aggregates, functions, variables).
 *   3. Check overload collisions on free functions in this namespace.
 */
void type_reference_resolver::visit_namespace(ns& ns)
{
    trace("[type_reference_resolver::visit_namespace] '{}'", {ns.get_short_name()});
    // Step 1: Run signature_resolver pre-pass on all aggregates in this namespace
    // Pre-pass: resolve all aggregate function signatures (parameter + return types)
    // WITHOUT processing function bodies.  This ensures that when function bodies
    // reference types from sibling classes (e.g. String's operator+ returning
    // StringBuilder), those constructor/function parameter types are already resolved.
    {
        signature_resolver sig_resolver(_log, _context, _unit);
        sig_resolver.resolve_signatures(ns);
    }

    // Step 1b: Eagerly materialise the aliases declared here, so that an alias
    // that is never used in this module is still fully typed when exported.
    materialize_aliases(ns, ns);

    // Step 2: Visit all children (namespaces, aggregates, functions, variables)
    // Full pass: visit everything (including function bodies).
    // Signature resolution is idempotent (already-resolved types are skipped),
    // so only the function bodies and expressions are newly processed.
    //
    // Use an index-based loop instead of range-based: template instantiation
    // triggered during body processing can add new aggregates to this namespace's
    // children list (via define_structure), which would invalidate range-based
    // iterators.  An index-based loop naturally picks up the new elements.
    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        ns.get_children()[i]->accept(*this);
    }
    // Step 3: Check overload collisions on free functions in this namespace
    // After all children are resolved, check for overload collisions among free functions.
    check_overload_collisions(ns);
}

void declaration_generator::visit_namespace(ns &ns) {
    trace("[declaration_generator::visit_namespace] '{}'", {ns.get_short_name()});
    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        ns.get_children()[i]->accept(*this);
    }
}

/**
 * Implementation of the C-ABI main proxy for the Application mode.
 *
 * When the unit has an Application class (_unit._application_class != nullptr),
 * emits LLVM IR directly to:
 *   1. Allocate the Application instance on the stack.
 *   2. Call the Application default constructor (sets up vtable pointers).
 *   3. Optionally build the args String[] from argc/argv.
 *   4. Call the entry-point 'main' — direct dispatch normally, or virtual
 *      dispatch (Phase 4) when the topmost declared 'main' in a
 *      ::k::Application abstract chain is itself abstract (its real
 *      implementation lives further down the chain, in an override).
 *   5. Call the Application destructor.
 *   6. Return the int result (or 0 for void mains).
 *
 * In legacy mode (no Application class) delegates to visit_function().
 */
void implementation_generator::visit_global_main_function(global_main_function& main_func) {
    if (!_unit._application_class) {
        // Legacy path: model body was built by type_reference_resolver
        visit_function(main_func);
        return;
    }

    auto& llvm_ctx = **_context;
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);

    // Retrieve the C proxy LLVM function (declared by declaration_generator)
    auto proxy_it = _context->_functions.find(main_func.shared_as<function>());
    if (proxy_it == _context->_functions.end()) {
        trace("[implementation_generator::visit_global_main_function] proxy function not found in function table");
        return;
    }
    llvm::Function* proxy_fn = proxy_it->second;

    // Create entry block
    llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(llvm_ctx, "entry", proxy_fn);
    _builder->SetInsertPoint(entry_bb);

    llvm::Value* argc_val = proxy_fn->getArg(0);  // i32
    llvm::Value* argv_val = proxy_fn->getArg(1);  // ptr

    auto app_class = _unit._application_class;

    // ── Allocate Application on stack ─────────────────────────────────────────
    auto app_struct_type = app_class->get_struct_type();
    llvm::Type* app_llvm_type = app_struct_type ? _context->get_llvm_type(app_struct_type) : nullptr;
    if (!app_llvm_type) {
        trace("[implementation_generator::visit_global_main_function] Application LLVM struct type not available");
        _builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));
        return;
    }
    llvm::AllocaInst* app_alloca = _builder->CreateAlloca(app_llvm_type, nullptr, "__app");

    // ── Call Application default constructor ──────────────────────────────────
    for (const auto& ctor : app_class->constructors()) {
        if (!ctor->is_deleted() && ctor->get_parameter_size() == 0) {
            auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
            if (ctor_it != _context->_functions.end()) {
                _builder->CreateCall(ctor_it->second->getFunctionType(),
                                     ctor_it->second, {app_alloca});
            }
            break;
        }
    }

    // ── Build args array if needed (Phase 1 + Application) ───────────────────
    llvm::Value* args_llvm_val = nullptr;
    if (main_func.has_args()) {
        k::name helper_name{false, {"k", "__k_argv_to_string_array"}};
        llvm::Function* helper_fn = nullptr;

        // Try _context->_functions (if declaration pass registered it)
        if (const kdi::kdi_function* kdi_h = _unit.find_imported_function(helper_name)) {
            auto imp = _unit.get_or_create_imported_function(kdi_h, _context);
            auto h_it = _context->_functions.find(imp);
            if (h_it != _context->_functions.end()) {
                helper_fn = h_it->second;
            }
            // Fallback: declare by mangled name from KDI
            if (!helper_fn && !kdi_h->mangled_name.empty()) {
                auto* fn_ty = llvm::FunctionType::get(ptr_ty, {i32_ty, ptr_ty}, false);
                helper_fn = get_module().getFunction(kdi_h->mangled_name);
                if (!helper_fn) {
                    helper_fn = llvm::Function::Create(
                        fn_ty, llvm::Function::ExternalLinkage,
                        kdi_h->mangled_name, get_module());
                }
            }
        }

        if (helper_fn) {
            args_llvm_val = _builder->CreateCall(
                helper_fn->getFunctionType(), helper_fn,
                {argc_val, argv_val}, "__k_args");
        }
    }

    // ── Call the entry-point 'main' (direct or virtual dispatch — Phase 4) ────
    auto& real_fn   = main_func.get_real_func();
    llvm::Value* ret_val = llvm::ConstantInt::get(i32_ty, 0);

    std::vector<llvm::Value*> main_args = {app_alloca};
    if (main_func.has_args() && args_llvm_val) {
        main_args.push_back(args_llvm_val);
    }

    llvm::Type* main_ret_type = real_fn.has_return_type()
        ? _context->get_llvm_type(real_fn.get_return_type())
        : llvm::Type::getVoidTy(llvm_ctx);
    if (!main_ret_type) main_ret_type = llvm::Type::getVoidTy(llvm_ctx);
    std::vector<llvm::Type*> main_param_types = {ptr_ty};
    for (auto& p : real_fn.parameters()) {
        auto* pty = p ? _context->get_llvm_type(p->get_type()) : nullptr;
        main_param_types.push_back(pty ? pty : ptr_ty);
    }
    auto* main_llvm_fn_type = llvm::FunctionType::get(main_ret_type, main_param_types, false);

    llvm::Value* call_ret = nullptr;
    if (_unit._application_entry_main_is_virtual) {
        // Phase 4: the entry declared 'main' is abstract at this level; its real
        // implementation is an override further down the ::k::Application chain.
        // Dispatch through the vtable slot of the aggregate that declares it.
        auto owner_klass = std::dynamic_pointer_cast<klass>(real_fn.get_owner());
        if (owner_klass && real_fn.get_vtable_slot() >= 0) {
            call_ret = emit_virtual_dispatch_call(*_builder, *owner_klass, app_alloca,
                real_fn.get_vtable_slot(), main_llvm_fn_type, main_args, _context, "main_ret");
        }
    } else {
        auto main_it = _context->_functions.find(real_fn.shared_as<function>());
        if (main_it != _context->_functions.end()) {
            llvm::Function* main_llvm_fn = main_it->second;
            call_ret = _builder->CreateCall(main_llvm_fn->getFunctionType(), main_llvm_fn,
                main_args, real_fn.has_return_type() ? "main_ret" : "");
        }
    }
    if (call_ret && real_fn.has_return_type()) {
        if (call_ret->getType() == i32_ty) {
            ret_val = call_ret;
        } else if (call_ret->getType()->isIntegerTy()) {
            ret_val = _builder->CreateZExtOrTrunc(call_ret, i32_ty, "main_ret_i32");
        }
    }

    // ── Call Application destructor ────────────────────────────────────────────
    auto dtor = app_class->get_destructor();
    if (dtor) {
        auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
        if (dtor_it != _context->_functions.end()) {
            _builder->CreateCall(dtor_it->second->getFunctionType(),
                                 dtor_it->second, {app_alloca});
        }
    }

    // ── Return ────────────────────────────────────────────────────────────────
    _builder->CreateRet(ret_val);
}

void implementation_generator::visit_namespace(ns &ns) {
    trace("[implementation_generator::visit_namespace] '{}'", {ns.get_short_name()});
    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        ns.get_children()[i]->accept(*this);
    }
}

//
// Member variable definition
//
void symbol_resolver::visit_member_variable_definition(member_variable_definition& var) {
    visit_named_element(var);
    // No symbol resolution today, because only primitive types are supported today.
    // TODO Add complex member resolution.
    // TODO visit the initialization expression if any
}

void type_reference_resolver::visit_member_variable_definition(member_variable_definition& var) {
    // __parent__ field is already assigned a resolved pointer type by symbol_resolver; skip.
    if (var.get_short_name() == "__parent__") return;
    // Do nothing for now
    // Everything is done at structure level
}

void declaration_generator::visit_member_variable_definition(member_variable_definition&) {
    // Do nothing for now
    // Everything is done at structure level
}

void implementation_generator::visit_member_variable_definition(member_variable_definition&) {
    // Do nothing for now
    // Everything is done at structure level
}


//
// Global variable definition
//

void symbol_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    visit_named_element(var);

    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    debug("[type_reference_resolver::visit_global_variable_definition] '{}'", {var.get_short_name()});
    visit_variable_definition(var);

    // Unconditionnally register global variable to global constructor for now, because we need to be sure it is registered before any possible use in other variable initialization expression.
    // TODO Add registering condition for trivial primitive initialization
    var.ancestor<unit>()->get_global_constructor_function().add_global_variable_definition(var.shared_as<global_variable_definition>());

    // If the variable's type is a struct with a destructor, also register it for global destruction.
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
            var.ancestor<unit>()->get_global_destructor_function().add_global_variable_definition(var.shared_as<global_variable_definition>());
        }
    }
}

void declaration_generator::visit_global_tool_function(global_tool_function&) {
    // No-op: global tool functions (global ctor / dtor) are created directly
    // by the implementation_generator with InternalLinkage.
}

void declaration_generator::visit_global_variable_definition(global_variable_definition& var) {
    debug("[declaration_generator::visit_global_variable_definition] '{}'", {var.get_mangled_name()});
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);
    if (!llvm_type) return; // type not yet resolved or unsupported

    // Static local variables (parent is a block) use InternalLinkage so they
    // are not visible outside the compilation unit.
    auto linkage = var.parent<block>()
        ? llvm::GlobalValue::InternalLinkage
        : llvm::GlobalValue::ExternalLinkage;

    auto variable = new llvm::GlobalVariable(*_context->_module, llvm_type, false, linkage, nullptr, var.get_mangled_name());
    _context->_global_vars.insert({var.shared_as<global_variable_definition>(), variable});
}


void implementation_generator::visit_global_variable_definition(global_variable_definition& var) {
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);

    // Generate initialization
    llvm::Constant* constInitValue = nullptr;

    auto init_expr_ctor = std::dynamic_pointer_cast<constructor_invocation_expression>(var.get_init_expr());
    if (type::is_primitive(var.get_type()) && init_expr_ctor && init_expr_ctor->size() == 1) {
        if (auto value = std::dynamic_pointer_cast<value_expression>(init_expr_ctor->argument(0))) {
            // Constant init expression
            if (auto constant = get_llvm_constant_from_value_expr(*value)) {
                // TODO Implement type conversion
                constInitValue = constant;
            }
        }
    }

    // For sized arrays with brace init, try to build a static constant initializer
    if (!constInitValue && type::is_sized_array(var.get_type())) {
        auto arr_init = std::dynamic_pointer_cast<array_init_expression>(var.get_init_expr());
        auto arr_type = std::dynamic_pointer_cast<sized_array_type>(var.get_type());
        if (arr_type) {
            auto elem_type = arr_type->get_subtype();
            auto* struct_llvm = arr_type->get_llvm_struct_type();
            auto* arr_data_type = arr_type->get_llvm_data_array_type();
            llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);
            size_t arr_size = arr_type->get_size();

            bool all_constant = true;
            std::vector<llvm::Constant*> elem_constants;

            if (arr_init) {
                for (size_t i = 0; i < arr_size; ++i) {
                    auto elem = (i < arr_init->size()) ? arr_init->element(i) : nullptr;
                    if (!elem) {
                        // Default-init = zero
                        elem_constants.push_back(llvm::Constant::getNullValue(llvm_elem_type));
                    } else if (auto value = std::dynamic_pointer_cast<value_expression>(elem)) {
                        auto c = get_llvm_constant_from_value_expr(*value);
                        if (c) {
                            elem_constants.push_back(c);
                        } else {
                            all_constant = false;
                            break;
                        }
                    } else if (auto cast = std::dynamic_pointer_cast<cast_expression>(elem)) {
                        // Handle cast_expression wrapping a value_expression
                        // (e.g. char→byte cast inserted by type resolver for byte[] { 'a', 'b' })
                        if (auto inner_val = std::dynamic_pointer_cast<value_expression>(cast->sub_expr())) {
                            auto c = get_llvm_constant_from_value_expr(*inner_val);
                            if (c && llvm_elem_type) {
                                // Perform the cast at constant level (e.g. i8→i8 for char→byte)
                                if (c->getType() == llvm_elem_type) {
                                    elem_constants.push_back(c);
                                } else if (c->getType()->isIntegerTy() && llvm_elem_type->isIntegerTy()) {
                                    // Integer-to-integer cast (trunc/zext/sext as needed)
                                    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c);
                                    if (ci) {
                                        elem_constants.push_back(llvm::ConstantInt::get(llvm_elem_type, ci->getZExtValue()));
                                    } else {
                                        all_constant = false;
                                        break;
                                    }
                                } else if (c->getType()->isFloatingPointTy() && llvm_elem_type->isFloatingPointTy()) {
                                    auto* cf = llvm::dyn_cast<llvm::ConstantFP>(c);
                                    if (cf) {
                                        bool lossy = false;
                                        llvm::APFloat val = cf->getValueAPF();
                                        val.convert(llvm_elem_type->isFloatTy() ? llvm::APFloat::IEEEsingle() : llvm::APFloat::IEEEdouble(),
                                                    llvm::APFloat::rmNearestTiesToEven, &lossy);
                                        elem_constants.push_back(llvm::ConstantFP::get(llvm_elem_type, val));
                                    } else {
                                        all_constant = false;
                                        break;
                                    }
                                } else {
                                    all_constant = false;
                                    break;
                                }
                            } else {
                                all_constant = false;
                                break;
                            }
                        } else {
                            all_constant = false;
                            break;
                        }
                    } else {
                        all_constant = false;
                        break;
                    }
                }
            }

            if (all_constant && elem_constants.size() == arr_size) {
                auto* size_const = llvm::ConstantInt::get(
                    llvm::Type::getInt32Ty(_context->llvm_context()), arr_size, false);
                auto* arr_const = llvm::ConstantArray::get(arr_data_type, elem_constants);
                llvm::Constant* struct_fields[] = {size_const, arr_const};
                constInitValue = llvm::ConstantStruct::get(struct_llvm, struct_fields);
            }
            // else: fall through to default zero init; dynamic init handled by global ctor
        }
    }

    if (!constInitValue) {
        // If no explicit initialization, or complex initialization, lets have 0-filled initialization:
        constInitValue = type->generate_default_value_initializer();
    }

    // Final fallback: if the type is an opaque pointer (e.g. callable_type),
    // use ConstantPointerNull; otherwise zeroinitializer.
    if (!constInitValue && llvm_type) {
        if (auto* ptr_ty = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
            constInitValue = llvm::ConstantPointerNull::get(ptr_ty);
        } else {
            constInitValue = llvm::Constant::getNullValue(llvm_type);
        }
    }

    auto variable_it = _context->_global_vars.find(var.shared_as<global_variable_definition>());
    if (variable_it == _context->_global_vars.end()) {
        // Not declared yet, should not append, but let's create it lazily anyway
        auto linkage = var.parent<block>()
            ? llvm::GlobalValue::InternalLinkage
            : llvm::GlobalValue::ExternalLinkage;
        auto variable = new llvm::GlobalVariable(*_context->_module, llvm_type, false, linkage, constInitValue, var.get_mangled_name());
        _context->_global_vars.insert({var.shared_as<global_variable_definition>(), variable});
    } else {
        // Already declared, just add initializer
        variable_it->second->setInitializer(constInitValue);
    }
}

} // namespace k::model::gen
