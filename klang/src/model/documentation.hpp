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

#ifndef KLANG_MODEL_DOCUMENTATION_HPP
#define KLANG_MODEL_DOCUMENTATION_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../parse/ast.hpp"

namespace k::model {
class element;
}

namespace k::model::doc {

struct doc_entity {
    std::string brief;
    std::string description;
    std::weak_ptr<element> owner;
    virtual ~doc_entity() = default;
};

struct unit_doc : public doc_entity {};
struct namespace_doc : public doc_entity {};
struct aggregate_doc : public doc_entity {};
struct union_doc : public doc_entity {};
struct enum_doc : public doc_entity {};
struct variable_doc : public doc_entity {};

struct param_doc {
    std::string name;
    std::string description;
};

struct return_doc {
    std::string description;
};

struct throws_doc {
    std::string type_name;
    std::string description;
};

struct template_param_doc {
    std::string name;
    std::string description;
};

struct tagged_doc {
    std::string tag;
    std::string value;
};

struct function_doc : public doc_entity {
    std::vector<param_doc> params;
    std::optional<return_doc> returns;
    std::vector<throws_doc> throws;
    std::vector<template_param_doc> template_params;
    std::vector<tagged_doc> tags;
};

/**
 * Build a doc_entity (or any subtype) from a structured AST documentation node.
 * Copies brief + description; function-specific fields are ignored for
 * non-function doc types.
 */
std::shared_ptr<doc_entity> build_doc_entity(
    const std::shared_ptr<element>& owner,
    const parse::ast::documentation& doc);

/**
 * Build a function_doc from a structured AST documentation node.
 * Copies all fields: brief, description, params, returns, throws,
 * template_params, and generic tags.
 */
std::shared_ptr<function_doc> build_function_doc(
    const std::shared_ptr<element>& owner,
    const parse::ast::documentation& doc);

template<typename DocT>
std::shared_ptr<DocT> build_typed_doc(
    const std::shared_ptr<element>& owner,
    const parse::ast::documentation& doc)
{
    auto out = std::make_shared<DocT>();
    out->brief       = doc.brief;
    out->description = doc.description;
    out->owner       = owner;
    return out;
}

} // namespace k::model::doc

#endif //KLANG_MODEL_DOCUMENTATION_HPP
