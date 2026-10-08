# Examples

Small, self-contained programs, each demonstrating one idea, in reading order.

Every example:

- is a single `.cpp` with a `main()` — no framework, no hidden setup, readable
  top to bottom
- prints what it is doing, and writes any images it produces into
  `build/examples/examples_out/` so you can open them
- **asserts its own results** and returns non-zero on failure, so `ctest` runs
  them and a regression fails a build rather than merely printing something
  nobody reads

They do not replace `tests/`. Unit tests and edge cases live there. These exist
to be *read* — by me while working through the curriculum, and by anyone
wanting to see how the library is actually used.

## Building

```sh
cmake --preset examples
cmake --build --preset examples
ctest --preset examples
```

Examples are **off by default** (`VC_BUILD_EXAMPLES=OFF`), so an ordinary build
and test run are unaffected.

## Adding one

1. Write `examples/NN_topic.cpp` with a `main()` that returns non-zero on
   failure.
2. Add `NN_topic` to `VC_EXAMPLE_TARGETS` in the top-level `CMakeLists.txt`.
3. Add a row to the index below.

## Index

| # | File | Shows |
|---|------|-------|
| 00 | `00_load_and_dump.cpp` | Loading an image, the `(x, y, ch)` index map, writing a PNG |
| 01 | `01_pixel_ops.cpp` | Per-pixel maps in colour space: grayscale, channel gains, invert, brightness. Linearity as a runnable assertion; why brightness is reversible only in float |
| 02 | `02_patch_distance.cpp` | SSD / SAD / L2 between patches — the same difference vector under three norms. The primitive block-matching and NLM are built from |
| 03 | `03_warp.cpp` | Affine warp by inverse mapping — transforming the *domain* rather than the range. Homogeneous coordinates, bilinear resampling, edge policy, and why downscaling aliases |
| 04 | `04_numderiv.cpp` | The derivative a computer can actually compute: forward differences, why the error stops improving as the step shrinks, and an image gradient as a subtraction of neighbours |
| 05 | `05_convolve.cpp` | Convolution, and the three conventions the formula leaves undeclared — where the kernel's origin sits, which way the offsets point, and what lies outside the image. Each fails silently: a shifted picture, a mirrored response, a darkened border |
| 06 | `06_dft.cpp` | The 1-D DFT and its inverse. The sign convention pinned by `[1,2,3,4] → X[1] = −2+2i`, because a symmetric input cannot see it. Conjugate symmetry, the shifted-impulse phase ramp, Parseval, and a 1024-point round trip that is the only check able to see the double accumulator |
| 07 | `07_dft2d.cpp` | The 2-D transform as a row pass then a column pass. Non-square **4×2 and 2×4 checked against each other**, so a transposed pass cannot hide; odd 5×3; and `grid2d`, dimensions as a type you cannot construct wrongly — written after `dft2d` first shipped with no validation at all |
| 08 | `08_spectrum_viz.cpp` | Turning a spectrum into something you can open. `fftshift`, `log(1+\|F\|)`, phase via `atan2`. Dumps the linear version **beside** the log one, because that comparison is the whole argument for the log and settles it faster than any explanation |
| 09 | `09_spectrum_atlas.cpp` | Sixteen known patterns and their spectra, as a training set — 48 PNGs with the expected reading printed so you can check yourself before looking. Start at 02 and 03: two visibly different pictures with **pixel-identical** spectra |
| 10 | `10_defect_atlas.cpp` | What artefacts look like: aliasing with and without a pre-blur, periodic row banding, motion blur, clipping, posterisation. Every signature measured in the written PNG rather than quoted — which caught two wrong predictions |
