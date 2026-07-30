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
  `vc_benchmark_harness`/`vc_benchmark_pipe`/`vc_benchmark_pipeline` behind `VC_BUILD_BENCHMARKS`).
  **All three suites produce real numbers and are baselined** (`bench/baselines/`): the substrate
  suite (buffer alloc/copy, `as<T>()`, packet box/unbox), the `pipe` suite's `passthrough` case
  (rung 1 + rung 2a on a real buffer), and the `pipeline` suite's `passthrough_pipeline` case
  (rung 2b through a real `run()`). ⚠️ **This bullet was stale and has been corrected in stages —
  see the §9 banners (2026-07-26, and two more dated 2026-07-27):** the `vc_image` ctor,
  `vc_pipeline::run()`/`validate()`, and `vc::io` are all implemented; the kernels now live in
  `tests/samples/` and are written. What still blocks a real *kernel* number (as opposed to the
  plumbing numbers above) is that the bench targets link the *library*, which no longer contains
  any kernel to measure. See §9.

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
floor (§6); the instrument for catching missed copies and design regressions. *(⚠️ corrected
2026-07-27: `run()` IS implemented and `passthrough` runs on a real buffer, so this tier measures
what it claims. The review is done: both the `pipe` `passthrough` case and the `pipeline`
`passthrough_pipeline` case are baseline-eligible and baselined — see §9.)*

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

**Tier 3 — milestone-driven:** codec I/O throughput (`vc::io` — implemented since
before this doc's 2026-07-27 correction pass, per §9, but not yet benchmarked
here); Eigen
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

**Status caveat (see §9):** ⚠️ *corrected 2026-07-27* — `run()` and the `vc_image` ctor are both
implemented, so rung 2b executes for real and rung 2a runs on a real buffer. Both rungs are now
baselined for the `passthrough` no-op stage. The ladder is still not measurable end-to-end for a
*kernel*, because the only stage the library exposes is `vc_passthrough_stage`; the real kernels
moved to `tests/samples/`, which the bench targets do not link. The table's "adds over previous"
column describes the *expected* overhead contributors; the bench confirms the actual attribution
for the plumbing layers — a kernel row awaits a production stage.

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

`<suite>` (`harness`/`pipe`/`pipeline` — one per bench binary, §7) is in the key because there is
one binary per category (§7) and three binaries would otherwise clobber one host-keyed file.
**Numbers are machine-specific** → baselines are **keyed by suite + host + compiler + flags**;
never compare across machines. nanobench's own `json()` carries per-result config but **no**
host/CPU/compiler identity, so the harness prepends that `meta` block itself (host, compiler,
`-march`, flags, git SHA, UTC time). **[CORRECTED 2026-07-27: `<suite>` used to read
`substrate`/`micro`/`macro` here, an earlier working name for the same three categories that was
never the actual `<suite>` string passed to `run_suite()`. Verified against the committed files in
`bench/baselines/` and the `run_suite(argc, argv, "…", …)` call in each `vc_benchmark_*.cpp`.]**

**`<host>` is not a stable key.** It comes from `cmake_host_system_information(... QUERY
HOSTNAME)`, which on macOS resolves to the mDNS/Bonjour local hostname — this changes silently on
a machine rename or some network/System Settings changes, with no code change and no warning. If
it changes, the next `--baseline` run writes a **new** `<suite>-<newhost>-<compiler>.{json,md}`
pair instead of continuing the existing one, quietly forking the timeseries and breaking the "git
history = poor-man's time series" property this section relies on (the old file just stops
accumulating history and looks abandoned). If this happens: either rename the old baseline
file(s) to the new hostname to preserve continuity (only valid if you are confident it is the
*same* machine under a new name), or treat it as knowingly starting a new series and note the
split in the commit message. See also `bench/baselines/README.md`.

**"Never compare across machines" is reported and warned on, not enforced.** **[CORRECTED
2026-07-27: added — this file previously stated the "never compare across host/compiler/flags"
invariant without saying whether `--compare` actually checks it. It does not: `parse_baseline_file`
in `vc_bench_support.cpp` only ever reads `doc["benches"]`; `doc["meta"]` was write-only, so a
hand-edited or stale-arch baseline compared meaningless numbers and reported clean. `--compare` now
prints BOTH sides' provenance (host/compiler/`-march`/flags/git SHA/measured-at) in its header for
every **usable** baseline (a baseline file that fails to parse at all still exits 1 before any
provenance is printed, unchanged from before), and emits a named, prominent warning when
host/compiler/march/flags differ between
the baseline and the current run — but this is visibility, not a gate: the warning never changes
the exit code, and a missing or malformed `meta` block is handled gracefully (reported as
"provenance unknown", never a crash or a silent blank). Hard-failing on a mismatch was considered
and rejected: a CI-generated baseline's `meta.host` is the ephemeral runner VM hostname
(`fv-az###-###`), different on every run, and `meta.compiler` churns with runner image updates —
hard equality would make the committed CI compare job permanently red. Comparability across
host/compiler/flags remains the **operator's** responsibility; `--compare` only makes a mismatch
impossible to miss in the log.]**

**Reproducibility metadata** stamped on every result set: **git SHA** (of the bench *definition*),
date, CPU, compiler + version, build flags (`-O`, pinned `-march`), and power/quiet state.

**Baseline lifecycle [DECIDED]:** a baseline is valid only for a fixed *(bench definition + host +
compiler + flags)*. When the bench **code** changes, the old baseline is silently invalid —
**regenerate it and stamp the new git SHA** (distinct from "perf changed"). Update baselines
**deliberately** when a real change lands (with a commit message saying why), **not** every commit.

**When to run:** the routine set (§7) before a release and whenever a hot path is touched;
investigation sweeps on demand.

