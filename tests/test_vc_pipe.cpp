// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — tests/test_vc_image.cpp already
// defines doctest's main for the whole test binary; only one TU may.
#include "doctest/doctest.h"

#include <limits>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

#include "samples/vc_sample_blur_stage.h"
#include "samples/vc_sample_grayscale_stage.h"
#include "samples/vc_sample_mean_brightness_stage.h"
#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"
#include "vc/vc_image_writer.h"

// =====================================================================
// GREEN — the framework primitives are fully implemented, so these pass
// today and pin the generic slot/packet/contract/port mechanism.
// =====================================================================

TEST_CASE("vc_pipe_packet: stores a value and unboxes it by type") {
    const vc::pipe::vc_pipe_packet p{42};
    CHECK(p.has_value());
    CHECK(p.get<int>() == 42);
    CHECK(p.type() == typeid(int));
}

TEST_CASE("vc_pipe_packet: get with the wrong type throws") {
    const vc::pipe::vc_pipe_packet p{std::string{"hello"}};
    CHECK(p.get<std::string>() == "hello");
    CHECK_THROWS_AS(p.get<int>(), vc::vc_exception);
}

TEST_CASE("vc_pipe_packet: default-constructed holds nothing") {
    const vc::pipe::vc_pipe_packet p;
    CHECK_FALSE(p.has_value());
}

TEST_CASE("vc_pipe_context: reads typed inputs and harvests typed outputs") {
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> in;
    in.emplace("x", vc::pipe::vc_pipe_packet{7});
    vc::pipe::vc_pipe_context ctx{std::move(in)};

    // Author-facing: read through a typed slot descriptor — no name spelled, no
    // type restated.
    CHECK(ctx.get_input(vc::pipe::slot<int>{"x"}) == 7);

    // Publish through a typed slot, then harvest every output at once (the
    // runner-facing coarse boundary — there is no per-slot public getter).
    ctx.set_output(vc::pipe::slot<double>{"y"}, vc::pipe::vc_pipe_packet{3.5});
    const auto outs = std::move(ctx).take_outputs();
    REQUIRE(outs.count("y") == 1);
    CHECK(outs.at("y").get<double>() == doctest::Approx(3.5));
}

TEST_CASE("vc_pipe_context: input and output are separate name namespaces") {
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> in;
    in.emplace("data", vc::pipe::vc_pipe_packet{1});
    vc::pipe::vc_pipe_context ctx{std::move(in)};

    ctx.set_output(vc::pipe::slot<int>{"data"}, vc::pipe::vc_pipe_packet{2});
    CHECK(ctx.get_input(vc::pipe::slot<int>{"data"}) == 1);

    const auto outs = std::move(ctx).take_outputs();
    CHECK(outs.at("data").get<int>() == 2);
}

TEST_CASE("vc_pipe_context: reading an unbound input slot throws") {
    const vc::pipe::vc_pipe_context ctx;
    CHECK_THROWS_AS(ctx.get_input(vc::pipe::slot<int>{"missing"}),
                    vc::vc_exception);
}

TEST_CASE("vc_pipe_context: typed slot overloads deduce the payload type") {
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> in;
    in.emplace("image", vc::pipe::vc_pipe_packet{
                            vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});
    vc::pipe::vc_pipe_context ctx{std::move(in)};

    // get_input(slot<vc_image>) deduces vc_image and unboxes without spelling
    // the type.
    CHECK_NOTHROW(
        ctx.get_input(vc::pipe::vc_sample_mean_brightness_stage::slots::image));

    // set_output(slot<double>) writes on the descriptor's name; harvest it
    // back.
    ctx.set_output(vc::pipe::vc_sample_mean_brightness_stage::slots::mean,
                   vc::pipe::vc_pipe_packet{0.5});
    const auto outs = std::move(ctx).take_outputs();
    REQUIRE(outs.count("mean") == 1);
    CHECK(outs.at("mean").get<double>() == doctest::Approx(0.5));
}

