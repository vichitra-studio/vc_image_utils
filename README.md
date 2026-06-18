# vc_image_utils

## License

Licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
See [LICENSE](LICENSE) for the full text.

Under the AGPL-3.0, any work based on or derived from this code must also be
licensed under the AGPL-3.0 when you distribute it; and if you run a modified
version as a network service (SaaS), you must make the complete corresponding
source available to that service's users. To use this code under different
terms — for example, in closed-source or proprietary products — a commercial
license is available.

**Commercial licensing:** studio@vichitracollective.com

## Dependencies

Third-party C++ dependencies (e.g. LibRaw, Eigen, FFTW, Halide, Catch2) are
managed **project-locally** via a pinned manifest (Conan 2 / vcpkg) — no
system-wide installs — and are wrapped behind thin interfaces so any one can be
swapped. The build configuration is added once the C++ project is scaffolded.
License obligations and the closed-app permissive-clean split are tracked in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions require agreement to the
[Contributor License Agreement](CLA.md).

---

© 2026 Shantanu Agarwal · Vichitra Collective
