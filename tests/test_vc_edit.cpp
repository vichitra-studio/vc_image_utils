// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — tests/test_vc_image.cpp already
// defines doctest's main for the whole test binary; only one TU may.
#include "doctest/doctest.h"

#include <cstddef>

#include "vc/edit/vc_build_pipeline.h"
#include "vc/edit/vc_export.h"
#include "vc/edit/vc_table.h"
#include "vc/edit/vc_edit_document.h"
#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_edit_settings_store.h"
#include "vc/edit/vc_edit_table.h"
#include "vc/edit/vc_image_meta.h"
#include "vc/edit/vc_render_image.h"
#include "vc/edit/vc_render_request.h"
#include "vc/edit/vc_stage_registry.h"
#include "vc/pipe/stages/vc_blur_stage.h"
#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_cancellation_token.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/pipe/vc_render_context.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#ifndef VC_TEST_OUTPUT_DIR
#error "VC_TEST_OUTPUT_DIR must be defined by CMake (see CMakeLists.txt)"
#endif

// =====================================================================
// GREEN — the edit-model plumbing that is FULLY WRITTEN (data structs,
// aggregates, stores, the cancellation token, the registry). These pass
// today and pin the scaffolded seams.
// =====================================================================

TEST_CASE("vc_edit_document: default settings match the design defaults") {
    const vc::edit::vc_edit_document doc;
    CHECK(doc.version == 1); // doc_version, reserved for migration
    CHECK(doc.capture.bracket_count == 5);
    CHECK(doc.capture.ev_spacing == doctest::Approx(2.0));
    CHECK(doc.exposure.enabled == true);
    CHECK(doc.exposure.ev == doctest::Approx(0.0));
    CHECK(doc.exposure.black == doctest::Approx(0.0));
}

TEST_CASE("vc_render_request: defaults to full resolution, whole image") {
    const vc::edit::vc_render_request req;
    CHECK(req.level == 0); // 0 = full resolution -> preview == export
    // All-zero ROI = whole image.
    CHECK(req.region.x == 0);
    CHECK(req.region.y == 0);
    CHECK(req.region.width == 0);
    CHECK(req.region.height == 0);
}

TEST_CASE("vc_edit_session: composes an immutable source and mutable edits") {
    const auto img = vc::vc_image::zeros(2, 2, 3);
    vc::edit::vc_edit_document doc;
    doc.exposure.ev = 2.0;

    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};
    vc::edit::vc_cached_edits_table cache{backing};
    vc::edit::vc_edit_session session{img, doc,
                                      std::make_unique<vc::edit::vc_memory_image_meta>(),
                                      persistent, cache};

    // source() exposes the immutable source geometry.
    CHECK(session.source().width() == 2);
    CHECK(session.source().height() == 2);
    CHECK(session.source().channels() == 3);

    // edits() (const) reads the composed document slice.
    CHECK(session.edits().exposure.ev == doctest::Approx(2.0));

    // edits() (non-const) is the source of truth for editing — mutate through it.
    session.edits().exposure.ev = 3.5;
    CHECK(session.edits().exposure.ev == doctest::Approx(3.5));

    // A metadata backend was injected at construction — the session enforces
    // it is non-null, so meta() hands back a plain reference and set()/get()
    // round-trip through it directly.
    session.meta().set("rating", vc::edit::vc_metadata_value{std::string{"5"}});
    CHECK(session.meta().get("rating").value().get<std::string>() == "5");
}

TEST_CASE("vc_memory_table: put/get round-trips bytes; a miss is nullopt") {
    vc::edit::vc_memory_table store;

    // A miss is expected control flow — nullopt, not a throw.
    CHECK_FALSE(store.get("absent").has_value());

    const vc::edit::data_bytes payload{std::byte{0x01}, std::byte{0x02},
                                        std::byte{0x03}};
    store.put("k", payload);

    const auto got = store.get("k");
    REQUIRE(got.has_value());
    CHECK(got->size() == 3);
    CHECK((*got)[0] == std::byte{0x01});
    CHECK((*got)[2] == std::byte{0x03});
}

TEST_CASE("vc_memory_image_meta: set/get round-trips; a miss is nullopt") {
    vc::edit::vc_memory_image_meta meta;
    CHECK_FALSE(meta.get("absent").has_value());
    meta.set("iso", vc::edit::vc_metadata_value{std::string{"100"}});
    REQUIRE(meta.get("iso").has_value());
    CHECK(meta.get("iso").value().get<std::string>() == "100");
}