TEST_CASE("vc_pipe_contract: records declared input/output slot types") {
    vc::pipe::vc_pipe_contract c;
    c.add_input_slot(vc::pipe::slot<int>{"a"});
    c.add_output_slot(vc::pipe::slot<double>{"b"});

    CHECK(c.input_slot_type("a") == std::type_index(typeid(int)));
    CHECK(c.output_slot_type("b") == std::type_index(typeid(double)));

    // Undeclared slot -> throws (this project's error channel), not a
    // nullopt. input_slot_type() is [[nodiscard]] (there's never a reason to
    // call it and ignore the result normally); the (void) here is that one
    // deliberate exception, testing only the throw.
    CHECK_THROWS_AS((void)c.input_slot_type("nope"), vc::vc_exception);

    // run() finds open ports by subtracting connected ports from the declared
    // slot NAMES (types come from the *_slot_type() lookups above), so the
    // contract enumerates names. Assert they round-trip in declaration order.
    const auto in_names = c.input_slot_names();
    REQUIRE(in_names.size() == 1);
    CHECK(in_names[0] == "a");

    const auto out_names = c.output_slot_names();
    REQUIRE(out_names.size() == 1);
    CHECK(out_names[0] == "b");
}

TEST_CASE("vc_pipe_contract: rejects a second slot with the same name in the"
          " same direction") {
    vc::pipe::vc_pipe_contract c;
    c.add_input_slot(vc::pipe::slot<int>{"a"});
    CHECK_THROWS_AS(c.add_input_slot(vc::pipe::slot<int>{"a"}),
                    vc::vc_exception);

    c.add_output_slot(vc::pipe::slot<double>{"b"});
    CHECK_THROWS_AS(c.add_output_slot(vc::pipe::slot<double>{"b"}),
                    vc::vc_exception);

    // Input and output are separate namespaces (Sec 4.2) — the same name
    // reused across directions is NOT a duplicate.
    CHECK_NOTHROW(c.add_output_slot(vc::pipe::slot<int>{"a"}));
}

TEST_CASE("stage_port: a typed-slot ctor builds a coordinate that compares + "
          "hashes") {
    const vc::pipe::stage_port p1{"grey",
                                  vc::pipe::vc_sample_grayscale_stage::slots::rgb};
    CHECK(p1.stage == "grey");
    CHECK(p1.slot == "rgb");

    // The name ctor reaches the same coordinate as the typed-slot ctor.
    const vc::pipe::stage_port p2{"grey", vc::pipe::slot_name{"rgb"}};
    CHECK(p1 == p2);

    std::unordered_map<vc::pipe::stage_port, int> m;
    m[p1] = 5;
    CHECK(m.at(p2) == 5); // p2 hashes/compares equal to p1
}

TEST_CASE("vc_pipeline: add() rejects a second stage with an already-used"
          " name") {
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    CHECK_THROWS_AS(
        pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a")),
        vc::vc_exception);
}

TEST_CASE("vc_pipeline: add() returns the stage name for typo-safe wiring") {
    vc::pipe::vc_pipeline pipe;
    const auto a =
        pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    const auto b =
        pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));

    CHECK(a == "a");
    CHECK(b == "b");

    // The returned name keys a port identically to a raw stage name — so
    // add()-wired and literal-wired graphs are interchangeable.
    CHECK(vc::pipe::stage_port{a, vc::pipe::vc_passthrough_stage::slots::out} ==
          vc::pipe::stage_port{vc::pipe::stage_name{"a"},
                               vc::pipe::vc_passthrough_stage::slots::out});

    // connect() takes each end as a stage name + typed slot; the returned names
    // build the from/to stages.
    CHECK_NOTHROW(pipe.connect(a, vc::pipe::vc_passthrough_stage::slots::out, b,
                               vc::pipe::vc_passthrough_stage::slots::in));
}

