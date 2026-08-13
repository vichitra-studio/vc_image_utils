// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/core/vc_image.h"
#include "vc/core/vc_image_meta.h"
#include "vc/core/vc_image_writer.h"
#include "vc/core/vc_pixel_buffer.h"
#include "vc/edit/vc_memory_image_meta.h"
#include "vc/io/vc_io_stb.h"

#ifndef VC_TEST_DATA_DIR
#error "VC_TEST_DATA_DIR must be defined by CMake (see CMakeLists.txt)"
#endif
#ifndef VC_TEST_OUTPUT_DIR
#error "VC_TEST_OUTPUT_DIR must be defined by CMake (see CMakeLists.txt)"
#endif

TEST_CASE("vc_image: zeros() validates and allocates") {
    vc::vc_image img = vc::vc_image::zeros<vc::buf_f32>(4, 2, 3);
    CHECK(img.width() == 4);
    CHECK(img.height() == 2);
    CHECK(img.channels() == 3);
    CHECK(img.pixel_count() == 4 * 2 * 3);
    REQUIRE(img.pixels() != nullptr);
    CHECK(img.pixels()->size() == img.pixel_count());
}

TEST_CASE("vc_image: zeros() rejects invalid dimensions") {
    // zeros() is [[nodiscard]] (it has no other effect); CHECK_THROWS_AS
    // expands to a bare-statement call, so the result is explicitly
    // discarded here — the test cares only that construction throws.
    CHECK_THROWS_AS((void)vc::vc_image::zeros<vc::buf_f32>(0, 2, 3),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)vc::vc_image::zeros<vc::buf_f32>(4, 0, 3),
                    vc::vc_exception);
    CHECK_THROWS_AS((void)vc::vc_image::zeros<vc::buf_f32>(4, 2, 0),
                    vc::vc_exception);
}

// vc_image_info composes shared_ptr<const vc::i_image_meta>; a mask
// is an image whose composed metadata is null (the default), and
// vc_image_writer::set_metadata() is the one write path onto it before
// seal(). This is written plumbing (not a rep), so it is GREEN.
TEST_CASE("vc_image_info: composed metadata is null by default") {
    const vc::vc_image_info meta{4, 2, 3};
    CHECK(meta.metadata() == nullptr);
}

TEST_CASE("vc_image_info: index() accepts every in-range coordinate") {
    const vc::vc_image_info meta{4, 2, 3};
    CHECK(meta.index(0, 0, 0) == 0);
    CHECK(meta.index(3, 1, 2) == meta.index(0, 0, 0) + (4 * 2 * 3 - 1));

    // Actually sweep the whole extent, rather than trusting the two corners:
    // every in-range coordinate must both survive the checks AND land on a
    // distinct offset inside [0, element_count()) — a bounds check that
    // accidentally rejected an interior coordinate, or index math that
    // collided two of them, would pass a corners-only test.
    std::set<std::size_t> seen;
    for (vc::image_dim y = 0; y < meta.height(); ++y) {
        for (vc::image_dim x = 0; x < meta.width(); ++x) {
            for (vc::channel_count ch = 0; ch < meta.channels(); ++ch) {
                const std::size_t i = meta.index(x, y, ch);
                CHECK(i < meta.element_count());
                CHECK(seen.insert(i).second); // no collisions
            }
        }
    }
    CHECK(seen.size() == meta.element_count());
}

