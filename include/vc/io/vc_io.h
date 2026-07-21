// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "vc/io/vc_io_types.h"
#include "vc/vc_image.h"

namespace vc::io {

class i_image_reader {
  public:
    virtual ~i_image_reader() = default;
    virtual vc::vc_image read(const path& p,
                              const read_config& config = {}) = 0;
};

class i_image_writer {
  public:
    virtual ~i_image_writer() = default;
    virtual void write(const path& p,
                       const vc::vc_image& image,
                       const write_config& config = {}) = 0;
};

// stb-backed implementation of both interfaces; see src/io/vc_io_stb.cpp.
class stb_image_reader : public i_image_reader {
  public:
    vc::vc_image read(const path& p, const read_config& config = {}) override;
};

class stb_image_writer : public i_image_writer {
  public:
    void write(const path& p,
               const vc::vc_image& image,
               const write_config& config = {}) override;
};

} // namespace vc::io
