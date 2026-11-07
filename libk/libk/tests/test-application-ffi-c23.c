/*
 * Verify that the Application FFI builds without the removed C23 atomic macro.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#define _GNU_SOURCE
#include <stdatomic.h>
#undef ATOMIC_VAR_INIT

#include "../src/runtime/application_ffi.c"