TEST_CASE("vc_passthrough_stage: carries a vc_image through its typed slots") {
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> in;
    in.emplace("in", vc::pipe::vc_pipe_packet{
                         vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});
    vc::pipe::vc_pipe_context ctx{std::move(in)};

    const vc::pipe::vc_passthrough_stage stage{"pass"};
    stage.process(ctx);

    const auto outs = std::move(ctx).take_outputs();
    REQUIRE(outs.count("out") == 1);
    CHECK(outs.at("out").type() == typeid(vc::vc_image));
}

TEST_CASE("vc_passthrough_stage: declares one image-in, one image-out slot") {
    vc::pipe::vc_pipe_contract c;
    const vc::pipe::vc_passthrough_stage stage{"pass"};
    stage.declare(c);

    CHECK(c.input_slot_type("in") == std::type_index(typeid(vc::vc_image)));
    CHECK(c.output_slot_type("out") == std::type_index(typeid(vc::vc_image)));
}

TEST_CASE("i_pipe: name() is per-instance, kind() is per-type") {
    const vc::pipe::vc_passthrough_stage a{"a"};
    const vc::pipe::vc_passthrough_stage b{"b"};
    CHECK(a.name() == "a");
    CHECK(b.name() == "b");
    CHECK(std::string{a.kind()} == "passthrough");
    CHECK(std::string{a.kind()} == std::string{b.kind()}); // same type -> same
}

TEST_CASE("i_pipe: paramless stages' params_hash() is a fixed 0") {
    const vc::pipe::vc_passthrough_stage pass{"pass"};
    const vc::pipe::vc_sample_grayscale_stage grey{"grey"};
    const vc::pipe::vc_sample_mean_brightness_stage mean{"mean"};

    CHECK(pass.params_hash() == 0);
    CHECK(grey.params_hash() == 0);
    CHECK(mean.params_hash() == 0);
}

// =====================================================================
// tests/samples/ — worked-example stages exercising the pipe framework's
// declare()/process() and the pipeline's validate()/run(). Real, hardcoded
// kernels; no dependency on vc_edit_session/vc_edit_document (see the class
// comment on each stage's header for why).
// =====================================================================

TEST_CASE("vc_sample_grayscale_stage: declares rgb-in -> grey-out (image->image)") {
    vc::pipe::vc_pipe_contract c;
    const vc::pipe::vc_sample_grayscale_stage stage{"grey"};
    stage.declare(c);

    CHECK(c.input_slot_type("rgb") == std::type_index(typeid(vc::vc_image)));
    CHECK(c.output_slot_type("grey") == std::type_index(typeid(vc::vc_image)));
}

TEST_CASE("vc_sample_grayscale_stage: process() computes the luminance kernel") {
    vc::vc_image_writer writer{1, 1, 3, vc::buf_f32{0.0f}};
    writer.at<vc::buf_f32>(0, 0, 0) = 1.0f; // R
    writer.at<vc::buf_f32>(0, 0, 1) = 0.0f; // G
    writer.at<vc::buf_f32>(0, 0, 2) = 0.0f; // B
    vc::vc_image red = std::move(writer).seal();

    const vc::pipe::vc_sample_grayscale_stage stage{"grey"};
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(std::string(vc::pipe::vc_sample_grayscale_stage::slots::rgb.name),
                   vc::pipe::vc_pipe_packet{red});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    stage.process(ctx);

    const auto outputs = std::move(ctx).take_outputs();
    const auto& grey =
        outputs.at(std::string(vc::pipe::vc_sample_grayscale_stage::slots::grey.name))
            .get<vc::vc_image>();
    CHECK(grey.channels() == 1);
    CHECK(grey.pixels()->as<vc::buf_f32>()[0] == doctest::Approx(0.299));
}

TEST_CASE("vc_sample_grayscale_stage: process() rejects an image with fewer than"
          " 3 channels") {
    const vc::pipe::vc_sample_grayscale_stage stage{"grey"};
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(std::string(vc::pipe::vc_sample_grayscale_stage::slots::rgb.name),
                   vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(2, 2, 1)});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    CHECK_THROWS_AS(stage.process(ctx), vc::vc_exception);
}

