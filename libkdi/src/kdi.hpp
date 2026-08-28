/*
 * K Language compiler — libkdi
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

#ifndef LIBKDI_KDI_HPP
#define LIBKDI_KDI_HPP

/**
 * @file kdi.hpp
 *
 * Public umbrella header for libkdi.
 *
 * Include this header to get access to all KDI DTOs, serialisation and
 * deserialisation functions, and validation utilities.
 */

// DTOs
#include "kdi_types.hpp"
#include "kdi_aggregates.hpp"
#include "kdi_file.hpp"

// I/O
#include "kdi_cbor.hpp"
#include "kdi_json.hpp"

// Dump
#include "kdi_doc.hpp"
#include "kdi_docgen.hpp"
#include "kdi_dump.hpp"
#include "kdi_mangling.hpp"
#include "kdi_query.hpp"

// Validation
#include "kdi_validate.hpp"

// Symbol cross-check
#include "kdi_symbols.hpp"

#endif // LIBKDI_KDI_HPP
