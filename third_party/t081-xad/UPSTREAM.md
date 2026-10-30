# T081 bundled RAR extractor sources

This directory vendors source-only snapshots used to build ep128emu-unar.

- XADMaster: https://github.com/MacPaw/XADMaster.git
  - commit: 7cb9ee0abbb163f261e4cb74501e15067032319c
  - license: LGPL-2.1-or-later (see XADMaster/LICENSE)
- UniversalDetector: https://github.com/MacPaw/universal-detector.git
  - commit: 4eb832d999628edcd3d134e46bd35357c8c99a85
  - license: see UniversalDetector/LICENSE

The EP128Emu custom build adds only local macOS ARM64 Makefiles. No upstream
binary is vendored. ep128emu-unar is always rebuilt from these sources.
XADMaster's bundled WavPack decoder sources are compiled directly into the
helper so no Homebrew or other third-party runtime library is required.

Local reproducibility patch:
- XADMaster/unar.m removes the compile-time __DATE__ text from the help banner.
  Runtime behavior and archive decoding are unchanged; this prevents a needless
  date-dependent binary difference between otherwise identical source builds.

Source-only hygiene:
- Upstream prebuilt Windows binary artifacts were intentionally removed from
  the vendored snapshot: Windows/Foundation.1.0.dll, Windows/lib/libbz2.a,
  and Windows/lib/libz.a. They are irrelevant to the macOS helper build.
