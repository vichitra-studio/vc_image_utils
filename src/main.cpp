// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// imgtoy -- load an image, apply one operation, write a PNG.
//
// The point of this program is not the operations; those already exist, tested,
// in examples/. It is to run the whole path end to end for the first time
// OUTSIDE a self-checking example: a real file in, a real file out, errors
// reported to a human rather than to an assertion.
//
// ---- Deliberately not a CLI framework ----
//
// Positional arguments and an if/else chain. No option parser, no subcommand
// registry, no help generator. P1's contract says: "if you find yourself
// writing a CLI argument parser or a benchmark framework, stop -- that is P4
// reconnaissance at most, noted, not built." The CLI front end with composable
// subcommands and JSON output is a P4 deliverable, and building a small version
// of it now would mean throwing that version away.
//
// ---- What is missing here, and why it is the interesting part ----
//
// Only PER-PIXEL operations. There is no rotate, no scale, no warp -- even
// though example 03 implements all three and they are the most obviously
// useful things a tool like this could do.
//
// The reason is worth recording rather than working around: warp lives in an
// anonymous namespace inside examples/03_warp.cpp, and examples/README.md says
// an example must be "a single .cpp with a main() -- no framework, no hidden
// setup, readable top to bottom". So there are exactly two ways to reach it
// from here, and both are decisions this phase should not be making:
//
//   duplicate the sampler into this file  -- ~150 lines of copied algorithm,
//                                            with two copies to keep correct
//   promote it into the library           -- which means deciding where image
//                                            operations live, what the buffer
//                                            abstraction owes them, and what
//                                            the module interface looks like
//
// The second is P4's whole job. The first is what you do instead of doing the
// second, and it is worse.
//
// So the friction is left standing and written down. That IS this session's
// output: four phases of toys have now produced one operation family that
// genuinely wants to be shared and cannot be, and the shape of what it wants
// is the input P4 needs. Note it; do not fix it here.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/core/vc_types.h"
#include "vc/io/vc_io_stb.h"

namespace {

// Rec.601 luma. The same weights example 01 verified to within 1 LSB -- and
// the same caveat: the input is gamma-encoded, so this is a perceptual
// grayscale, not a physically-linear one. Linear-light conversion needs the
// colour science that arrives at P6.
constexpr float luma_r = 0.299F;
constexpr float luma_g = 0.587F;
constexpr float luma_b = 0.114F;

[[nodiscard]] vc::vc_image to_grayscale(const vc::vc_image& src) {
    if (src.channels() < 3) {
        throw vc::vc_exception(vc::vc_error_code::invalid_argument,
                               "grayscale: input needs at least 3 channels");
    }
    vc::vc_image_writer out{src.width(), src.height(), 1, vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            out.at<vc::buf_f32>(x, y, 0) =
                (luma_r * src.at<vc::buf_f32>(x, y, 0)) +
                (luma_g * src.at<vc::buf_f32>(x, y, 1)) +
                (luma_b * src.at<vc::buf_f32>(x, y, 2));
        }
    }
    return std::move(out).seal();
}

// Applied to every channel including alpha, which is wrong for an RGBA image
// -- inverting opacity is not what anyone means. Left as is because nothing
// upstream distinguishes an alpha channel yet: channel_count says how many
// there are, not what they mean. That distinction is part of the image
// descriptor P4 designs (colour space, channel kind), and inventing a
// half-version of it here would be the same mistake as duplicating the warp.
[[nodiscard]] vc::vc_image invert(const vc::vc_image& src) {
    vc::vc_image_writer out{src.width(), src.height(), src.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            for (vc::channel_count ch = 0; ch < src.channels(); ++ch) {
                out.at<vc::buf_f32>(x, y, ch) =
                    1.0F - src.at<vc::buf_f32>(x, y, ch);
            }
        }
    }
    return std::move(out).seal();
}

// Multiplies. NOT clamped: values above 1.0 survive into the returned image,
// and only the PNG writer clips them on the way out. That is deliberate --
// clamping here would destroy highlight data that a later stage might want,
// and it is the display, not the pipeline, that has a maximum. The
// scene-referred worldview this belongs to arrives properly at P4.5.
[[nodiscard]] vc::vc_image scale_values(const vc::vc_image& src, float factor) {
    vc::vc_image_writer out{src.width(), src.height(), src.channels(),
                            vc::buf_f32{0.0F}};
    for (vc::image_dim y = 0; y < src.height(); ++y) {
        for (vc::image_dim x = 0; x < src.width(); ++x) {
            for (vc::channel_count ch = 0; ch < src.channels(); ++ch) {
                out.at<vc::buf_f32>(x, y, ch) =
                    src.at<vc::buf_f32>(x, y, ch) * factor;
            }
        }
    }
    return std::move(out).seal();
}

void print_usage(const char* program) {
    std::cerr << "usage: " << program << " <input> <output.png> <op> [arg]\n"
              << "\nops:\n"
              << "  copy               decode and re-encode, nothing else\n"
              << "  grayscale          Rec.601 luma; 3+ channels in, 1 out\n"
              << "  invert             1 - v, every channel\n"
              << "  brightness <k>     multiply every channel by k\n"
              << "\ngeometric operations (rotate/scale/warp) are deliberately\n"
              << "absent -- see the note at the top of src/main.cpp\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage(argc > 0 ? argv[0] : "imgtoy");
        return EXIT_FAILURE;
    }

    const vc::io::path input_path = argv[1];
    const vc::io::path output_path = argv[2];
    const std::string_view op = argv[3];

    // One try around everything. A CLI has exactly one sensible response to a
    // failure anywhere in the pipeline -- say what went wrong and exit -- so
    // there is nothing to gain from catching closer to each call.
    try {
        vc::io::stb_image_reader reader;
        vc::io::stb_image_writer writer;

        // f32 rather than the u8 default: every operation below is arithmetic,
        // and 8-bit arithmetic loses precision at every step while float has
        // roughly 30,000x the headroom over the same [0, 1] range.
        const vc::vc_image input = reader.read(
            input_path, vc::io::read_config{.dtype = vc::pixel_dtype::f32});

        std::cout << "in:  " << input.width() << " x " << input.height()
                  << " x " << input.channels() << '\n';

        vc::vc_image output = input;
        if (op == "copy") {
            // nothing -- the decode/encode round trip on its own
        } else if (op == "grayscale") {
            output = to_grayscale(input);
        } else if (op == "invert") {
            output = invert(input);
        } else if (op == "brightness") {
            if (argc < 5) {
                std::cerr << "brightness needs a factor, e.g. "
                             "`brightness 1.5`\n";
                return EXIT_FAILURE;
            }
            // stof throws on a non-numeric argument; the catch below reports
            // it. Checking the string by hand would be re-implementing what
            // the standard library already does correctly.
            output = scale_values(input, std::stof(argv[4]));
        } else {
            std::cerr << "unknown op: " << op << "\n\n";
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        writer.write(
            output_path, output,
            vc::io::write_config{.format = vc::io::vc_image_format::png});

        std::cout << "out: " << output.width() << " x " << output.height()
                  << " x " << output.channels() << "  -> "
                  << output_path.string() << '\n';
        return EXIT_SUCCESS;

    } catch (const vc::vc_exception& e) {
        std::cerr << "[" << vc::to_string(e.code()) << "] " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        // Everything the library does not raise itself -- std::stof on a bad
        // argument, a filesystem error escaping the io layer. Reported rather
        // than allowed to terminate, so the user gets a message instead of an
        // abort.
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
