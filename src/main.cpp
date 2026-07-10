// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#include <cstdlib>
#include <iostream>

#include "vc/io/vc_io.h"
#include "vc/vc_exception.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <input-image> <output.png>\n";
        return EXIT_FAILURE;
    }

    const vc::io::path input_path = argv[1];
    const vc::io::path output_path = argv[2];

    // TODO(you): implement the round-trip.
    //   1. vc::io::stb_image_reader reader;
    //      vc::io::stb_image_writer writer;
    //   2. Wrap in try { ... } catch (const vc::vc_exception& e) { ... }
    //   3. vc::vc_image img = reader.read(input_path);
    //      writer.write(output_path, img);
    //   4. On catch: print "[" << vc::to_string(e.code()) << "] " << e.what()
    //      to std::cerr and return EXIT_FAILURE.
    //   5. On success: print a short confirmation (dimensions/channels) and
    //      return EXIT_SUCCESS.

    (void)input_path;
    (void)output_path;
    std::cerr << "TODO: not yet implemented\n";
    return EXIT_FAILURE;
}
