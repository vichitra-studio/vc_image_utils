// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — tests/test_vc_image.cpp already
// defines doctest's main for the whole test binary; only one TU may.
#include "doctest/doctest.h"

#include <string>
#include <utility>

#include "vc/pipe/stages/vc_blur_stage.h"
#include "vc/pipe/vc_pipe_context.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/vc_image.h"
#include "vc/vc_param_schema.h"

// recording_store is a PURE test spy — it lives in tests/support/ (out of
// this test TU) because test doubles never ship in library code; see
// tests/support/recording_store.h for why it lives there rather than beside
// vc_memory_table/vc_memory_image_meta in the library.
#include "support/recording_store.h"

using vc::test_support::recording_store;

// =====================================================================
// GREEN — the param SCHEMA framework (vc/vc_param_schema.h) is FULLY
// WRITTEN. save()/load() walk a schema and round-trip a params struct
// through any sink with the {double,int,bool} set/get surface. These pass
// today and pin the mechanism you'll reuse for every toy stage.
// =====================================================================

TEST_CASE("param schema: save() writes one entry per field with the right value") {
    vc::pipe::vc_blur_params p;
    p.radius = 4.5;
    p.normalize = false;

    recording_store store;
    vc::params::save(p, store);

    CHECK(store.kv.size() == 2); // one entry per schema field, no more
    REQUIRE(store.kv.count("radius") == 1);
    REQUIRE(store.kv.count("normalize") == 1);
    CHECK(std::get<double>(store.kv.at("radius")) == doctest::Approx(4.5));
    CHECK(std::get<bool>(store.kv.at("normalize")) == false);
}

TEST_CASE("param schema: load() round-trips a saved params struct back to equal") {
    vc::pipe::vc_blur_params original;
    original.radius = 7.25;
    original.normalize = false;

    recording_store store;
    vc::params::save(original, store);

    vc::pipe::vc_blur_params restored; // starts at defaults (1.0, true)
    vc::params::load(restored, store);

    CHECK(restored.radius == doctest::Approx(7.25));
    CHECK(restored.normalize == false);
}

TEST_CASE("param schema: load() is a TOTAL get — a miss leaves the current value") {
    const recording_store empty; // nothing stored

    vc::pipe::vc_blur_params p;
    p.radius = 9.0; // pretend a prior edit; the empty store has no "radius" key

    // A missing key must NOT clobber: get() returns the fallback (= current value),
    // so the field is left as-is. This is what makes an unknown/removed key a
    // no-op (forward/back-compat for free).
    vc::params::load(p, empty);

    CHECK(p.radius == doctest::Approx(9.0)); // unchanged
    CHECK(p.normalize == true);              // unchanged (default)
}

// =====================================================================
// GREEN — the worked-example stage's STRUCTURE (ctor stores params,
// kind(), name()). The kernel itself is the RED spec below.
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