TEST_CASE("vc_cancellation_source: cancel() is observed by its token; default"
          " token is never cancelled") {
    vc::pipe::vc_cancellation_source src;
    const auto token = src.token();
    CHECK_FALSE(token.cancelled()); // not cancelled until cancel()

    src.cancel();
    CHECK(token.cancelled()); // the flag is shared, so the token observes it

    // A default-constructed token has a null flag => never cancelled (this is
    // what every one-arg run() call binds).
    const vc::pipe::vc_cancellation_token never;
    CHECK_FALSE(never.cancelled());
}

TEST_CASE("vc_render_context: cancelled() delegates to its wrapped token; a"
          " default context is never cancelled") {
    // A default context wraps a never-cancelled token, so every existing
    // one-argument run()/vc_pipe_context call site is unaffected by this
    // milestone.
    const vc::pipe::vc_render_context default_context;
    CHECK_FALSE(default_context.cancelled());

    // Wrapping a live source's token: cancelled() tracks the source, the
    // same way vc_cancellation_token itself does — the context is a thin,
    // control-only host over the token, not a second cancellation
    // mechanism.
    vc::pipe::vc_cancellation_source src;
    const vc::pipe::vc_render_context ctx{src.token()};
    CHECK_FALSE(ctx.cancelled());

    src.cancel();
    CHECK(ctx.cancelled()); // the flag is shared, so the context observes it
}

TEST_CASE("i_edit_table: vc_cached_edits_table / vc_persistent_edits_table both"
          " construct over an injected i_table, and conform to the"
          " interface (GREEN — construction is plumbing, not a rep)") {
    vc::edit::vc_memory_table backing;

    // Both stores are injected the SAME backing store type, per the design
    // (they differ in key scheme / eviction / serialization lifecycle, not
    // in the storage engine beneath them).
    vc::edit::vc_cached_edits_table cache{backing};
    vc::edit::vc_persistent_edits_table persistent{backing};

    // Both conform to i_edit_table — a consumer can hold either behind
    // the one interface pointer, source-agnostically.
    const vc::edit::i_edit_table* cache_iface = &cache;
    const vc::edit::i_edit_table* persistent_iface = &persistent;
    CHECK(cache_iface != nullptr);
    CHECK(persistent_iface != nullptr);
}

TEST_CASE("vc_stage_registry: register -> has -> create round-trips a kind") {
    vc::edit::vc_stage_registry registry;
    CHECK_FALSE(registry.has("passthrough")); // nothing registered yet

    registry.register_kind("passthrough", [](vc::pipe::stage_name name) {
        return std::make_unique<vc::pipe::vc_passthrough_stage>(std::move(name));
    });
    CHECK(registry.has("passthrough"));

    const auto stage = registry.create("passthrough", "pass");
    REQUIRE(stage != nullptr);
    CHECK(std::string{stage->kind()} == "passthrough"); // per-type identity
    CHECK(stage->name() == "pass");                     // per-instance name

    // An unknown kind throws (map-lookup-or-throw plumbing).
    CHECK_THROWS_AS(registry.create("nope", "x"), vc::vc_exception);
}

TEST_CASE("vc_stage_registry: register_stage -> create(kind,name,session)"
          " round-trips a kind through the session-aware path (GREEN —"
          " create() is map-lookup plumbing; the builder here is a"
          " non-throwing STUB, not vc_blur_stage::from_session, which is a"
          " separate rep)") {
    vc::edit::vc_stage_registry registry;
    CHECK_FALSE(registry.has("blur")); // nothing registered yet

    // A stub builder (NOT vc_blur_stage::from_session — that is a TODO(you)
    // rep and would make this structural test red for the wrong reason).
    // Concept-enforced at THIS call: the builder is invocable with `const
    // vc_edit_session&` and its result constructs vc::pipe::vc_blur_stage
    // alongside a stage_name.
    registry.register_stage<vc::pipe::vc_blur_stage>(
        "blur", [](const vc::edit::vc_edit_session&) {
            return vc::pipe::vc_blur_params{};
        });
    CHECK(registry.has("blur"));

    const auto img = vc::vc_image::zeros(2, 2, 3);
    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};
    vc::edit::vc_cached_edits_table cache{backing};
    const vc::edit::vc_edit_session session{
        img, vc::edit::vc_edit_document{},
        std::make_unique<vc::edit::vc_memory_image_meta>(), persistent, cache};

    const auto stage = registry.create("blur", "b", session);
    REQUIRE(stage != nullptr);
    CHECK(std::string{stage->kind()} == "blur"); // per-type identity
    CHECK(stage->name() == "b");                 // per-instance name

    // An unknown kind throws (map-lookup-or-throw plumbing) on this path too.
    CHECK_THROWS_AS(registry.create("nope", "x", session), vc::vc_exception);
}

