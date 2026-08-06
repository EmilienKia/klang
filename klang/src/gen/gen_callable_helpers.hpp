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

#ifndef KLANG_GEN_CALLABLE_HELPERS_HPP
#define KLANG_GEN_CALLABLE_HELPERS_HPP

#include <memory>
#include <vector>

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>

#include "../model/context.hpp"
#include "../model/model.hpp"
#include "../model/type.hpp"

namespace k::model::gen {

/**
 * Peel indirections and const layers until the underlying fat callable type is
 * reached. Returns nullptr when @p t does not denote a fat callable.
 *
 * An unbound member function reference (`T::*(int)`) is deliberately *not* a fat
 * callable: it keeps the historical bare function pointer representation.
 */
inline std::shared_ptr<callable_type> peel_to_callable(std::shared_ptr<type> t) {
    for (unsigned int guard = 0; t && guard < 8u; ++guard) {
        t = type::remove_const(t);
        if (auto ct = std::dynamic_pointer_cast<callable_type>(t)) {
            return ct->is_unbound_member() ? nullptr : ct;
        }
        if (type::is_reference(t) || type::is_link(t) || type::is_pointer(t)
            || type::is_view(t) || type::is_drain(t)) {
            t = t->get_subtype();
            continue;
        }
        break;
    }
    return nullptr;
}

/** Build a `%__k.callable` value from its two fields. */
inline llvm::Value* build_callable_value(llvm::IRBuilder<>& builder,
                                         llvm::StructType* callable_ty,
                                         llvm::Value* fn_ptr,
                                         llvm::Value* ctx_ptr)
{
    llvm::Value* agg = llvm::UndefValue::get(callable_ty);
    agg = builder.CreateInsertValue(agg, fn_ptr, {0}, "callable.fn");
    agg = builder.CreateInsertValue(agg, ctx_ptr, {1}, "callable.ctx");
    return agg;
}

/** Extract the function address field of a `%__k.callable` value. */
inline llvm::Value* extract_fn(llvm::IRBuilder<>& builder, llvm::Value* callable_val) {
    return builder.CreateExtractValue(callable_val, {0}, "callable.fn");
}

/** Extract the invocation context field of a `%__k.callable` value. */
inline llvm::Value* extract_ctx(llvm::IRBuilder<>& builder, llvm::Value* callable_val) {
    return builder.CreateExtractValue(callable_val, {1}, "callable.ctx");
}

/**
 * Select, among the overloads of @p candidates, the single one whose parameter
 * list matches the prototype of @p proto.
 *
 * Selection uses the *parameter* list only: K cannot overload on return type
 * (design decision D11), so a surviving candidate whose return type is
 * incompatible must be rejected by the caller instead of being discarded here.
 *
 * @return The unique match, or nullptr when there is none or several.
 */
inline std::shared_ptr<function> select_overload_for_prototype(
    const std::vector<std::shared_ptr<function>>& candidates,
    const callable_type& proto,
    bool* ambiguous = nullptr)
{
    if (ambiguous) *ambiguous = false;
    std::shared_ptr<function> found;
    for (const auto& cand : candidates) {
        if (!cand) continue;
        if (cand->get_parameter_size() != proto.get_parameter_types().size()) continue;
        bool ok = true;
        for (size_t i = 0; i < proto.get_parameter_types().size(); ++i) {
            auto p = cand->get_parameter(i);
            if (!p || !type::are_equal(p->get_type(), proto.get_parameter_types()[i])) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        if (found) {
            if (ambiguous) *ambiguous = true;
            return nullptr;
        }
        found = cand;
    }
    return found;
}

} // namespace k::model::gen

#endif // KLANG_GEN_CALLABLE_HELPERS_HPP