TEST_CASE("sample stages: a non-f32 image is rejected by validate_inputs(),"
          " naming the stage, not by a raw vc_pixel_buffer dtype throw") {
    // slot<vc_image> pins the PAYLOAD type but not the image's dtype, so a
    // u8 image satisfies the declared contract and still reaches the kernel.
    // Each sample kernel reads as<buf_f32>(); without an explicit guard the
    // failure surfaced from inside vc_pixel_buffer as "requested dtype does
    // not match stored dtype", naming neither the stage nor the slot.
    const auto u8_image = vc::vc_image::zeros<vc::buf_u8>(4, 4, 3);

    auto ctx_for = [&u8_image](const vc::pipe::slot_name& slot) {
        std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> in;
        in.emplace(slot, vc::pipe::vc_pipe_packet{u8_image});
        return vc::pipe::vc_pipe_context{std::move(in)};
    };

    const vc::pipe::vc_sample_grayscale_stage grey{"grey"};
    auto grey_ctx = ctx_for(
        std::string(vc::pipe::vc_sample_grayscale_stage::slots::rgb.name));
    CHECK_THROWS_AS(grey.process(grey_ctx), vc::vc_exception);

    const vc::pipe::vc_sample_mean_brightness_stage mean{"mean"};
    auto mean_ctx = ctx_for(
        std::string(vc::pipe::vc_sample_mean_brightness_stage::slots::image.name));
    CHECK_THROWS_AS(mean.process(mean_ctx), vc::vc_exception);

    const vc::pipe::vc_sample_blur_stage blur{"blur",
                                              vc::pipe::vc_sample_blur_params{}};
    auto blur_ctx =
        ctx_for(std::string(vc::pipe::vc_sample_blur_stage::slots::in.name));
    CHECK_THROWS_AS(blur.process(blur_ctx), vc::vc_exception);
}

TEST_CASE("vc_sample_mean_brightness_stage: declares a NON-IMAGE (double) output"
          " — the heterogeneous type contract this design exists for") {
    vc::pipe::vc_pipe_contract c;
    const vc::pipe::vc_sample_mean_brightness_stage stage{"mean"};
    stage.declare(c);

    CHECK(c.input_slot_type("image") == std::type_index(typeid(vc::vc_image)));
    // The point of this stage: a NON-image (double) output slot.
    CHECK(c.output_slot_type("mean") == std::type_index(typeid(double)));
}

TEST_CASE("vc_sample_mean_brightness_stage: process() averages every element") {
    vc::vc_image_writer writer{2, 2, 1, vc::buf_f32{0.0f}};
    writer.at<vc::buf_f32>(0, 0, 0) = 0.0f;
    writer.at<vc::buf_f32>(1, 0, 0) = 0.0f;
    writer.at<vc::buf_f32>(0, 1, 0) = 1.0f;
    writer.at<vc::buf_f32>(1, 1, 0) = 1.0f;
    vc::vc_image image = std::move(writer).seal();

    const vc::pipe::vc_sample_mean_brightness_stage stage{"mean"};
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(std::string(vc::pipe::vc_sample_mean_brightness_stage::slots::image.name),
                   vc::pipe::vc_pipe_packet{image});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    stage.process(ctx);

    const auto outputs = std::move(ctx).take_outputs();
    CHECK(outputs.at(std::string(vc::pipe::vc_sample_mean_brightness_stage::slots::mean.name))
              .get<double>() == doctest::Approx(0.5));
}

// =====================================================================
// vc_sample_blur_stage — the worked-example stage that OWNS params: ctor
// stores them, kind(), name(), a hand-written params_hash(), and a real
// box-blur kernel.
// =====================================================================