**Automated regression detection: `--compare <baseline.json>`.** Every bench binary supports a
third mode alongside the bare run (print) and `--baseline` (overwrite):
`./vc_benchmark_<suite> --compare bench/baselines/<suite>-<host>-<compiler>.json` runs the suite,
then reads that baseline file back and prints a per-row delta table. It is **read-only** — it
never writes `path`, the deliberate contrast with `--baseline`, which overwrites the committed
file in place with no diff-first step. `--baseline` and `--compare` **cannot be combined** in one
invocation (rejected at argument-parsing time, before either mode runs — see "Cannot combine
`--baseline` and `--compare`" below).

The exit code makes it usable as a CI gate, split into two distinct non-zero values so a caller can
tell "the comparison ran cleanly but found a real regression" (advisory-worthy — safe to soften
with `|| true` in a non-blocking CI step) apart from "the comparison itself could not be trusted"
(never worth softening): **0** iff every row present on **both** the baseline and this run was
actually comparable and none regressed past the threshold outside its noise band; **1** for an
**infrastructure failure** — something kept part of the comparison from happening at all; **2**
for a **pure regression** — the comparison ran cleanly but a real regression was found. If both
kinds of problem occur in the same run, **1 wins**: an infrastructure failure is the more serious
claim ("this verdict cannot be trusted at all") and is never silently downgraded to the softer,
advisory-shaped 2 just because a real regression was also visible among the rows that DID compare.

Exit **1** situations — several distinct ways "no comparison actually happened" for part of the
run, which must never look identical to "no regression happened":

- a missing/unreadable/malformed baseline **file** (a hard configuration error, not a silent
  skip) or when no case matches an over-narrow filter;
- a row present on **both** the baseline and this run whose median is **missing, zero, or
  negative on either side** — reported as its own `NOT COMPARABLE` status, never as a `+0.00%`
  delta. (Previously, `.value("median(elapsed)", 0.0)` silently defaulted an absent key to `0.0`,
  and the delta formula's `b.median_s > 0.0` guard turned that — and any negative value — into a
  flat `+0.00% ok` for every such row, which meant a baseline that lost its `median(elapsed)`
  field, e.g. to a nanobench field rename, would pass the **entire suite** with exit 0 while
  comparing nothing at all.) **[CORRECTED 2026-07-27: this bullet previously described the check
  as one-sided ("a row present in the baseline with a missing/zero/negative median"). The
  underlying check is symmetric — `!b.median_ok || !c.median_ok` in `run_compare`
  (`bench/vc_bench_support.cpp`) — a row is `NOT COMPARABLE` if its median is untrustworthy on
  EITHER the baseline or the current-run side.]**
- a row with a field **present but of the wrong JSON type** (a hand-edited or version-skewed
  baseline) — nlohmann's `.value()` throws `json::type_error` on a type mismatch (only an ABSENT
  key gets the default), and that exception is now always caught: the offending row is skipped
  with a clear message naming the row and field, and the run's exit code is forced non-zero.
  **Nothing in `--compare` ever aborts** — every failure mode here is a `std::cerr` message plus
  a non-zero `return`, never an unhandled exception (previously, a type-mismatched field could
  `SIGABRT` the whole process with `libc++abi: ... type_error`, exit 134).
- A `"benches"` array element that is **not even a JSON object**, a case object **missing its
  `"results"` field**, one whose `"results"` is present but **not an array**, a `"results"`
  array element that is **not a JSON object**, or a `"results"` array element that **is** a JSON
  object but carries none of nanobench's row fields (`"title"`/`"name"`/`"median(elapsed)"`/
  `"medianAbsolutePercentError(elapsed)"`). **[CORRECTED 2026-07-27: added a fifth shape — a
  `"results"` element that IS a JSON object but has none of nanobench's four row fields, which
  `extract_row`'s later `.value()`-based extraction would otherwise absorb silently (every field
  defaulting: title/name to `""`, `median(elapsed)` absent triggering its own `NOT COMPARABLE`
  status, `medianAbsolutePercentError(elapsed)` to `0.0`), producing a row keyed `" :: "` with no
  diagnostic printed for that element specifically. Landed after the other four shapes, in a
  later commit; see
  the dedicated comment in `extract_row` (`bench/vc_bench_support.cpp`) for why detection requires
  ALL FOUR fields to be absent, not just one — a row missing only `median(elapsed)` is already the
  separate, already-diagnosed `NOT COMPARABLE` case just below.]** **[CORRECTED 2026-07-27: added — this is the
  EIGHTH confirmed instance of this repo's dominant defect class ("exits 0 while silently
  skipping its job") in this codebase, and the one that had escaped every fix above it. The
  pre-fix guard in `parse_case_results` was one unconditional early return —
  `if (!case_obj.is_object() || !case_obj.contains("results") || !case_obj["results"].is_array())
  { return rows; }` — contributing zero rows with NO diagnostic for any of those three shapes;
  `extract_row`'s own `if (!r.is_object()) return;` for a `"results"` array element had the
  identical shape one level deeper, and was explicitly called out in its own comment as a
  "pre-existing precedent" the wrong-JSON-type fix above deliberately aligned with rather than
  fixing. Reproduced directly: a `"benches"` array with 4 non-object elements (a bare string,
  number, `null`, and array) mixed among 2 valid case objects printed "2 compared, 6 new,
  0 missing" and exited **0** — nothing distinguished the 4 garbage elements from an
  intentionally-trimmed baseline, because nothing named them. Now every element that cannot
  contribute a row is reported by position and JSON type, and `had_row_error` is set exactly as
  it already was for BUG 2's wrong-JSON-type row — reusing that mechanism rather than adding a
  parallel one — forcing the run's exit code non-zero.**  A well-formed case object with a
  genuinely **empty** `"results"` array is deliberately NOT in this bullet: that is exactly the
  shape nanobench's own `json()` template (which emits `"results": [` unconditionally, only the
  per-row section is conditional) produces for a case whose `Bench` completed zero `.run()` calls
  before `.render()` — a real, honestly-empty case, not malformed input. It is still reported
  (informationally, so this is never a *silent* zero-row contribution either), but does not set
  `had_row_error` and does not affect the exit code; its zero rows are accounted for at the
  array level by the zero-extracted-rows paragraph below, same as before. Checked against all
  three committed baselines in `bench/baselines/`: none contain any of the malformed shapes, and
  none contain a case with an empty `"results"` array, so this does not turn the primary local/CI
  workflow red.]**
