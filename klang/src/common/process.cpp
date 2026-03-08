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

#include "process.hpp"

#include <boost/process.hpp>


namespace k::tools {

namespace bp = boost::process;

exec_result lookup_run_process(
    const std::string& exe_name,
    const std::vector<std::string>& args,
    const std::optional<std::string>& stdin_data,
    std::optional<std::chrono::milliseconds> timeout
) {
    bp::filesystem::path exe_path = bp::search_path(exe_name);
    if (exe_path.empty())
        throw tool_not_found(exe_name);
    return run_process(exe_path.string(), args, stdin_data, timeout);
}

std::filesystem::path lookup_tool(const std::string& exe_name) {
    bp::filesystem::path p = bp::search_path(exe_name);
    if (p.empty())
        throw tool_not_found(exe_name);
    return std::filesystem::path(p.string());
}

exec_result run_process(
    const std::string& exe_path,
    const std::vector<std::string>& args,
    const std::optional<std::string>& stdin_data,
    std::optional<std::chrono::milliseconds> timeout
) {
    std::ostringstream out_buf, err_buf;

    bp::ipstream out_stream;   // child's stdout
    bp::ipstream err_stream;   // child's stderr
    bp::opstream in_stream;    // child's stdin

    // Build command line
    std::vector<std::string> cmd{exe_path};
    cmd.insert(cmd.end(), args.begin(), args.end());

    bp::child child(
        bp::exe = exe_path,
        bp::args = args,
        bp::std_out > out_stream,
        bp::std_err > err_stream,
        bp::std_in < in_stream
    );

    // If you have input, write it and close stdin to signal EOF
    if (stdin_data) {
        in_stream << *stdin_data;
    }
    //in_stream.pipe().close();

    // Optionally implement a simple timeout by polling for completion
    const auto start = std::chrono::steady_clock::now();

    // Drain the streams while the process runs to avoid deadlocks with large outputs
    std::string line;
    std::string errline;
    while (child.running()) {
        // read what is available (non-blocking-ish per line)
        while (std::getline(out_stream, line)) {
            out_buf << line << '\n';
        }
        while (std::getline(err_stream, errline)) {
            err_buf << errline << '\n';
        }
        if (timeout && (std::chrono::steady_clock::now() - start) > *timeout) {
            child.terminate();
            throw std::runtime_error("Process timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Drain any remaining data after process exits
    while (std::getline(out_stream, line)) {
        out_buf << line << '\n';
    }
    while (std::getline(err_stream, errline)) {
        err_buf << errline << '\n';
    }

    child.wait();
    return exec_result{
        child.exit_code(),
        out_buf.str(),
        err_buf.str()
    };
}


} // k::tools