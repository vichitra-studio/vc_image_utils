// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// Single translation unit that compiles stb image implementations.
// Isolating the defines here keeps compilation cost out of library sources.
// Warnings from stb headers are suppressed via SYSTEM include path in CMakeLists.txt.

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
