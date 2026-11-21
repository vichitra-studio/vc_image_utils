# Third-Party Licenses

This project links third-party libraries, each wrapped behind a thin interface
so it can be swapped. License obligations differ between the two distributed
artifacts:

- **Desktop reference library (AGPL-3.0):** GPL-compatible dependencies are
  acceptable here (e.g. FFTW, exiv2).
- **Closed / paid app:** must remain **permissive-clean** — **no GPL
  dependencies** (swap FFTW → PFFFT/pocketfft, exiv2 → libexif/TinyEXIF).

Populate the table below as each dependency is actually linked (from P4 onward).
Many permissive licenses (BSD/MIT/Apache/MPL) require their copyright notice to
be reproduced on distribution — paste the required notice text in the entry.

## Dependencies

### doctest v2.5.2
- License: MIT
- Used in: tests only (not distributed)
- Link type: header-only
- Notice required on distribution: no (test-only, not shipped)
- Source: https://github.com/doctest/doctest

      The MIT License (MIT)
      Copyright (c) 2016-2023 Viktor Kirilov

      Permission is hereby granted, free of charge, to any person obtaining a copy
      of this software and associated documentation files (the "Software"), to deal
      in the Software without restriction, including without limitation the rights
      to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
      copies of the Software, and to permit persons to whom the Software is
      furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be included in
      all copies or substantial portions of the Software.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
      THE SOFTWARE.

### stb_image v2.30
- License: Public Domain (Unlicense) / MIT — dual; we elect public domain
- Used in: desktop library, app
- Link type: header-only (implementation compiled via `src/io/vc_io_stb.cpp`)
- Notice required on distribution: no under public domain election; MIT fallback text
  retained below for jurisdictions that do not recognise public-domain dedication
- Source: https://github.com/nothings/stb
- Pinned commit: `013ac3beddff3dbffafd5177e7972067cd2b5083` (2024-05-31)

      MIT License (Alternative A — fallback for non-public-domain jurisdictions)
      Copyright (c) 2017 Sean Barrett

      Permission is hereby granted, free of charge, to any person obtaining a copy of
      this software and associated documentation files (the "Software"), to deal in
      the Software without restriction, including without limitation the rights to
      use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
      of the Software, and to permit persons to whom the Software is furnished to do
      so, subject to the following conditions:
      The above copyright notice and this permission notice shall be included in all
      copies or substantial portions of the Software.
      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
      SOFTWARE.

### stb_image_write v1.16
- License: Public Domain (Unlicense) / MIT — dual; we elect public domain
- Used in: desktop library, app
- Link type: header-only (implementation compiled via `src/io/vc_io_stb.cpp`)
- Notice required on distribution: no under public domain election; MIT fallback text
  retained below for jurisdictions that do not recognise public-domain dedication
- Source: https://github.com/nothings/stb
- Pinned commit: `1ee679ca2ef753a528db5ba6801e1067b40481b8` (2021-07-12)

      MIT License (Alternative A — fallback for non-public-domain jurisdictions)
      Copyright (c) 2017 Sean Barrett

      Permission is hereby granted, free of charge, to any person obtaining a copy of
      this software and associated documentation files (the "Software"), to deal in
      the Software without restriction, including without limitation the rights to
      use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
      of the Software, and to permit persons to whom the Software is furnished to do
      so, subject to the following conditions:
      The above copyright notice and this permission notice shall be included in all
      copies or substantial portions of the Software.
      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
      SOFTWARE.

### nanobench v4.3.11
- License: MIT
- Used in: benchmarks only (not distributed — built behind `VC_BUILD_BENCHMARKS`, off by default)
- Link type: header-only (implementation compiled via `bench/vc_bench_impl.cpp`)
- Notice required on distribution: no (bench-only, not shipped)
- Source: https://github.com/martinus/nanobench
- Pinned tag: `v4.3.11`

      The MIT License (MIT)
      Copyright (c) 2019-2023 Martin Leitner-Ankerl <martin.ankerl@gmail.com>

      Permission is hereby granted, free of charge, to any person obtaining a copy
      of this software and associated documentation files (the "Software"), to deal
      in the Software without restriction, including without limitation the rights
      to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
      copies of the Software, and to permit persons to whom the Software is
      furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be included in all
      copies or substantial portions of the Software.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
      THE SOFTWARE.

