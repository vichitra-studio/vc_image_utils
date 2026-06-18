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

_None linked yet._

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
