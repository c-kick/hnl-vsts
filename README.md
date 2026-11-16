# hnl-vsts

Audio plugins by HNL Design (effects and an instrument), built as VST3 for Windows (tested in Reason 12.5+).

| Plugin | What it is |
| --- | --- |
| [HNL Master](plugins/hnl-master) | Mastering chain: multiband compressor, equaliser, exciter (tape, tube, retro), stereo widener, reverb, true-peak limiter, loudness meters, presets |
| [HNL Tape Master](plugins/hnl-tape-master) | Two-track mastering tape machine: saturation, tape compression, head response by speed, formula and head width, wow, flutter, hiss |
| [HNL Tape Echo](plugins/hnl-tape-echo) | Tape echo with three playback heads and a spring reverb: saturating repeats, pitch bends, wow and flutter, self-oscillation, tempo sync |
| [HNL Digital Piano](plugins/hnl-digital-piano) | Instrument: digital pianos of the 1980s and 1990s in stereo (workstation piano, FM electric piano), with doubling, reverb and an on-screen keyboard |

![HNL Master](plugins/hnl-master/docs/in-use.png)

## Layout

- `plugins/<name>/`: one folder per plugin. Audio engine in `dsp/` (header-only C++17, no dependencies), the DPF
  plugin and its NanoVG UI next to it, and offline tests in `tests/`.
- `common/dsp/`: building blocks shared by the plugins (filters, oversampling, true-peak detection, a small reverb).
- `common/ui/`: UI parts shared by the plugins (preset bar, help tips); `common/Presets.hpp`: how presets are
  defined.
- `dpf/`: the [DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF), as a git submodule.
- `toolchain/`: wrappers that run zig as the Windows cross-compiler.
- `tools/ui-preview/`: a stand-in for DPF's drawing API that writes a plugin UI as SVG, to check a layout on a
  machine without Windows or OpenGL.

## Building

Needs `make`, a host `g++` for the tests, and [zig](https://ziglang.org) for the Windows build (path in
`toolchain/zwrap`, override with `ZIG=`).

```sh
git submodule update --init --recursive
./test.sh               # offline tests, native
./build.sh              # dist/<Plugin>.vst3 for 64-bit Windows
./build.sh hnl-master   # one plugin
```

`plugins/<name>/tests/preview.sh` writes the UI as SVG files into `build/`; `tools/ui-preview/shot.mjs` turns them
into PNG with a Playwright Chromium.

Install by copying the `<Plugin>.vst3` folder from `dist/` to `C:\Program Files\Common Files\VST3\`.

## Adding a plugin

Create `plugins/<name>/` with a `Makefile` that includes `../../dpf/Makefile.plugins.mk` (copy the one from
`hnl-master`), a `DistrhoPluginInfo.h` with its own `DISTRHO_PLUGIN_UNIQUE_ID`, and `tests/run.sh`. `build.sh` and
`test.sh` pick it up by folder name.

## Versions

Each plugin has its own version (`getVersion()` in its plugin source) and is tagged `<name>/v<version>`.