// The sweep above proves index() is a BIJECTION onto [0, element_count) — but
// a transposed layout is a bijection too, so that test cannot tell row-major
// from column-major. Neither can the two corner checks: for this 4x2x3
// descriptor, (y*width + x)*channels + ch and (x*height + y)*channels + ch
// BOTH yield 0 at (0,0,0) and 23 at (3,1,2). The two formulas first disagree
// at an interior coordinate — index(1,0,0) is 3 row-major, 6 transposed.
//
// So this pins the layout DECISION itself (vc_image_info.h: interleaved, x
// fastest) rather than just its self-consistency. Swapping the formula fails
// here, loudly, instead of surfacing downstream as a transposed image that
// every existing test still passes. When the planned planar switch
// lands, this is the test that must be deliberately rewritten — which is the
// point: the layout change should not be silent.
TEST_CASE("vc_image_info: index() is interleaved row-major, x fastest") {
    const vc::vc_image_info meta{4, 2, 3};

    // Interior coordinates a transposed formula gets wrong.
    CHECK(meta.index(1, 0, 0) == 3);
    CHECK(meta.index(0, 1, 0) == 12);
    CHECK(meta.index(2, 1, 1) == 19);

    // The same invariant stated as strides: one step in x costs `channels`
    // elements, one step in y costs a whole row.
    CHECK(meta.index(2, 1, 0) - meta.index(1, 1, 0) ==
          static_cast<std::size_t>(meta.channels()));
    CHECK(meta.index(1, 1, 0) - meta.index(1, 0, 0) ==
          static_cast<std::size_t>(meta.width()) * meta.channels());

    // Channels of one pixel are adjacent — the defining property of
    // interleaved (as opposed to planar) storage.
    CHECK(meta.index(1, 1, 1) - meta.index(1, 1, 0) == 1);
}

TEST_CASE("vc_image_info: index() throws on an out-of-range coordinate") {
    const vc::vc_image_info meta{4, 2, 3};
    CHECK_THROWS_AS((void)meta.index(4, 0, 0), vc::vc_exception);
    CHECK_THROWS_AS((void)meta.index(0, 2, 0), vc::vc_exception);
    CHECK_THROWS_AS((void)meta.index(0, 0, 3), vc::vc_exception);
}

TEST_CASE("vc_image_info: a default-constructed descriptor is 0x0x0, so even "
          "index(0, 0, 0) is out of range") {
    // Contract change worth pinning: before index() checked its arguments this
    // returned 0. An empty descriptor addresses no elements, so it throws.
    const vc::vc_image_info meta;
    CHECK(meta.element_count() == 0);
    CHECK_THROWS_AS((void)meta.index(0, 0, 0), vc::vc_exception);
}

TEST_CASE("vc_image_writer: at() propagates index()'s range check") {
    // at() is the call path the check exists for — it feeds index()'s result
    // straight to std::span::operator[], which would not catch it.
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    CHECK_NOTHROW(writer.at<vc::buf_f32>(3, 1, 2) = 1.0f);
    CHECK_THROWS_AS(writer.at<vc::buf_f32>(4, 0, 0), vc::vc_exception);
    CHECK_THROWS_AS(writer.at<vc::buf_f32>(0, 2, 0), vc::vc_exception);
    CHECK_THROWS_AS(writer.at<vc::buf_f32>(0, 0, 3), vc::vc_exception);
}

TEST_CASE(
    "vc_image_writer/vc_image_info: set_metadata is visible through seal()") {
    auto md = std::make_shared<vc::edit::vc_memory_image_meta>();
    md->set("iso", vc::vc_metadata_value{std::string{"400"}});

    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    CHECK(writer.meta().metadata() == nullptr); // null until set

    writer.set_metadata(md);
    CHECK(writer.meta().metadata() == md);

    vc::vc_image img = std::move(writer).seal();
    REQUIRE(img.meta().metadata() != nullptr);
    CHECK(img.meta().metadata() == md);
    const auto iso = img.meta().metadata()->get("iso");
    REQUIRE(iso.has_value());
    CHECK(iso->get<std::string>() == "400");
}

// ---- i_image_meta's in-place write path ----
//
// with_value() exists so a caller can edit a stored value WITHOUT the copy
// that get()-modify-set() forces, and — via the const overload — read one the
// same way. The cases below pin each part separately: the edit lands, a miss
// is reported rather than thrown, no payload copy is made on either the write
// or the read path, and get() by contrast still copies (asserted directly, so
// that cost stays a documented property instead of drifting silently).

