# Benchmark baseline: pipeline

| key | value |
|---|---|
| suite | pipeline |
| host | Vichitras-MacBook-Pro.local |
| compiler | AppleClang-17.0.0.17000013 |
| -march | native |
| flags | -O3 -DNDEBUG -march=native -std=c++20 |
| git | 510fe8b |
| generated | 2026-07-26T22:47:15Z |

> Machine-specific — never compare across host/compiler/flags. Regenerate (and stamp the new git SHA) when the bench CODE changes, not every commit. See docs/benchmarking.md Sec 8.

| relative |               ns/op |                op/s |    err% |     total | passthrough_pipeline
|---------:|--------------------:|--------------------:|--------:|----------:|:---------------------
|   100.0% |               35.96 |       27,809,790.02 |    0.7% |      2.38 | `rung2a stage.process()`
|    11.6% |              309.71 |        3,228,852.95 |    0.7% |      2.43 | `passthrough pipeline.run()`