TEST_CASE("vc_sample_blur_stage: is constructed with its params and exposes them") {
    vc::pipe::vc_sample_blur_params cfg;
    cfg.radius = 2.0;
    const vc::pipe::vc_sample_blur_stage blur{"blur", cfg};

    CHECK(std::string{blur.kind()} == "sample_blur");
    CHECK(blur.name() == "blur");
    CHECK(blur.params().radius == doctest::Approx(2.0));
    CHECK(blur.params().normalize == true);
}

TEST_CASE("vc_sample_blur_stage: params_hash() is deterministic and varies with radius,"
          " not with name") {
    vc::pipe::vc_sample_blur_params a;
    a.radius = 2.0;
    vc::pipe::vc_sample_blur_params b;
    b.radius = 3.0;

    const vc::pipe::vc_sample_blur_stage blur_a{"blur", a};
    const vc::pipe::vc_sample_blur_stage blur_a_again{"blur", a};
    const vc::pipe::vc_sample_blur_stage blur_a_renamed{"blur2", a};
    const vc::pipe::vc_sample_blur_stage blur_b{"blur", b};

    CHECK(blur_a.params_hash() == blur_a_again.params_hash());   // deterministic
    CHECK(blur_a.params_hash() == blur_a_renamed.params_hash()); // name-independent
    CHECK(blur_a.params_hash() != blur_b.params_hash());         // radius varies it
}

TEST_CASE("vc_sample_blur_stage: rejects a non-positive or NaN radius before"
          " the kernel ever runs") {
    // A VALID "in" packet is bound (unlike the process() tests above, which
    // only assert the kernel publishes something) specifically so that if
    // validate_inputs() ever failed to reject one of these params,
    // do_process() would run to completion and NOT throw — making these
    // CHECK_THROWS_AS calls a real regression check on the radius validation
    // itself, not just "process() throws for some reason or other".
    auto bound_ctx = [] {
        std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
        inputs.emplace(
            std::string(vc::pipe::vc_sample_blur_stage::slots::in.name),
            vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});
        return vc::pipe::vc_pipe_context{std::move(inputs)};
    };

    vc::pipe::vc_sample_blur_params zero;
    zero.radius = 0.0;
    vc::pipe::vc_sample_blur_params negative;
    negative.radius = -1.0;
    vc::pipe::vc_sample_blur_params nan_radius;
    nan_radius.radius = std::numeric_limits<double>::quiet_NaN();

    // Constructed as separate locals, not inline inside CHECK_THROWS_AS: a
    // brace-init argument list's commas are not parenthesis-protected, so
    // the preprocessor would otherwise mis-split the macro's own arguments.
    const vc::pipe::vc_sample_blur_stage zero_blur{"blur", zero};
    auto zero_ctx = bound_ctx();
    CHECK_THROWS_AS(zero_blur.process(zero_ctx), vc::vc_exception);

    const vc::pipe::vc_sample_blur_stage negative_blur{"blur", negative};
    auto negative_ctx = bound_ctx();
    CHECK_THROWS_AS(negative_blur.process(negative_ctx), vc::vc_exception);

    const vc::pipe::vc_sample_blur_stage nan_blur{"blur", nan_radius};
    auto nan_ctx = bound_ctx();
    CHECK_THROWS_AS(nan_blur.process(nan_ctx), vc::vc_exception);
}

// =====================================================================
// Driven DIRECTLY through a vc_pipe_context — NOT through run() — so this
// isolates the kernel itself from the pipeline runner.
// =====================================================================

TEST_CASE("vc_sample_blur_stage: process() publishes a blurred image on its output slot") {
    const vc::pipe::vc_sample_blur_stage blur{"blur", vc::pipe::vc_sample_blur_params{}};

    // Seed the input slot with a source image and drive process() directly.
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        std::string(vc::pipe::vc_sample_blur_stage::slots::in.name),
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(4, 4, 3)});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};

    CHECK_NOTHROW(blur.process(ctx));

    const auto outputs = std::move(ctx).take_outputs();
    CHECK(outputs.count(std::string(vc::pipe::vc_sample_blur_stage::slots::out.name)) ==
          1);
    // A zeros() source stays all-zero under a box blur, so this only checks
    // that the kernel runs and publishes — see the dedicated numeric test
    // below for the actual blur behaviour on a non-uniform image.
}