// The read path is read-ONLY, and that is a compile-time guarantee rather than
// a convention. get() returns optional<const value>, so a write through the
// box it hands back does not compile. This matters because the failure mode it
// replaces was SILENT: with optional<value>, the line below compiled clean and
// mutated the temporary get() had just returned, which died at the end of the
// full expression — the store never changed and nothing said so. Asserted as a
// requires-expression so reverting get()'s return type breaks the build here
// instead of quietly reopening the hole.
//
// Both checks are written against a template parameter on purpose: a
// requires-expression only turns a bad expression into `false` when it is
// DEPENDENT. Spelled with the concrete type it is non-dependent, so the
// compiler diagnoses it immediately and the test file simply fails to build —
// which is the opposite of testing it.
namespace {
template <typename M>
concept writable_through_get = requires(M& m) {
    m.get("iso")->template get<std::string>() = std::string{};
};

template <typename V>
concept writable_in_place =
    requires(V& v) { v.template get<std::string>() = std::string{}; };
} // namespace

static_assert(
    !writable_through_get<vc::edit::vc_memory_image_meta>,
    "i_image_meta::get() must stay read-only — writes go through with_value()");

// ...while the same write through with_value()'s reference DOES compile: the
// const above must block the discarded-temporary path only, not in-place
// editing, or it would have taken the feature with it.
static_assert(writable_in_place<vc::vc_metadata_value>,
              "vc_any's mutable get<T>() is what with_value() exists to reach");

TEST_CASE("i_image_meta: with_value() edits the stored value in place") {
    vc::edit::vc_memory_image_meta meta;
    meta.set("iso", vc::vc_metadata_value{std::string{"400"}});

    CHECK(meta.with_value(
        "iso", [](vc::vc_metadata_value& v) { v.get<std::string>() = "800"; }));

    const auto iso = meta.get("iso");
    REQUIRE(iso.has_value());
    CHECK(iso->get<std::string>() == "800");
}

TEST_CASE("i_image_meta: with_value() reports a miss instead of throwing") {
    vc::edit::vc_memory_image_meta meta;
    bool called = false;
    CHECK_FALSE(meta.with_value(
        "absent", [&](vc::vc_metadata_value&) { called = true; }));
    CHECK_FALSE(called); // the callback must not run on a miss
}

// A payload whose copy is observable, standing in for the real motivating case
// (a color matrix). At namespace scope because a local class may not have a
// static data member. Used by the copy-count case below.
namespace {
struct counted_payload {
    static inline int copies = 0;
    std::vector<double> data{1.0, 2.0, 3.0};
    counted_payload() = default;
    counted_payload(const counted_payload& o) : data(o.data) {
        ++copies;
    }
    counted_payload(counted_payload&&) = default;
    counted_payload& operator=(const counted_payload&) = default;
    counted_payload& operator=(counted_payload&&) = default;
};
} // namespace

TEST_CASE("i_image_meta: with_value() copies no payload — the whole point") {
    // Going through get()-modify-set() would copy the payload out of the store
    // and move a whole new one back; with_value() edits it where it lives.
    vc::edit::vc_memory_image_meta meta;
    meta.set("matrix", vc::vc_metadata_value{counted_payload{}});

    counted_payload::copies = 0;
    REQUIRE(meta.with_value("matrix", [](vc::vc_metadata_value& v) {
        v.get<counted_payload>().data[0] = 42.0;
    }));
    CHECK(counted_payload::copies == 0);

    // And the edit is genuinely in the store, not in a discarded temporary.
    // REQUIRE, not a bare call: every assertion below lives INSIDE the
    // callback, so a with_value() that returned false would skip all of them
    // and leave this case passing while verifying nothing.
    counted_payload::copies = 0;
    REQUIRE(meta.with_value("matrix", [](vc::vc_metadata_value& v) {
        CHECK(v.get<counted_payload>().data[0] == 42.0);
    }));
    CHECK(counted_payload::copies == 0);
}

TEST_CASE("i_image_meta: a throwing callback propagates and keeps the partial "
          "edit (basic guarantee, not strong)") {
    // Pinned because it is a deliberate trade, not an accident: rolling back
    // would require copying the payload aside before every call, which is the
    // exact cost with_value() exists to avoid. A backend that silently
    // swallowed the exception, or one that discarded the partial write, would
    // both be wrong — and both would pass without this case.
    vc::edit::vc_memory_image_meta meta;
    meta.set("iso", vc::vc_metadata_value{std::string{"400"}});

    CHECK_THROWS_AS(meta.with_value("iso",
                                    [](vc::vc_metadata_value& v) {
                                        v.get<std::string>() = "PARTIAL";
                                        throw vc::vc_exception(
                                            vc::vc_error_code::invalid_argument,
                                            "callback failed");
                                    }),
                    vc::vc_exception);

    const auto iso = meta.get("iso");
    REQUIRE(iso.has_value());
    CHECK(iso->get<std::string>() == "PARTIAL"); // kept, not rolled back
}

