/*
 * Exact semantic and native RTTI data emission. No executable invokers.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef KLANG_GEN_RTTI_HPP
#define KLANG_GEN_RTTI_HPP

#include "gen_helpers.hpp"
#include "gen_redirect.hpp"
#include "resolvers_scope_lookup.hpp"
#include "../model/context.hpp"
#include "../model/mangler.hpp"
#include "../model/constant_value.hpp"
#include "../model/aggregate_value.hpp"
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace k::model::gen {

struct rtti_receiver_step {
    uint64_t offset;
    bool dereference;
};
struct rtti_receiver_projection {
    int64_t delta = 0;
    bool indirect = false;
    std::vector<rtti_receiver_step> steps;
};

// Uses the same finalized subobject locator as ordinary virtual dispatch.
rtti_receiver_projection describe_rtti_receiver_projection(
    const aggregate& containing, const aggregate& receiver, const aggregate& owner,
    const llvm::DataLayout& layout);

// Field order is shared by free/member/constructor emission and rtti.k.
// These are native K records, not a second implementation of a C ABI classifier.
class rtti_emitter {
    std::shared_ptr<context> ctx;
    unit& source;
    llvm::Module& mod;
    llvm::LLVMContext& lc;
    llvm::PointerType* ptr;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Constant* nil;

    llvm::Constant* u32(uint32_t n) const { return llvm::ConstantInt::get(i32, n); }
    llvm::Constant* u64(uint64_t n) const { return llvm::ConstantInt::get(i64, n); }
    llvm::Constant* index(int n) const { return llvm::ConstantInt::getSigned(i32, n); }

    llvm::GlobalVariable* global(llvm::Constant* init, const std::string& name) {
        return new llvm::GlobalVariable(mod, init->getType(), true,
            llvm::GlobalValue::PrivateLinkage, init, name);
    }

    bool supports_image_local_relocations() const {
        const llvm::Triple triple(mod.getTargetTriple());
        return triple.isOSLinux() && triple.getArch() == llvm::Triple::x86_64
            && triple.isOSBinFormatELF();
    }

    llvm::Constant* image_local_reference(llvm::GlobalObject* definition) {
        if (!definition || definition->isDeclarationForLinker()) return nil;
        if (definition->hasLocalLinkage()) return definition;
        if (!supports_image_local_relocations()) return nil;
        const auto name = "__k_rtti_local_" + definition->getName().str();
        if (auto* alias = mod.getNamedAlias(name)) {
            if (alias->getAliasee() != definition || !alias->hasPrivateLinkage()
                || !alias->isDSOLocal())
                throw std::runtime_error("Conflicting image-local RTTI relocation: " + name);
            return alias;
        }
        // A local ELF symbol denotes the existing definition's address, not its
        // preemptable public name or PLT entry. Do not change public K linkage.
        auto* alias = llvm::GlobalAlias::create(definition->getValueType(),
            definition->getAddressSpace(), llvm::GlobalValue::PrivateLinkage,
            name, definition, &mod);
        alias->setDSOLocal(true);
        return alias;
    }

    llvm::Constant* record(const std::vector<llvm::Constant*>& fields) {
        std::vector<llvm::Type*> types;
        for (auto* field : fields) types.push_back(field->getType());
        return llvm::ConstantStruct::get(llvm::StructType::get(lc, types), fields);
    }

    llvm::StructType* type_record_type() {
        return llvm::StructType::get(lc, {
            i32, i32, i32, i64, i64, i64, ptr, ptr, ptr, ptr, ptr, ptr,
            ptr, ptr, ptr, ptr, i64, ptr, ptr, ptr, i64, i64, i64, ptr});
    }

    llvm::StructType* signature_record_type() {
        return llvm::StructType::get(lc, {ptr, ptr, i32, ptr, ptr, ptr, i32, ptr, ptr, ptr, ptr});
    }

    std::pair<uint64_t, uint64_t> layout(llvm::Type* t) const {
        if (!t || !t->isSized()) return {0, 0};
        auto size = mod.getDataLayout().getTypeAllocSize(t);
        if (size.isScalable()) return {0, 0};
        return {size.getFixedValue(), mod.getDataLayout().getABITypeAlign(t).value()};
    }

    std::shared_ptr<union_type_def> union_for(const std::shared_ptr<struct_type>& st) {
        return find_union_by_struct_type(source.get_root_namespace(), st);
    }

    enum class declaration_role { concrete, recipe, erased_body };

    declaration_role role_of(std::shared_ptr<element> declaration) const {
        bool erased = false;
        for (; declaration; declaration = declaration->parent<element>()) {
            if (auto agg = std::dynamic_pointer_cast<aggregate>(declaration)) {
                if (agg->is_template()) return declaration_role::recipe;
                auto definition = agg->get_tpl_definition();
                erased |= definition && definition->is_generic() && !agg->has_tpl_args();
            } else if (auto un = std::dynamic_pointer_cast<union_type_def>(declaration)) {
                if (un->is_template()) return declaration_role::recipe;
            } else if (auto fn = std::dynamic_pointer_cast<function>(declaration)) {
                if (fn->is_template()) return declaration_role::recipe;
            }
        }
        return erased ? declaration_role::erased_body : declaration_role::concrete;
    }

    llvm::Constant* nominal(const std::shared_ptr<aggregate>& agg) {
        if (!agg || !agg->has_vtable() || role_of(agg) == declaration_role::recipe) return nil;
        auto* gv = get_or_declare_typeinfo_global(mod, mangler::mangle_rtti(agg->get_name()),
            has_external_rtti_definition(agg), is_annotation_aggregate(agg));
        // All existing Class/Interface/AnnotationType layouts put the TypeInfo
        // subobject at flattened field 2; never treat the complete descriptor
        // pointer as a TypeInfo reference.
        if (!gv->getValueType()->isStructTy())
            throw std::runtime_error("Missing nominal RTTI layout for " + agg->get_fq_name());
        return llvm::ConstantExpr::getInBoundsGetElementPtr(gv->getValueType(), gv,
            llvm::ArrayRef<llvm::Constant*>{u32(0), u32(2)});
    }

    llvm::Constant* producer() {
        const auto unit_symbol = mangler::mangle_rtti_unit(source.get_unit_name());
        const auto symbol = unit_symbol + "_semantic_producer";
        if (auto* existing = mod.getNamedGlobal(symbol)) return existing;
        auto* unit = mod.getNamedGlobal(unit_symbol);
        // The anchor identifies this image's definition, not a preempting unit
        // with the same exported symbol in another loaded image.
        return global(record({image_local_reference(unit),
            text(source.get_unit_name().to_string()), text(mod.getTargetTriple()),
            text(mod.getDataLayoutStr()), u32(3)}), symbol);
    }

    std::shared_ptr<union_type_def> union_declaration(
        const std::shared_ptr<union_type_def>& concrete) {
        if (!concrete->has_tpl_args()) return concrete;
        auto parent = concrete->parent<element>();
        auto* holder = dynamic_cast<union_holder*>(parent.get());
        if (!holder) return nullptr;
        for (const auto& [name, candidate] : holder->unions()) {
            auto* info = candidate->get_tpl_info();
            if (!info) continue;
            for (const auto& [key, instance] : info->instantiations)
                if (auto* un = std::get_if<std::shared_ptr<union_type_def>>(&instance);
                    un && *un == concrete) return candidate;
        }
        return nullptr;
    }

    k::name declaration_name(const std::shared_ptr<element>& declaration) {
        auto named = std::dynamic_pointer_cast<named_element>(declaration);
        if (!named) throw std::runtime_error("RTTI declaration has no semantic name");
        if (auto scope = std::dynamic_pointer_cast<ns>(declaration)) {
            if (auto owner = scope->parent<unit>())
                return owner->get_unit_name().with_root_prefix();
            if (scope->get_name().has_root_prefix()) return scope->get_name();
            if (auto owner = scope->parent<ns>())
                return declaration_name(owner).with_back(scope->get_short_name());
            throw std::runtime_error("RTTI namespace has no declaration scope");
        }
        auto name = named->get_short_name();
        const tpl_info* info = nullptr;
        if (auto agg = std::dynamic_pointer_cast<aggregate>(declaration)) {
            if (auto primary = agg->get_tpl_definition(); primary && primary != agg)
                return declaration_name(primary);
            if (agg->has_tpl_args()) name = agg->get_tpl_base_name();
            info = agg->get_tpl_info();
        } else if (auto un = std::dynamic_pointer_cast<union_type_def>(declaration)) {
            if (auto primary = union_declaration(un); primary && primary != un)
                return declaration_name(primary);
            if (un->has_tpl_args()) name = un->get_tpl_base_name();
            info = un->get_tpl_info();
        } else if (auto fn = std::dynamic_pointer_cast<function>(declaration)) {
            if (fn->has_tpl_args()) name = fn->get_tpl_base_name();
            info = fn->get_tpl_info();
        } else if (auto alias = std::dynamic_pointer_cast<alias_definition>(declaration)) {
            info = alias->get_tpl_info();
        }
        if (info && !info->origin_module_ns_fq.empty())
            return unit::make_origin_absolute_name(info->origin_module_ns_fq, name);
        if (auto owner = declaration->parent<aggregate>())
            return declaration_name(owner).with_back(name);
        if (auto owner = declaration->parent<union_type_def>())
            return declaration_name(owner).with_back(name);
        if (named->get_name().has_root_prefix())
            return named->get_name().without_back().with_back(name);
        if (auto owner = declaration->parent<ns>())
            return declaration_name(owner).with_back(name);
        return named->get_name().without_back().with_back(name);
    }

    bool local_declaration(const std::shared_ptr<element>& declaration) {
        if (!declaration || declaration->ancestor<unit>().get() != &source) return false;
        for (auto scope = declaration; scope; scope = scope->parent<element>()) {
            if (auto agg = std::dynamic_pointer_cast<aggregate>(scope)) {
                if (agg->is_imported()) return false;
                if (auto* info = agg->get_tpl_info(); info && !info->origin_module_ns_fq.empty())
                    return false;
                if (auto definition = agg->get_tpl_definition();
                    definition && (definition == agg || !local_declaration(definition))) return false;
            } else if (auto alias = std::dynamic_pointer_cast<alias_definition>(scope)) {
                if (alias->is_imported()) return false;
                if (auto* info = alias->get_tpl_info(); info && !info->origin_module_ns_fq.empty())
                    return false;
            } else if (auto un = std::dynamic_pointer_cast<union_type_def>(scope)) {
                if (auto* info = un->get_tpl_info(); info && !info->origin_module_ns_fq.empty())
                    return false;
                if (un->has_tpl_args()) {
                    auto definition = union_declaration(un);
                    if (!definition || !local_declaration(definition)) return false;
                } else if (!un->get_ast_aggregate_decl() && !un->parent<aggregate>()) return false;
            }
        }
        if (auto en = std::dynamic_pointer_cast<enumeration>(declaration))
            return en->get_ast_enum_decl() || en->parent<aggregate>()
                || (en->parent<union_type_def>() && local_declaration(en->parent<union_type_def>()));
        return true;
    }

    llvm::Constant* nominal_identity(uint32_t kind, const std::shared_ptr<element>& declaration,
                                    k::name identity, llvm::Constant* origin) {
        if (!declaration || identity.empty())
            throw std::runtime_error("RTTI nominal type has no declaration identity");
        auto* owner = producer();
        auto* declared_producer = local_declaration(declaration) ? owner : nil;
        // This is a symbolic declaration key, not a display name or an ABI
        // classifier. Strong aliases must not pass through mangle_type().
        const auto key = std::to_string(kind) + ":" + mangler::mangle_fq_name(identity, true);
        return global(record({owner, declared_producer, text(key), origin}),
            "__k_rtti_nominal_identity");
    }

    std::vector<template_argument> exact_template_arguments(
        const std::vector<template_argument>& args, const std::vector<bool>& packs) {
        if (packs.empty()) return args;
        std::vector<template_argument> result;
        size_t cursor = 0;
        for (size_t p = 0; p < packs.size(); ++p) {
            if (!packs[p]) {
                if (cursor >= args.size())
                    throw std::runtime_error("Incomplete RTTI concrete template arguments");
                result.push_back(args[cursor++]);
                continue;
            }
            if (cursor < args.size() && (args[cursor].is_pack()
                || (!args[cursor].type_arg && !args[cursor].value_arg))) {
                result.push_back(args[cursor++]);
                continue;
            }
            const auto remaining = std::count(packs.begin() + p + 1, packs.end(), false);
            std::vector<std::shared_ptr<type>> elements;
            while (cursor < args.size() && args.size() - cursor > static_cast<size_t>(remaining)) {
                if (!args[cursor].is_type())
                    throw std::runtime_error("RTTI type pack contains a non-type argument");
                elements.push_back(args[cursor++].type_arg);
            }
            result.push_back(template_argument::make_pack(std::move(elements)));
        }
        if (cursor != args.size())
            throw std::runtime_error("Unbound RTTI concrete template arguments");
        return result;
    }

    std::shared_ptr<type> template_value_type(
        const std::shared_ptr<type>& value, std::shared_ptr<element> declaration,
        unsigned depth = 0) {
        if (!value) return nullptr;
        if (depth == 256) throw std::runtime_error("Cyclic RTTI template value type binding");
        if (auto named = std::dynamic_pointer_cast<unresolved_type>(value)) {
            if (named->is_resolved())
                return template_value_type(named->get_resolved(), declaration, depth + 1);
            if (named->is_template_param_placeholder()) return value;
            if (auto definition = named->get_definition_namespace()) declaration = definition;
            else if (auto fn = std::dynamic_pointer_cast<function>(declaration);
                     fn && fn->get_template_definition_scope())
                declaration = fn->get_template_definition_scope();
            // Value arguments can retain the parameter's unresolved type even
            // after constexpr evaluation has reduced an enum entry to an integer.
            // Bind its semantic identifier lexically, never through the context's
            // short-name registry or the payload's primitive representation.
            if (declaration && !named->has_template_args()) {
                if (auto alias = scope_lookup::lookup_alias(declaration, named->type_id())) {
                    if (auto bound = alias->get_declared_type())
                        return template_value_type(bound, declaration, depth + 1);
                    return value;
                }
                if (auto en = scope_lookup::lookup_enumeration(
                        declaration, named->type_id().to_string())) {
                    if (auto bound = en->get_enum_type()) return bound;
                }
            }
        } else if (auto qualified = std::dynamic_pointer_cast<const_type>(value)) {
            auto inner = template_value_type(qualified->get_subtype(), declaration, depth + 1);
            if (inner != qualified->get_subtype()) return type::make_pinned_const(inner);
        }
        return value;
    }

    llvm::Constant* template_origin(const std::string& identity,
                                   const std::vector<template_argument>& raw_args,
                                   llvm::Constant* enclosing,
                                   const std::shared_ptr<element>& declaration,
                                   const std::vector<bool>& packs = {}) {
        if (identity.empty() && enclosing == nil) return nil;
        const auto args = exact_template_arguments(raw_args, packs);
        std::vector<llvm::Constant*> arguments;
        for (const auto& arg : args) {
            uint32_t kind = 0;
            llvm::Constant* at = nil;
            llvm::Constant* value = nil;
            llvm::Constant* pack = nil;
            uint64_t size = 0, alignment = 0;
            if (arg.is_pack()) {
                kind = 2;
                pack = type_array(arg.pack_types, "__k_rtti_pack");
            } else if (arg.value_arg) {
                kind = 1;
                auto vt = template_value_type(arg.value_type, declaration);
                if (!vt) {
                    vt = std::visit([&](const auto& v) -> std::shared_ptr<type> {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, bool>) return ctx->from_type(primitive_type::BOOL);
                        else if constexpr (std::is_same_v<T, char>) return ctx->from_type(primitive_type::CHAR);
                        else if constexpr (std::is_same_v<T, unsigned char>) return ctx->from_type(primitive_type::UNSIGNED_BYTE);
                        else if constexpr (std::is_same_v<T, short>) return ctx->from_type(primitive_type::SHORT);
                        else if constexpr (std::is_same_v<T, unsigned short>) return ctx->from_type(primitive_type::UNSIGNED_SHORT);
                        else if constexpr (std::is_same_v<T, int>) return ctx->from_type(primitive_type::INT);
                        else if constexpr (std::is_same_v<T, unsigned int>) return ctx->from_type(primitive_type::UNSIGNED_INT);
                        else if constexpr (std::is_same_v<T, long>) return ctx->from_type(primitive_type::LONG);
                        else if constexpr (std::is_same_v<T, unsigned long>) return ctx->from_type(primitive_type::UNSIGNED_LONG);
                        else if constexpr (std::is_same_v<T, long long>) return ctx->from_type(primitive_type::LONG_LONG);
                        else if constexpr (std::is_same_v<T, unsigned long long>) return ctx->from_type(primitive_type::UNSIGNED_LONG_LONG);
                        else if constexpr (std::is_same_v<T, float>) return ctx->from_type(primitive_type::FLOAT);
                        else if constexpr (std::is_same_v<T, double>) return ctx->from_type(primitive_type::DOUBLE);
                        else if constexpr (std::is_same_v<T, std::shared_ptr<aggregate_value>>)
                            return v ? v->get_type()->get_struct_type() : nullptr;
                        else return nullptr;
                    }, *arg.value_arg);
                }
                if (!vt) throw std::runtime_error("RTTI template value has no semantic type");
                at = describe(vt);
                auto* constant = ctx->get_llvm_constant_from_constant_value(
                    constant_value(*arg.value_arg), vt);
                if (!constant || constant->getType() != vt->get_llvm_type())
                    throw std::runtime_error("RTTI template value has no exact typed initializer");
                std::tie(size, alignment) = layout(constant->getType());
                if (!alignment) throw std::runtime_error("RTTI template value has no layout");
                value = global(constant, "__k_rtti_template_value");
            } else if (arg.type_arg) {
                at = describe(arg.type_arg);
            } else {
                // An empty type pack is meaningful even though is_pack() is false.
                kind = 2;
            }
            arguments.push_back(global(record({u32(kind), at, value, u64(size), u64(alignment), pack}),
                "__k_rtti_template_arg"));
        }
        return global(record({text(identity), pointer_array(arguments, "__k_rtti_template_args"),
                              enclosing}), "__k_rtti_template_origin");
    }

    llvm::Constant* aggregate_origin(const std::shared_ptr<aggregate>& agg) {
        if (!agg) return nil;
        auto enclosing = agg->ancestor<aggregate>();
        auto* outer = aggregate_origin(enclosing);
        if (!agg->has_tpl_args()) return outer;
        return template_origin(declaration_name(agg).to_string(),
            agg->get_tpl_args(), outer, agg->get_tpl_definition() ? agg->get_tpl_definition() : agg,
            agg->get_tpl_parameter_packs());
    }

    bool unresolved_arguments(const std::vector<template_argument>& arguments,
                              const std::shared_ptr<element>& declaration) {
        for (const auto& argument : arguments) {
            if (argument.type_arg && unresolved(argument.type_arg)) return true;
            if (argument.value_type
                && unresolved(template_value_type(argument.value_type, declaration))) return true;
            for (const auto& element : argument.pack_types) if (unresolved(element)) return true;
        }
        return false;
    }

    bool unresolved(const std::shared_ptr<type>& t) {
        if (!t) return false; // the compiler's void return
        if (auto u = std::dynamic_pointer_cast<unresolved_type>(t))
            return u->is_resolved() ? unresolved(u->get_resolved()) : u->type_id().to_string() != "void";
        if (auto u = std::dynamic_pointer_cast<unresolved_callable_type>(t))
            return !u->is_resolved() || unresolved(u->get_resolved());
        if (type::is_erased_param(t)) return true;
        if (auto alias = std::dynamic_pointer_cast<alias_type>(t)) {
            auto declaration = alias->get_alias();
            return (declaration && role_of(declaration->parent<element>()) != declaration_role::concrete)
                || !alias->get_underlying() || unresolved(alias->get_underlying());
        }
        if (auto callable = std::dynamic_pointer_cast<callable_type>(t)) {
            if (unresolved(callable->get_return_type())) return true;
            for (const auto& p : callable->get_parameter_types()) if (unresolved(p)) return true;
            for (const auto& p : callable->get_throws()) if (unresolved(p)) return true;
            if (auto method = std::dynamic_pointer_cast<member_function_reference_type>(t);
                method && unresolved(method->get_member_of()->get_struct_type())) return true;
            return false;
        }
        if (auto child = t->get_subtype()) return unresolved(child);
        if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
            if (auto agg = st->get_struct())
                return role_of(agg) != declaration_role::concrete
                    || unresolved_arguments(agg->get_tpl_args(),
                        agg->get_tpl_definition() ? agg->get_tpl_definition() : agg);
            auto un = union_for(st);
            return un && (role_of(un) != declaration_role::concrete
                || unresolved_arguments(un->get_tpl_args(), union_declaration(un)));
        }
        if (auto en = std::dynamic_pointer_cast<enum_type>(t))
            return role_of(en->get_enumeration()) != declaration_role::concrete;
        return !std::dynamic_pointer_cast<primitive_type>(t) && !std::dynamic_pointer_cast<enum_type>(t);
    }

    llvm::Type* described_storage(const std::shared_ptr<type>& t) {
        if (!unresolved(t)) return t->get_llvm_type();
        auto value = t;
        for (unsigned depth = 0; value && depth < 256; ++depth) {
            if (auto named = std::dynamic_pointer_cast<unresolved_type>(value);
                named && named->is_resolved()) value = named->get_resolved();
            else if (auto alias = std::dynamic_pointer_cast<alias_type>(value)) {
                auto declaration = alias->get_alias();
                if (declaration && role_of(declaration->parent<element>()) == declaration_role::recipe)
                    return nullptr;
                value = alias->get_underlying();
            } else if (std::dynamic_pointer_cast<const_type>(value)
                       || type::is_reference(value) || type::is_view(value)
                       || type::is_link(value) || type::is_pointer(value)
                       || type::is_owner(value) || type::is_drain(value)) {
                value = value->get_subtype();
            } else {
                auto st = std::dynamic_pointer_cast<struct_type>(value);
                // A compiled erased body owns storage, not a concrete semantic
                // receiver contract. Qualifiers/addressers preserve that fact.
                return st && role_of(st->get_struct()) == declaration_role::erased_body
                    ? t->get_llvm_type() : nullptr;
            }
        }
        if (value) throw std::runtime_error("Cyclic RTTI storage type binding");
        return nullptr;
    }

    llvm::Type* native_type(const std::shared_ptr<type>& t) {
        if (!t) return llvm::Type::getVoidTy(lc);
        if (auto u = std::dynamic_pointer_cast<unresolved_type>(t);
            u && u->type_id().to_string() == "void") return llvm::Type::getVoidTy(lc);
        return unresolved(t) ? nullptr : t->get_llvm_type();
    }

    const function& native_source(const function& fn) const {
        auto* current = &fn;
        for (unsigned depth = 0; depth < 256; ++depth) {
            if (!current->is_redirected()) return *current;
            auto target = current->get_redirect_target();
            if (!target) throw std::runtime_error("Unresolved RTTI redirect target: " + fn.get_fq_name());
            current = target.get();
        }
        throw std::runtime_error("Cyclic RTTI redirect target: " + fn.get_fq_name());
    }

    struct effective_argument {
        std::shared_ptr<type> exact;
        int logical;
        bool vararg;
    };

    static bool has_receiver(const function& fn) {
        return fn.is_member() && !fn.is_static() && !fn.is_extern();
    }

    std::vector<effective_argument> effective_arguments(const function& fn, bool actual = false) const {
        std::vector<effective_argument> result;
        if (has_receiver(fn)) {
            auto receiver = fn.get_this_parameter();
            result.push_back({receiver ? receiver->get_type() : nullptr, -2, false});
        }
        for (const auto& parameter : fn.parameters())
            result.push_back({parameter->get_type(), static_cast<int>(result.size())
                - (has_receiver(fn) ? 1 : 0), parameter->is_varargs()});
        if (actual && fn.get_imported_redirect_contract() && !result.empty())
            if (auto receiver = implementation_receiver_type(fn)) result.front().exact = receiver;
        return result;
    }

    bool compatible_provider(const function& fn, const function& provider,
                             const std::shared_ptr<type>& provider_return) const {
        if (provider.is_deleted() || provider.is_abstract_func()
            || !type::are_equal(std::const_pointer_cast<type>(fn.get_return_type()),
                                provider_return)) return false;
        const auto source_arguments = effective_arguments(fn);
        const auto target_arguments = effective_arguments(provider, true);
        if (source_arguments.size() != target_arguments.size()) return false;
        for (size_t n = 0; n < source_arguments.size(); ++n) {
            if (type::are_equal(source_arguments[n].exact, target_arguments[n].exact)) continue;
            auto carrier = fn.get_redirect_receiver_type();
            if (!carrier && (fn.get_imported_redirect_contract()
                             || provider.get_imported_redirect_contract()))
                carrier = implementation_receiver_type(fn);
            // Only the resolver's validated binding (or an explicitly
            // unverified imported carrier) may authorize receiver projection.
            if (n != 0 || !carrier || !type::are_equal(carrier, target_arguments[n].exact)
                || !can_project_redirect_receiver(source_arguments[n].exact, carrier)) return false;
        }
        for (const auto& thrown : provider.get_throws_spec()) {
            const bool covered = std::any_of(fn.get_throws_spec().begin(), fn.get_throws_spec().end(),
                [&](const auto& allowed) {
                    if (type::are_equal(thrown, allowed)) return true;
                    auto actual = std::dynamic_pointer_cast<struct_type>(thrown);
                    auto permitted = std::dynamic_pointer_cast<struct_type>(allowed);
                    return actual && permitted && actual->get_struct() && permitted->get_struct()
                        && actual->get_struct()->is_derived_from(permitted->get_struct());
                });
            if (!covered) return false;
        }
        return true;
    }

    std::vector<int> native_argument_sources(const std::shared_ptr<function>& fn,
                                             llvm::Function* body) const {
        if (auto mapping = ctx->_native_argument_sources.find(fn.get());
            mapping != ctx->_native_argument_sources.end()) return mapping->second;
        const auto& provider = native_source(*fn);
        if (!provider.is_external() && !provider.is_extern())
            throw std::runtime_error("Missing authoritative native RTTI mapping for " + fn->get_fq_name());
        // Imported declarations are explicitly unverified/ineligible. This
        // provisional mapping supports introspection, never call preparation.
        std::vector<int> result;
        for (unsigned n = 0; n < body->arg_size(); ++n)
            if (body->hasParamAttribute(n, llvm::Attribute::StructRet)) result.push_back(-1);
        if (fn->is_member() && !fn->is_static() && !fn->is_extern()) result.push_back(-2);
        for (unsigned n = 0; n < fn->get_parameter_size(); ++n) result.push_back(n);
        return result;
    }

    llvm::Constant* native_value(llvm::Type* native, const std::shared_ptr<type>& semantic,
                                int logical, llvm::AttributeSet attributes,
                                uint32_t extra_flags, bool& supported, uint64_t offset = 0) {
        uint32_t kind = 5, width = 0, flags = extra_flags;
        if (native->isVoidTy()) kind = 0;
        else if (native->isIntegerTy()) {
            kind = 1;
            width = native->getIntegerBitWidth();
            if (width != 1 && width != 8 && width != 16 && width != 32 && width != 64)
                supported = false;
        } else if (native->isFloatTy() || native->isDoubleTy()) {
            kind = 2;
            width = native->isFloatTy() ? 32 : 64;
        } else if (native->isPointerTy()) {
            kind = 3;
            width = mod.getDataLayout().getPointerTypeSizeInBits(native);
            if (native->getPointerAddressSpace() != 0) supported = false;
        } else {
            if (native->isAggregateType()) kind = 4;
            supported = false;
        }
        llvm::Type* pointee = nullptr;
        if (attributes.hasAttribute(llvm::Attribute::StructRet)) {
            flags |= 1;
            pointee = attributes.getAttribute(llvm::Attribute::StructRet).getValueAsType();
        }
        if (attributes.hasAttribute(llvm::Attribute::ByVal)) {
            flags |= 2;
            pointee = attributes.getAttribute(llvm::Attribute::ByVal).getValueAsType();
            supported = false;
        }
        if (attributes.hasAttribute(llvm::Attribute::SExt)) flags |= 4;
        if (attributes.hasAttribute(llvm::Attribute::ZExt)) flags |= 8;
        if (attributes.hasAttribute(llvm::Attribute::InReg)) { flags |= 16; supported = false; }
        if (attributes.hasAttribute(llvm::Attribute::NoAlias)) flags |= 32;
        // Keep the exact attribute set as introspection, but refuse unfamiliar
        // ABI-affecting attributes rather than silently claiming a call recipe.
        for (auto a : attributes) {
            if (a.isStringAttribute()) { supported = false; continue; }
            switch (a.getKindAsEnum()) {
            case llvm::Attribute::StructRet: case llvm::Attribute::ByVal:
            case llvm::Attribute::SExt: case llvm::Attribute::ZExt:
            case llvm::Attribute::NoAlias: case llvm::Attribute::Alignment:
            case llvm::Attribute::NonNull: case llvm::Attribute::NoUndef:
            case llvm::Attribute::Dereferenceable: case llvm::Attribute::DereferenceableOrNull:
            case llvm::Attribute::ReadOnly: case llvm::Attribute::WriteOnly:
            case llvm::Attribute::NoCapture:
                break;
            default: supported = false; break;
            }
        }
        const auto [size, align] = layout(native);
        auto [psize, palign] = layout(pointee);
        if (auto explicit_align = attributes.getAlignment()) palign = explicit_align->value();
        std::vector<llvm::Constant*> fields;
        if ((logical == -3 || logical == -4) && native->isAggregateType() && native->isSized()) {
            if (auto* structure = llvm::dyn_cast<llvm::StructType>(native)) {
                const auto* sl = mod.getDataLayout().getStructLayout(structure);
                for (unsigned n = 0; n < structure->getNumElements(); ++n) {
                    fields.push_back(native_value(structure->getElementType(n), nullptr, -4,
                        {}, 0, supported, sl->getElementOffset(n)));
                }
            } else if (auto* array = llvm::dyn_cast<llvm::ArrayType>(native)) {
                // The exact extent is separate: allocation sizes cannot recover
                // a count when native elements have zero size.
                fields.push_back(native_value(array->getElementType(), nullptr, -4, {}, 0, supported));
            }
        }
        return global(record({u32(kind), u32(width), u64(size), u64(align),
            logical == -4 ? nil : describe(semantic), index(logical), u32(flags), u64(psize), u64(palign),
            text(attributes.getAsString()), u64(offset), pointer_array(fields, "__k_rtti_native_fields"),
            u64(native->isArrayTy() ? llvm::cast<llvm::ArrayType>(native)->getNumElements() : 0)}),
            "__k_rtti_native_value");
    }

    std::shared_ptr<type> lifecycle_value_type(std::shared_ptr<type> value) {
        for (unsigned depth = 0; value && depth < 256; ++depth) {
            if (auto named = std::dynamic_pointer_cast<unresolved_type>(value);
                named && named->is_resolved()) value = named->get_resolved();
            else if (auto qualified = std::dynamic_pointer_cast<const_type>(value))
                value = qualified->get_inner_type();
            else return value; // In particular, never cross a nominal alias boundary.
        }
        if (value) throw std::runtime_error("Cyclic RTTI lifecycle type binding");
        return nullptr;
    }

    llvm::Constant* lifecycle(const std::shared_ptr<aggregate>& agg) {
        if (unresolved(agg->get_struct_type())) return nil;
        std::vector<llvm::Constant*> operations;
        auto add = [&](const std::shared_ptr<function>& fn, uint32_t operation) {
            if (!fn) return;
            uint32_t flags = static_cast<uint32_t>(fn->get_visibility());
            if (fn->is_deleted()) flags |= 4;
            if (fn->is_compiler_generated()) flags |= 8;
            operations.push_back(global(record({u32(operation), u32(flags), signature(fn)}),
                "__k_rtti_lifecycle"));
        };
        add(agg->get_destructor(), 0);
        for (const auto& ctor : agg->constructors()) {
            if (!ctor || ctor->get_parameter_size() != 1) continue;
            auto p = lifecycle_value_type(ctor->parameters().front()->get_type());
            if (type::is_drain(p)) {
                if (type::are_equal(lifecycle_value_type(p->get_subtype()), agg->get_struct_type()))
                    add(ctor, 2);
            } else if (type::is_reference(p)
                && type::are_equal(lifecycle_value_type(p->get_subtype()), agg->get_struct_type())) {
                // The legacy copy marker can precede final referent resolution,
                // and deleted constructors do not visit an implementation body.
                add(ctor, 1);
            }
        }
        return pointer_array(operations, "__k_rtti_lifecycle_list");
    }

    llvm::Constant* projections(const std::shared_ptr<aggregate>& agg) {
        if (unresolved(agg->get_struct_type())) return nil;
        std::vector<llvm::Constant*> bases;
        auto* storage = llvm::dyn_cast_or_null<llvm::StructType>(agg->get_struct_type()->get_llvm_type());
        if ((!storage || !storage->isSized()) && agg->has_bases())
            throw std::runtime_error("Missing finalized RTTI receiver layout [aggregate="
                + agg->get_fq_name() + "]");
        if (!storage || !storage->isSized()) return nil;
        for (const auto& base : agg->get_bases()) {
            if (!base.base) throw std::runtime_error("Unresolved RTTI receiver base [aggregate="
                + agg->get_fq_name() + ", base=" + base.raw_name + "]");
            const uint32_t flags = static_cast<uint32_t>(base.vis) | (base.is_virtual ? 4 : 0);
            bases.push_back(global(record({describe(base.base->get_struct_type()),
                projection(*agg, *agg, *base.base), u32(flags)}), "__k_rtti_projection"));
        }
        return pointer_array(bases, "__k_rtti_projections");
    }

    llvm::Constant* projection(const aggregate& containing, const aggregate& receiver,
                               const aggregate& owner) {
        const auto recipe = describe_rtti_receiver_projection(
            containing, receiver, owner, mod.getDataLayout());
        std::vector<llvm::Constant*> steps;
        for (const auto& step : recipe.steps)
            steps.push_back(global(record({u64(step.offset), u32(step.dereference ? 1 : 0)}),
                "__k_rtti_receiver_step"));
        return global(record({llvm::ConstantInt::getSigned(i64, recipe.delta),
            pointer_array(steps, "__k_rtti_receiver_steps"), u32(recipe.indirect ? 1 : 0)}),
            "__k_rtti_receiver_projection");
    }

public:
    rtti_emitter(std::shared_ptr<context> context, unit& unit)
        : ctx(std::move(context)), source(unit), mod(ctx->module()), lc(mod.getContext()),
          ptr(llvm::PointerType::get(lc, 0)), i32(llvm::Type::getInt32Ty(lc)),
          i64(llvm::Type::getInt64Ty(lc)), nil(llvm::ConstantPointerNull::get(ptr)) {}

    llvm::Constant* text(const std::string& value) {
        std::vector<uint32_t> characters;
        for (unsigned char c : value) characters.push_back(c);
        characters.push_back(0);
        return global(record({u32(characters.size()),
            llvm::ConstantDataArray::get(lc, characters)}), "__k_rtti_text");
    }

    llvm::Constant* pointer_array(const std::vector<llvm::Constant*>& values,
                                  const std::string& name) {
        if (values.empty()) return nil;
        auto* array = llvm::ArrayType::get(ptr, values.size());
        return global(record({u32(values.size()), llvm::ConstantArray::get(array, values)}), name);
    }

    llvm::Constant* type_array(const std::vector<std::shared_ptr<type>>& values,
                              const std::string& name) {
        std::vector<llvm::Constant*> types;
        for (const auto& t : values) types.push_back(describe(t));
        return pointer_array(types, name);
    }

    llvm::Constant* describe(std::shared_ptr<type> t) {
        if (auto u = std::dynamic_pointer_cast<unresolved_type>(t)) {
            if (u->is_resolved()) return describe(u->get_resolved());
            if (u->type_id().to_string() == "void") t = nullptr;
        }
        if (auto u = std::dynamic_pointer_cast<unresolved_callable_type>(t); u && u->is_resolved())
            return describe(u->get_resolved());
        if (!t && ctx->_rtti_void) return ctx->_rtti_void;
        if (t) {
            for (const auto& [existing, gv] : ctx->_rtti_types)
                if (existing == t) return gv;
        }
        auto* gv = new llvm::GlobalVariable(mod, type_record_type(), true,
            llvm::GlobalValue::PrivateLinkage, nullptr, "__k_rtti_type");
        if (t) ctx->_rtti_types.emplace_back(t, gv);
        else ctx->_rtti_void = gv;
        // Publish the forward global before descending through any callable,
        // template argument, receiver, canonical type or lifecycle signature.
        uint32_t kind = 0, detail = 0, flags = 2;
        uint64_t extent = 0, data_offset = 0, array_length_offset = 0;
        llvm::Constant *element = nil, *result = nil, *params = nil, *throws = nil;
        llvm::Constant *identity = text("void"), *nominal_type = nil, *origin = nil;
        llvm::Constant* nominal_origin = nil;
        llvm::Constant *operations = nil, *member = nil, *base_projections = nil;
        llvm::Constant* alternatives = nil;
        uint64_t tag_offset = 0, payload_offset = 0;
        llvm::Type* native = nullptr;
        if (t) {
            identity = text(t->to_string()); // display/nominal identity, never used to infer representation
            flags = unresolved(t) ? 0 : 2;
            native = described_storage(t);
            if (auto p = std::dynamic_pointer_cast<primitive_type>(t)) {
                kind = 1; detail = p->get_type();
            } else if (std::dynamic_pointer_cast<const_type>(t)) kind = 2;
            else if (type::is_erased_param(t)) {
                kind = 21; flags = 0;
            } else if (std::dynamic_pointer_cast<reference_type>(t)) kind = 3;
            else if (std::dynamic_pointer_cast<view_type>(t)) kind = 4;
            else if (std::dynamic_pointer_cast<link_type>(t)) kind = 5;
            else if (std::dynamic_pointer_cast<pointer_type>(t)) kind = 6;
            else if (std::dynamic_pointer_cast<owner_type>(t)) kind = 7;
            else if (std::dynamic_pointer_cast<drain_type>(t)) kind = 8;
            else if (auto a = std::dynamic_pointer_cast<array_type>(t)) {
                kind = 9;
                if (auto sized = std::dynamic_pointer_cast<sized_array_type>(t)) {
                    kind = 10; extent = sized->get_size();
                }
                auto* st = llvm::dyn_cast_or_null<llvm::StructType>(native);
                if (st && st->isSized()) {
                    data_offset = mod.getDataLayout().getStructLayout(st)->getElementOffset(1);
                    array_length_offset = mod.getDataLayout().getStructLayout(st)->getElementOffset(0);
                    if (!st->getElementType(0)->isIntegerTy())
                        throw std::runtime_error("RTTI array length has an unsupported representation");
                    detail = st->getElementType(0)->getIntegerBitWidth();
                }
            } else if (auto callable = std::dynamic_pointer_cast<callable_type>(t)) {
                kind = callable->is_unbound_member() ? 12 : 11;
                detail = static_cast<uint32_t>(callable->get_addresser());
                result = describe(callable->get_return_type());
                params = type_array(callable->get_parameter_types(), "__k_rtti_callable_params");
                throws = type_array(callable->get_throws(), "__k_rtti_callable_throws");
                if (auto method = std::dynamic_pointer_cast<member_function_reference_type>(t))
                    member = describe(method->get_member_of()->get_struct_type());
            } else if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
                kind = 13;
                if (auto agg = st->get_struct()) {
                    identity = text(agg->get_fq_name());
                    if (is_annotation_aggregate(agg)) kind = 16;
                    else if (agg->is_interface()) kind = 15;
                    else if (agg->is_class()) kind = 14;
                    nominal_type = nominal(agg);
                    origin = aggregate_origin(agg);
                    auto declaration = agg->get_tpl_definition();
                    nominal_origin = nominal_identity(kind, declaration ? declaration : agg,
                        declaration_name(agg), origin);
                    if (agg->is_abstract()) flags |= 16;
                    // Erased syntheses have physical storage, not concrete
                    // semantic receiver/base contracts.
                    if ((flags & 2) != 0) {
                        operations = lifecycle(agg);
                        base_projections = projections(agg);
                        if (!agg->get_destructor()) flags |= 4;
                        if (agg->is_imported() && agg->has_vtable()) flags |= 8;
                    }
                } else if (auto un = union_for(st)) {
                    kind = 18;
                    identity = text(un->get_fq_name());
                    origin = aggregate_origin(un->ancestor<aggregate>());
                    if (un->has_tpl_args())
                        origin = template_origin(
                            declaration_name(un).to_string(),
                            un->get_tpl_args(), origin, union_declaration(un),
                            un->get_tpl_parameter_packs());
                    auto declaration = union_declaration(un);
                    nominal_origin = nominal_identity(kind, declaration ? declaration : un,
                        declaration_name(un), origin);
                    auto* storage = llvm::dyn_cast_or_null<llvm::StructType>(native);
                    if (storage && storage->isSized() && storage->getNumElements() >= 2) {
                        const auto* sl = mod.getDataLayout().getStructLayout(storage);
                        tag_offset = sl->getElementOffset(0);
                        payload_offset = sl->getElementOffset(1);
                    } else if ((flags & 2) != 0) {
                        throw std::runtime_error("RTTI union has no finalized storage layout");
                    }
                    if (un->get_kind_enum()) element = describe(un->get_kind_enum()->get_enum_type());
                    if (un->get_polymorphic_base())
                        member = describe(un->get_polymorphic_base()->get_struct_type());
                    std::vector<llvm::Constant*> cases;
                    for (const auto* alt : un->all_alternatives_ptrs()) {
                        if (!alt->resolved_type)
                            throw std::runtime_error("RTTI union alternative has no exact type");
                        cases.push_back(global(record({text(alt->name), describe(alt->resolved_type),
                            u32(alt->index), u32(alt->is_const ? 1 : 0)}), "__k_rtti_union_case"));
                    }
                    alternatives = pointer_array(cases, "__k_rtti_union_cases");
                } else {
                    throw std::runtime_error("RTTI struct has neither aggregate nor union identity: " + st->name());
                }
            } else if (auto e = std::dynamic_pointer_cast<enum_type>(t)) {
                kind = 17;
                element = describe(e->get_underlying_type());
                if (auto en = e->get_enumeration()) {
                    identity = text(en->get_fq_name());
                    origin = aggregate_origin(en->ancestor<aggregate>());
                    nominal_origin = nominal_identity(kind, en, declaration_name(en), origin);
                } else throw std::runtime_error("RTTI enum has no nominal declaration");
                if (e->is_object_backed()) member = describe(e->get_object_type());
            } else if (auto a = std::dynamic_pointer_cast<alias_type>(t)) {
                kind = 19; identity = text(a->get_fq_name()); element = describe(a->get_underlying());
                if (auto declaration = a->get_alias()) {
                    origin = aggregate_origin(declaration->ancestor<aggregate>());
                    if (declaration->is_template()) {
                        std::vector<template_argument> args;
                        std::string name = declaration->get_fq_name() + "<";
                        for (const auto& argument : a->get_template_arguments()) {
                            if (!args.empty()) name += ", ";
                            name += argument->to_string();
                            args.push_back(template_argument::make_type(argument));
                        }
                        identity = text(name + ">");
                        origin = template_origin(
                            declaration_name(declaration).to_string(), args, origin, declaration);
                    }
                    nominal_origin = nominal_identity(kind, declaration, declaration_name(declaration), origin);
                } else {
                    throw std::runtime_error("RTTI strong alias has no nominal declaration");
                }
            } else {
                kind = 20; flags = 0;
            }
            if (kind >= 2 && kind <= 10) {
                auto subtype = t->get_subtype();
                if (!subtype) throw std::runtime_error("RTTI wrapper has no referent");
                element = describe(subtype);
            }
        }
        auto [size, align] = layout(native);
        auto canonical = t ? type::canonical(t) : nullptr;
        auto allocation_type = canonical;
        while (auto qualified = std::dynamic_pointer_cast<const_type>(allocation_type))
            allocation_type = qualified->get_inner_type();
        auto allocation_array = std::dynamic_pointer_cast<array_type>(allocation_type);
        // An unsized array's LLVM header is sized, but not its complete value.
        // Qualifiers/aliases preserve this; addressers still have fixed storage.
        if (align && (!allocation_array || allocation_array->is_sized())) flags |= 1;
        auto* canonical_descriptor = canonical && canonical != t
            ? describe(canonical) : gv;
        gv->setInitializer(llvm::ConstantStruct::get(type_record_type(), {
            u32(kind), u32(detail), u32(flags), u64(size), u64(align), u64(extent),
            element, result, params, throws, nominal_type, identity, canonical_descriptor,
            origin, operations, nil, u64(data_offset), member, base_projections,
            alternatives, u64(tag_offset), u64(payload_offset), u64(array_length_offset),
            nominal_origin}));
        return gv;
    }

    llvm::Constant* signature(const std::shared_ptr<function>& fn, bool refresh = false) {
        llvm::GlobalVariable* gv;
        if (auto found = ctx->_rtti_signatures.find(fn.get()); found != ctx->_rtti_signatures.end()) {
            if (!refresh) return found->second;
            gv = found->second;
        } else {
            gv = new llvm::GlobalVariable(mod, signature_record_type(), true,
                llvm::GlobalValue::PrivateLinkage, nullptr, "__k_rtti_signature");
            ctx->_rtti_signatures.emplace(fn.get(), gv);
        }
        auto* body = ctx->lookup_llvm_function(fn);
        const auto& provider = body ? native_source(*fn) : *fn;
        auto provider_model = std::const_pointer_cast<function>(provider.shared_as<const function>());
        const auto provider_return = std::const_pointer_cast<type>(provider.get_return_type());
        const auto provider_arguments = effective_arguments(provider, true);
        const bool provider_entry_verified = !body || ctx->lookup_llvm_function(provider_model) == body;
        auto ret = fn->get_return_type();
        bool unresolved_signature = role_of(fn) == declaration_role::recipe
            || fn->get_erased_target() || unresolved(ret)
            || unresolved_arguments(fn->get_tpl_args(), fn);
        std::vector<std::shared_ptr<type>> parameters;
        bool aggregate_input = false;
        for (const auto& p : fn->parameters()) {
            if (!p || !p->get_type())
                throw std::runtime_error("RTTI callable has an absent parameter type: " + fn->get_fq_name());
            parameters.push_back(p->get_type());
            unresolved_signature |= unresolved(p->get_type());
            auto canonical = type::remove_const(type::canonical(p->get_type()));
            auto* storage = unresolved(canonical) ? nullptr : canonical->get_llvm_type();
            aggregate_input |= storage && storage->isAggregateType();
        }
        for (const auto& thrown : fn->get_throws_spec()) unresolved_signature |= unresolved(thrown);
        if (auto owner = fn->get_owner();
            owner && has_receiver(*fn) && unresolved(owner->get_struct_type())) unresolved_signature = true;
        uint32_t eligibility = 0;
        if (fn->is_deleted()) eligibility = 2;
        else if (unresolved_signature) eligibility = 3;
        else if (provider.is_extern()) eligibility = 8;
        else if (aggregate_input) eligibility = 5;
        else if (fn->is_abstract_func()) eligibility = 1;
        else if (provider.is_external()) eligibility = 9;
        else if (!body) eligibility = 4;
        if (has_redirect_contract(*fn) && !fn->is_deleted() && !unresolved_signature
            && (!compatible_provider(*fn, provider, provider_return)
                || !provider_entry_verified)) eligibility = 10;

        llvm::Constant* receiver = nil;
        const bool source_has_receiver = has_receiver(*fn);
        if (source_has_receiver) {
            if (!fn->get_owner()) throw std::runtime_error("RTTI method lacks its receiver declaration");
            uint32_t flags = (fn->is_const_member() ? 1 : 0) | (fn->is_virtual() ? 6 : 0);
            receiver = global(record({describe(fn->get_owner()->get_struct_type()),
                index(fn->get_vtable_slot()), u32(flags)}), "__k_rtti_receiver");
        }
        auto* enclosing_origin = aggregate_origin(std::const_pointer_cast<aggregate>(fn->get_owner()));
        llvm::Constant* origin = nil;
        if (fn->has_tpl_args() || fn->is_template())
            origin = template_origin(declaration_name(fn).to_string(),
                fn->get_tpl_args(), enclosing_origin, fn, fn->get_tpl_parameter_packs());
        llvm::Constant* native = nil;
        if (body && !unresolved_signature && provider_entry_verified) {
            bool supported = true;
            int result_index = -1, receiver_index = -1, body_receiver_index = -1;
            const auto argument_sources = native_argument_sources(fn, body);
            const auto provider_sources = native_argument_sources(provider_model, body);
            const bool producer_mapping = !provider.is_external();
            if (argument_sources.size() != body->arg_size()
                || provider_sources.size() != body->arg_size()) supported = false;
            std::vector<bool> seen(parameters.size(), false);
            for (unsigned n = 0; n < argument_sources.size(); ++n) {
                const int logical = argument_sources[n];
                if (logical == -1) {
                    if (result_index != -1) supported = false;
                    result_index = n;
                } else if (logical == -2) {
                    if (receiver_index != -1) supported = false;
                    receiver_index = n;
                } else if (logical >= 0 && static_cast<size_t>(logical) < seen.size()) {
                    if (seen[logical]) supported = false;
                    seen[logical] = true;
                } else supported = false;
            }
            if (std::find(seen.begin(), seen.end(), false) != seen.end()) supported = false;
            if (source_has_receiver != (receiver_index >= 0)) supported = false;
            for (unsigned n = 0; n < body->arg_size(); ++n)
                if (body->hasParamAttribute(n, llvm::Attribute::StructRet)
                    != (static_cast<int>(n) == result_index)) supported = false;
            if (implementation_provider_is_member(*fn) && !provider.is_extern()) {
                for (unsigned n = 0; n < body->arg_size(); ++n)
                    if (static_cast<int>(n) != result_index) {
                        body_receiver_index = n;
                        break;
                    }
                if (body_receiver_index < 0) supported = false;
            }
            std::vector<llvm::Constant*> args;
            uint32_t native_flags = 2 | (producer_mapping ? 0 : 4) | (body->isVarArg() ? 8 : 0);
            for (auto& arg : body->args()) {
                const int n = arg.getArgNo();
                const int source_index = static_cast<size_t>(n) < argument_sources.size()
                    ? argument_sources[n] : -4;
                const int provider_index = static_cast<size_t>(n) < provider_sources.size()
                    ? provider_sources[n] : -4;
                std::shared_ptr<type> semantic;
                if (provider_index == -1) semantic = provider_return;
                else {
                    const int ordinal = provider_index == -2 ? 0
                        : provider_index + (has_receiver(provider) ? 1 : 0);
                    if (ordinal >= 0 && static_cast<size_t>(ordinal) < provider_arguments.size())
                        semantic = provider_arguments[ordinal].exact;
                    else supported = false;
                }
                if (n == receiver_index && !semantic) supported = false;
                std::shared_ptr<type> source_type;
                if (source_index == -2 && fn->get_this_parameter())
                    source_type = fn->get_this_parameter()->get_type();
                else if (source_index >= 0 && static_cast<size_t>(source_index) < parameters.size())
                    source_type = parameters[source_index];
                uint32_t binding_flags = 0;
                if (source_index != -1 && !type::are_equal(source_type, semantic)) {
                    auto carrier = implementation_receiver_type(*fn);
                    if (!has_redirect_contract(*fn) || (source_index != -2 && source_index != 0)
                        || !carrier || !type::are_equal(carrier, semantic)
                        || !can_project_redirect_receiver(source_type, carrier)) supported = false;
                    else binding_flags = 128;
                }
                if (source_index != -1 && semantic && !unresolved(semantic)
                    && semantic->get_llvm_type() != arg.getType()) supported = false;
                uint32_t cleanup = 0;
                if (provider_index >= 0 && callee_cleans_owner_parameter(semantic)) {
                    cleanup = 64; native_flags |= 1;
                }
                args.push_back(native_value(arg.getType(), semantic, source_index,
                    body->getAttributes().getParamAttrs(n), cleanup | binding_flags, supported));
            }
            auto* result = native_value(body->getReturnType(), provider_return, -3,
                body->getAttributes().getRetAttrs(), 0, supported);
            llvm::Type* result_storage = native_type(provider_return);
            if (result_index >= 0) {
                if (static_cast<unsigned>(result_index) < body->arg_size()
                    && body->hasParamAttribute(result_index, llvm::Attribute::StructRet))
                    result_storage = body->getParamAttribute(result_index, llvm::Attribute::StructRet).getValueAsType();
                else supported = false;
                if (!body->getReturnType()->isVoidTy()) supported = false;
                if (!provider_return
                    || result_storage != provider_return->get_llvm_type()) supported = false;
            } else if (provider_return
                       && native_type(provider_return) != body->getReturnType()
                       && !dynamic_cast<const destructor*>(&provider)) {
                supported = false;
            }
            auto [result_size, result_align] = layout(result_storage);
            if (result_index >= 0 && static_cast<unsigned>(result_index) < body->arg_size())
                if (auto alignment = body->getAttributes().getParamAttrs(result_index).getAlignment())
                    result_align = std::max(result_align, alignment->value());
            if (result_storage && !result_storage->isVoidTy() && !result_align) supported = false;
            const auto triple = llvm::Triple(mod.getTargetTriple());
            const bool supported_abi = body->getCallingConv() == llvm::CallingConv::C
                && !body->isVarArg() && !body->hasFnAttribute(llvm::Attribute::Naked)
                && triple.isOSLinux() && triple.getArch() == llvm::Triple::x86_64
                && mod.getDataLayout().getPointerSize() == 8
                && mod.getDataLayout().isLittleEndian();
            native = global(record({u32(1), u32(body->getCallingConv()),
                text(mod.getTargetTriple()), text(mod.getDataLayoutStr()), result,
                pointer_array(args, "__k_rtti_native_args"), index(result_index), index(receiver_index),
                u64(result_size), u64(result_align), u32(native_flags), index(body_receiver_index)}),
                "__k_rtti_native_signature");
            if (eligibility == 0 && !supported_abi) eligibility = 7;
            else if (eligibility == 0 && !supported) eligibility = 6;
        }
        const bool refused_contract = eligibility == 10 || provider.is_external() || provider.is_extern();
        llvm::Constant* entry = nil;
        if (body && !fn->is_deleted() && !fn->is_abstract_func()
            && !unresolved_signature && !refused_contract) {
            // Forward declarations are cleared or bound after implementation.
            entry = body->isDeclaration() ? static_cast<llvm::Constant*>(body)
                                         : image_local_reference(body);
        }
        if (eligibility == 0 && entry->isNullValue())
            eligibility = supports_image_local_relocations() ? 4 : 7;
        auto erased_target = fn->get_erased_target();
        auto* erased_body = erased_target ? ctx->lookup_llvm_function(erased_target) : nullptr;
        const auto symbol = erased_target
            ? (erased_body ? erased_body->getName().str() : erased_target->get_mangled_name())
            : provider.get_imported_redirect_contract()
            ? implementation_provider_symbol(*fn)
            : body ? body->getName().str() : fn->get_mangled_name();
        gv->setInitializer(llvm::ConstantStruct::get(signature_record_type(), {
            describe(ret), type_array(parameters, "__k_rtti_signature_params"),
            u32(parameters.size()), native, receiver, origin, u32(eligibility),
            text(symbol), entry,
            type_array(fn->get_throws_spec(), "__k_rtti_throws"), enclosing_origin}));
        return gv;
    }

    llvm::Constant* parameter_record(std::vector<llvm::Constant*> fields,
                                     const std::shared_ptr<parameter>& parameter,
                                     size_t position) {
        if (!parameter || !parameter->get_type())
            throw std::runtime_error("RTTI parameter has no exact semantic type");
        uint32_t flags = (parameter->has_default_expr() ? 1 : 0)
            | (parameter->is_varargs() ? 2 : 0) | (parameter->is_pack_expansion() ? 4 : 0)
            | (parameter->is_ffi_cstring() ? 8 : 0);
        fields.push_back(u32(position));
        fields.push_back(u32(flags));
        fields.push_back(describe(parameter->get_type()));
        return record(fields);
    }

    llvm::Constant* function_record(std::vector<llvm::Constant*> fields,
                                    const std::shared_ptr<function>& function) {
        fields.push_back(signature(function));
        return record(fields);
    }

    // Run after every body/vtable has been filled. Normalize existing language
    // adjustment thunks to the actual declaration in context::_functions.
    // The later executor projects to that body's receiver using the cast map;
    // it must not execute the ordinary vtable adjustment thunk.
    void finalize_virtual_bodies() {
        // A forward descriptor can predate a late native declaration. Complete
        // it in place so existing recursive references keep the same global.
        for (const auto& [model, native] : ctx->_functions) {
            auto found = ctx->_rtti_signatures.find(model.get());
            if (!native || found == ctx->_rtti_signatures.end()
                || !found->second->hasInitializer()) continue;
            auto* init = found->second->getInitializer();
            const auto eligibility = llvm::cast<llvm::ConstantInt>(
                init->getAggregateElement(6u))->getZExtValue();
            if (init->getAggregateElement(3u)->isNullValue()
                && eligibility != 2 && eligibility != 3)
                signature(model, true);
        }
        for (size_t n = 0; n < ctx->_rtti_types.size(); ++n) {
            // signature() may append types; do not retain references into the vector.
            auto t = ctx->_rtti_types[n].first;
            auto* descriptor = ctx->_rtti_types[n].second;
            if (!descriptor->hasInitializer())
                throw std::runtime_error("Unfinished recursive RTTI type descriptor");
            auto st = std::dynamic_pointer_cast<struct_type>(t);
            auto agg = st ? st->get_struct() : nullptr;
            if (!agg || !agg->has_vtable() || unresolved(t)) continue;
            std::vector<llvm::Constant*> bodies;
            auto table = [&](const std::shared_ptr<aggregate>& subobject,
                             const std::shared_ptr<vtable_layout>& vt) {
                auto fail = [&](const std::string& reason, unsigned llvm_slot,
                                llvm::Function* target, const std::shared_ptr<function>& model) {
                    throw std::runtime_error(reason
                        + " [unit=" + source.get_unit_name().to_string()
                        + ", aggregate=" + agg->get_fq_name()
                        + ", sourceSubobject=" + subobject->get_fq_name()
                        + ", abstract=" + (agg->is_abstract() ? "true" : "false")
                        + ", imported=" + (agg->is_imported() ? "true" : "false")
                        + ", slot=" + (llvm_slot ? std::to_string(llvm_slot - 1) : "<none>")
                        + ", llvmSlot=" + std::to_string(llvm_slot)
                        + ", vtable=" + (vt && vt->llvm_global
                            ? vt->llvm_global->getName().str() : "<missing>")
                        + ", target=" + (target ? target->getName().str() : "<missing>")
                        + ", model=" + (model ? model->get_fq_name()
                            + " (" + model->get_mangled_name() + ")" : "<missing>")
                        + ", contracts=" + std::to_string(vt ? vt->entries.size() : 0) + "]");
                };
                if (agg->is_abstract()) {
                    // Ordinary codegen emits abstract RTTI and bodies, but no
                    // abstract instance vtable. Dispatch needs a concrete map.
                    auto declaration = agg->get_vtable();
                    auto* rtti = declaration ? declaration->llvm_rtti_global : nullptr;
                    if (!agg->is_imported()
                        && (!rtti || !rtti->hasInitializer() || !rtti->isConstant()))
                        fail("Missing finalized RTTI abstract declaration", 0, nullptr, nullptr);
                    return;
                }
                if (!vt || !vt->llvm_global || !vt->llvm_global->hasInitializer()) {
                    if (agg->is_imported()) return;
                    fail("Missing finalized RTTI virtual table", 0, nullptr, nullptr);
                }
                auto* init = vt->llvm_global->getInitializer();
                auto* table_type = llvm::dyn_cast<llvm::StructType>(init->getType());
                if (!table_type) fail("Unsupported RTTI virtual table initializer", 0, nullptr, nullptr);
                for (unsigned slot = 1; slot < table_type->getNumElements(); ++slot) {
                    auto* entry = init->getAggregateElement(slot);
                    auto* target = llvm::dyn_cast<llvm::Function>(entry->stripPointerCasts());
                    // Vtable generators append entries in vector order; the
                    // secondary layout records the selected override in that order.
                    auto model = slot - 1 < vt->entries.size() ? vt->entries[slot - 1].func : nullptr;
                    if (!target) {
                        if (!entry->isNullValue())
                            fail("Unsupported RTTI virtual target relocation", slot, target, model);
                        continue; // an abstract slot has no callable body
                    }
                    if (vt->entries.size() + 1 != table_type->getNumElements())
                        fail("RTTI virtual table contract count differs from its initializer", slot, target, model);
                    if (!model || !has_receiver(*model))
                        fail("Missing semantic RTTI virtual slot contract", slot, target, model);
                    auto* actual_body = ctx->lookup_llvm_function(model);
                    if (!actual_body)
                        fail("Missing registered RTTI virtual body", slot, target, model);
                    if (target != actual_body) {
                        // Only normalize a single forwarding call to an actual
                        // registered K body. Names and pointer shapes are not proof.
                        if (!target->hasLocalLinkage() || target->size() != 1)
                            fail("RTTI virtual target is not a local receiver forwarder", slot, target, model);
                        llvm::CallBase* forward = nullptr;
                        for (auto& block : *target) for (auto& instruction : block) {
                            auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                            if (call) {
                                if (forward) fail("Ambiguous RTTI virtual body mapping", slot, target, model);
                                forward = call;
                            } else if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
                                if (!load->isSimple())
                                    fail("Side-effecting RTTI receiver forwarder", slot, target, model);
                            } else if (!llvm::isa<llvm::GetElementPtrInst>(instruction)
                                       && !llvm::isa<llvm::CastInst>(instruction)
                                       && !llvm::isa<llvm::ReturnInst>(instruction)
                                       && instruction.getOpcode() != llvm::Instruction::Add
                                       && instruction.getOpcode() != llvm::Instruction::Sub) {
                                fail("Unsupported RTTI receiver forwarder instruction", slot, target, model);
                            }
                        }
                        if (!forward || forward->getCalledFunction() != actual_body
                            || forward->arg_size() != target->arg_size())
                            fail("RTTI virtual slot is not a single-body forwarder", slot, target, model);
                        const auto mapping = native_argument_sources(model, actual_body);
                        for (unsigned a = 0; a < forward->arg_size(); ++a)
                            if ((a >= mapping.size() || mapping[a] != -2)
                                && forward->getArgOperand(a) != target->getArg(a))
                                fail("RTTI receiver forwarder changes an explicit argument", slot, target, model);
                        auto* ret = llvm::dyn_cast<llvm::ReturnInst>(target->getEntryBlock().getTerminator());
                        if (!ret || (ret->getReturnValue() && ret->getReturnValue() != forward))
                            fail("RTTI receiver forwarder changes the native result", slot, target, model);
                    }
                    auto receiver_owner = implementation_receiver_owner(*model);
                    if (!receiver_owner)
                        fail("Missing direct RTTI virtual body mapping", slot, target, model);
                    bodies.push_back(global(record({describe(subobject->get_struct_type()),
                        u32(slot - 1), signature(model), describe(receiver_owner->get_struct_type()),
                        image_local_reference(vt->llvm_global),
                        projection(*agg, *subobject, *receiver_owner)}),
                        "__k_rtti_virtual_body"));
                }
            };
            table(agg, agg->get_vtable());
            if (auto klass = std::dynamic_pointer_cast<k::model::klass>(agg))
                for (const auto& [base, vt] : klass->get_secondary_vtables()) table(base, vt);
            auto* old = descriptor->getInitializer();
            std::vector<llvm::Constant*> fields;
            for (unsigned f = 0; f < type_record_type()->getNumElements(); ++f)
                fields.push_back(f == 15 ? pointer_array(bodies, "__k_rtti_virtual_bodies")
                                        : old->getAggregateElement(f));
            descriptor->setInitializer(record(fields));
        }
        std::vector<llvm::GlobalValue*> roots;
        for (const auto& [model, descriptor] : ctx->_rtti_signatures) {
            if (!descriptor->hasInitializer())
                throw std::runtime_error("Unfinished recursive RTTI signature for " + model->get_fq_name());
            auto* init = descriptor->getInitializer();
            auto* entry = init->getAggregateElement(8u);
            auto* body = llvm::dyn_cast<llvm::Function>(entry->stripPointerCastsAndAliases());
            if (body && body->isDeclarationForLinker() && !native_source(*model).is_external()
                    && !native_source(*model).is_extern()) {
                // A declaration is not a callable body. Preserve its native
                // shape and any more specific ineligibility, but no entry.
                const auto eligibility = llvm::cast<llvm::ConstantInt>(
                    init->getAggregateElement(6u))->getZExtValue();
                std::vector<llvm::Constant*> fields;
                for (unsigned f = 0; f < signature_record_type()->getNumElements(); ++f)
                    fields.push_back(f == 6 && eligibility == 0 ? u32(4) : f == 8 ? nil
                        : init->getAggregateElement(f));
                descriptor->setInitializer(record(fields));
            } else if (body && !body->isDeclarationForLinker()) {
                auto* local = image_local_reference(body);
                if (entry != local) {
                    std::vector<llvm::Constant*> fields;
                    const auto eligibility = llvm::cast<llvm::ConstantInt>(
                        init->getAggregateElement(6u))->getZExtValue();
                    for (unsigned f = 0; f < signature_record_type()->getNumElements(); ++f)
                        fields.push_back(f == 6 && eligibility == 0 && local->isNullValue()
                            ? u32(7) : f == 8 ? local : init->getAggregateElement(f));
                    descriptor->setInitializer(record(fields));
                }
                roots.push_back(body);
            }
        }
        if (!roots.empty()) llvm::appendToCompilerUsed(mod, roots);
    }
};

} // namespace k::model::gen
#endif
