// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// The ONE translation unit that compiles nanobench's implementation. Every
// other bench TU includes <nanobench.h> for declarations only; defining
// ANKERL_NANOBENCH_IMPLEMENT in exactly one place is what keeps the single
// vendored header ODR-clean across the bench executables — the same single-impl
// shape as doctest's DOCTEST_CONFIG_IMPLEMENT and stb's STB_IMAGE_IMPLEMENTATION
// (docs/benchmarking.md Sec 3.3). This file has no other contents by design.
#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"
