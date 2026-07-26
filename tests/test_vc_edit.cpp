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
#include "vc/edit/vc_edit_document_io.h"
#include "vc/edit/vc_edit_session.h"
#include "vc/edit/vc_edit_table.h"
#include "vc/edit/vc_image_meta.h"
#include "vc/edit/vc_render_image.h"
#include "vc/edit/vc_render_request.h"
#include "vc/edit/vc_stage_registry.h"
#include "samples/vc_sample_blur_stage.h"
#include "vc/pipe/i_pipe.h"
#include "vc/pipe/stages/vc_passthrough_stage.h"
#include "vc/pipe/vc_cancellation_token.h"
#include "vc/pipe/vc_pipe_contract.h"
#include "vc/pipe/vc_pipe_packet.h"
#include "vc/pipe/vc_pipe_types.h"
#include "vc/pipe/vc_pipeline.h"
#include "vc/pipe/vc_render_context.h"
#include "vc/vc_exception.h"
#include "vc/vc_image.h"

#include <concepts>
#include <filesystem>
#include <fstream>
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

// =====================================================================
// GREEN — vc_edit_document_io.h: save_edit_document()/load_edit_document()
// are the actual deliverable (persist a document across phases as a real
// file). JSON is an implementation detail of vc_edit_document_io.cpp — these
// tests never mention nlohmann, only files, matching the public surface.
// =====================================================================

TEST_CASE("save_edit_document()/load_edit_document(): round-trip a full"
          " document through a real file") {
    vc::edit::vc_edit_document doc;
    doc.capture.bracket_count = 7;
    doc.capture.ev_spacing = 1.5;
    doc.exposure.enabled = false;
    doc.exposure.ev = 2.25;
    doc.exposure.black = -0.1;

    const std::filesystem::path path =
        std::string(VC_TEST_OUTPUT_DIR) + "/edit_document_test_output.json";
    std::filesystem::remove(path); // clean slate from a prior run

    vc::edit::save_edit_document(path, doc);
    CHECK(std::filesystem::exists(path));

    const auto restored = vc::edit::load_edit_document(path);
    CHECK(restored.capture.bracket_count == 7);
    CHECK(restored.capture.ev_spacing == doctest::Approx(1.5));
    CHECK(restored.exposure.enabled == false);
    CHECK(restored.exposure.ev == doctest::Approx(2.25));
    CHECK(restored.exposure.black == doctest::Approx(-0.1));
}

TEST_CASE("load_edit_document(): a missing file throws file_not_found") {
    const std::filesystem::path path =
        std::string(VC_TEST_OUTPUT_DIR) + "/does_not_exist.json";
    std::filesystem::remove(path);

    CHECK_THROWS_AS(vc::edit::load_edit_document(path), vc::vc_exception);
}

TEST_CASE("load_edit_document(): a file missing an entire slice loads it at"
          " its defaults (back-compat: a document saved before that slice"
          " existed)") {
    const std::filesystem::path path =
        std::string(VC_TEST_OUTPUT_DIR) + "/edit_document_missing_slice.json";
    {
        std::ofstream out(path);
        // "exposure" is absent, as if saved before that slice existed.
        out << R"({"version": 1, "capture": {"bracket_count": 3,)"
               R"( "ev_spacing": 1.0}})";
    }

    const auto restored = vc::edit::load_edit_document(path);

    CHECK(restored.capture.bracket_count == 3);
    CHECK(restored.exposure.enabled == true);            // untouched default
    CHECK(restored.exposure.ev == doctest::Approx(0.0)); // untouched default
}