TEST_CASE("vc_sample_blur_stage: a normalized box blur moves boundary pixels"
          " toward the local mean, leaving pixels far from the edge exact") {
    // A sharp vertical step (left half 0.0, right half 1.0), 1 channel, wide
    // enough that radius=1 leaves columns 0 and 5 with a uniform 3-wide
    // neighbourhood (still exactly 0/1) while columns 2 and 3 straddle the
    // edge and must land strictly between 0 and 1.
    vc::vc_image_writer writer{6, 1, 1, vc::buf_f32{0.0f}};
    for (vc::image_dim x = 3; x < 6; ++x) {
        writer.at<vc::buf_f32>(x, 0, 0) = 1.0f;
    }
    vc::vc_image step = std::move(writer).seal();

    vc::pipe::vc_sample_blur_params params;
    params.radius = 1.0;
    params.normalize = true;
    const vc::pipe::vc_sample_blur_stage blur{"blur", params};

    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(std::string(vc::pipe::vc_sample_blur_stage::slots::in.name),
                   vc::pipe::vc_pipe_packet{step});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    blur.process(ctx);
    const auto outputs = std::move(ctx).take_outputs();
    const auto& blurred =
        outputs.at(std::string(vc::pipe::vc_sample_blur_stage::slots::out.name))
            .get<vc::vc_image>();
    const auto out_pixels = blurred.pixels()->as<vc::buf_f32>();

    CHECK(out_pixels[blurred.meta().index(0, 0, 0)] == doctest::Approx(0.0));
    CHECK(out_pixels[blurred.meta().index(5, 0, 0)] == doctest::Approx(1.0));
    CHECK(out_pixels[blurred.meta().index(2, 0, 0)] > 0.0);
    CHECK(out_pixels[blurred.meta().index(2, 0, 0)] < 1.0);
    CHECK(out_pixels[blurred.meta().index(3, 0, 0)] > 0.0);
    CHECK(out_pixels[blurred.meta().index(3, 0, 0)] < 1.0);
}

TEST_CASE("vc_sample_blur_stage: normalize=false publishes the raw neighbourhood"
          " sum instead of its mean") {
    // A uniform image whose every source pixel is 1.0: a NORMALIZED blur
    // would leave it at 1.0 everywhere; an un-normalized blur inflates the
    // interior to the raw kernel-weight sum (9.0 for a radius-1 3x3
    // neighbourhood) — the two are only distinguishable on a non-zero image.
    const vc::vc_image source = vc::vc_image::with_fill<vc::buf_f32>(3, 3, 1, 1.0f);

    vc::pipe::vc_sample_blur_params params;
    params.radius = 1.0;
    params.normalize = false;
    const vc::pipe::vc_sample_blur_stage blur{"blur", params};

    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(std::string(vc::pipe::vc_sample_blur_stage::slots::in.name),
                   vc::pipe::vc_pipe_packet{source});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    blur.process(ctx);
    const auto outputs = std::move(ctx).take_outputs();
    const auto& blurred =
        outputs.at(std::string(vc::pipe::vc_sample_blur_stage::slots::out.name))
            .get<vc::vc_image>();
    const auto out_pixels = blurred.pixels()->as<vc::buf_f32>();

    // Interior pixel (1,1): a full 3x3 neighbourhood, all 1.0 -> sum 9.0.
    CHECK(out_pixels[blurred.meta().index(1, 1, 0)] == doctest::Approx(9.0));
    // Corner pixel (0,0): a clamped 2x2 neighbourhood -> sum 4.0.
    CHECK(out_pixels[blurred.meta().index(0, 0, 0)] == doctest::Approx(4.0));
}

