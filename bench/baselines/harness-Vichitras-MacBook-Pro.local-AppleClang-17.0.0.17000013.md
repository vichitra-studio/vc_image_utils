# Benchmark baseline: harness

| key | value |
|---|---|
| suite | harness |
| host | Vichitras-MacBook-Pro.local |
| compiler | AppleClang-17.0.0.17000013 |
| -march | native |
| flags | -O3 -DNDEBUG -march=native -std=c++20 |
| git | 510fe8b |
| generated | 2026-07-26T22:47:06Z |

> Machine-specific — never compare across host/compiler/flags. Regenerate (and stamp the new git SHA) when the bench CODE changes, not every commit. See docs/benchmarking.md Sec 8.

| relative |            ns/pixel |             pixel/s |    err% |     total | alloc_fill
|---------:|--------------------:|--------------------:|--------:|----------:|:-----------
|   100.0% |                0.52 |    1,914,325,725.85 |    0.5% |      0.37 | `alloc+fill+sum f32`
|   612.8% |                0.09 |   11,731,875,260.67 |    2.7% |      0.24 | `alloc+fill+sum u8`

| relative |               ns/op |                op/s |    err% |     total | copy
|---------:|--------------------:|--------------------:|--------:|----------:|:-----
|   100.0% |                3.23 |      309,571,859.28 |    0.2% |      2.44 | `shallow copy (shared_ptr refcount)`
|     0.0% |          308,928.46 |            3,237.00 |    1.4% |      2.45 | `deep copy (buffer value)`

| relative |            ns/pixel |             pixel/s |    err% |     total | as_access
|---------:|--------------------:|--------------------:|--------:|----------:|:----------
|   100.0% |                0.50 |    2,016,054,290.66 |    1.2% |      0.35 | `as<T> hoisted once`
|    75.6% |                0.66 |    1,523,662,565.61 |    0.4% |      0.45 | `as<T> per element`

| relative |               ns/op |                op/s |    err% |     total | packet
|---------:|--------------------:|--------------------:|--------:|----------:|:-------
|   100.0% |               12.97 |       77,127,492.23 |    0.4% |      0.24 | `box+unbox vc_image handle`
| 1,433.3% |                0.90 |    1,105,496,444.74 |    0.7% |      0.24 | `box+unbox double scalar`

