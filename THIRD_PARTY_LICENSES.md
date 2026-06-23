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