TEST_CASE("vc_sample_blur_stage: a radius larger than the image clamps to the"
          " whole image instead of wrapping the uint32 bounds math") {
    // Regression test. `y + radius` is image_dim (uint32) arithmetic, so
    // without the clamp in do_process() a huge radius wraps past UINT32_MAX,
    // std::min picks the small wrapped value, and y1 lands BELOW y — the
    // pixel gets averaged over a truncated band that need not contain itself.
    // Silently wrong, not a crash, which is exactly why it needs pinning.
    //
    // A 1x16 ramp (row y holds y/100) makes the two outcomes numerically
    // distinct: the correct whole-column mean is 0.075, whereas the wrapped
    // band (rows 0-4) yields 0.02.
    vc::vc_image_writer writer{1, 16, 1, vc::buf_f32{0.0f}};
    for (vc::image_dim y = 0; y < 16; ++y) {
        writer.at<vc::buf_f32>(0, y, 0) = static_cast<float>(y) / 100.0f;
    }
    vc::vc_image ramp = std::move(writer).seal();

    vc::pipe::vc_sample_blur_params params;
    params.radius = 4294967290.0; // chosen to wrap for mid rows
    const vc::pipe::vc_sample_blur_stage blur{"blur", params};

    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(std::string(vc::pipe::vc_sample_blur_stage::slots::in.name),
                   vc::pipe::vc_pipe_packet{ramp});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};
    blur.process(ctx);

    const auto outputs = std::move(ctx).take_outputs();
    const auto& blurred =
        outputs.at(std::string(vc::pipe::vc_sample_blur_stage::slots::out.name))
            .get<vc::vc_image>();
    const auto px = blurred.pixels()->as<vc::buf_f32>();

    // Every row must see the whole column, so all rows share the same mean.
    CHECK(px[blurred.meta().index(0, 10, 0)] == doctest::Approx(0.075));
    CHECK(px[blurred.meta().index(0, 0, 0)] == doctest::Approx(0.075));
    CHECK(px[blurred.meta().index(0, 15, 0)] == doctest::Approx(0.075));
}

TEST_CASE("vc_pipeline: validate() accepts a matched image->image chain") {
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_sample_grayscale_stage>("grey"));
    pipe.add(std::make_unique<vc::pipe::vc_sample_mean_brightness_stage>("mean"));
    pipe.connect(
        "grey", vc::pipe::vc_sample_grayscale_stage::slots::grey, "mean",
        vc::pipe::vc_sample_mean_brightness_stage::slots::image); // image->img
    CHECK_NOTHROW(pipe.validate());
}

TEST_CASE("vc_pipeline: validate() rejects a type-mismatched connection") {
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_sample_mean_brightness_stage>("mean"));
    pipe.add(std::make_unique<vc::pipe::vc_sample_grayscale_stage>("grey"));
    // Wire mean's `double` output into grayscale's image input -> mismatch.
    pipe.connect("mean", vc::pipe::vc_sample_mean_brightness_stage::slots::mean,
                 "grey", vc::pipe::vc_sample_grayscale_stage::slots::rgb);
    CHECK_THROWS_AS(pipe.validate(), vc::vc_exception);
}

TEST_CASE("vc_pipeline: validate() rejects two connections wired into the"
          " same input slot") {
    // A declared input slot holds exactly one packet, so b.in can't be fed
    // by both a1 and a2 — this is a malformed graph, not a merge.
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a1"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a2"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));
    pipe.connect("a1", vc::pipe::vc_passthrough_stage::slots::out, "b",
                 vc::pipe::vc_passthrough_stage::slots::in);
    pipe.connect("a2", vc::pipe::vc_passthrough_stage::slots::out, "b",
                 vc::pipe::vc_passthrough_stage::slots::in); // same to-port
    CHECK_THROWS_AS(pipe.validate(), vc::vc_exception);
}