// =====================================================================
// SPEC (RED until you implement the TODO(you) bodies). Each fails loudly
// until its named rep is written — the same convention as
// tests/test_vc_pipe.cpp. This is your edit-model worklist. Every failure
// here must trace to exactly one TODO(you).
// =====================================================================

TEST_CASE("build_pipeline: assembles a runnable pipeline from a one-stage"
          " session (RED until build_pipeline() is written)") {
    // The spine: vc_edit_session -> build_pipeline -> (run -> vc_image). The
    // scaffold body throws, so this is RED; when build_pipeline assembles the
    // passthrough spine it returns a pipeline and stops throwing -> GREEN. The
    // failure is isolated to build_pipeline (not run(), which has no public stage
    // accessor to observe) — see the throwing-shell rationale in the header.
    const auto img = vc::vc_image::zeros(2, 2, 3);
    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};
    vc::edit::vc_cached_edits_table cache{backing};
    const vc::edit::vc_edit_session session{
        img, vc::edit::vc_edit_document{},
        std::make_unique<vc::edit::vc_memory_image_meta>(), persistent, cache};
    const vc::edit::vc_render_request request; // full-res, whole image

    CHECK_NOTHROW(
        [[maybe_unused]] auto pipe =
            vc::edit::build_pipeline(session, request)); // TODO(you)
}

TEST_CASE("render_image: renders a one-stage session end-to-end (RED until"
          " build_pipeline()/run() are written)") {
    // Composes build_pipeline + vc_pipeline::run() (the spine diagram,
    // edit_model_scaffold_plan.md Sec 3, made callable). The spine's one
    // stage is vc_passthrough_stage (identity — see its worked-reference
    // process()), so a correct render_image reproduces the source's geometry
    // exactly. RED today: the shell throws unconditionally.
    const auto img = vc::vc_image::zeros(4, 3, 3);
    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};
    vc::edit::vc_cached_edits_table cache{backing};
    const vc::edit::vc_edit_session session{
        img, vc::edit::vc_edit_document{},
        std::make_unique<vc::edit::vc_memory_image_meta>(), persistent, cache};
    const vc::edit::vc_render_request request; // full-res, whole image

    const auto rendered =
        vc::edit::render_image(session, request); // TODO(you)

    CHECK(rendered.width() == 4);
    CHECK(rendered.height() == 3);
    CHECK(rendered.channels() == 3);
}

TEST_CASE("export_image: renders a session and writes it to a file (RED"
          " until render_image()/export_image() are written)") {
    // The end of the export chain: vc_edit_session -> export_image -> a real
    // file on disk. Checks only that the file lands
    // (std::filesystem::exists), not its contents — decoding it back would
    // entangle this test's red with stb_image_reader's own separate,
    // unimplemented rep (the same throwing-shell isolation reasoning as
    // vc_build_pipeline.h).
    const auto img = vc::vc_image::zeros(4, 3, 3);
    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};
    vc::edit::vc_cached_edits_table cache{backing};
    const vc::edit::vc_edit_session session{
        img, vc::edit::vc_edit_document{},
        std::make_unique<vc::edit::vc_memory_image_meta>(), persistent, cache};

    const vc::edit::vc_export_config config{
        .path = std::string(VC_TEST_OUTPUT_DIR) + "/export_test_output.png",
        .write = {}};
    std::filesystem::remove(config.path); // clean slate from a prior run

    vc::edit::export_image(session, config); // TODO(you)

    CHECK(std::filesystem::exists(config.path));
}

TEST_CASE("vc_edit_settings_writer/vc_edit_settings_reader: round-trip a scalar through a"
          " vc_memory_table (RED until set()/get() are written)") {
    // Typed adapter over the byte store: a set() then get() must round-trip.
    // The shells throw, so this is RED; when the hand-rolled JSON mapping is
    // written the value survives the trip -> GREEN. vc_edit_settings_reader is
    // a TOTAL get — a miss yields the fallback, so this also pins the
    // miss-hiding half of the split.
    vc::edit::vc_memory_table store;
    vc::edit::vc_edit_settings_writer writer{store};
    const vc::edit::vc_edit_settings_reader reader{store};

    writer.set("exposure.ev", 1.5);                       // TODO(you): set()
    CHECK(reader.get("exposure.ev", 0.0) ==               // TODO(you): get()
          doctest::Approx(1.5));
    CHECK(reader.get("missing.key", -2.0) ==              // total get: default
          doctest::Approx(-2.0));
}

