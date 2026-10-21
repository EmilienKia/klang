/*
 * Borrowed Argument compiler contract.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef KLANG_GEN_ARGUMENTS_HPP
#define KLANG_GEN_ARGUMENTS_HPP

#include "../model/expressions.hpp"
#include <string_view>

namespace k::model::gen {

enum class argument_intrinsic {
    none, invalid, capture, borrow, read, get, take_owner, take, drain, is_null
};

// Private native Argument ABI, in the same order as argument.k. The slot's
// extra indirection is never represented as a public K double-pointer type.
enum argument_field : unsigned {
    argument_slot, argument_logical, argument_binding, argument_referent,
    argument_category_field, argument_capabilities, argument_indirect, argument_field_count
};

bool is_canonical_argument_intrinsic(const function& fn, std::string_view name);
argument_intrinsic get_argument_intrinsic(const function& fn);
// Final lowering gate; provisional overload recognition must not require layout.
bool has_argument_intrinsic_contract(const function& fn);

struct argument_source {
    std::shared_ptr<type> logical;
    std::shared_ptr<type> binding;
    std::shared_ptr<type> referent;
    unsigned category = 0;
    unsigned capabilities = 0;
    bool indirect = false;
    bool variable_slot = false;
};

// Throws invalid_argument for known non-addressable/temporary explicit captures.
argument_source describe_argument_source(const std::shared_ptr<expression>& expr);
std::shared_ptr<type> argument_access_type(const function& fn);

} // namespace k::model::gen
#endif