TEST_CASE("vc_pipeline: run() drives a packet through a two-stage chain") {
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));
    pipe.connect("a", vc::pipe::vc_passthrough_stage::slots::out, "b",
                 vc::pipe::vc_passthrough_stage::slots::in);

    // Open input: a.in (b.in is fed by the connection). Open output: b.out.
    std::unordered_map<vc::pipe::stage_port, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        vc::pipe::stage_port{"a", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});

    const auto outputs = pipe.run(std::move(inputs));

    const auto it = outputs.find(
        vc::pipe::stage_port{"b", vc::pipe::vc_passthrough_stage::slots::out});
    REQUIRE(it != outputs.end());
    CHECK(it->second.type() == typeid(vc::vc_image));
}

TEST_CASE("vc_pipeline: run() fans one output out to TWO downstream inputs"
          " without corrupting either copy") {
    // a.out feeds both b.in and c.in — a legitimate fan-out (nothing
    // restricts a connection's `from` to a single use). Regression test for
    // the bug where the first consumer's std::move left the second with an
    // empty (moved-from) packet.
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("c"));
    pipe.connect("a", vc::pipe::vc_passthrough_stage::slots::out, "b",
                 vc::pipe::vc_passthrough_stage::slots::in);
    pipe.connect("a", vc::pipe::vc_passthrough_stage::slots::out, "c",
                 vc::pipe::vc_passthrough_stage::slots::in);

    std::unordered_map<vc::pipe::stage_port, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        vc::pipe::stage_port{"a", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(3, 2, 3)});

    const auto outputs = pipe.run(std::move(inputs));

    const auto b_it = outputs.find(
        vc::pipe::stage_port{"b", vc::pipe::vc_passthrough_stage::slots::out});
    const auto c_it = outputs.find(
        vc::pipe::stage_port{"c", vc::pipe::vc_passthrough_stage::slots::out});
    REQUIRE(b_it != outputs.end());
    REQUIRE(c_it != outputs.end());

    // Both branches must see the SAME, intact image — not one moved-from and
    // the other valid.
    CHECK(b_it->second.has_value());
    CHECK(c_it->second.has_value());
    const auto& b_image = b_it->second.get<vc::vc_image>();
    const auto& c_image = c_it->second.get<vc::vc_image>();
    CHECK(b_image.width() == 3);
    CHECK(b_image.height() == 2);
    CHECK(c_image.width() == 3);
    CHECK(c_image.height() == 2);
}

TEST_CASE("vc_pipeline: run() rejects an input map that does not cover exactly"
          " the open inputs") {
    // The map-variant contract (Sec 12.2): the caller's map must cover EXACTLY
    // the open inputs — a missing OR extra key is an error. Here b.in is fed by
    // the connection (not open), so passing it is an extra key.
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));
    pipe.connect("a", vc::pipe::vc_passthrough_stage::slots::out, "b",
                 vc::pipe::vc_passthrough_stage::slots::in);

    std::unordered_map<vc::pipe::stage_port, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        vc::pipe::stage_port{"a", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});
    inputs.emplace( // extra: b.in is not an open input
        vc::pipe::stage_port{"b", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});

    CHECK_THROWS_AS(pipe.run(std::move(inputs)), vc::vc_exception);
}

TEST_CASE("vc_pipeline: run() injects and harvests MULTIPLE open ports") {
    // Two unconnected passthroughs: two open inputs, two open outputs.
    // Exercises the map-variant's exactly-cover-the-open-inputs rule and
    // multi-key harvest.
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));

    std::unordered_map<vc::pipe::stage_port, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        vc::pipe::stage_port{"a", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(2, 2, 3)});
    inputs.emplace(
        vc::pipe::stage_port{"b", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(4, 4, 3)});

    const auto outputs = pipe.run(std::move(inputs));

    CHECK(outputs.size() == 2);
    CHECK(outputs.count(vc::pipe::stage_port{
              "a", vc::pipe::vc_passthrough_stage::slots::out}) == 1);
    CHECK(outputs.count(vc::pipe::stage_port{
              "b", vc::pipe::vc_passthrough_stage::slots::out}) == 1);
}
