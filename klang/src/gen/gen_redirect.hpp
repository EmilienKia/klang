/*
 * K Language compiler
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef KLANG_GEN_REDIRECT_HPP
#define KLANG_GEN_REDIRECT_HPP

#include "../model/model.hpp"
#include "../model/model_aggregate.hpp"
#include "../model/imported.hpp"
#include "../model/type.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>

namespace k::model::gen {

inline const function& redirect_provider(const function& fn) {
    return fn.is_redirected() && fn.get_redirect_target()
        ? redirect_provider(*fn.get_redirect_target()) : fn;
}

inline bool has_redirect_contract(const function& fn) {
    return fn.is_redirected() || fn.get_imported_redirect_contract().has_value();
}

inline const std::string& implementation_provider_symbol(const function& fn) {
    const auto& provider = redirect_provider(fn);
    const auto& imported = provider.get_imported_redirect_contract();
    return imported ? imported->symbol : provider.get_mangled_name();
}

inline bool implementation_provider_is_member(const function& fn) {
    const auto& provider = redirect_provider(fn);
    const auto& imported = provider.get_imported_redirect_contract();
    return imported ? imported->is_member : provider.is_member() && !provider.is_static();
}

inline bool implementation_provider_is_external(const function& fn) {
    const auto& provider = redirect_provider(fn);
    return provider.is_extern() || dynamic_cast<const imported_function*>(&provider)
        || dynamic_cast<const imported_method*>(&provider);
}

inline bool can_project_redirect_receiver(const std::shared_ptr<type>& source,
                                          const std::shared_ptr<type>& target) {
    if (!type::is_reference(source) || !type::is_reference(target)) return false;
    auto source_object = source->get_subtype();
    auto target_object = target->get_subtype();
    if (type::is_const(source_object) && !type::is_const(target_object)) return false;
    auto source_struct = std::dynamic_pointer_cast<struct_type>(type::remove_const(source_object));
    auto target_struct = std::dynamic_pointer_cast<struct_type>(type::remove_const(target_object));
    if (!source_struct || !target_struct) return false;
    auto source_owner = source_struct->get_struct();
    auto target_owner = target_struct->get_struct();
    return source_owner && target_owner
        && (source_owner == target_owner || source_owner->is_derived_from(target_owner));
}

inline std::shared_ptr<type> implementation_receiver_type(const function& fn) {
    if (fn.get_redirect_receiver_type()) return fn.get_redirect_receiver_type();
    const auto& provider = redirect_provider(fn);
    if (const auto& imported = provider.get_imported_redirect_contract())
        return imported->receiver_type;
    if (provider.is_member() && !provider.is_static()) {
        auto receiver = provider.get_this_parameter();
        return receiver ? receiver->get_type() : nullptr;
    }
    return fn.is_member() && !fn.is_static() && !provider.parameters().empty()
        ? provider.parameters().front()->get_type() : nullptr;
}

inline std::shared_ptr<type> source_receiver_type(const function& fn) {
    if (fn.is_member() && !fn.is_static()) {
        auto receiver = fn.get_this_parameter();
        return receiver ? receiver->get_type() : nullptr;
    }
    return fn.parameters().empty() ? nullptr : fn.parameters().front()->get_type();
}

inline std::shared_ptr<aggregate> implementation_receiver_owner(const function& fn) {
    auto receiver = implementation_receiver_type(fn);
    auto object = receiver ? type::remove_const(receiver->get_subtype()) : nullptr;
    auto structure = std::dynamic_pointer_cast<struct_type>(object);
    return structure ? structure->get_struct() : nullptr;
}

llvm::Value* project_redirect_receiver(llvm::IRBuilder<>& builder, llvm::Value* receiver,
                                      const function& fn, const llvm::DataLayout& layout);

} // namespace k::model::gen

#endif