- Zero rows anywhere reaching an actual delta comparison, in a **whole-suite (unfiltered)
  invocation**. **[CORRECTED 2026-07-27: this bullet documents a fix landed on this date, not
  long-standing behavior — before it, a syntactically clean run could exit 0 having compared
  literally nothing (e.g. a baseline that parsed but yielded zero rows, or total key drift between
  the baseline and this run from a rename that leaves every key on only one side), because every
  row landed in the "new"/"missing" informational branches below, none of which set a failure flag
  on their own.]** Gated on **no positional `FILTER`** deliberately: running
  `--compare` narrowed to inspect one brand-new, not-yet-baselined case is a designed interactive
  workflow (see the "new (not in baseline)" case below) and legitimately compares zero rows every
  time — that must stay exit 0. No CI workflow currently invokes these binaries at all (see
  above), so nothing in CI passes a filter either — gating on "whole-suite" closes the
  CI-relevant hole without breaking the interactive one. **[CORRECTED 2026-07-27:
  `.github/workflows/benchmark.yml` now exists and its `benchmark` job's three `--compare`
  steps invoke each binary with no positional filter (a whole-suite run), so this gate is now
  live for CI, not just a closed hole in a hypothetical. See the `[CORRECTED 2026-07-27: ...]`
  note on the "no CI workflow currently invokes it" passage further down (§8, "Automated
  regression detection") for what the workflow actually does and does not verify yet.]**
- At least one **`missing`** row — present in the baseline, absent from this run — in a
  **whole-suite (unfiltered) invocation**. **[CORRECTED 2026-07-27: added — this bullet did not
  exist before this date; a "missing" row was purely informational at every filter level, which
  meant a rename-heavy refactor could drop most of a suite's rows out of the comparison (every
  renamed case becomes "missing" on the baseline side and "new" on the current side) and the run
  still printed a clean pass. Reproduced directly: renaming 7 of 8 rows in a baseline made a
  whole-suite `--compare` print 7 `missing` + 7 `new` and compare only 1 row, yet the summary line
  read "every comparable row was actually compared and none regressed" — a claim that was false of
  what actually happened.]** A `missing` row means baseline **coverage was lost** — the case was
  renamed, removed, or otherwise stopped running — which is a materially different claim from a
  `new` row (a case that simply did not exist yet, benign by construction). Gated on
  **no positional `FILTER`**, for the identical reason the zero-rows bullet above is: a `--compare`
  narrowed to inspect one case legitimately leaves every OTHER baseline row `missing`, every time,
  and that must stay exit 0.
- A **duplicate row key** — two or more rows whose `(title, name)` pair is identical — on
  **either side** (the baseline file, or this run's own case results), at **any** filter level
  (unlike the two bullets above, this one is not gated on whole-suite). **[CORRECTED 2026-07-27:
  "identical `(title, name)` pair" replaces an earlier "same `title :: name` string" description —
  the actual match/dedup key (`match_key()` in `bench/vc_bench_support.cpp`) is a length-prefixed
  encoding of the `(title, name)` pair, not the unescaped `" :: "`-joined display string
  (`row_key()`, used only for the printed "case" column and diagnostic text). Two structurally
  different pairs can render to the identical display string — e.g. `title="X"`, `name="Y :: Z"`
  vs. `title="X :: Y"`, `name="Z"` both print as `"X :: Y :: Z"` — and those are correctly treated
  as distinct, non-colliding rows; only an identical `(title, name)` pair triggers this.]**
  **[CORRECTED 2026-07-27: added — this
  bullet did not exist before this date. `baseline_by_key` was filled with an unconditional
  `.emplace(key, row)` on a `std::unordered_map`, whose documented behavior is a no-op once a key
  is already present — so only the FIRST row for a repeated key ever survived — while
  `baseline_order` (an unconditional `push_back`, never deduplicated) still drove the comparison
  loop once per duplicate. The result: the delta table printed and counted the SAME surviving row
  multiple times, and every duplicate beyond the first had its asserted value silently discarded —
  not `missing`, not `NOT COMPARABLE`, not caught by the row-parse-error handling above, landing in
  no bucket at all. Reproduced directly: a baseline with two rows keyed
  `alloc_fill :: alloc+fill+sum f32`, the first matching the real run and the second asserting a
  median ~30,000% away, printed the first row's (matching) delta twice and exited **0** with
  "compare OK" — the massive second assertion never appeared anywhere. A second reproduction (three
  identical copies of one row) showed the summary's own compared-row count inflated to 3 for what
  was really one distinct row's value, undercutting the FINDING-1 promise just above that these
  counts are literally true of what happened.]** Now treated as malformed input, in the same class
  as a row with an unusable median or a wrong-typed field: `--compare` names the offending key and
  how many times it appeared, and the ordering vector used to drive the table is deduplicated so a
  repeated key is printed and counted only once (for its first occurrence) rather than once per
  duplicate. Checked against all three committed baselines in `bench/baselines/`: none contain a
  duplicate key today, so this does not turn the primary local/CI workflow red. Not reachable from
  an ordinary suite definition either (each `bench_case`'s own name, and each `.run()` row name
  inside its body, is conventionally distinct) — but nothing in the parsing enforces that as an
  invariant, so a hand-edited baseline or a copy-paste bug in a suite's case list could still
  produce one, and the tool must not silently paper over it. Applied **symmetrically** to this
  run's own rows, not just the baseline's — see `bench_row`'s header comment above for why both
  sides are always validated the same way rather than trusting one because "it's our own output".

By contrast, a **`new`** row (in this run, not the baseline) is **always** purely informational and
never fails the run, at any filter level — nothing was ever asserted about a case that simply did
not exist yet, unlike the corrupt-row case above where the baseline (or this run) explicitly
asserted a row and that assertion turned out to be unusable. A **`missing`** row (in the baseline,
not this run) is informational only for a **filtered** invocation (a narrower `--list` filter is
exactly what makes every other baseline case read as `missing`, by design); for a **whole-suite**
invocation it is now fatal (exit 1 — see the bullet above). **[CORRECTED 2026-07-27: this paragraph
previously treated `new` and `missing` identically ("a row present on only one side is purely
informational and never by itself fails the run"). That symmetry was itself the hole Finding 2
closed — see the `missing`-row bullet above.]** Likewise, a baseline that parses but has **zero
extractable rows** (an empty `"benches"` array, or one whose elements are all well-formed case
objects with a legitimately empty `"results"` array) is reported loudly; whether it fails the run
now depends on the same whole-suite-vs-filtered distinction above — for a whole-suite run, nothing
in the baseline means nothing can reach a real comparison, which the zero-compared-rows check above
turns into exit 1 rather than a silently clean run. **[CORRECTED 2026-07-27: this paragraph
previously also credited "elements that are all non-objects/have no usable `results`" as landing
here, non-fatally. That was the bug the bullet above this section documents: a `"benches"` array
element that is not a well-formed case shape at all is no longer folded into this silent,
whole-array-only accounting — each such element is now individually reported via `had_row_error`
at the point it is parsed (which forces a non-zero exit on its own), not only visible here as an
unexplained drop in the total row count. This paragraph now covers only the genuinely-empty case,
which is the one shape that was, and remains, correctly non-fatal.]** Every summary line
`--compare` prints — the OK line and every
FAILED line — now names how many rows were actually **compared**, how many were **new**, and how
many were **missing**, so the OK line in particular can no longer read as a clean pass while most
of a suite silently fell out of the comparison. **[CORRECTED 2026-07-27: added — the OK line
previously read "every comparable row was actually compared and none regressed past N%"
unconditionally, which stayed technically defensible only because "comparable" was doing silent
work: a row that never got the chance to be compared (new/missing) was excluded from what counted
as "comparable" without saying so.]**

The default threshold is **15% median slowdown**, overridable with `--threshold <percent>`. It is
set from measurement, not theory: six clean, unmodified-code runs of the `pipeline` suite's
`rung2a stage.process()` row measured a **+8.85% top-to-bottom cross-run spread** on this machine
(macOS has no core pinning, §4.3) — a 10% threshold would leave almost no margin above that and
would false-alarm on ordinary noise. 15% sits clearly above the worst spread actually observed.

A delta is only flagged once it *also* exceeds the two measurements' combined error band —
`combined_err_abs`, the SUM of each side's absolute error **in seconds** (`median_s * err_frac`
per side), summed conservatively with no independence assumed; this is not a sum of percentages.
**This is not the cross-run noise defense** — nanobench's `medianAbsolutePercentError` is a WITHIN-run statistic
(how much one run's own epochs disagreed with each other); it structurally cannot see the
across-run drift the 8.85% figure above measures. The err%-band check exists only to suppress a
marginal threshold-crossing when the underlying measurement (this run's or the baseline's) was
itself visibly untrustworthy — `--threshold` alone is what stands between the suite and cross-run
noise.

**The err%-band is capped, not unlimited.** Without a ceiling, `combined_err_abs` (baseline
err_abs + current err_abs, no cap) can suppress an *arbitrarily large* regression as long as both
sides happen to report a wide enough err% — reproduced directly: a baseline row with a 150% err%
suppressed a real +105.16% delta as `ok (within combined err% band)`. Once either side's own err%
exceeds **10%** (`kMaxTrustworthyErrFrac` in `vc_bench_support.cpp`), that measurement is no longer
trusted enough to *license suppression*: the band is not applied and the row falls through to the
ordinary threshold-based `REGRESSION`/`IMPROVEMENT` decision, annotated
`[err% measurement untrustworthy > 10% — band not applied]`. This ceiling is a fixed, **absolute**
figure — deliberately *not* expressed as a multiple of `--threshold` — because err% measures
within-run measurement quality (a property of the run itself), while `--threshold` measures how
much slowdown a human cares about; tying the two together would make the sanity check tighten or
loosen for reasons that have nothing to do with measurement quality (e.g. a stricter `--threshold`
on a CI runner would, as a side effect, start flagging ordinary noisy-but-honest rows as
"untrustworthy"). Verified not to over-correct: a legitimate case at the boundary — a 10% baseline
err%, 0.4-0.7% current err%, ~10.4-10.7% combined band — still suppresses correctly *below* the
band and still flags correctly *above* it (a +18.66%/+17.51% delta clears the ~10.5% band and is
flagged, as it always was); the cap only ever changes behavior once err% itself is implausibly
high. The one committed baseline row whose err% tops 2% (`alloc_fill :: alloc+fill+sum u8`, 2.70%)
is nowhere near the 10% ceiling, so no committed baseline is affected.

**[CORRECTED 2026-07-27: added — the worked example just above reasons in summed err%
*percentages* ("10% + 0.4-0.7% ≈ ~10.4-10.7% combined band", "18.66% > 10.4%"), a few lines after
this section's own authoritative definition of `combined_err_abs` as a sum of each side's absolute
error **in seconds**, "not a sum of percentages" (above). The two are not in conflict, but sit close
enough to read as one: summing percentages is only an *approximation* of summing absolute seconds,
and it holds *because* both rows' medians are comparable in magnitude in that example (so a
percentage-of-median and an absolute-seconds error track each other closely). It is not the
definition — `combined_err_abs = median_s_baseline * err_frac_baseline + median_s_current *
err_frac_current`, per the code and the paragraph above, is. Do not generalize the percentage-sum
shortcut to a pair of rows with dissimilar medians; reach for the absolute-seconds formula instead.]**

**Measured err% distribution [CORRECTED 2026-07-27: added — nobody had previously characterized
where real err% values sit relative to the 10% ceiling].** 121 benchmark runs were collected on
this dev box to sanity-check the ceiling against reality: `harness` N=50, `pipe` N=50, `pipeline`
N=20, plus one pre-formal exploratory `harness` run taken moments after the build finished
(excluded from the formal-window figures below and reported separately). **The machine was not
quiet per §4.3's own standard** while these were collected — WindowServer, mail-sync helpers, a VM
process, several `claude` processes, a browser, and a chat client were all running throughout (load
average ~1.9-3.8 on a 14-core box) — so this is "normally loaded dev box" data, not clean-room data;
if anything that makes the finding below slightly stronger, since it held under non-ideal
conditions, but it should not be read as a quiet-machine result.
- Max single-side err% (nanobench's `medianAbsolutePercentError`) across the 120 formal-window
  runs: **5.40%** (`pipe`, `rung1`) — 54% of the ceiling's value.
- The pre-formal exploratory run hit **8.1%** (`alloc_fill :: u8`), with nanobench's own
  "Unstable" warning (~39.3 iters) — 81% of the ceiling's value. Kept out of the formal-window
  statistics above rather than averaged in, since it may be build-contamination noise rather than
  steady-state variance; it is still one of the 121 runs and is the single closest approach to the
  ceiling across all of them.
- Highest err% among the *committed* baselines: **2.70%** (`alloc_fill :: alloc+fill+sum u8`,
  verified directly against `bench/baselines/harness-*.json`'s
  `medianAbsolutePercentError(elapsed)` field).
- **The 10% ceiling was never crossed in any of the 121 runs.**

*Methodology note:* the capture script's row-parsing regex initially used `:?\w*\s*` to skip
nanobench's instability marker ahead of the backtick-quoted row name. That pattern only tolerates
one leading colon plus word characters; it does not match nanobench's actual marker,
`` :wavy_dash: `` (colon–word–colon–space), so an unfixed version silently dropped the whole row
from the parse rather than merely losing the marker — no error, just absence. Caught by matching
the pattern against the actual captured line before trusting it, and fixed (`[^`]*` in place of
`:?\w*\s*`). Impact: exactly 2 of the 121 captured runs carried that marker — the pre-formal
`alloc_fill :: u8` row (8.1%) and the formal-window `pipe`/`rung1` row (5.40%) — i.e. an unfixed
script would have silently dropped precisely the two highest-err% rows in the dataset, the two
this section leans on hardest.

*What this does and does not establish:* the false alarm this ceiling guards against needs BOTH a
side's `err_frac > kMaxTrustworthyErrFrac` AND `|delta_pct| >= --threshold` on the same row. These
121 runs measured only the first conjunct, and it never came close to the ceiling — so the honest
claim is that the per-side ceiling was never approached closer than 81% of its value across all 121
runs (54% if the flagged-Unstable exploratory run is excluded), not that the compound condition or
the false-alarm path itself was exercised end-to-end. The compound condition has not been
reproduced.

The false-alarm vector this ceiling defends against is also asymmetric in its consequences: when
the err%-band is not applied (`exceeds_err_band` forced true), the row falls through to the plain
threshold decision, and only `delta_pct > 0.0` sets `any_regression` (see `run_compare` in
`vc_bench_support.cpp`) — a large speedup instead becomes `IMPROVEMENT (verify — …)`, which is
informational and does not fail the run. So an untrustworthy-err% false alarm can only manifest as
a spurious `REGRESSION`, never as a spurious failing `IMPROVEMENT`.

A large **improvement** (delta beyond `-threshold`, outside the err% band) is flagged
informationally (`IMPROVEMENT (verify — …)`) but never fails the run: an implausible speedup more
often means the benchmark stopped measuring the real work (dead-code elimination, a silently
narrowed workload) than a genuine optimization, and there is no automatic way to tell those apart
from here.

**[CORRECTED 2026-07-27: added — two observations of the same row swinging under real machine load,
on top of the 121-run err% survey above.]** While verifying an unrelated fix on this same dev box,
with several other agents building and running processes concurrently in the same session (well
outside §4.3's "quiet machine" standard), the `harness` suite's `alloc_fill :: alloc+fill+sum u8`
row swung sharply twice: (1) **same binary, same commit (`145754d`), 15 seconds apart** — a
`--baseline` run immediately followed by a `--compare` against the file it had just written showed
that row at -40.76% (`IMPROVEMENT`, informational, does not fail the run — see above); (2) a
separate whole-suite `harness --compare` against the *older* committed baseline (git `510fe8b`, six
commits behind this run's `145754d` — verified via `git diff --stat 510fe8b..145754d -- src/
include/`: empty, so no benchmarked *kernel* code changed in that span, only bench-harness/docs/CI
work; the "current" side's binary also carried this same fix's own uncommitted
`vc_bench_support.{cpp,h}` changes, which touch only the `--compare` machinery, not anything a
benchmark case measures) showed the same row at +70.68%, past the 15% threshold and outside its
err% band (`REGRESSION`, exit 2) — and passed cleanly on an immediate re-run with no change in
between. Taken
together these are **two observations on a heavily loaded box, not a characterized rate** — neither
is averaged into, or compared against, the 121-run survey above, which was collected under lighter
(if still imperfectly quiet) conditions. What they establish directly: the 15% threshold **can**
false-alarm under real contention, not just in principle — including between two runs of the
identical binary against a baseline it had just written itself. It is direct evidence for treating
the CI regression gate (`exit 2`, "pure regression") as **advisory** — worth a human look, not an
auto-block — exactly as
its two-way exit-code split (§8, "Automated regression detection") already assumes.

**Cannot combine `--baseline` and `--compare` in one invocation.** `--baseline` **writes**
`<baseline-dir>/<suite>-<host>-<compiler>.json` (`<baseline-dir>` defaults to the committed
`bench/baselines/`, or `--baseline-dir`'s override — see the sharp-edge passage below) before
`--compare` would **read** its target, so passing both risks — or, when the paths coincide,
*guarantees* — comparing a freshly-written run
against itself: every row `+0.00% ok`, exit 0, on every invocation, forever, regardless of what
actually regressed. Realistic trigger: a CI script that passes both flags "to do both at once", or
a copy-paste/typo. The combination is rejected **unconditionally** at argument-parsing time (before
either mode runs) — not only when the two paths textually match, since path-string equality cannot
reliably detect this (relative vs. absolute paths, symlinks, and `--baseline`'s write path is
computed from suite+host+compiler, which the caller may not even know in advance). There is no
legitimate use for combining them in one invocation: updating the baseline *and* checking a run
against a separate, already-committed snapshot is just two ordinary invocations
(`--baseline`, then `--compare <archived.json>`), which is unambiguous and costs nothing.

**The manual route still exists and is still valid** for anything `--compare` doesn't cover — a
by-hand spot check, or a comparison `--compare` was never meant to key across (different
suite/host/compiler/flags). To do it: re-run with `--baseline`, then `git diff` the resulting
`.json`/`.md` against what is committed, and read the medians yourself against err%, flagging a
change **only when both runs have low err%** (otherwise it is noise). **Sharp edge, unchanged:**
because `--baseline` overwrites the committed file in place, a contributor who runs it and `git
add`s the result — without a passing `--compare` or a diff first — will silently promote a
regression to baseline; the overwrite itself still does not fail or warn. **Practical sensitivity
floor for the manual route:** measured cross-run spread on unmodified code is on the order of ~3%
for the epoch-tuned `passthrough_pipeline` case (§9's note) but was measured as high as 8.85% for
the `rung2a` row above under the same tuning — treat "roughly 5-6%" as an optimistic floor, not a
guarantee, and prefer `--compare`'s wider default threshold when in doubt. No cross-run
significance test beyond the threshold/err%-band mechanism above (§3.4); if a dashboard is ever
wanted, a service such as **Bencher.dev** can ingest benchmark JSON — verify its current
nanobench-adapter support before adopting. **[OPEN]:** whether/when to wire `--compare` into an
actual CI gate — shared CI runners are noisy, so CI perf numbers may need to stay advisory rather
than a hard gate; `--compare`'s exit code makes a gate *possible*, it does not mean one exists —
no CI workflow currently invokes it. **[CORRECTED 2026-07-27: resolved, partially.
`.github/workflows/benchmark.yml`'s `benchmark` job now defines three `--compare` steps, one per
binary, on every push/pull_request — though today only the first (`harness`) actually executes
before the job stops (see below for why). Exit-code handling is split to match the noise concern
raised here: exit 1 (an infrastructure failure — missing/malformed baseline, an uncomparable row,
or zero rows compared) BLOCKS the job; exit 2 (a clean comparison that found a real regression) is logged
as a `::warning::` and does NOT fail the job. That split exists precisely because this passage is
right that CI-runner noise is a real risk — the 15% default threshold and the 8.85% cross-run
figure it's built from (see "Automated regression detection" above) were measured only on the
maintainer's macOS box, never on a GitHub-hosted Linux runner, so a pure-regression verdict is not
(yet) trustworthy enough to hard-block on. What is NOT resolved: there is no CI baseline yet.
`bench/baselines/ci/` does not exist in this tree; a second job, `benchmark-bootstrap`
(`workflow_dispatch` only), generates one via `--baseline-dir bench/baselines/ci`, but that job has
never been run, so each `--compare` step in `benchmark` would fail closed with exit 1 ("could not
open file") if it ran — in practice only the FIRST such step (`harness`) actually runs, since
Actions stops the job on that failure and the `pipe`/`pipeline` compare steps never execute at all
until a human runs the bootstrap job, renames its artifact's output (the filename stem is
`host_compiler_key()`, not `<suite>-linux`, so a rename step is unavoidable — see the workflow's
own comments), and commits the result. Until then this gate is wired but inert —
every push/pull_request run of the `benchmark` job is expected to fail on the `--compare` steps,
by design, not as a bug.]**

**Known sharp edge — no scratch location for `--baseline`:** **[CORRECTED 2026-07-27: this
heading originally asserted there is no scratch location for `--baseline`; that is no longer
true — `--baseline-dir` (below) now provides one. The underlying clobber hazard the heading warns
about is still real and unchanged; only the "no scratch location" gap is closed. See below for
exactly what changed.]**
`VC_BENCH_BASELINE_DIR` is still baked at CMake **configure** time to
`${CMAKE_CURRENT_SOURCE_DIR}/bench/baselines` **by default** — that much is unchanged. What changed:
`--baseline` now accepts a **`--baseline-dir <path>`** CLI override (rejected as a hard error if
given without `--baseline`, since `--compare` already takes a full file path and has no use for a
directory). `--baseline-dir <path> --baseline` writes to `<path>` instead of the committed
directory, creating `<path>` (`mkdir -p`) if it does not already exist; the compiled-in default is
completely unaffected when `--baseline-dir` is omitted — plain `--baseline` still writes straight
into `bench/baselines/` exactly as before. A separate flag rather than `--baseline <path>`: the
existing `--baseline` takes no argument and `[FILTER ...]` is positional, so `--baseline /tmp/x`
would be genuinely ambiguous between "the write directory" and "a filter substring that happens to
look like a path" — a "does it look like a path" heuristic was rejected as this codebase's dominant
defect class (silently doing the wrong thing while still exiting 0); a separate flag has no such
ambiguity. **The underlying hazard this sharp edge warns about is unchanged and still real:**
running bare `--baseline` (no `--baseline-dir`) against this checkout still overwrites the committed
baseline files with no diff-first step — this happened for real once, during `--compare`'s own
development, and had to be reverted with `git checkout`. Always pass `--baseline-dir` pointed at a
scratch location (e.g. somewhere under `/tmp`) when experimenting; the old workaround (build and
experiment in a full separate copy of the tree — a worktree or clone, since a separate build
directory alone does not help, as the CMake **source** dir is what gets baked in) still works too,
but is no longer the only option.

**[CORRECTED 2026-07-27: this passage previously stated there is no `--check`/`--compare` mode and
that regression checking is a fully manual procedure. That was accurate when written but was made
false by a `--compare` implementation landing the same day on a branch developed in parallel — see
above for what it actually does. The manual `git diff` route described above is still valid and is
still the only way to compare across machines/suites/thresholds `--compare` doesn't key across;
there is still no CI gate wired up to either mode.]**

**[CORRECTED 2026-07-27 (second pass, same date) — the previous correction is now itself stale.**
`.github/workflows/benchmark.yml` wires `--compare` into CI as of this pass (see the `[CORRECTED
2026-07-27: resolved, partially. ...]` note further up this section for the exact mechanics: exit 1
blocks, exit 2 is advisory-only). `--baseline` remains unwired to CI by design — see this file's
"Cannot combine `--baseline` and `--compare`" passage and the `benchmark-bootstrap` job's own
comments for why a baseline-writing job must stay `workflow_dispatch`-only and never run on
push/pull_request. **This has NOT been validated on GitHub Actions.** The branch this workflow was
authored on was deliberately kept unpushed, so nothing here has executed on a real runner or on
Linux at all. What WAS verified locally (macOS, Apple Silicon — see §4.3 for why that is not the
target platform this workflow builds for): the `cmake -S -B -G Ninja -DCMAKE_BUILD_TYPE=Release
-DVC_BUILD_BENCHMARKS=ON` configure/build shape, the three binary targets building and their smoke
runs passing, and the compare-step exit-code `case` logic (0/1/2/unexpected) end-to-end against the
real `vc_benchmark_pipe` binary using hand-written baseline JSON. What was **not** verified, even
locally: the exact `-DVC_BENCH_MARCH=x86-64-v2` flag the workflow passes — AppleClang on this
arm64 host rejects it outright (`error: unsupported argument 'x86-64-v2' to option '-march='`,
since that flag names an x86_64 psABI microarchitecture level, meaningless on arm64). The
locally-verified build substituted `-DVC_BENCH_MARCH=native` instead; the x86-64-v2-specific
command in this workflow is unverified on any host, and nobody has built this project Release with
benchmarks on Linux/GCC at all — see BUILDING.md, which states the project has otherwise only been
built with Apple Clang. Do not read "the workflow file exists" as "the workflow has been proven to
work."]**

**Layout:**

```
third_party/nanobench/nanobench.h           # vendored, v4.3.11 (subdir, like doctest/stb)
bench/
  vc_bench_support.{h,cpp}                   # argv name-filter, house config, JSON+MD baseline render
  vc_bench_impl.cpp                          # the ONE ANKERL_NANOBENCH_IMPLEMENT TU
  vc_benchmark_harness.cpp                   # Tier-1 substrate (real numbers, baselined)
  vc_benchmark_pipe.cpp                      # rung 1 + rung 2a (real; baselined since 2026-07-27, §9)
  vc_benchmark_pipeline.cpp                  # rung 2b — pipeline.run() (real; baselined since 2026-07-27, §9)
  baselines/<suite>-<host>-<compiler>.{json,md}   # committed reference results
docs/benchmarking.md                        # this file
```

*This layout is **now in the tree** — built behind the `VC_BUILD_BENCHMARKS` CMake option
(off by default; Release-only; FATAL if combined with a sanitizer build). Each category is its own
executable with an `argv` substring filter, `--list`, `--baseline`, and `--compare` (§7). Baseline files are
written only for baseline-eligible cases (§9).*

---

## 9. Current status — what is real today [NOW]

> ### ⚠️ CORRECTED 2026-07-26 — read this before the text below
>
> The section below was written 2026-07 and is **substantially out of date**; it is kept for
> history. Verified state as of 2026-07-26, by building and running the suite:
>
> - **`vc_image` construction works.** `vc_image_info::element_count()`, the private ctor, and
>   `vc_image_writer`/`seal()` are all implemented; `zeros()`/`with_fill()` allocate real buffers.
> - **`vc_pipeline::run()` and `validate()` are implemented**, including the between-stage
>   cancellation check.
> - **`vc::io` read/write is implemented** — a JPEG-in/PNG-out round-trip test passes.
> - **The kernels are written**, but they **moved out of the library** to `tests/samples/`
>   (`vc_sample_grayscale_stage`, `vc_sample_mean_brightness_stage`, `vc_sample_blur_stage`) and
>   are compiled only into the test binary. The `grayscale`/`mean_brightness` cases were therefore
>   **deleted** from `bench/vc_benchmark_pipe.cpp`, which now holds only `passthrough`.
> - Still genuinely unimplemented: `build_pipeline`, `render_image`, `export_image`, the two
>   edit-table stores' `get`/`set`, and `src/main.cpp`'s round-trip.
>
> **Net effect on benchmarking:** the blocker is no longer "kernels are stubs" but "the library
> exposes no kernel to benchmark". Measuring one again means either a production stage with a real
> kernel, or letting a bench target link `tests/samples/`.

> ### ⚠️ CORRECTED 2026-07-27 — supersedes the "still unimplemented" list in the 2026-07-26 block above
>
> Commit `eaa13b3` ("Implement build_pipeline and render_image spine") landed in the same batch
> that produced the 2026-07-26 correction above, but this doc was never updated to reflect it.
> Verified 2026-07-27 by reading the sources directly:
>
> - **`build_pipeline()` and `render_image()` are now implemented**
>   (`src/edit/vc_build_pipeline.cpp`, `src/edit/vc_render_image.cpp`). `build_pipeline` wires a
>   single `vc::pipe::vc_passthrough_stage` into a `vc_pipeline`, feeds it `session.source()`, and
>   calls `validate()`; `render_image` calls `run()` on that graph and unwraps the single open
>   output, throwing if there is not exactly one.
> - **Still genuinely unimplemented today:** `export_image` (`src/edit/vc_export.cpp` —
>   `TODO(you)`, throws `"export_image not yet implemented"`) and the two edit-table stores'
>   `get`/`set` (`src/edit/vc_edit_table.cpp` — `vc_cached_edits_table::get`/`set` and
>   `vc_persistent_edits_table::get`/`set`, all four still `TODO(you)` and throw).
> - **`src/main.cpp`'s round-trip is also still unimplemented**, re-verified now: its `main()` body
>   is a `TODO(you)` block that prints `"TODO: not yet implemented"` to `stderr` and returns
>   `EXIT_FAILURE` without touching `vc::io` at all. This is a fourth stub outside the three
>   `TODO(you)` bodies this learning build treats as sanctioned red specs (`export_image`, and the
>   two edit-table stores' `get`/`set`) — it carries no test coverage, so it does not show up in
>   the suite's pass/fail count, but it is not implemented.
> - **Net effect on benchmarking is unchanged, for a different reason than before:**
>   `build_pipeline`/`render_image` only assemble and run a single passthrough stage — no
>   production kernel is reachable through them yet — so the library still "exposes no kernel to
>   benchmark," per the 2026-07-26 note above. That conclusion, and the rest of this section's
>   prose, does not depend on `build_pipeline`/`render_image` being stubs and needs no further
>   correction.
>
> ### ⚠️ CORRECTED 2026-07-27 (second pass, same date) — the gating rationale below was stale
>
> "No kernel to benchmark" (above) is still true and is a different claim from "nothing is
> baseline-eligible." The `pipe`/`passthrough` and `pipeline`/`passthrough_pipeline` cases don't
> measure a kernel — they measure the no-op `vc_passthrough_stage` through real `process()`/`run()`
> plumbing on a real buffer — and that plumbing is now genuinely implemented and stable enough to
> baseline. Both flipped to baseline-eligible today; see the inline `[CORRECTED 2026-07-27: ...]`
> annotations in the "Honest state"/"Consequences" lists below, which were the last remaining
> stale prose in this section.

Honest state (verified 2026-07): **every piece of real compute in the repo is a `TODO(you)`
stub.** **[CORRECTED 2026-07-27: false today — see the two banners above. Every claim in this
"Honest state" list and the "Consequences" list below is superseded; both lists are kept verbatim
as the historical record of what the doc originally said, with the true 2026-07-27 state marked
inline at each point it was wrong.]**

- `vc_image`'s constructor (`src/vc_image.cpp`) is unimplemented — it leaves `pixels_` null and
  dimensions 0. **[CORRECTED 2026-07-27: false — `vc_image_info::element_count()`, the private
  ctor, and `vc_image_writer`/`seal()` are all implemented (commit `5daa148`); `zeros()`/
  `with_fill()` allocate real buffers.]**
- All stage kernels (`vc_grayscale_stage`, `vc_mean_brightness_stage`) are stubs; `vc_passthrough`
  is implemented but is a no-op carry. **[CORRECTED 2026-07-27: `vc_grayscale_stage` and
  `vc_mean_brightness_stage` are neither current nor stubbed — they don't exist under those names
  any more. They were renamed `vc_sample_grayscale_stage`/`vc_sample_mean_brightness_stage`, fully
  implemented, and moved to `tests/samples/`, so they are no longer part of the library or of any
  bench target. `vc_passthrough` remains implemented, as stated.]**
- **`vc_pipeline::run()` and `validate()` are themselves stubs** (`src/pipe/vc_pipeline.cpp`):
  `run()`'s body is `return {}` — it does not walk stages, compute open ports, move packets, or
  call any `process()`; `validate()` does no contract/type check. The pipeline layer executes
  nothing yet. **[CORRECTED 2026-07-27: false — both are fully implemented. `run()` walks stages
  in insertion order, resolves open input/output ports, moves packets through each stage's
  `process()`, and honors fan-out via a remaining-reads count; `validate()` checks topology (no
  backwards edges, no self-loops, no duplicate input wiring) and connection type-compatibility.]**
- `vc::io` `read`/`write` (`src/io/vc_io_stb.cpp`) throw — not yet implemented. **[CORRECTED
  2026-07-27: false — both are implemented via vendored stb; a JPEG-in/PNG-out round-trip test
  passes.]**

Consequences:

- **The harness is built and runs today.** `bench/` is in the tree behind `VC_BUILD_BENCHMARKS`;
  the substrate suite already yields real, stable numbers (e.g. shallow-copy ≈ 3 ns vs deep-copy
  ≈ 325 µs; `vc_image` box+unbox ≈ 12 ns vs inline `double` ≈ 1 ns) — and is the **only** suite
  baseline-eligible today. The passthrough rung 1 → 2a delta already prices one framework layer but
  is a *partial* floor (null buffer, see below), so it runs+prints but is **not** committed yet.
  The kernel/stage/pipeline cases are wired to the real API and activate with **zero rework** as the
  stubs fill. **[CORRECTED 2026-07-27: `harness` is no longer the *only* baseline-eligible suite —
  `pipe`'s `passthrough` case and `pipeline`'s `passthrough_pipeline` case are both baseline-eligible
  and baselined as of this date (§8's baseline files). The passthrough rung 1 → 2a delta is now the
  **full** floor, not partial: `image` holds a real allocated buffer, so rung 1 prices a genuine
  `shared_ptr` refcount bump (≈ 3.15 ns), not a null-handle no-op.]**
- **The first real number is gated on the `vc_image` ctor + one kernel** (`grayscale::process`) —
  that is P1 implementation work, on the critical path regardless of benchmarking. **[CORRECTED
  2026-07-27: the `vc_image` ctor half of this landed (see above); "one kernel" no longer applies
  to `grayscale::process` specifically — that stage moved to `tests/samples/` and is not a library
  kernel the bench targets can link. The first *plumbing* numbers (not a kernel) are in; a real
  kernel number still awaits a production stage.]**
- The rung ladder (§6) is a **design target, not runnable end-to-end today**:
  - **Rung 2a (`process()`)** works now for `passthrough` (that stage is implemented), but is only
    a **partial** floor: with `pixels_` null it exercises `std::any` boxing + map lookups but
    **not** the `shared_ptr` refcount or any memory traffic. It becomes the full floor once the
    ctor allocates. **[CORRECTED 2026-07-27: the ctor allocates now — this is the full floor, and
    it is baselined.]**
  - **Rung 2b (`run()`) measures nothing today** — an empty-map construction and early return, not
    the plumbing §6's table describes. Real rung-2b numbers appear only once `run()`/`validate()`
    are implemented. **[CORRECTED 2026-07-27: `run()`/`validate()` are implemented (see above);
    rung 2b now measures the real plumbing this section describes, and is baselined. Getting a
    stable cross-run number required raising this case's `minEpochTime` to 200 ms — the default
    20 ms epoch shows low within-run err% but a reproducibly unstable cross-run median, driven by
    the per-iteration `unordered_map` input-sink allocation `run()`'s move-sink signature requires.
    The magnitude is not a fixed property of the case: independent re-runs at the default epoch
    have observed roughly 7-17% median spread depending on machine load; nanobench's own err% —
    which is a within-run statistic — does not surface this because the instability is an
    across-run effect. 200 ms converges the cross-run median to roughly 3% spread in observed
    runs. An earlier version of this note cited a single "~15%" figure as if it characterized the
    effect; that specific magnitude did not reproduce and has been replaced with the range above.]**

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
  **[CORRECTED 2026-07-27: decided, as a mix of both.** `.github/workflows/benchmark.yml`'s
  `benchmark` job treats an infrastructure failure (exit 1 — missing/malformed baseline, an
  uncomparable row, zero rows compared) as a **hard gate**, and a pure regression (exit 2) as
  **advisory-only** (a `::warning::`, non-blocking) — see §8's "Automated regression detection" for
  why: the 15%/8.85% figures behind the regression threshold were measured only on the
  maintainer's macOS box, never on a CI runner, so shared-runner noise is exactly the reason the
  regression half stays soft. **Still open:** whether that split is right once real CI-vs-CI
  baseline data exists — it has not been validated on an actual runner (the branch was kept
  unpushed) and no CI baseline has been generated yet, so this decision itself is unproven in
  practice, only reasoned about in advance.]**
- **`-march` value:** pinned generic vs `native` — `native` is faster but its numbers are not
  portable across machines (already mitigated by host-keyed baselines, but pin the choice).
  **[CORRECTED 2026-07-27: this is the general library-wide default (`VC_BENCH_MARCH` in
  `CMakeLists.txt`, still `native`) and remains genuinely open — unchanged by the note above.**
  `.github/workflows/benchmark.yml` separately pins `-march=x86-64-v2` for its own CI-specific
  reason (a baseline written in one job invocation must be comparable against a `--compare` run in
  a later one, which may land on a different physical runner), not as a resolution of this
  broader question about the tool's own default.]**

---

## 12. Provenance

Decision and methodology synthesized from a study of prior art (2026-07): OpenCV's `ts` /
`opencv_perf_*` module (`PERF_TEST`, `TEST_CYCLE`, median-of-N, `SANITY_CHECK`); darktable's
`-d perf` runtime instrumentation (total-pipe time is the reliable signal); google/benchmark
(`DoNotOptimize`, throughput counters, `compare.py` Mann–Whitney); libjpeg-turbo `tjbench` (MP/s
as the headline unit); Eigen BTL (size sweeps); and nanobench itself. Full comparison lives in the
session notes; this document is the settled decision.