### nlohmann/json v3.11.3
- License: MIT
- Used in: desktop library, app (the edit-document JSON codec, Kind A — docs/edit_model.md Sec 4.4)
- Link type: header-only (single header `third_party/nlohmann/json.hpp`, reached via the
  blanket `third_party` SYSTEM PRIVATE include on the library targets — the same wiring as stb)
- Notice required on distribution: yes (MIT — reproduce the copyright + permission notice)
- Source: https://github.com/nlohmann/json
- Pinned tag: `v3.11.3`

      MIT License
      Copyright (c) 2013-2023 Niels Lohmann <https://nlohmann.me>

      Permission is hereby granted, free of charge, to any person obtaining a copy
      of this software and associated documentation files (the "Software"), to deal
      in the Software without restriction, including without limitation the rights
      to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
      copies of the Software, and to permit persons to whom the Software is
      furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be included in all
      copies or substantial portions of the Software.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
      THE SOFTWARE.

### Eigen 5.0.1
- License: **MPL-2.0** primary, plus **Apache-2.0** and **BSD-3-Clause** for three files (below)
- Used in: **tests and examples only — NOT linked into the distributed library**
- Link type: header-only (no compiled TU; `vc_eigen` is an INTERFACE include path)
- Notice required on distribution: **not yet** — nothing shipped contains Eigen code. This
  becomes **yes** the moment Eigen is linked into `vc_image_utils_lib` or the app; see
  "If this ever ships" below.
- Source: https://gitlab.com/libeigen/eigen
- **Pinned at tag `5.0.1` = commit `bc3b39870ecb690a623a3f49149a358b95c5781d` (2025-11-08).**
  Vendored tree verified byte-identical to that checkout (`diff -r`, and the committed blobs
  re-checked via `git archive` rather than the working copy).

**WHY THE VERSION IS LOAD-BEARING, AND NOT A DETAIL.** Eigen's **3.4 line ships LGPL-licensed
files** (`COPYING.LGPL` and `COPYING.GPL` are both present in 3.4.1) and documents a
preprocessor symbol, `EIGEN_MPL2_ONLY`, that makes including one a compile error. **Eigen 5.0
removed all LGPL code outright (upstream MR !1197; see `CHANGELOG.md`), and removed the symbol
with it.** Defining `EIGEN_MPL2_ONLY` here would therefore do *nothing* — it would read as
enforcement while enforcing nothing — so it is deliberately **not** set.

Consequence worth stating plainly: **the distro package is the risky path and vendoring is the
safe one**, because system packages track the 3.4 line. Do not replace this vendored copy with
a `find_package(Eigen3)` without redoing the licence check.

The two files that carried LGPL in 3.x are present here and **both read MPL2 in their own
headers**: `Eigen/src/SparseCholesky/SimplicialCholesky.h`, and
`Eigen/src/OrderingMethods/Amd.h`, whose header records that CSparse's author Timothy A. Davis
executed a licence with Google LLC permitting MPL2 distribution.

**ENFORCEMENT.** Since the flag is gone, the pin is asserted at compile time in
`tests/test_third_party_eigen.cpp`. Note that the obvious check is wrong: `EIGEN_WORLD_VERSION`
is **3 in both 5.0.1 and 3.4.1** — upstream calls it a permanent name for "the Eigen3 library",
not a version — so the assertion uses `EIGEN_MAJOR/MINOR/PATCH_VERSION` (semver as of 5.0.0)
and `EIGEN_VERSION_STRING`.

**WHAT WAS EXCLUDED, AND THE ONE THAT MATTERS.** Only `Eigen/`, `LICENSE` and the `COPYING.*`
files are vendored. **`bench/btl/COPYING` is GNU GPL v2** — the Benchmark Template Library and
its GMM++/blaze/blitz comparison interfaces. It is not a header and would never be compiled
into anything shipped, but copying the whole upstream tarball would place GPL-licensed files
inside this repository, against this document's own rule that the closed artifact stays
permissive-clean. Also excluded: `blas/`, `lapack/`, `test/`, `demos/`, `doc/`, `failtest/`,
`ci/`, `cmake/`, `debug/`, `scripts/`, and `unsupported/`. `COPYING.MINPACK` covers
`unsupported/`'s Levenberg-Marquardt only, and is vendored for completeness rather than need.

