# Building the plugins on macOS

The plugins are released for 64-bit Windows. For macOS there are only experimental
[test builds](https://github.com/c-kick/hnl-vsts/releases/tag/macos-test). The code itself is not tied to Windows: the audio engines are plain C++17 and the plugin and UI
layer is [DPF](https://github.com/DISTRHO/DPF), which also builds on macOS. So you can compile them yourself on a
Mac.

**This has only been tried on a build server.** The test builds are made there in much the same way
(`.github/workflows/macos.yml`), but there is no Mac among the machines the plugins are developed and tested on, so
the steps below have not been run by hand. If a step fails, an issue with the compiler output is welcome.

## What you need

- A Mac with macOS 10.15 or later. Older systems may work, but lack parts of the C++17 library.
- The Xcode command line tools, for `clang`, `make` and `git`:

  ```sh
  xcode-select --install
  ```

zig, which the Windows build uses, is not needed.

## Get the source

```sh
git clone --recursive https://github.com/c-kick/hnl-vsts.git
cd hnl-vsts
```

If you cloned without `--recursive`, fetch DPF afterwards with `git submodule update --init --recursive`.

## Patch DPF

Once, before the first build:

```sh
patch -p1 -d dpf < patches/dpf-macos-focus.patch
```

Without it the plugins work, but a plugin window that has been clicked in keeps the keyboard: space no longer
starts the host until you click outside the plugin, and the first click in a plugin window that is not the active
one only makes it active. The patch changes four files of DPF; `git -C dpf checkout . && git -C dpf submodule foreach --recursive git checkout .` undoes it.

## Build

Do not use `./build.sh`: it cross-compiles for Windows. Call `make` in the folder of the plugin you want instead.
DPF sees that it runs on macOS and builds for the processor of your Mac.

```sh
make -C plugins/hnl-leveller            # VST3 and CLAP
make -C plugins/hnl-leveller TARGETS="vst3 clap au"   # also an Audio Unit, for Logic Pro and GarageBand
```

All of them at once:

```sh
for p in plugins/*/; do make -C "$p" TARGETS="vst3 clap au" || break; done
```

HNL Channel Spacer is the exception: leave `au` out for it. DPF's Audio Unit wrapper knows no sidechain input, so
its Audio Unit builds but does not work (Apple's `auval` rejects it). Its VST3 and CLAP builds have the sidechain.

The results are bundles in `dist/`: `HNL-Leveller.vst3`, `HNL-Leveller.clap` and `HNL-Leveller.component`.
`make -C plugins/<name> clean` removes them again.

## Install

Copy the bundles to the folders of your user account (create a folder if it is not there yet), then restart the
host or let it scan for plugins again:

| Format | Folder |
| --- | --- |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |
| CLAP | `~/Library/Audio/Plug-Ins/CLAP/` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components/` |

```sh
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/CLAP ~/Library/Audio/Plug-Ins/Components
cp -R dist/*.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R dist/*.clap ~/Library/Audio/Plug-Ins/CLAP/
cp -R dist/*.component ~/Library/Audio/Plug-Ins/Components/
```

A new Audio Unit is sometimes only found after the system's own check has run: `auval -a` lists what it knows,
and logging out and in again makes it look afresh.

## Security warnings

A plugin you compiled on your own Mac is not held back by Gatekeeper, because it was never downloaded. On a Mac
with Apple silicon every binary must carry a signature; the linker adds a local ("ad hoc") one by itself. If a
host still refuses a plugin, sign the bundle again yourself:

```sh
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/VST3/HNL-Leveller.vst3
```

A build that you pass on to someone else is another matter: their Mac will block it until they remove the
download mark (`xattr -dr com.apple.quarantine <bundle>`), or until it is signed and notarised with a paid Apple
developer account. That is the reason there are no ready-made Mac builds.

## What differs from Windows

- **Your own presets** are kept in `~/Library/Application Support/HNL/<plugin name>/Presets/`, not in `%APPDATA%`.
  The files are the same, so presets can be copied between a Mac and a Windows PC.
- **Songs** saved on one system should open on the other with the same settings: parameters and state are the
  same in every build.

## The tests

The offline tests are plain C++ and should run with the compiler of the command line tools. `./test.sh` uses the
`timeout` command, which macOS does not have; run a plugin's tests directly instead:

```sh
common/tests/run.sh
plugins/hnl-leveller/tests/run.sh
```
