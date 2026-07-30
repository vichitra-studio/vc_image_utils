# Benchmark baseline: pipe

| key | value |
|---|---|
| suite | pipe |
| host | Vichitras-MacBook-Pro.local |
| compiler | AppleClang-17.0.0.17000013 |
| -march | native |
| flags | -O3 -DNDEBUG -march=native -std=c++20 |
| git | 510fe8b |
| generated | 2026-07-26T22:47:09Z |

> Machine-specific — never compare across host/compiler/flags. Regenerate (and stamp the new git SHA) when the bench CODE changes, not every commit. See docs/benchmarking.md Sec 8.

| relative |               ns/op |                op/s |    err% |     total | passthrough
|---------:|--------------------:|--------------------:|--------:|----------:|:------------
|   100.0% |                3.19 |      313,194,838.27 |    1.3% |      0.22 | `rung1 raw shallow copy`
|     8.9% |               35.88 |       27,868,036.65 |    0.5% |      0.24 | `rung2a stage.process()`

