/*
* K Language compiler
 *
 * Copyright 2026 Emilien Kia
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

#ifndef KLANG_PROCESS_HPP
#define KLANG_PROCESS_HPP


#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace k::tools {

struct exec_result {
    int exit_code;
    std::string out;
    std::string err;
};


exec_result lookup_run_process(
    const std::string& exe_name,
    const std::vector<std::string>& args = {},
    const std::optional<std::string>& stdin_data = std::nullopt,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt
);

exec_result run_process(
    const std::string& exe_path,
    const std::vector<std::string>& args = {},
    const std::optional<std::string>& stdin_data = std::nullopt,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt
);

} // k::tools
#endif //KLANG_PROCESS_HPP