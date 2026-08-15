# LittleCMS-CIECAM16

An independent fork of [Little CMS](https://www.littlecms.com) 2.19 that adds the
**CIECAM16 color appearance model (CIE 248:2022)** and the **CAT16 chromatic
adaptation transform** on top of lcms2. It is a drop-in, API- and ABI-compatible
superset: everything upstream does, plus CIECAM16.

Maintainer: xizhi <xizhi284@gmail.com>

## What you get on top of lcms2

```c
// CIECAM16 appearance model (mirrors the CIECAM02 API shape)
cmsHANDLE cmsCIECAM16Init(cmsContext ContextID, const cmsViewingConditions* pVC);
void      cmsCIECAM16Done(cmsHANDLE hModel);
void      cmsCIECAM16Forward(cmsHANDLE hModel, const cmsCIEXYZ* pIn, cmsJCh* pOut);
void      cmsCIECAM16Reverse(cmsHANDLE hModel, const cmsJCh* pIn, cmsCIEXYZ* pOut);
void      cmsCIECAM16ForwardEx(cmsHANDLE hModel, const cmsCIEXYZ* pIn,
                               cmsCIECAM16Appearance* pOut);   // Q, M, s, H too
void      cmsCIECAM16ReverseEx(cmsHANDLE hModel, const cmsCIECAM16Appearance* pIn,
                               cmsCIEXYZ* pOut);

// CAT16 chromatic adaptation
cmsBool   cmsCAT16(...);
cmsBool   cmsCAT16Ex(...);   // with per-end degree-of-adaptation override
```

Everything else — every `cms*` function, structure, profile feature, and the
`lcms2_plugin.h` plug-in API — is identical to upstream.

## Coexists with upstream lcms2

The CMake build installs under a fully independent identity with **zero file
overlap** with upstream lcms2, so both can live in the same prefix:

| | upstream lcms2 | this fork |
|---|---|---|
| CMake package | `lcms2` | `lcms2_ciecam16` |
| CMake targets | `lcms2::lcms2` | `lcms2_ciecam16::lcms2_ciecam16` (+ `_static`) |
| Library | `liblcms2` | `liblcms2_ciecam16` |
| Headers | `include/lcms2.h` | `include/lcms2_ciecam16/lcms2.h` |
| pkg-config | `lcms2.pc` | `lcms2_ciecam16.pc` |
| Tools | `transicc`, `tificc`, ... | `transicc-c16`, `tificc-c16`, ... |
| Plugins | `liblcms2_fast_float`, ... | `liblcms2_ciecam16_fast_float`, ... |

One rule: **do not link both libraries into the same program** — they export
the same `cms*` symbols. Pick one per binary.

## Building

CMake is the supported (and only renamed) build path:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /your/prefix
```

Useful options (all `ON` unless noted): `LCMS2_BUILD_SHARED`,
`LCMS2_BUILD_STATIC`, `LCMS2_BUILD_TOOLS`, `LCMS2_BUILD_TESTS`,
`LCMS2_WITH_FASTFLOAT` (OFF, GPL-3.0), `LCMS2_WITH_THREADED_PLUGIN` (OFF, GPL-3.0).

> Windows/MSYS2 note: pass your MinGW compiler explicitly, e.g.
> `-DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe`, otherwise CMake may pick
> up an unrelated clang from PATH.

The autotools, meson and MSVC project files differ from upstream only by the
minimal addition of `cmscam16.c` (and the matching `.def` exports), to minimize
merge friction. They still produce upstream-named `lcms2` artifacts — use CMake
for anything you ship. This means their installed layout is upstream's own
(bare `include/` headers, `lcms2.pc`, `liblcms2`) and intentionally so: only
the CMake build carries the independent `lcms2_ciecam16` identity.

## Using it from your project

Switching an existing lcms2 consumer is a one-field rename; source code
(`#include <lcms2.h>`, all API calls) is unchanged.

**CMake**

```cmake
find_package(lcms2_ciecam16 REQUIRED)   # was: find_package(lcms2 ...)
target_link_libraries(myapp PRIVATE lcms2_ciecam16::lcms2_ciecam16)
```

**pkg-config**

```sh
pkg-config --cflags --libs lcms2_ciecam16
```

**Manual**

```sh
cc main.c -I<prefix>/include/lcms2_ciecam16 -L<prefix>/lib -llcms2_ciecam16
```

## Tools

The classic command-line tools are built and installed with a `-c16` suffix
so they never shadow upstream's: `transicc-c16`, `linkicc-c16`, `psicc-c16`,
`jpgicc-c16`, `tificc-c16`, `tifdiff-c16` (man pages likewise, e.g.
`man transicc-c16`). They are byte-for-byte the same sources as upstream's,
just linked against this fork's library.

## Conformance

Little CMS is a **FULL IMPLEMENTATION** of ICC specification 4.4. It fully
supports all kinds of V2 and V4 profiles, including abstract, devicelink and
named color profiles. The CIECAM16 implementation follows CIE 248:2022 and is
validated against the worked examples of the standard plus independent
third-party implementations.

### Please see the complete documentation in the doc folder

## License

Same as upstream: MIT license for the library. The optional plug-ins
(`fast_float`, `threaded`, disabled by default) are GPL-3.0. Little CMS is
Copyright (c) Marti Maria Saguer; CIECAM16 additions by this fork's authors.

## Contact

Questions, bug reports and patches: **xizhi284@gmail.com**
