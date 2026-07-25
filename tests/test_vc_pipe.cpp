// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — tests/test_vc_image.cpp already
// defines doctest's main for the whole test binary; only one TU may.
#include "doctest/doctest.h"

#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

#include "vc/pipe/stages/vc_blur_stage.h"
#include "vc/pipe/stages/vc_grayscale_stage.h"
#include "vc/pipe/stages/vc_mean_brightness_stage.h"
#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"

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
        ctx.get_input(vc::pipe::vc_mean_brightness_stage::slots::image));

    // set_output(slot<double>) writes on the descriptor's name; harvest it
    // back.
    ctx.set_output(vc::pipe::vc_mean_brightness_stage::slots::mean,
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
                                  vc::pipe::vc_grayscale_stage::slots::rgb};
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
    const vc::pipe::vc_grayscale_stage grey{"grey"};
    const vc::pipe::vc_mean_brightness_stage mean{"mean"};

    CHECK(pass.params_hash() == 0);
    CHECK(grey.params_hash() == 0);
    CHECK(mean.params_hash() == 0);
}

// =====================================================================
// SPEC (RED until you implement the TODO(you) bodies). These define the
// target behaviour for the stage declare()/process() and the pipeline
// validate()/run(). They fail loudly until written — the same convention
// as tests/test_vc_image.cpp. This is your worklist.
// =====================================================================

TEST_CASE("vc_grayscale_stage: declares rgb-in -> grey-out (image->image)") {
    vc::pipe::vc_pipe_contract c;
    const vc::pipe::vc_grayscale_stage stage{"grey"};
    stage.declare(c); // TODO(you): vc_grayscale_stage::declare()

    // RED until declare() is written: the lookup throws on the undeclared slot,
    // which doctest reports as a failure — exactly the "fail loudly"
    // convention.
    CHECK(c.input_slot_type("rgb") == std::type_index(typeid(vc::vc_image)));
    CHECK(c.output_slot_type("grey") == std::type_index(typeid(vc::vc_image)));
}

TEST_CASE("vc_mean_brightness_stage: declares a NON-IMAGE (double) output"
          " — the heterogeneous type contract this design exists for") {
    vc::pipe::vc_pipe_contract c;
    const vc::pipe::vc_mean_brightness_stage stage{"mean"};
    stage.declare(c); // TODO(you): vc_mean_brightness_stage::declare()

    CHECK(c.input_slot_type("image") == std::type_index(typeid(vc::vc_image)));
    // The point of this stage: a NON-image (double) output slot.
    CHECK(c.output_slot_type("mean") == std::type_index(typeid(double)));
}

// =====================================================================
// GREEN — the worked-example stage's STRUCTURE (ctor stores params,
// kind(), name()) and its hand-written params_hash(). The kernel itself
// is the RED spec below.
// =====================================================================

TEST_CASE("vc_blur_stage: is constructed with its params and exposes them") {
    vc::pipe::vc_blur_params cfg;
    cfg.radius = 2.0;
    const vc::pipe::vc_blur_stage blur{"blur", cfg};

    CHECK(std::string{blur.kind()} == "blur");
    CHECK(blur.name() == "blur");
    CHECK(blur.params().radius == doctest::Approx(2.0));
    CHECK(blur.params().normalize == true);
}

TEST_CASE("vc_blur_stage: params_hash() is deterministic and varies with radius,"
          " not with name") {
    vc::pipe::vc_blur_params a;
    a.radius = 2.0;
    vc::pipe::vc_blur_params b;
    b.radius = 3.0;

    const vc::pipe::vc_blur_stage blur_a{"blur", a};
    const vc::pipe::vc_blur_stage blur_a_again{"blur", a};
    const vc::pipe::vc_blur_stage blur_a_renamed{"blur2", a};
    const vc::pipe::vc_blur_stage blur_b{"blur", b};

    CHECK(blur_a.params_hash() == blur_a_again.params_hash());   // deterministic
    CHECK(blur_a.params_hash() == blur_a_renamed.params_hash()); // name-independent
    CHECK(blur_a.params_hash() != blur_b.params_hash());         // radius varies it
}

// =====================================================================
// SPEC (RED until you implement the kernel). Driven DIRECTLY through a
// vc_pipe_context — NOT through run() (a separate TODO) — so this is red
// for exactly ONE reason: vc_blur_stage::process() is unwritten.
// =====================================================================

TEST_CASE("vc_blur_stage: process() publishes a blurred image on its output slot"
          " (RED until you implement the kernel)") {
    const vc::pipe::vc_blur_stage blur{"blur", vc::pipe::vc_blur_params{}};

    // Seed the input slot with a source image and drive process() directly.
    std::unordered_map<vc::pipe::slot_name, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        std::string(vc::pipe::vc_blur_stage::slots::in.name),
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros<vc::buf_f32>(4, 4, 3)});
    vc::pipe::vc_pipe_context ctx{std::move(inputs)};

    // The shell throws (TODO(you)), so this is RED; a written kernel does not
    // throw and publishes on slots::out -> GREEN.
    CHECK_NOTHROW(blur.process(ctx));

    const auto outputs = std::move(ctx).take_outputs();
    CHECK(outputs.count(std::string(vc::pipe::vc_blur_stage::slots::out.name)) ==
          1);
    // TODO(you): once your kernel is written, strengthen this beyond "an output
    // exists" — e.g. feed a sharp edge and assert interior pixels move toward the
    // local mean (a normalized blur, box or gaussian, satisfies that).
}

TEST_CASE("vc_pipeline: validate() accepts a matched image->image chain") {
    // RED until vc_grayscale_stage::declare() is written: validate() itself
    // is done, but grey's declare() is still a TODO(you) stub, so it never
    // declares the "grey" output slot validate() looks up here — the throw
    // this produces today is "no output slot named 'grey' was declared", not
    // a connection type mismatch. The mismatch case below is the real driver
    // for validate()'s own type-comparison logic.
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_grayscale_stage>("grey"));
    pipe.add(std::make_unique<vc::pipe::vc_mean_brightness_stage>("mean"));
    pipe.connect(
        "grey", vc::pipe::vc_grayscale_stage::slots::grey, "mean",
        vc::pipe::vc_mean_brightness_stage::slots::image); // image->img
    CHECK_NOTHROW(pipe.validate()); // TODO(you): vc_grayscale_stage::declare()
}

TEST_CASE("vc_pipeline: validate() rejects a type-mismatched connection") {
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_mean_brightness_stage>("mean"));
    pipe.add(std::make_unique<vc::pipe::vc_grayscale_stage>("grey"));
    // Wire mean's `double` output into grayscale's image input -> mismatch.
    pipe.connect("mean", vc::pipe::vc_mean_brightness_stage::slots::mean,
                 "grey", vc::pipe::vc_grayscale_stage::slots::rgb);
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