TEST_CASE("vc_cached_edits_table: get misses, then hits after set()"
          " (RED until get()/set() are written)") {
    // Content-hash adapter over the byte store: a miss returns nullopt
    // (=> recompute), and a set() then makes the same hash hit. The shells
    // throw, so this is RED; when written, the miss-then-hit sequence holds
    // -> GREEN. vc_cached_edits_table EXPOSES misses (opposite of
    // vc_edit_settings_reader) — this is the reproducible, content-hash-keyed
    // store; this failure traces to exactly vc_cached_edits_table::get/set, not
    // vc_persistent_edits_table's (see the sibling test below).
    vc::edit::vc_memory_table backing;
    vc::edit::vc_cached_edits_table cache{backing};

    // Miss before set: nullopt so the caller recomputes.
    CHECK_FALSE(cache.get("hash-abc").has_value()); // TODO(you): get()

    const vc::edit::data_bytes computed{std::byte{0xAB}, std::byte{0xCD}};
    cache.set("hash-abc", computed);                 // TODO(you): set()

    // Hit after set: the same content hash returns the bytes.
    const auto hit = cache.get("hash-abc");         // TODO(you): get()
    REQUIRE(hit.has_value());
    CHECK(hit->size() == 2);
}

TEST_CASE("vc_persistent_edits_table: get misses, then hits after set()"
          " (RED until get()/set() are written)") {
    // Id adapter over the byte store: a miss returns nullopt, and a set()
    // then makes the same id hit. The shells throw, so this is RED; when
    // written, the miss-then-hit sequence holds -> GREEN. This is the
    // non-reproducible, id-keyed store — a SEPARATE object from
    // vc_cached_edits_table above (own backing store instance, own rep), so
    // this failure traces to exactly vc_persistent_edits_table::get/set.
    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};

    // Miss before set: nullopt.
    CHECK_FALSE(persistent.get("mask-id-1").has_value()); // TODO(you): get()

    const vc::edit::data_bytes computed{std::byte{0xEF}};
    persistent.set("mask-id-1", computed);                 // TODO(you): set()

    // Hit after set: the same id returns the bytes.
    const auto hit = persistent.get("mask-id-1");         // TODO(you): get()
    REQUIRE(hit.has_value());
    CHECK(hit->size() == 1);
}

TEST_CASE("vc_pipeline: run() observes a PRE-CANCELLED run context and stops"
          " (RED until run()'s cancellation check is written)") {
    // Amendment (a): run(inputs, run_context) checks cancellation BETWEEN
    // stages. The setup mirrors the passing two-stage run() success case in
    // tests/test_vc_pipe.cpp EXACTLY — a two-stage a->b chain with valid,
    // exactly-covering open inputs — so the ONLY delta from a green run is the
    // pre-cancelled context: red->green tracks cancellation and nothing else.
    // Two stages (not one) so a pre-cancelled context must be observed under
    // BOTH readings of "checked between stages" (before-each-stage AND
    // strictly between a and b). Built with the raw vc_pipeline API, not
    // build_pipeline, so this does not depend on that separate rep.
    //
    // ASSUMED POLICY: cancel => throw vc::vc_exception. This matches the guiding
    // comment in vc_pipeline::run()'s body. run() currently returns {} (its body
    // is a TODO(you) rep), so it does NOT throw today -> RED. A return-empty
    // cancel policy is equally valid but must flip this expectation.
    vc::pipe::vc_pipeline pipe;
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("a"));
    pipe.add(std::make_unique<vc::pipe::vc_passthrough_stage>("b"));
    pipe.connect("a", vc::pipe::vc_passthrough_stage::slots::out, "b",
                 vc::pipe::vc_passthrough_stage::slots::in);

    // Open input: a.in (b.in is fed by the connection). Open output: b.out.
    std::unordered_map<vc::pipe::stage_port, vc::pipe::vc_pipe_packet> inputs;
    inputs.emplace(
        vc::pipe::stage_port{"a", vc::pipe::vc_passthrough_stage::slots::in},
        vc::pipe::vc_pipe_packet{vc::vc_image::zeros(2, 2, 3)});

    vc::pipe::vc_cancellation_source src;
    src.cancel(); // pre-cancelled before run() is even entered
    const vc::pipe::vc_render_context run_context{src.token()};

    CHECK_THROWS_AS(pipe.run(std::move(inputs), run_context), // TODO(you): run()
                    vc::vc_exception);
}