TEST_CASE("i_image_meta: the const with_value() reads without copying, where "
          "get() cannot") {
    vc::edit::vc_memory_image_meta meta;
    meta.set("matrix", vc::vc_metadata_value{counted_payload{}});
    const vc::i_image_meta& reader = meta;

    // get() is the copying read — that is inherent to returning by value, and
    // is exactly the cost the const overload exists to let a caller avoid.
    counted_payload::copies = 0;
    CHECK(reader.get("matrix").has_value());
    CHECK(counted_payload::copies == 1);

    counted_payload::copies = 0;
    CHECK(reader.with_value("matrix", [](const vc::vc_metadata_value& v) {
        CHECK(v.get<counted_payload>().data.size() == 3);
    }));
    CHECK(counted_payload::copies == 0);

    CHECK_FALSE(reader.with_value("absent", [](const vc::vc_metadata_value&) {
        FAIL("callback must not run on a miss");
    }));
}

// The case the const overload actually exists for: a sealed vc_image exposes
// its captured metadata as shared_ptr<const i_image_meta>, so the mutable
// with_value() is unreachable there. Without a const overload this caller has
// no copy-free way to read its own metadata at all.
TEST_CASE("vc_image: metadata on a sealed image is readable without a copy") {
    auto md = std::make_shared<vc::edit::vc_memory_image_meta>();
    md->set("matrix", vc::vc_metadata_value{counted_payload{}});

    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    writer.set_metadata(md);
    const vc::vc_image img = std::move(writer).seal();

    const vc::const_image_meta_ptr& meta = img.meta().metadata();
    REQUIRE(meta != nullptr);

    counted_payload::copies = 0;
    CHECK(meta->with_value("matrix", [](const vc::vc_metadata_value& v) {
        CHECK(v.get<counted_payload>().data[0] == 1.0);
    }));
    CHECK(counted_payload::copies == 0);
}

// The other half of that write path: "no metadata" is reachable ONLY as the
// default, never as an argument. Without this, the throw could be deleted and
// every other test here would still pass — the mask default above is what a
// silently-accepted null would look like.
TEST_CASE("vc_image_writer: set_metadata rejects null") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    CHECK_THROWS_AS(writer.set_metadata(nullptr), vc::vc_exception);

    // The rejection leaves the writer usable and its metadata untouched, so a
    // caller that recovers still seals a valid (mask-shaped) image.
    CHECK(writer.meta().metadata() == nullptr);
    const vc::vc_image img = std::move(writer).seal();
    CHECK(img.meta().metadata() == nullptr);
}

TEST_CASE("vc_image_writer: with_pixels() mutation survives seal()") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    writer.with_pixels<vc::buf_f32>([](std::span<vc::buf_f32> px) {
        for (std::size_t i = 0; i < px.size(); ++i) {
            px[i] = static_cast<float>(i);
        }
    });

    vc::vc_image img = std::move(writer).seal();
    REQUIRE(img.pixels() != nullptr);
    const auto sealed = img.pixels()->as<vc::buf_f32>();
    REQUIRE(sealed.size() == img.pixel_count());
    for (std::size_t i = 0; i < sealed.size(); ++i) {
        CHECK(sealed[i] == static_cast<float>(i));
    }
}

TEST_CASE("vc_image_writer: with_pixels() works with an explicitly-typed"
          " span parameter") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{1.0f}};
    writer.with_pixels<vc::buf_f32>(
        [](std::span<vc::buf_f32> px) { px[0] = 42.0f; });

    vc::vc_image img = std::move(writer).seal();
    CHECK(img.pixels()->as<vc::buf_f32>()[0] == 42.0f);
}

