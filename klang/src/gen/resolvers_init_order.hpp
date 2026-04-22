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
#ifndef KLANG_RESOLVERS_INIT_ORDER_HPP
#define KLANG_RESOLVERS_INIT_ORDER_HPP
#include "resolvers_common.hpp"
namespace k::model::gen {
/**
 * Global initialization/finalization order resolver.
 *
 * This pass runs AFTER type_reference_resolver has registered all global
 * variables and static constructors into the global_constructor_function.
 * It computes a single unified topological ordering over all "init items"
 * (static_constructors and global_variable_definitions) and stores it in
 * the global_constructor_function and global_destructor_function.
 *
 * Algorithm: Kahn's BFS topological sort on a dependency graph.
 * Construction order = topological order; Destruction order = exact reverse.
 */
class init_order_resolver : protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;
    [[noreturn]] void throw_error(unsigned int code,
                                  const std::string& message,
                                  const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }
public:
    init_order_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& u)
        : k::log::logger_relay(logger), _context(context), _unit(u) {}
    /**
     * Run the resolver: compute the unified ordered init/finit sequence and
     * store it into the global_constructor_function and global_destructor_function.
     */
    void resolve();
private:
    /** An init node is either a static_constructor or a global_variable_definition. */
    using node_t = init_item; // alias for clarity
    /** Return a human-readable label for a node (for error messages). */
    static std::string node_label(const node_t& n);
    void collect_global_deps_from_expr(
        const std::shared_ptr<expression>& expr,
        std::vector<std::shared_ptr<global_variable_definition>>& out_globals,
        std::vector<std::shared_ptr<struct_type>>&               out_struct_types,
        std::unordered_set<const function*>&                     visited_funcs);
    void collect_deps_for_global(
        const std::shared_ptr<global_variable_definition>& gv,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx);
    void collect_deps_for_sctor(
        const std::shared_ptr<static_constructor>& sctor,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx);
};
} // namespace k::model::gen
#endif //KLANG_RESOLVERS_INIT_ORDER_HPP
