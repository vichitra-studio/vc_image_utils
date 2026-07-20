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