TEST_CASE("load_edit_document(): an unknown extra key is ignored"
          " (forward-compat: a document saved by newer code, read by older"
          " code)") {
    const std::filesystem::path path =
        std::string(VC_TEST_OUTPUT_DIR) + "/edit_document_extra_key.json";
    {
        std::ofstream out(path);
        out << R"({"version": 1,)"
               R"( "capture": {"bracket_count": 5, "ev_spacing": 2.0},)"
               R"( "exposure": {"enabled": true, "ev": 0.0, "black": 0.0},)"
               R"( "white_balance": {"temp": 5500}})"; // unknown to this build
    }

    vc::edit::vc_edit_document restored;
    CHECK_NOTHROW(restored = vc::edit::load_edit_document(path));
    CHECK(restored.capture.bracket_count == 5);
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
    const auto img = vc::vc_image::zeros<vc::buf_f32>(2, 2, 3);
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

// Fill in the session -> params slot for the sample stage. A LIBRARY stage
// declares its specialization in vc/edit/vc_stage_params.h and defines it in
// src/edit/vc_stage_params.cpp; vc_sample_blur_stage is a tests/samples/
// worked example, so its specialization lives here in the one TU that uses
// it — which also demonstrates the trait's OPEN-SET property: a translation
// can be added from any file, without editing the framework header. Defined
// inline in the struct (hence implicitly inline), so this stays ODR-safe.
namespace vc::edit {
template <>
struct vc_stage_params<vc::pipe::vc_sample_blur_stage> {
    static vc::pipe::vc_sample_blur_params
    from_session(const vc_edit_session& session) {
        // DERIVED from a session slice rather than returning defaults, so the
        // test below can prove two things a constant could not: that the
        // trait is really consulted (a default-valued result is
        // indistinguishable from "never called"), and that the session
        // reaching it is the very one handed to create(). A real stage would
        // read its own knob; the sample stage has none, so exposure.ev
        // stands in as an arbitrary observable input.
        vc::pipe::vc_sample_blur_params params;
        params.radius = 1.0 + session.edits().exposure.ev;
        return params;
    }
};
} // namespace vc::edit

// The ENFORCEMENT itself, pinned at compile time — the whole point of
// replacing the builder argument with this trait. A stage that never stated
// its session -> params translation does NOT satisfy the concept, so
// register_stage<X>() will not compile for it. Asserted rather than merely
// documented so that accidentally making the concept vacuously true (e.g.
// giving the primary template a definition) fails the build here.
namespace {
// A stage type that is params-SHAPED — its ctor takes (stage_name, params),
// so std::constructible_from is satisfiable — but that deliberately has NO
// vc_stage_params specialization.
//
// The subject has to be shaped this way for the negative assert to mean
// anything. Using vc_passthrough_stage here (an earlier revision did) makes
// the assert OVERDETERMINED: its ctor takes only a name, so the concept is
// false whether or not a specialization exists — verified by compiling a
// probe that specialized the trait for it and watching the assert still
// pass. With this type, "missing specialization" is the ONLY reason left,
// so the assert genuinely pins the enforcement.
struct unregistered_params {
    double v = 0.0;
};
class unregistered_stage : public vc::pipe::i_pipe {
  public:
    unregistered_stage(vc::pipe::stage_name name, unregistered_params)
        : i_pipe(std::move(name)) {
    }
    const char* kind() const override {
        return "unregistered";
    }
    std::size_t params_hash() const override {
        return 0;
    }
    void declare(vc::pipe::vc_pipe_contract&) const override {
    }

  private:
    void validate_inputs(const vc::pipe::vc_pipe_context&) const override {
    }
    void do_process(vc::pipe::vc_pipe_context&) const override {
    }
};
} // namespace

// Sanity: the type really IS constructible from (name, params), so the
// negative assert below cannot be passing for that reason.
static_assert(std::constructible_from<unregistered_stage, vc::pipe::stage_name,
                                      unregistered_params>);
static_assert(!vc::edit::vc_stage_params_req<unregistered_stage>,
              "a stage with no vc_stage_params specialization must NOT "
              "satisfy vc_stage_params_req");

// This also pins that naming the undefined primary inside the concept is
// SFINAE-friendly (evaluates to false) rather than a hard error — the
// property that makes register_stage()'s clean diagnostic possible, and the
// one most at risk of differing on another toolchain.

// ...and the positive half: the specialization above really does satisfy it.
static_assert(vc::edit::vc_stage_params_req<vc::pipe::vc_sample_blur_stage>,
              "vc_sample_blur_stage's vc_stage_params specialization should "
              "satisfy vc_stage_params_req");

TEST_CASE("vc_stage_registry: register_stage -> create(kind,name,session)"
          " round-trips a kind through the session-aware path, taking its"
          " params from the vc_stage_params<StageT> trait") {
    vc::edit::vc_stage_registry registry;
    CHECK_FALSE(registry.has("sample_blur")); // nothing registered yet

    // No builder argument: the translation comes from the specialization
    // above. Concept-enforced at THIS call (vc_stage_params_req) — had the
    // specialization been missing, this line would not compile.
    registry.register_stage<vc::pipe::vc_sample_blur_stage>("sample_blur");
    CHECK(registry.has("sample_blur"));

    // A NON-default slice value, so the radius the trait derives from it
    // (1.0 + ev = 4.0) is distinguishable both from the params default (1.0)
    // and from "the trait was never called".
    const auto img = vc::vc_image::zeros<vc::buf_f32>(2, 2, 3);
    vc::edit::vc_edit_document doc;
    doc.exposure.ev = 3.0;
    vc::edit::vc_memory_table backing;
    vc::edit::vc_persistent_edits_table persistent{backing};
    vc::edit::vc_cached_edits_table cache{backing};
    const vc::edit::vc_edit_session session{
        img, doc, std::make_unique<vc::edit::vc_memory_image_meta>(),
        persistent, cache};

    const auto stage = registry.create("sample_blur", "b", session);
    REQUIRE(stage != nullptr);
    CHECK(std::string{stage->kind()} == "sample_blur"); // per-type identity
    CHECK(stage->name() == "b");                 // per-instance name

    // THE point of the trait: the params the stage was built with really came
    // out of vc_stage_params<StageT>::from_session(), fed by THIS session.
    // Without this the test would pass even if create() default-constructed
    // the params and never consulted the trait at all.
    const auto* blur =
        dynamic_cast<const vc::pipe::vc_sample_blur_stage*>(stage.get());
    REQUIRE(blur != nullptr);
    CHECK(blur->params().radius == doctest::Approx(4.0)); // 1.0 + ev(3.0)

    // An unknown kind throws (map-lookup-or-throw plumbing) on this path too.
    CHECK_THROWS_AS(registry.create("nope", "x", session), vc::vc_exception);
}

TEST_CASE("vc_stage_registry: one kind cannot be claimed by BOTH registration"
          " paths") {
    // The two paths own separate maps, so without an explicit guard the same
    // kind string could live in both and resolve to a DIFFERENT stage type
    // depending on which create() overload the caller used — a silently wrong
    // stage, with has() unable to distinguish them. Rejected at registration
    // instead, in both directions.
    auto paramless = [](vc::pipe::stage_name name) {
        return std::make_unique<vc::pipe::vc_passthrough_stage>(
            std::move(name));
    };

    SUBCASE("paramless first, then session-aware") {
        vc::edit::vc_stage_registry registry;
        registry.register_kind("clash", paramless);
        CHECK_THROWS_AS(
            registry.register_stage<vc::pipe::vc_sample_blur_stage>("clash"),
            vc::vc_exception);

        // STRONG guarantee: the rejected registration must leave NO residue.
        // Throwing but still having inserted into session_factories_ would
        // reintroduce the very split-brain this guard exists to prevent, and
        // a bare CHECK_THROWS_AS cannot tell the two apart.
        CHECK(registry.create("clash", "n")->kind() == std::string{"passthrough"});
        const auto img = vc::vc_image::zeros<vc::buf_f32>(2, 2, 3);
        vc::edit::vc_memory_table backing;
        vc::edit::vc_persistent_edits_table persistent{backing};
        vc::edit::vc_cached_edits_table cache{backing};
        const vc::edit::vc_edit_session session{
            img, vc::edit::vc_edit_document{},
            std::make_unique<vc::edit::vc_memory_image_meta>(), persistent,
            cache};
        CHECK_THROWS_AS(registry.create("clash", "n", session),
                        vc::vc_exception);
    }

    SUBCASE("session-aware first, then paramless") {
        vc::edit::vc_stage_registry registry;
        registry.register_stage<vc::pipe::vc_sample_blur_stage>("clash");
        CHECK_THROWS_AS(registry.register_kind("clash", paramless),
                        vc::vc_exception);

        // Same strong guarantee in the other direction: the name-only map
        // must be untouched, so create(kind,name) still reports unknown kind.
        CHECK_THROWS_AS(registry.create("clash", "n"), vc::vc_exception);
    }

    SUBCASE("re-registering on the SAME path is still last-wins, not an error") {
        vc::edit::vc_stage_registry registry;
        registry.register_kind("same", paramless);
        CHECK_NOTHROW(registry.register_kind("same", paramless));

        registry.register_stage<vc::pipe::vc_sample_blur_stage>("also_same");
        CHECK_NOTHROW(
            registry.register_stage<vc::pipe::vc_sample_blur_stage>(
                "also_same"));
    }
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
    const auto img = vc::vc_image::zeros<vc::buf_f32>(2, 2, 3);
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
    const auto img = vc::vc_image::zeros<vc::buf_f32>(4, 3, 3);
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
    const auto img = vc::vc_image::zeros<vc::buf_f32>(4, 3, 3);
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

TEST_CASE("vc_cached_edits_table: get misses, then hits after set()"
          " (RED until get()/set() are written)") {
    // Content-hash adapter over the byte store: a miss returns nullopt
    // (=> recompute), and a set() then makes the same hash hit. The shells
    // throw, so this is RED; when written, the miss-then-hit sequence holds
    // -> GREEN. vc_cached_edits_table EXPOSES misses (a TOTAL-get reader
    // would instead hide a miss behind a default) — this is the
    // reproducible, content-hash-keyed store; this failure traces to
    // exactly vc_cached_edits_table::get/set, not
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

TEST_CASE("vc_pipeline: run() observes a PRE-CANCELLED run context and"
          " stops") {
    // Amendment (a): run(inputs, run_context) checks cancellation BETWEEN
    // stages. The setup mirrors the passing two-stage run() success case in
    // tests/test_vc_pipe.cpp EXACTLY — a two-stage a->b chain with valid,
    // exactly-covering open inputs — so the ONLY delta from a green run is the
    // pre-cancelled context. Two stages (not one) so a pre-cancelled context
    // must be observed under BOTH readings of "checked between stages"
    // (before-each-stage AND strictly between a and b). Built with the raw
    // vc_pipeline API, not build_pipeline, so this does not depend on that
    // separate rep.
    //
    // POLICY: cancel => throw vc::vc_exception (vc_error_code::user_cancelled,
    // via vc::throw_if_cancelled) — matches vc_pipeline::run()'s own body.
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

    vc::pipe::vc_cancellation_source src;
    src.cancel(); // pre-cancelled before run() is even entered
    const vc::pipe::vc_render_context run_context{src.token()};

    CHECK_THROWS_AS(pipe.run(std::move(inputs), run_context), vc::vc_exception);
}
