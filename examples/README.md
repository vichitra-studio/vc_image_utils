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
