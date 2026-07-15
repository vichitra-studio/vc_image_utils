# Performance Benchmarking — vc_image_utils

How we measure the performance of this library: the tool we use, the method, *what* we
benchmark, and how results are documented and stored. This document captures the *design we
are building toward* and the reasoning behind each decision — not a tutorial for nanobench
(read its own docs for that).

> **Status legend** (same tags as `pipe_design.md`):
> - **[DECIDED]** — settled; build to this.
> - **[NOW]** — in scope for the near-term scaffold.
> - **[LATER]** — designed here, deliberately not built yet; built when the problem demands it.
> - **[OPEN]** — a genuine unresolved question; do not assume an answer.
> - **[ASSUMPTION]** — taken as true but not yet verified or committed.
>
> Living document. Update it when a decision changes — do not let code drift from it (same rule
> as `coding_guidelines.md`). Reasoning captured in the **2026-07 benchmarking design session**.

---

## 0. TL;DR

- **Tool: [martinus/nanobench](https://github.com/martinus/nanobench) `v4.3.11`, vendored as a
  single header** in `third_party/`, like `doctest` and `stb`. **[DECIDED]**
- **`vc::utils::perf::scoped_timer` stays** — it is *instrumentation* (how long did one real run
  take, live, in the app), a different job from *benchmarking* (how fast is this code, reliably,
  over thousands of runs). We keep both. **[DECIDED]**
- **What we benchmark** = *subjects* (the data substrate, kernels, stages, representative
  pipelines, framework plumbing) × *dimensions* (size, dtype, channels, … — run on demand, not
  routinely). See §5.
- **The core measurement is an A/B ladder** (raw kernel → `process()` → `run()`) whose *deltas*
  are the framework overhead — this answers "are missed copies / design choices causing
  slowdowns?". See §6.
- **Scalability principle:** a small **routine regression set** (run often, baseline-tracked)
  kept separate from **ad-hoc investigation sweeps** (run on demand, not tracked). See §7.
- **Honest current status:** the **harness is built and runs** (nanobench vendored; `bench/` with
  `vc_benchmark_harness`/`vc_benchmark_pipe`/`vc_benchmark_pipeline` behind `VC_BUILD_BENCHMARKS`). The **substrate
  suite already produces real numbers** (buffer alloc/copy, `as<T>()`, packet box/unbox). But every
  piece of real *compute* is still a `TODO(you)` stub (`vc_image` ctor, all kernels, `run()`,
  `vc::io`), so the kernel/stage/pipeline suites are wired-and-ready but measure stubs — the
  **first real kernel number arrives when the `vc_image` ctor and one kernel are implemented**.
  See §9.

---

## 1. Scope — what this covers, and what it does not

**Benchmarking here = controlled, repeatable measurement of speed / throughput** of the library's
own code, on a development machine, to (a) know how fast a kernel is, (b) compare two
implementations of the same thing, and (c) catch performance regressions over time.

Out of scope for *this* document / tool, each its own track:

- **Correctness** — that is the `doctest` test suite. A benchmark *assumes* correctness (§4.5);
  it does not establish it.
- **Peak memory** — matters (the Block-2 HDR merge has a ~170 MB peak-memory budget — source:
  curriculum master overview §5, `vc_docs/curriculum/00_CV_Curriculum_Master_Overview.md`), but it
  is a separate instrument (RSS sampling / an OIIO-`--runstats`-style dump), not a timing harness.
  **[LATER]**
- **On-device (Pixel 10) performance** — desktop MP/s does **not** predict on-device MP/s
  (different ISA, memory system, thermal envelope). Device benchmarking is a separate track with
  its own baselines. **[LATER]**

---

## 2. Two timing tools, and why both exist [DECIDED]

They answer different questions and do not compete:

| | `scoped_timer` (`vc/utils/vc_perf.h`) | nanobench |
|---|---|---|
| **Question** | "How long did *this actual run* take, live, in the app?" | "How fast is this code, *reliably*, averaged over many runs?" |
| **Runs the code** | Once — whatever really happened | Thousands, auto-scaled |
| **Statistics** | None — one raw number | median, err%, MP/s, relative comparison |
| **Lives** | *Inside* the pipeline, in real usage | In a separate `bench/` executable |
| **Prior art** | darktable `-d perf`, RawTherapee per-stage timers | google/benchmark, OpenCV `ts` perf module |

**Rule [DECIDED]: `scoped_timer` / `vc::utils::perf` must be OFF inside a nanobench timed region.**
It routes through `vc::utils::log`, and logging I/O inside a benchmarked loop pollutes the number.
Per-stage `scoped_timer` attribution of a pipeline is a **separate, un-timed diagnostic pass**, not
part of the measured region.

---

## 3. Tool decision — nanobench [DECIDED]

### 3.1 The decision

Adopt **nanobench `v4.3.11`** as a vendored single header. Do **not** extend `scoped_timer` into a
benchmark harness, and do **not** adopt google/benchmark yet.

### 3.2 Why nanobench (on the criteria that actually matter here)

1. **Cleanest code for *our* primary need.** Our headline job is the harness-vs-processing A/B
   (§6). nanobench expresses it in-process with one flag — `Bench().relative(true)` plus a few
   `.run(...)` calls — and prints the ratios directly. google/benchmark has no in-process
   "relative" mode; you emit two JSON runs and diff them with `tools/compare.py`. For the exact
   thing we care about, nanobench keeps our benchmark code smaller and more readable.
2. **Fits the image domain out of the box.** `.unit("pixel").batch(width*height)` reports
   **megapixels/sec** directly — the resolution-independent unit for image ops (the libjpeg-turbo
   `tjbench` lesson). `doNotOptimizeAway(x)` is the dead-code-elimination guard. Median + **err%**
   (median % error) is reported by default, so an untrustworthy (noisy) run announces itself.
3. **An artifact we own.** It is one header. It has no build system and no transitive
   dependencies, so there is less that can break under us over a multi-year project (desktop now,
   cross-compile later). If upstream never ships another release, our vendored copy keeps
   compiling; any future-compiler fix is a one-line patch in a header we control.

### 3.3 Verified facts (checked 2026-07, not assumed)

- **License:** MIT. **Language:** C++11 and later (compatible with this project's C++20).
- **`v4.3.11` has no functional drift versus master.** Of the six commits after the tag (through
  the last push, 2024-10), five touched only docs and nanobench's *own* test files; one
  (`5d579e6`, "Fix docs") changed a single **doc-comment inside** `src/include/nanobench.h`
  (`minEpochTime()`'s documented default) — no executable code. So the header is *functionally*
  unchanged for ~3.5 years and vendoring the release loses nothing versus master. *(That comment
  fix records nanobench's real default `minEpochTime()` of ~1 ms — see §4.1.)*
- **ODR** across translation units is handled by the vendoring pattern itself: one copied header,
  with the implementation compiled in exactly one `.cpp` via `#define ANKERL_NANOBENCH_IMPLEMENT`
  (the same single-impl shape as `doctest`'s `DOCTEST_CONFIG_IMPLEMENT` and `stb`). *(There is no
  version-tagged inline namespace — an earlier claim to that effect was wrong and is retracted.)*
- **API surface we rely on** (confirmed present on `Bench`): `run`, `batch`, `unit`, `relative`,
  `minEpochIterations`, `epochs`, `warmup`, `complexityN`, `complexityBigO`, `render`,
  `performanceCounters`, and the free function `doNotOptimizeAway`. Output: default is a Markdown
  table; named templates for `json`, `csv`, `htmlBoxplot`, `pyperf`.

### 3.4 Known limitations (accept knowingly; mitigations in §4, §7, §8)

- **No auto-registration and no `--benchmark_filter`.** Unlike google/benchmark, benches are not
  self-registering and there is no built-in name filter. We engineer selection ourselves (§7).
- **No cross-run statistical significance test.** nanobench gives median + err% *within* a run;
  it has no equivalent of google/benchmark's `compare.py` Mann–Whitney U test *between* two runs.
  For a solo project, err%-aware threshold-on-median is sufficient (§8).
- **Hardware performance counters are Linux-only.** The cyc/op, ins/op, IPC columns use
  `perf_event_open`, which does **not** exist on macOS/Darwin. On the current dev Mac we get
  wall-clock median + err% only; cycle-accurate numbers appear only when run on Linux or the
  device. See §4.3.

### 3.5 When to revisit google/benchmark **[OPEN]**

Reconsider **only** when one becomes true: (a) multiple contributors need auto-registration /
filtering, or (b) we want built-in cross-run **significance testing**, `Threads` scaling sweeps,
or `Complexity` Big-O fitting as first-class features. All are Block-4+ concerns. Until then, the
in-process `.relative()` ergonomics win.

---

## 4. Methodology — measuring reliably [DECIDED]

### 4.1 The traps a naive timer falls into, and nanobench's answers

- **One run is noise** → nanobench runs many epochs (default 11), auto-scaling the iteration count
  so each epoch reaches a stable minimum time (`minEpochTime()`, default ~1 ms), and reports the
  **median** (robust to OS-scheduler outliers).
- **The compiler deletes unused work** → wrap the result in `doNotOptimizeAway(x)`.
- **Fast ops are unmeasurable alone** → nanobench batches iterations; for very cheap ops (e.g. a
  single refcount bump) also raise `minEpochIterations` and/or repeat the op N times in the
  lambda and set `.batch(N)`.

### 4.2 Report throughput, not just latency

For any data-parallel image op, report **megapixels/sec** via `.unit("pixel").batch(w*h)` (and
optionally ns/pixel). Latency in ms alone hides the effect of image size; MP/s normalizes across
resolutions and is the cross-comparable unit.

### 4.3 Machine control — the macOS reality [ASSUMPTION: dev box is Apple-Silicon macOS]

The textbook advice (pin to a core, disable turbo, `taskset`) is **Linux-shaped and largely
unavailable on Apple Silicon**: thread affinity is mostly ignored, and P-core/E-core scheduling
*adds* variance. nanobench's `perf_event` counters are also Linux-only (§3.4). Practical
mitigations on this dev box:

- Run on **AC power**, machine otherwise **quiet** (no build/browser load).
- Prefer the **ratio** metric (`.relative()`), which cancels most machine-wide drift, over
  absolute ms/MP-s when comparing implementations.
- **Gate on err%** — treat any run with a high err% as untrustworthy regardless of its median.
- Use enough repetitions/epochs that err% settles.

Cycle-accurate measurement, if ever needed, means running the same bench on a **Linux** host or
the device — a deliberate separate run, not the default.

### 4.4 Build configuration — pin it [DECIDED]

Benchmarks build **Release only**, with pinned optimization flags, in a target **separate from
the `doctest` test build** (a `-O0`/sanitizer build must never leak into a perf number), behind a
CMake option (e.g. `VC_BUILD_BENCHMARKS`) that is **off by default** so a normal build and the
test run are unaffected. `-march` must be **pinned** (not left to default), or two runs on the
same machine diverge. See §8 for how flags are recorded with each result.

A separate bench executable is required **regardless of tool choice** anyway: `doctest` (unlike
Catch2) has no benchmarking facility, so a bench cannot be "just another `TEST_CASE`" in the test
binary — verified, and the reason the bench targets stand alone from `vc_image_utils_tests`.

### 4.5 Correctness before timing [DECIDED]

A benchmark of a wrong kernel is a fast lie. Before publishing a number, **assert the kernel's
output once, outside the timed loop** (the OpenCV `SANITY_CHECK` lesson). Where practical, a bench
file shares its expected-output check with the corresponding `doctest` case — which presumes both
use the same input (see the input-data question, §11).

### 4.6 Cold vs warm cache

Microbenchmarks are **warm-cache** by nature (the input buffer is reused across iterations) and so
can be optimistic versus a real pipeline that touches each frame once. State which regime a bench
represents; a cold-cache variant is an investigation sweep (§7), not a routine number.

---

## 5. What to benchmark — the surface (subjects × dimensions) [DECIDED]

Two axes. **Subjects** = *what code* is measured. **Dimensions** = *how the input is varied*.
Your earlier three categories (harness, stages, pipelines) are all pipeline *compute*; the
substrate beneath them and the dimensions across them complete the picture.

### 5.1 Subjects, by priority tier

**Tier key:** *Tier 1* = foundational, benchmark early (sits under everything). *Tier 2* =
per-algorithm, added as each is built. *Tier 3* = milestone-driven, later.

**Tier 1 — the data substrate** (`vc_image` / `vc_pixel_buffer`). Highest value because it sits
*under every stage*: a cost here is a hidden tax on everything and is invisible in a per-stage
number.

- Buffer **allocate + fill + consume** (a full produce-then-discard frame pass) — the ctor does
  `vector<T>(count, fill)`, a full-buffer fill; at 12 MP × 4 ch × f32 ≈ 200 MB that traffic is
  real, and the HDR merge *streams* frames (allocate → use → discard), so it is hot-path. *Note
  (learned in the bench):* an **isolated** allocate-only or fill-only number is not reliably
  measurable — clang's heap-allocation elision deletes a fill (and even the alloc) when the buffer
  is only escaped by pointer and never read, even with a non-zero fill and a memory clobber. So the
  bench prices the realistic **produce-and-consume** lifecycle (fill, then sum every element, so
  the writes genuinely flow to an observed result) rather than a synthetic memset the optimizer
  erases.
- **Copy**: shallow (`vc_image` copy = `shared_ptr` refcount bump) vs deep (`vc_pixel_buffer`
  copy) — *proves* the shared-handle assumption and prices the unavoidable deep-copy cases.
- **`as<T>()` access**: hoisted-once vs called per-pixel — it does a `holds_alternative` check
  each call; the result *becomes a coding guideline*.
- **dtype / variant dispatch** cost — justifies "resolve dtype once at the top of a kernel."
- **`std::any` packet box/unbox** (`vc_pipe_packet`) — a ~32-byte `vc_image` exceeds the
  small-buffer optimization of common standard libraries (libstdc++ ~1 pointer, libc++ ~3
  pointers; implementation-dependent), so a heap allocation per packet is *expected*; the bench
  confirms the actual cost.

**Tier 1 — framework plumbing.** A no-op stage through `process()` and `run()` — the overhead
floor (§6); the instrument for catching missed copies and design regressions. *(Design target
today — see §9: `run()` is not implemented, and `passthrough` runs on a null buffer.)*

**Tier 2 — kernels (micro), reported in MP/s.** One per algorithm, added as it is built: grayscale
luminance, mean-brightness (P1); box/Gaussian/Laplacian convolution, separable vs 2D, pyramid
down/up-sample (P2); gradients, Harris response, feature detect (P3); MTB / Lucas-Kanade
alignment, Debevec merge-accumulate (Block 2); demosaic, denoise, tone-map (Block 6).

**Tier 2 — A/B pairs** (the `.relative()` ladder). Same kernel, two implementations. *Now:*
naive-vs-naive (loop order, `f32` vs `u8`, separable vs 2D). *Block 4+:* naive-C++ vs Halide.

**Tier 2 — stages.** Each stage through `stage.process(ctx)` — the kernel *plus* one layer of
framework (§6).

**Tier 2 — representative pipelines** (NOT the powerset — see §5.3). The real, shipped `vc::pipe`
chains, end-to-end, reporting total ms + whole-pipe MP/s; trust total-pipe time (darktable's
lesson) and use `scoped_timer` for per-stage attribution in a separate diagnostic pass.

**Tier 3 — milestone-driven:** codec I/O throughput (`vc::io`, once implemented); Eigen
small-solve and FFT backend timings, and **backend-swap** benches (e.g. FFTW → pocketfft) to
confirm a swap does not regress; the JNI boundary (Block 3); CLI startup.

### 5.2 Dimensions (run *ad hoc*, not routinely)

Image-size sweep (cache cliffs / O(n) linearity, `complexityN`); dtype (u8/u16/f32 — the merge's
uint16-in/float32-accum conversion cost); channel count; algorithm parameter (blur radius,
denoise window); cold vs warm cache; thread count (Block 4+). These multiply the subjects and
belong to the *investigation* set (§7), not the routine set.

### 5.3 "All combinations of pipeline" is not a target [DECIDED]

The powerset of stages is combinatorially impossible and unnecessary. Benchmark the
**representative / shipped** pipelines only. The whole ≈ (sum of per-stage kernel costs) +
(measured plumbing overhead) — which is exactly what the §6 ladder isolates, so per-stage +
overhead numbers already predict a novel wiring without benchmarking it directly.

### 5.4 Not this tool's job

Peak memory (§1, separate instrument); an **allocation-count guard** (a strong idea — count
mallocs per run as a machine-independent, deterministic missed-copy detector — but it is its own
instrumentation project: a global `operator new` override or a custom allocator through
`vc_pixel_buffer`; **[LATER]**, build when missed copies actually bite); on-device numbers (§1).

---

## 6. The rung ladder — isolating processing from plumbing [DECIDED]

The core A/B. Run the *same kernel* at three rungs in one `Bench().relative(true)`; the **deltas
between rungs are the framework overhead**:

| Rung | Call | Adds over previous |
|---|---|---|
| 1 | raw kernel on pre-allocated in/out buffers | — (pure compute + memory bandwidth) |
| 2a | `stage.process(ctx)` | `std::any` box/unbox, two `unordered_map<slot_name>` lookups (context), virtual `process()` dispatch |
| 2b | `pipeline.run(...)` | open-input/open-output computation, `unordered_map<stage_port>` hashing, `shared_ptr` refcount churn |

nanobench prints e.g. *"process() is 2.4×, run() is 3.8× the raw kernel"* directly. **That ratio
is the "is the design causing slowdowns?" instrument.** A missed/accidental deep copy shows up as
an MP/s cliff and a jump above the known plumbing cost.

**Status caveat (see §9):** this ladder is a *design target*. Today rung 2b (`run()`) is
unimplemented (measures an empty-map return) and rung 2a runs on a null buffer — so real ratios
appear only as those stubs land. The table's "adds over previous" column describes the *expected*
overhead contributors; the bench confirms the actual attribution.

Caveat, stated so it is not misread as a defect: on a **fat** kernel (convolution on a 12 MP
frame) the plumbing is negligible; on a **cheap** kernel it can dominate. That contrast *is* the
finding — it tells you where the type-erasure/dispatch model costs you and where it is free.

**Design corollary [DECIDED]:** kernels take a `(const input, output)` signature and **allocate
nothing in the hot path** (buffers supplied by the caller). This is simultaneously the fast
production path, the shared-buffer-safe path (no aliasing surprises from the `shared_ptr`-backed
buffer), and the re-runnable benchmark path (fixed input, overwritten output, no per-iteration
allocation or drift). The move-sink `run()` signature is kept only at the pipeline boundary where
it earns its keep.

---

## 7. Scalability — keeping the suite runnable as it grows [DECIDED]

Over P1 → P25 the suite grows to dozens of kernels × sizes × dtypes. The principle that keeps it
runnable is **separating two sets** — not buying heavier tooling:

- **Routine regression set** — one headline MP/s per hot kernel + the few real pipelines + the
  plumbing floor. Small, fast, run often, **baseline-tracked** (§8).
- **Investigation sweeps** — the §5.2 dimensions. Run **on demand** while profiling one kernel,
  **never routinely**, **not** baseline-tracked.

Conflating them yields a suite too slow to run, which then rots.

**Mechanism** (because nanobench has no auto-registration / filter, §3.4): **one binary per
category** — `vc_benchmark_harness`, `vc_benchmark_pipe`, `vc_benchmark_pipeline` — each with a simple `argv`
name-filter in its `main()` so a single bench can be run in isolation. That is the whole scaling
mechanism; it is a small amount of code we own.

---

## 8. Documentation & result storage [DECIDED]

**Reference doc** = this file. Each bench file self-documents what it measures and why.

**Results, two formats, both git-committed:**

| File | Format | Purpose |
|---|---|---|
| `bench/baselines/<suite>-<host>-<compiler>.json` | nanobench `json` under a `meta` header | machine-diffable baseline; **git history = poor-man's time series** (no dashboard needed) |
| `bench/baselines/<suite>-<host>-<compiler>.md` | nanobench Markdown (default) under a `meta` header | human-readable snapshot; drops into blog / build-log posts |

`<suite>` (`substrate`/`micro`/`macro`) is in the key because there is one binary per category
(§7) and three binaries would otherwise clobber one host-keyed file. **Numbers are
machine-specific** → baselines are **keyed by suite + host + compiler + flags**; never compare
across machines. nanobench's own `json()` carries per-result config but **no** host/CPU/compiler
identity, so the harness prepends that `meta` block itself (host, compiler, `-march`, flags, git
SHA, UTC time).

**Reproducibility metadata** stamped on every result set: **git SHA** (of the bench *definition*),
date, CPU, compiler + version, build flags (`-O`, pinned `-march`), and power/quiet state.

**Baseline lifecycle [DECIDED]:** a baseline is valid only for a fixed *(bench definition + host +
compiler + flags)*. When the bench **code** changes, the old baseline is silently invalid —
**regenerate it and stamp the new git SHA** (distinct from "perf changed"). Update baselines
**deliberately** when a real change lands (with a commit message saying why), **not** every commit.

**When to run:** the routine set (§7) before a release and whenever a hot path is touched;
investigation sweeps on demand. **Regression check:** run current vs committed baseline; flag a
median regression past a threshold **only when both runs have low err%** (otherwise it is noise).
No cross-run significance test (§3.4); if a dashboard is ever wanted, a service such as
**Bencher.dev** can ingest benchmark JSON — verify its current nanobench-adapter support before
adopting. **[OPEN]:** whether/when to gate CI on this — shared CI runners are noisy, so CI perf
numbers may be advisory rather than a hard gate.

**Layout:**

```
third_party/nanobench/nanobench.h           # vendored, v4.3.11 (subdir, like doctest/stb)
bench/
  vc_bench_support.{h,cpp}                   # argv name-filter, house config, JSON+MD baseline render
  vc_bench_impl.cpp                          # the ONE ANKERL_NANOBENCH_IMPLEMENT TU
  vc_benchmark_harness.cpp                   # Tier-1 substrate (real numbers today)
  vc_benchmark_pipe.cpp                      # rung 1 + rung 2a (all wired; none baselined until reps land, §9)
  vc_benchmark_pipeline.cpp                  # rung 2b — pipeline.run() (wired; stubbed today)
  baselines/<suite>-<host>-<compiler>.{json,md}   # committed reference results
docs/benchmarking.md                        # this file
```

*This layout is **now in the tree** — built behind the `VC_BUILD_BENCHMARKS` CMake option
(off by default; Release-only; FATAL if combined with a sanitizer build). Each category is its own
executable with an `argv` substring filter, `--list`, and `--baseline` (§7). Baseline files are
written only for baseline-eligible cases (§9).*

---

## 9. Current status — what is real today [NOW]

Honest state (verified 2026-07): **every piece of real compute in the repo is a `TODO(you)`
stub.**

- `vc_image`'s constructor (`src/vc_image.cpp`) is unimplemented — it leaves `pixels_` null and
  dimensions 0.
- All stage kernels (`vc_grayscale_stage`, `vc_mean_brightness_stage`) are stubs; `vc_passthrough`
  is implemented but is a no-op carry.
- **`vc_pipeline::run()` and `validate()` are themselves stubs** (`src/pipe/vc_pipeline.cpp`):
  `run()`'s body is `return {}` — it does not walk stages, compute open ports, move packets, or
  call any `process()`; `validate()` does no contract/type check. The pipeline layer executes
  nothing yet.
- `vc::io` `read`/`write` (`src/io/vc_io_stb.cpp`) throw — not yet implemented.

Consequences:

- **The harness is built and runs today.** `bench/` is in the tree behind `VC_BUILD_BENCHMARKS`;
  the substrate suite already yields real, stable numbers (e.g. shallow-copy ≈ 3 ns vs deep-copy
  ≈ 325 µs; `vc_image` box+unbox ≈ 12 ns vs inline `double` ≈ 1 ns) — and is the **only** suite
  baseline-eligible today. The passthrough rung 1 → 2a delta already prices one framework layer but
  is a *partial* floor (null buffer, see below), so it runs+prints but is **not** committed yet.
  The kernel/stage/pipeline cases are wired to the real API and activate with **zero rework** as the
  stubs fill.
- **The first real number is gated on the `vc_image` ctor + one kernel** (`grayscale::process`) —
  that is P1 implementation work, on the critical path regardless of benchmarking.
- The rung ladder (§6) is a **design target, not runnable end-to-end today**:
  - **Rung 2a (`process()`)** works now for `passthrough` (that stage is implemented), but is only
    a **partial** floor: with `pixels_` null it exercises `std::any` boxing + map lookups but
    **not** the `shared_ptr` refcount or any memory traffic. It becomes the full floor once the
    ctor allocates.
  - **Rung 2b (`run()`) measures nothing today** — an empty-map construction and early return, not
    the plumbing §6's table describes. Real rung-2b numbers appear only once `run()`/`validate()`
    are implemented.

---

## 10. Deferred / later [LATER]

- **Allocation-count guard** (§5.4) — deterministic missed-copy detector; its own instrumentation
  project.
- **Cross-run significance / dashboard** — Bencher.dev if the solo err%-threshold ever proves
  insufficient.
- **Thread-scaling** benches — when the pipeline threads (Block 4+).
- **Halide A/B mechanics** — the JIT/AOT compile cost must sit *outside* the timed region
  (warm up first), and Halide `Buffer` ↔ `vc_pixel_buffer` interop needs a thin wrap. Design when
  Block 4 reaches it.
- **On-device (Pixel) benchmarking** — separate track, separate baselines; desktop MP/s does not
  predict device MP/s.
- **google/benchmark migration** — only at the §3.5 threshold.

---

## 11. Open questions [OPEN]

- **Input data:** synthetic seeded fill vs committed fixture images (`tests/data/`) for
  deterministic, comparable inputs. Likely synthetic for size-swept micro-benches, fixtures for
  representative-pipeline macros — confirm when the first bench is written.
- **CI integration:** advisory-only vs a hard regression gate, given shared-runner noise (§8).
- **`-march` value:** pinned generic vs `native` — `native` is faster but its numbers are not
  portable across machines (already mitigated by host-keyed baselines, but pin the choice).

---

## 12. Provenance

Decision and methodology synthesized from a study of prior art (2026-07): OpenCV's `ts` /
`opencv_perf_*` module (`PERF_TEST`, `TEST_CYCLE`, median-of-N, `SANITY_CHECK`); darktable's
`-d perf` runtime instrumentation (total-pipe time is the reliable signal); google/benchmark
(`DoNotOptimize`, throughput counters, `compare.py` Mann–Whitney); libjpeg-turbo `tjbench` (MP/s
as the headline unit); Eigen BTL (size sweeps); and nanobench itself. Full comparison lives in the
session notes; this document is the settled decision.