TEST_CASE("vc_image_writer: with_pixels() works with a generic auto"
          " parameter") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{1.0f}};
    writer.with_pixels<vc::buf_f32>([](auto px) { px[0] = 42.0f; });

    vc::vc_image img = std::move(writer).seal();
    CHECK(img.pixels()->as<vc::buf_f32>()[0] == 42.0f);
}

TEST_CASE("vc_image_writer: with_pixels() span size equals the writer's"
          " element count") {
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    writer.with_pixels<vc::buf_f32>([&](std::span<vc::buf_f32> px) {
        CHECK(px.size() == writer.pixel_count());
        CHECK(px.size() == 4 * 2 * 3);
    });
}

TEST_CASE("vc_image_writer: with_pixels() throws on a dtype that does not"
          " match the buffer, same contract as at()") {
    // The buffer was allocated as buf_f32 (see the fill value below); asking
    // with_pixels() for buf_u8 must fail the same way at<u8>() already does —
    // both forward straight to
    // vc_pixel_buffer::as<T>(), which throws vc::vc_exception when the
    // requested T doesn't match the stored dtype (vc_pixel_buffer.h).
    vc::vc_image_writer writer{4, 2, 3, vc::buf_f32{0.0f}};
    try {
        writer.with_pixels<vc::buf_u8>([](std::span<vc::buf_u8>) {});
        FAIL("with_pixels() should have thrown on a dtype mismatch");
    } catch (const vc::vc_exception& e) {
        CHECK(e.code() == vc::vc_error_code::invalid_argument);
    }
}

TEST_CASE("vc_error_code: round-trips through to_int/to_error_code") {
    CHECK(vc::to_error_code(vc::to_int(vc::vc_error_code::decode_error)) ==
          vc::vc_error_code::decode_error);
    CHECK(vc::to_string(vc::vc_error_code::file_not_found) == "file_not_found");
    // pipe_invalid_topology is a newly split-out enumerator (was folded into
    // pipe_connection_mismatch) — both switches in vc_error_code.cpp are
    // exhaustive but neither has a `default` label, so the build (-Wall
    // /-Wextra, no -Werror) would only warn on a missing case, not fail. A
    // missing case in to_error_code() would still be caught loudly at
    // runtime — it falls past the switch into a throw — but a missing case
    // in to_string() fails silently, falling through to
    // "unknown_error_code" instead of throwing; pin both explicitly.
    CHECK(vc::to_error_code(
              vc::to_int(vc::vc_error_code::pipe_invalid_topology)) ==
          vc::vc_error_code::pipe_invalid_topology);
    CHECK(vc::to_string(vc::vc_error_code::pipe_invalid_topology) ==
          "pipe_invalid_topology");
}