**RE-RUNNING THE CHECK WHEN THE PIN MOVES.** This replaces `EIGEN_MPL2_ONLY`, which no longer
exists. It measures the real include closure rather than trusting four file headers:

      # from the repo root, with <tu.cpp> including <Eigen/Eigenvalues>, <Eigen/SVD>, <Eigen/QR>
      c++ -std=c++20 -I third_party/eigen -M tu.cpp | tr ' \\' '\n\n' \
        | grep '^third_party/eigen' | sort -u > closure.txt
      grep -il 'LGPL\|GNU General Public\|GNU Lesser' $(cat closure.txt)   # must print nothing
      grep -Li 'mozilla' $(cat closure.txt)                                # review each hit

  **Corrected 2026-11-20 (third review pass): the closure is 204 DISTINCT FILES, not 188.**
  The recipe above prints **217 path strings**, and `sort -u` dedupes *strings*, not *files* —
  clang's `-M` reaches some headers by more than one spelling (`src/Eigenvalues/./…` and
  `src/Eigenvalues/././…` are the same file), so resolve the paths before counting:

      while read -r f; do python3 -c "import os,sys;print(os.path.realpath(sys.argv[1]))" "$f"; \
        done < closure.txt | sort -u | wc -l        # 204

  **The load-bearing claim is re-verified and holds: ZERO files in that closure mention LGPL,
  GNU Lesser, or GNU General Public.** That is the conclusion this vendoring rests on, and it
  was re-run against the 204 distinct files, not the old count.

  Note `grep -i mozilla` on one line only — Eigen wraps its notice so that "Mozilla" and
  "Public License" fall on separate lines, and a `grep 'Mozilla Public'` returns every file as a
  false positive.

  **What the `grep -Li mozilla` review actually found**, recorded in full so the next audit does
  not redo it: **19** of the 204 carry no Mozilla notice. Exactly **two** of those carry a
  licence notice of their own — `BFloat16.h` (Apache-2.0) and `MKL_support.h` (BSD-3-Clause),
  both tabled below. The remaining **17 carry no copyright or licence text at all**: the
  per-directory `InternalHeaderCheck.h` files, the `plugins/*.inc` fragments,
  `DisableStupidWarnings.h`, `ReenableStupidWarnings.h`, `Eigen/Version`, and
  `arch/NEON/GeneralBlockPanelKernel.h`. Un-noticed files in an MPL-2.0 distribution take the
  project's own licence; they are listed here because an earlier version of this section implied
  the review had found only three hits, which was an undercount of the hits rather than of the
  licences.

**THE THREE NON-MPL2 FILES IN THAT CLOSURE.** All permissive; their full texts are in the
vendored `COPYING.APACHE` and `COPYING.BSD`.

| File | License | Copyright |
|---|---|---|
| `Eigen/src/Core/arch/Default/BFloat16.h` | Apache-2.0 | Copyright 2017 The TensorFlow Authors. All Rights Reserved. |
| `Eigen/src/Core/util/MKL_support.h` | BSD-3-Clause | Copyright (c) 2011, Intel Corporation. All rights reserved. |
| `Eigen/src/Geometry/AlignedBox.h` — **`AlignedBox::transform()` only**, rest is MPL2 | BSD-3-Clause | Copyright (c) 2011-2014, Willow Garage, Inc.; Copyright (c) 2014-2015, Open Source Robotics Foundation |

*Not* licence hits, recorded so the next audit does not re-investigate them:
`Eigen/src/Core/util/Memory.h` and `Eigen/src/Core/util/Macros.h` mention BSD only as an
**operating system** (`EIGEN_OS_BSD`, FreeBSD malloc alignment).

**IF THIS EVER SHIPS** — i.e. if Eigen is linked into the library or the app, which P11b's DLT
solve could prompt — three things become due, none of them onerous, and **none of which
threaten a paid licence**, because MPL-2.0 is file-level copyleft and does not reach this
project's own sources:
1. Reproduce the MPL-2.0 notice and make the Eigen source available (pointing at the pinned
   tag above satisfies this).
2. Reproduce the Apache-2.0 and BSD-3 notices in the table above.
3. If any vendored Eigen *file* is modified, that file stays MPL-2.0. Prefer wrapping over
   patching, per this project's thin-interface rule.

<!-- Template — copy one block per dependency:

### <name> <version>
- License: <SPDX id>
- Used in: [ ] desktop library   [ ] app
- Link type: static / dynamic / header-only
- Notice required on distribution: yes / no
- Source: <url>
- Required notice text:

      <paste the copyright/license notice the dependency requires>

-->
