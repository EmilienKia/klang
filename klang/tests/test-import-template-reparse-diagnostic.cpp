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

/**
 * Regression test for TODO.md "Silent failure when re-parsing an imported
 * template definition.":
 *
 * `kdi_importer::materialise_template_def()` re-parses the template source
 * text carried verbatim inside the KDI. If that source fails to parse (or
 * model-build) — e.g. because it was produced by an incompatible/buggy
 * compiler version, or the KDI file was corrupted — the template used to
 * become silently unavailable for cross-module instantiation, with no
 * diagnostic at all.
 *
 * This test builds a normal library KDI exporting a template, corrupts the
 * template's stored source text directly in the on-disk .kdi file (CBOR),
 * then re-imports it and verifies that a `compiler_diag::
 * ERR_KDI_TEMPLATE_REPARSE_FAILED` error diagnostic is now reported instead
 * of silence.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/errors.hpp"

#include <filesystem>

TEST_CASE("Broken imported template source is reported instead of silently dropped",
          "[import][template][kdi][reparse-diagnostic]") {
    // Build a normal library exporting a template with a body (not just a
    // generic signature), so the KDI carries its source text verbatim.
    std::string kdi_path = build_kdi_for_import_warning_test(R"K(
        module tpl_reparse_lib;

        template<typename T>
        struct Holder {
            val : T;
        }
    )K");

    // Corrupt the stored template source text directly in the .kdi file so
    // that re-parsing it during a subsequent import fails.
    {
        kdi::kdi_file file = kdi::kdi_read_cbor_file(kdi_path);
        bool found = false;
        for (auto& tdef : file.unit.root_ns.template_defs) {
            if (tdef.name == "Holder") {
                tdef.source = "this is not valid K syntax {{{ ???";
                found = true;
            }
        }
        REQUIRE(found);
        REQUIRE(kdi::kdi_write_cbor_file(file, kdi_path));
    }

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tpl_reparse_lib", kdi_path);

    auto diags = run_importer_with_logger("tpl_reparse_consumer", {"tpl_reparse_lib"}, *resolver);

    bool found_error = false;
    for (const auto& d : diags) {
        if (d.code == static_cast<unsigned int>(k::diag::compiler_diag::ERR_KDI_TEMPLATE_REPARSE_FAILED)) {
            found_error = true;
            CHECK(d.level == k::log::diagnostic::severity::error);
        }
    }
    CHECK(found_error);

    std::filesystem::remove(kdi_path);
    std::filesystem::remove(std::filesystem::path(kdi_path).replace_extension(".so"));
}
