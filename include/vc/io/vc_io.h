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

  protected:
    // A user-declared copy constructor suppresses the implicit default one,
    // so it must be restated explicitly for stb_image_reader (vc_io_stb.h,
    // and any other derived class) to keep default-constructing.
    i_image_reader() = default;

    // Protected copy ops: block slicing assignment/construction through a
    // base i_image_reader&/by-value while still letting a derived reader's
    // own (compiler-generated) copy ops chain to these.
    i_image_reader(const i_image_reader&) = default;
    i_image_reader& operator=(const i_image_reader&) = default;
};

class i_image_writer {
  public:
    virtual ~i_image_writer() = default;
    virtual void write(const path& p,
                       const vc::vc_image& image,
                       const write_config& config = {}) = 0;

  protected:
    // See i_image_reader above: the copy ctor's declaration would otherwise
    // suppress the implicit default one that stb_image_writer (vc_io_stb.h)
    // relies on.
    i_image_writer() = default;

    // Slicing prevention, same reasoning as i_image_reader above.
    i_image_writer(const i_image_writer&) = default;
    i_image_writer& operator=(const i_image_writer&) = default;
};

} // namespace vc::io