// LEAK REGRESSION GUARD. read() decodes into a raw stb buffer before it builds
// the writer, so every throw between those two points has to release it. This
// exercises the one such path a caller can reach deterministically:
// read_config::dtype is an unvalidated field, so an out-of-range value falls
// through read()'s switch to its `default:` throw with the decoded image
// already allocated. Before that buffer became a unique_ptr, twenty iterations
// of this leaked 63.5 MB (measured with macOS `leaks`); it is now 0.
//
// Why no test caught the original: this was the only reachable post-decode
// throw and nothing exercised it. Note the guard only BITES in CI —
// .github/workflows/build.yml runs the debug-sanitizers preset on
// ubuntu-latest, where LeakSanitizer is on by default. On macOS/arm64 ASan
// reports "detect_leaks is not supported on this platform", so running this
// locally proves the throw, never the absence of a leak.
TEST_CASE("stb_image_reader: an unsupported dtype throws without leaking the "
          "decoded buffer") {
    vc::io::stb_image_reader reader;
    const vc::io::path input =
        std::string(VC_TEST_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    const vc::io::read_config bad{.dtype = static_cast<vc::pixel_dtype>(99)};

    // Repeated so a reintroduced leak is large enough to be unmistakable
    // rather than a single allocation lost in the noise.
    for (int i = 0; i < 8; ++i) {
        try {
            (void)reader.read(input, bad);
            FAIL("read() should have thrown on an unsupported dtype");
        } catch (const vc::vc_exception& e) {
            CHECK(e.code() == vc::vc_error_code::decode_error);
        }
    }
}

TEST_CASE("stb round-trip: JPEG in, PNG out, dimensions and pixels match") {
    vc::io::stb_image_reader reader;
    vc::io::stb_image_writer writer;

    const vc::io::path input =
        std::string(VC_TEST_DATA_DIR) + "/test_1_jpeg_3ch.jpg";
    const vc::io::path output =
        std::string(VC_TEST_OUTPUT_DIR) + "/roundtrip_test_output.png";

    // Explicit dtype: read()'s default is buf_u8 (vc_io_types.h), but this
    // test asserts against buf_f32 below — the request must match what it
    // checks, not rely on whatever the default happens to be.
    const vc::io::read_config f32_config{.dtype = vc::pixel_dtype::f32};

    vc::vc_image img = reader.read(input, f32_config);
    CHECK(img.channels() == 3);
    CHECK(img.width() > 0);
    CHECK(img.height() > 0);

    // Explicit format: write_config's default is JPEG (vc_io_types.h), but
    // this test's name and tolerance below assume a lossless PNG round-trip
    // — the request must match that, not rely on the default.
    const vc::io::write_config png_config{.format =
                                              vc::io::vc_image_format::png};
    writer.write(output, img, png_config);

    vc::vc_image roundtripped = reader.read(output, f32_config);
    CHECK(roundtripped.width() == img.width());
    CHECK(roundtripped.height() == img.height());
    CHECK(roundtripped.channels() == img.channels());

    REQUIRE(img.pixels() != nullptr);
    REQUIRE(roundtripped.pixels() != nullptr);
    const auto& original = *img.pixels();
    const auto& reconstructed = *roundtripped.pixels();
    REQUIRE(original.size() == reconstructed.size());

    const auto original_pixels = original.as<vc::buf_f32>();
    const auto reconstructed_pixels = reconstructed.as<vc::buf_f32>();

    // PNG is lossless, so JPEG-decoded values written to PNG and read back
    // should match the original decode almost exactly — a 1/255 tolerance
    // accounts for float rounding, not any re-compression loss.
    bool all_close = true;
    for (std::size_t i = 0; i < original_pixels.size(); ++i) {
        if (std::fabs(original_pixels[i] - reconstructed_pixels[i]) >
            (1.0f / 255.0f)) {
            all_close = false;
            break;
        }
    }
    CHECK(all_close);

    // Guards on the DECODE, which the round trip above cannot provide. A
    // read() that returned a constant buffer — all zeros, say — would
    // round-trip flawlessly: write zeros to PNG, read zeros back, every value
    // matches inside tolerance and every check above goes green on entirely
    // broken data. Same for a decode that collapsed all three channels into
    // one. Faithful write-then-read says nothing about whether the pixels are
    // real, so these two assert it directly: the fixture is a photograph, so
    // it is neither constant nor three copies of a single channel.
    bool all_same = true;
    for (std::size_t i = 1; i < original_pixels.size(); ++i) {
        if (original_pixels[i] != original_pixels[0]) {
            all_same = false;
            break;
        }
    }
    CHECK_FALSE(all_same);

    bool rgb_identical = true;
    for (vc::image_dim y = 0; y < img.height() && rgb_identical; ++y) {
        for (vc::image_dim x = 0; x < img.width(); ++x) {
            const auto r = original_pixels[img.meta().index(x, y, 0)];
            const auto g = original_pixels[img.meta().index(x, y, 1)];
            const auto b = original_pixels[img.meta().index(x, y, 2)];
            if (r != g || g != b) {
                rgb_identical = false;
                break;
            }
        }
    }
    CHECK_FALSE(rgb_identical);

    MESSAGE("wrote (square fixture): " << output.string());
}

// The square 1024x1024 fixture above cannot detect a width/height swap
// anywhere in the I/O path — both dimensions are the same number, so a swap is
// invisible by construction. Only the tiny synthetic 3x5 pattern covered that,
// and a synthetic pattern cannot be eyeballed for "does this look like the
// photograph it came from".
//
// This case closes both gaps with a real, non-square photograph: geometry is
// asserted to survive the round trip, and the PNG it leaves behind is the
// artifact the "visually matches in an external viewer" criterion is about.
// COLOUR MANAGEMENT, and why there are two fixtures rather than one.
//
// The round trip below is byte-exact, yet the JPEG and the PNG can still LOOK
// different side by side — because stb drops the colour profile. stbi_load
// ignores ICC entirely, so the profile never reaches vc_image, and
// stbi_write_png emits no colour chunk at all (no iCCP, sRGB, gAMA or cHRM).
// A viewer renders the JPEG through its embedded profile and the PNG through
// its untagged fallback, which is sRGB.
//
// That gives two different outcomes, and both are worth having:
//
//   sRGB source     -> untagged PNG renders IDENTICALLY. Colour is signal
//                      here, so an R/B swap or a gamma error is visible by
//                      eye. This is the fixture the visual check wants.
//   Rec.2020 source -> untagged PNG renders cooler and less saturated. The
//                      SAME numbers read through a narrower gamut. Kept
//                      deliberately: it is the only thing in the repo that
//                      demonstrates the profile is dropped, and it becomes
//                      the regression fixture once colour management lands.
//
// Both are also non-square, which the 1024x1024 fixture above cannot be — a
// width/height swap anywhere in the I/O path is invisible on a square image,
// and until these existed only the synthetic 3x5 pattern covered it.
TEST_CASE("stb round-trip: non-square photos survive byte-exactly, and are "
          "left for visual inspection") {
    struct fixture {
        const char* file;
        const char* output;
        const char* colour_note;
    };
    const std::vector<fixture> fixtures{
        {.file = "test_2_2x1_jpeg_srgb_3ch.jpg",
         .output = "visual_check_srgb.png",
         .colour_note = "sRGB source: the PNG should match the JPEG in COLOUR "
                        "TOO. A difference here is a real bug."},
        {.file = "test_2_2x1_jpeg_rec2020_3ch.jpg",
         .output = "visual_check_rec2020.png",
         .colour_note =
             "Rec.2020 source: expect a cooler, less saturated PNG — stb "
             "drops the profile. Compare geometry and detail, NOT colour."},
    };

    vc::io::stb_image_reader reader;
    vc::io::stb_image_writer writer;
    const vc::io::read_config u8_config{.dtype = vc::pixel_dtype::u8};

    for (const auto& f : fixtures) {
        CAPTURE(f.file);

        const vc::io::path input = std::string(VC_TEST_DATA_DIR) + "/" + f.file;
        const vc::io::path output =
            std::string(VC_TEST_OUTPUT_DIR) + "/" + f.output;

        const vc::vc_image img = reader.read(input, u8_config);

        // The property that matters, asserted instead of literal dimensions —
        // swapping a fixture should not turn this red, but a fixture that is
        // square would silently stop testing anything.
        REQUIRE(img.width() != img.height());
        CHECK(img.channels() == 3);

        writer.write(
            output, img,
            vc::io::write_config{.format = vc::io::vc_image_format::png});

        const vc::vc_image loaded = reader.read(output, u8_config);
        CHECK(loaded.width() == img.width());
        CHECK(loaded.height() == img.height());
        CHECK(loaded.channels() == img.channels());

        // u8 through PNG is a straight copy on both stb paths, so this can be
        // EXACT on a real photograph — no tolerance, unlike the f32 case.
        const auto original = img.pixels()->as<vc::buf_u8>();
        const auto reconstructed = loaded.pixels()->as<vc::buf_u8>();
        REQUIRE(original.size() == reconstructed.size());
        bool identical = true;
        for (std::size_t i = 0; i < original.size(); ++i) {
            if (original[i] != reconstructed[i]) {
                identical = false;
                break;
            }
        }
        CHECK(identical);

        // std::string(f.file), not a bare f.file: this doctest build has no
        // toStream overload for const char*, so a raw pointer binds to the
        // BOOL overload and renders as "1" — silently, while the surrounding
        // literals and the integers print correctly. Wrapping it selects the
        // std::string path. (Concatenating with + instead does not compile:
        // MESSAGE(x) expands to `mb * x`, and * binds tighter than +.)
        MESSAGE("VISUAL CHECK — open beside tests/data/"
                << std::string(f.file) << ": " << output.string() << "  ("
                << img.width() << " x " << img.height() << ")");
        MESSAGE("  " << std::string(f.colour_note));
    }
}

// The f32 round-trip above can only assert a 1/255 TOLERANCE, and that is a
// property of the DTYPE rather than of PNG: writing f32 quantises
// (v*255 + 0.5 -> byte) and reading back divides by 255, so an f32 value not
// already sitting on the k/255 grid cannot survive unchanged. Its fixture is
// also a 1024x1024 photograph — square, and smooth enough that a one-element
// slip moves a pixel by less than the tolerance in most of the frame.
//
// This case is deliberately the opposite on all three counts:
//   * buf_u8 end to end — both stb paths are a straight std::copy, so PNG is
//     genuinely lossless here and equality can be asserted EXACTLY
//   * a synthetic pattern with a DISTINCT value in every one of the 45
//     elements, so any misplacement lands a value that provably belongs
//     elsewhere instead of one that happens to be nearly identical
//   * NON-SQUARE, odd in both axes (3 wide, 5 tall) — a square fixture lets a
//     width/height swap in the write path produce a file that round-trips and
//     still looks structurally plausible
//
// Scope, stated so it is not over-read: this covers the I/O layer — the
// width/height/stride arguments handed to stbi_write_png, the channel count,
// and any lossy step in the u8 path. It canNOT catch a consistent transpose
// inside index(), because write() copies the flat buffer verbatim and read()
// reinterprets it the same wrong way; that is the job of the interleaved
// row-major case above.
TEST_CASE("stb round-trip: synthetic u8 pattern survives PNG pixel-exactly") {
    constexpr vc::image_dim kWidth = 3;
    constexpr vc::image_dim kHeight = 5;
    constexpr vc::channel_count kChannels = 3;

    // Injective over this extent: the x term steps by 40 while 7y + ch spans
    // only 0..30, so the three x-bands cannot overlap. Hence all 45 values
    // are distinct — which is the property the test leans on.
    const auto value = [](vc::image_dim x, vc::image_dim y,
                          vc::channel_count ch) -> vc::buf_u8 {
        return static_cast<vc::buf_u8>(10 + 40 * x + 7 * y + ch);
    };

    vc::vc_image_writer writer{kWidth, kHeight, kChannels, vc::buf_u8{0}};
    for (vc::image_dim y = 0; y < kHeight; ++y) {
        for (vc::image_dim x = 0; x < kWidth; ++x) {
            for (vc::channel_count ch = 0; ch < kChannels; ++ch) {
                writer.at<vc::buf_u8>(x, y, ch) = value(x, y, ch);
            }
        }
    }
    const vc::vc_image original = std::move(writer).seal();

    const vc::io::path output =
        std::string(VC_TEST_OUTPUT_DIR) + "/synthetic_roundtrip.png";
    vc::io::stb_image_writer png_writer;
    png_writer.write(
        output, original,
        vc::io::write_config{.format = vc::io::vc_image_format::png});

    vc::io::stb_image_reader reader;
    const vc::vc_image loaded =
        reader.read(output, vc::io::read_config{.dtype = vc::pixel_dtype::u8});

    // Checked before the pixels: a swapped width/height would otherwise show
    // up as a confusing wall of value mismatches rather than as the geometry
    // error it actually is.
    REQUIRE(loaded.width() == kWidth);
    REQUIRE(loaded.height() == kHeight);
    REQUIRE(loaded.channels() == kChannels);

    const auto pixels = loaded.pixels()->as<vc::buf_u8>();
    REQUIRE(pixels.size() == original.pixel_count());
    for (vc::image_dim y = 0; y < kHeight; ++y) {
        for (vc::image_dim x = 0; x < kWidth; ++x) {
            for (vc::channel_count ch = 0; ch < kChannels; ++ch) {
                CHECK(pixels[loaded.meta().index(x, y, ch)] == value(x, y, ch));
            }
        }
    }
}
