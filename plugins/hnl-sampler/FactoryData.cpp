// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The sounds and instruments that come with the sampler, as the files under factory/.
// Written by tools/factory-sounds: do not edit, run tools/factory-sounds/make.sh.
//
// A compiler that knows #embed (GCC 15, Clang 19) takes the files in with that. An older one has the
// assembler do it (.incbin), which looks for the files from the folder the compiler is run in: this
// one, as the Makefile and tests/run.sh do. HNL_FACTORY_INCBIN asks for that way on any compiler.
#include "dsp/Factory.hpp"

#if defined(__has_embed) && !defined(HNL_FACTORY_INCBIN)

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif

namespace {

const unsigned char k0[] = {
#embed "factory/kit/kick_1a.flac"
};
const unsigned char k1[] = {
#embed "factory/kit/kick_1b.flac"
};
const unsigned char k2[] = {
#embed "factory/kit/kick_2a.flac"
};
const unsigned char k3[] = {
#embed "factory/kit/kick_2b.flac"
};
const unsigned char k4[] = {
#embed "factory/kit/kick_3a.flac"
};
const unsigned char k5[] = {
#embed "factory/kit/kick_3b.flac"
};
const unsigned char k6[] = {
#embed "factory/kit/stick_1.flac"
};
const unsigned char k7[] = {
#embed "factory/kit/stick_2.flac"
};
const unsigned char k8[] = {
#embed "factory/kit/snare_1a.flac"
};
const unsigned char k9[] = {
#embed "factory/kit/snare_1b.flac"
};
const unsigned char k10[] = {
#embed "factory/kit/snare_2a.flac"
};
const unsigned char k11[] = {
#embed "factory/kit/snare_2b.flac"
};
const unsigned char k12[] = {
#embed "factory/kit/snare_3a.flac"
};
const unsigned char k13[] = {
#embed "factory/kit/snare_3b.flac"
};
const unsigned char k14[] = {
#embed "factory/kit/rim_1.flac"
};
const unsigned char k15[] = {
#embed "factory/kit/rim_2.flac"
};
const unsigned char k16[] = {
#embed "factory/kit/tom_low_1.flac"
};
const unsigned char k17[] = {
#embed "factory/kit/tom_low_2.flac"
};
const unsigned char k18[] = {
#embed "factory/kit/tom_low_3.flac"
};
const unsigned char k19[] = {
#embed "factory/kit/tom_high_1.flac"
};
const unsigned char k20[] = {
#embed "factory/kit/tom_high_2.flac"
};
const unsigned char k21[] = {
#embed "factory/kit/tom_high_3.flac"
};
const unsigned char k22[] = {
#embed "factory/kit/hat_closed_1a.flac"
};
const unsigned char k23[] = {
#embed "factory/kit/hat_closed_1b.flac"
};
const unsigned char k24[] = {
#embed "factory/kit/hat_closed_2a.flac"
};
const unsigned char k25[] = {
#embed "factory/kit/hat_closed_2b.flac"
};
const unsigned char k26[] = {
#embed "factory/kit/hat_closed_3a.flac"
};
const unsigned char k27[] = {
#embed "factory/kit/hat_closed_3b.flac"
};
const unsigned char k28[] = {
#embed "factory/kit/hat_pedal_1.flac"
};
const unsigned char k29[] = {
#embed "factory/kit/hat_pedal_2.flac"
};
const unsigned char k30[] = {
#embed "factory/kit/hat_open_1.flac"
};
const unsigned char k31[] = {
#embed "factory/kit/hat_open_2.flac"
};
const unsigned char k32[] = {
#embed "factory/kit/crash_1.flac"
};
const unsigned char k33[] = {
#embed "factory/kit/crash_2.flac"
};
const unsigned char k34[] = {
#embed "factory/kit/ride_1.flac"
};
const unsigned char k35[] = {
#embed "factory/kit/ride_2.flac"
};
const unsigned char k36[] = {
#embed "factory/kit/bell.flac"
};
const unsigned char k37[] = {
#embed "factory/Jazz Kit.hnlsampler"
};
const unsigned char k38[] = {
#embed "factory/strings/c2.flac"
};
const unsigned char k39[] = {
#embed "factory/strings/e2.flac"
};
const unsigned char k40[] = {
#embed "factory/strings/g2.flac"
};
const unsigned char k41[] = {
#embed "factory/strings/b2.flac"
};
const unsigned char k42[] = {
#embed "factory/strings/d3.flac"
};
const unsigned char k43[] = {
#embed "factory/strings/f3.flac"
};
const unsigned char k44[] = {
#embed "factory/strings/g3.flac"
};
const unsigned char k45[] = {
#embed "factory/strings/b3.flac"
};
const unsigned char k46[] = {
#embed "factory/strings/d4.flac"
};
const unsigned char k47[] = {
#embed "factory/strings/fs4.flac"
};
const unsigned char k48[] = {
#embed "factory/strings/a4.flac"
};
const unsigned char k49[] = {
#embed "factory/strings/c5.flac"
};
const unsigned char k50[] = {
#embed "factory/strings/e5.flac"
};
const unsigned char k51[] = {
#embed "factory/strings/g5.flac"
};
const unsigned char k52[] = {
#embed "factory/strings/b5.flac"
};
const unsigned char k53[] = {
#embed "factory/strings/d6.flac"
};
const unsigned char k54[] = {
#embed "factory/Strings.hnlsampler"
};
const unsigned char k55[] = {
#embed "factory/organ/c1.flac"
};
const unsigned char k56[] = {
#embed "factory/organ/fs1.flac"
};
const unsigned char k57[] = {
#embed "factory/organ/c2.flac"
};
const unsigned char k58[] = {
#embed "factory/organ/fs2.flac"
};
const unsigned char k59[] = {
#embed "factory/organ/c3.flac"
};
const unsigned char k60[] = {
#embed "factory/organ/fs3.flac"
};
const unsigned char k61[] = {
#embed "factory/organ/c4.flac"
};
const unsigned char k62[] = {
#embed "factory/organ/fs4.flac"
};
const unsigned char k63[] = {
#embed "factory/organ/c5.flac"
};
const unsigned char k64[] = {
#embed "factory/organ/fs5.flac"
};
const unsigned char k65[] = {
#embed "factory/organ/c6.flac"
};
const unsigned char k66[] = {
#embed "factory/Pipe Organ.hnlsampler"
};

#define HNL_FILE(n) k##n, sizeof(k##n)

#else

#if defined(__APPLE__)
#define HNL_SECTION ".section __TEXT,__const"
#elif defined(_WIN32)
#define HNL_SECTION ".section .rdata,\"dr\""
#else
#define HNL_SECTION ".section .rodata"
#endif
#define HNL_STR2(x) #x
#define HNL_STR(x) HNL_STR2(x)
#define HNL_NAME(n) HNL_STR(__USER_LABEL_PREFIX__) "hnl_sampler_factory_" #n
#define HNL_BIN(n, file) \
    __asm__(HNL_SECTION "\n" HNL_NAME(n) ":\n.incbin \"" file "\"\n" HNL_NAME(n) "_end:\n.byte 0\n.text\n"); \
    extern "C" const unsigned char hnl_sampler_factory_##n[], hnl_sampler_factory_##n##_end[];

HNL_BIN(0, "factory/kit/kick_1a.flac")
HNL_BIN(1, "factory/kit/kick_1b.flac")
HNL_BIN(2, "factory/kit/kick_2a.flac")
HNL_BIN(3, "factory/kit/kick_2b.flac")
HNL_BIN(4, "factory/kit/kick_3a.flac")
HNL_BIN(5, "factory/kit/kick_3b.flac")
HNL_BIN(6, "factory/kit/stick_1.flac")
HNL_BIN(7, "factory/kit/stick_2.flac")
HNL_BIN(8, "factory/kit/snare_1a.flac")
HNL_BIN(9, "factory/kit/snare_1b.flac")
HNL_BIN(10, "factory/kit/snare_2a.flac")
HNL_BIN(11, "factory/kit/snare_2b.flac")
HNL_BIN(12, "factory/kit/snare_3a.flac")
HNL_BIN(13, "factory/kit/snare_3b.flac")
HNL_BIN(14, "factory/kit/rim_1.flac")
HNL_BIN(15, "factory/kit/rim_2.flac")
HNL_BIN(16, "factory/kit/tom_low_1.flac")
HNL_BIN(17, "factory/kit/tom_low_2.flac")
HNL_BIN(18, "factory/kit/tom_low_3.flac")
HNL_BIN(19, "factory/kit/tom_high_1.flac")
HNL_BIN(20, "factory/kit/tom_high_2.flac")
HNL_BIN(21, "factory/kit/tom_high_3.flac")
HNL_BIN(22, "factory/kit/hat_closed_1a.flac")
HNL_BIN(23, "factory/kit/hat_closed_1b.flac")
HNL_BIN(24, "factory/kit/hat_closed_2a.flac")
HNL_BIN(25, "factory/kit/hat_closed_2b.flac")
HNL_BIN(26, "factory/kit/hat_closed_3a.flac")
HNL_BIN(27, "factory/kit/hat_closed_3b.flac")
HNL_BIN(28, "factory/kit/hat_pedal_1.flac")
HNL_BIN(29, "factory/kit/hat_pedal_2.flac")
HNL_BIN(30, "factory/kit/hat_open_1.flac")
HNL_BIN(31, "factory/kit/hat_open_2.flac")
HNL_BIN(32, "factory/kit/crash_1.flac")
HNL_BIN(33, "factory/kit/crash_2.flac")
HNL_BIN(34, "factory/kit/ride_1.flac")
HNL_BIN(35, "factory/kit/ride_2.flac")
HNL_BIN(36, "factory/kit/bell.flac")
HNL_BIN(37, "factory/Jazz Kit.hnlsampler")
HNL_BIN(38, "factory/strings/c2.flac")
HNL_BIN(39, "factory/strings/e2.flac")
HNL_BIN(40, "factory/strings/g2.flac")
HNL_BIN(41, "factory/strings/b2.flac")
HNL_BIN(42, "factory/strings/d3.flac")
HNL_BIN(43, "factory/strings/f3.flac")
HNL_BIN(44, "factory/strings/g3.flac")
HNL_BIN(45, "factory/strings/b3.flac")
HNL_BIN(46, "factory/strings/d4.flac")
HNL_BIN(47, "factory/strings/fs4.flac")
HNL_BIN(48, "factory/strings/a4.flac")
HNL_BIN(49, "factory/strings/c5.flac")
HNL_BIN(50, "factory/strings/e5.flac")
HNL_BIN(51, "factory/strings/g5.flac")
HNL_BIN(52, "factory/strings/b5.flac")
HNL_BIN(53, "factory/strings/d6.flac")
HNL_BIN(54, "factory/Strings.hnlsampler")
HNL_BIN(55, "factory/organ/c1.flac")
HNL_BIN(56, "factory/organ/fs1.flac")
HNL_BIN(57, "factory/organ/c2.flac")
HNL_BIN(58, "factory/organ/fs2.flac")
HNL_BIN(59, "factory/organ/c3.flac")
HNL_BIN(60, "factory/organ/fs3.flac")
HNL_BIN(61, "factory/organ/c4.flac")
HNL_BIN(62, "factory/organ/fs4.flac")
HNL_BIN(63, "factory/organ/c5.flac")
HNL_BIN(64, "factory/organ/fs5.flac")
HNL_BIN(65, "factory/organ/c6.flac")
HNL_BIN(66, "factory/Pipe Organ.hnlsampler")

#define HNL_FILE(n) hnl_sampler_factory_##n, static_cast<size_t>(hnl_sampler_factory_##n##_end - hnl_sampler_factory_##n)

namespace {

#endif

const hnl::sampler::factory::Item kItems[] = {
    {"kit/kick_1a.flac", HNL_FILE(0)},
    {"kit/kick_1b.flac", HNL_FILE(1)},
    {"kit/kick_2a.flac", HNL_FILE(2)},
    {"kit/kick_2b.flac", HNL_FILE(3)},
    {"kit/kick_3a.flac", HNL_FILE(4)},
    {"kit/kick_3b.flac", HNL_FILE(5)},
    {"kit/stick_1.flac", HNL_FILE(6)},
    {"kit/stick_2.flac", HNL_FILE(7)},
    {"kit/snare_1a.flac", HNL_FILE(8)},
    {"kit/snare_1b.flac", HNL_FILE(9)},
    {"kit/snare_2a.flac", HNL_FILE(10)},
    {"kit/snare_2b.flac", HNL_FILE(11)},
    {"kit/snare_3a.flac", HNL_FILE(12)},
    {"kit/snare_3b.flac", HNL_FILE(13)},
    {"kit/rim_1.flac", HNL_FILE(14)},
    {"kit/rim_2.flac", HNL_FILE(15)},
    {"kit/tom_low_1.flac", HNL_FILE(16)},
    {"kit/tom_low_2.flac", HNL_FILE(17)},
    {"kit/tom_low_3.flac", HNL_FILE(18)},
    {"kit/tom_high_1.flac", HNL_FILE(19)},
    {"kit/tom_high_2.flac", HNL_FILE(20)},
    {"kit/tom_high_3.flac", HNL_FILE(21)},
    {"kit/hat_closed_1a.flac", HNL_FILE(22)},
    {"kit/hat_closed_1b.flac", HNL_FILE(23)},
    {"kit/hat_closed_2a.flac", HNL_FILE(24)},
    {"kit/hat_closed_2b.flac", HNL_FILE(25)},
    {"kit/hat_closed_3a.flac", HNL_FILE(26)},
    {"kit/hat_closed_3b.flac", HNL_FILE(27)},
    {"kit/hat_pedal_1.flac", HNL_FILE(28)},
    {"kit/hat_pedal_2.flac", HNL_FILE(29)},
    {"kit/hat_open_1.flac", HNL_FILE(30)},
    {"kit/hat_open_2.flac", HNL_FILE(31)},
    {"kit/crash_1.flac", HNL_FILE(32)},
    {"kit/crash_2.flac", HNL_FILE(33)},
    {"kit/ride_1.flac", HNL_FILE(34)},
    {"kit/ride_2.flac", HNL_FILE(35)},
    {"kit/bell.flac", HNL_FILE(36)},
    {"Jazz Kit.hnlsampler", HNL_FILE(37)},
    {"strings/c2.flac", HNL_FILE(38)},
    {"strings/e2.flac", HNL_FILE(39)},
    {"strings/g2.flac", HNL_FILE(40)},
    {"strings/b2.flac", HNL_FILE(41)},
    {"strings/d3.flac", HNL_FILE(42)},
    {"strings/f3.flac", HNL_FILE(43)},
    {"strings/g3.flac", HNL_FILE(44)},
    {"strings/b3.flac", HNL_FILE(45)},
    {"strings/d4.flac", HNL_FILE(46)},
    {"strings/fs4.flac", HNL_FILE(47)},
    {"strings/a4.flac", HNL_FILE(48)},
    {"strings/c5.flac", HNL_FILE(49)},
    {"strings/e5.flac", HNL_FILE(50)},
    {"strings/g5.flac", HNL_FILE(51)},
    {"strings/b5.flac", HNL_FILE(52)},
    {"strings/d6.flac", HNL_FILE(53)},
    {"Strings.hnlsampler", HNL_FILE(54)},
    {"organ/c1.flac", HNL_FILE(55)},
    {"organ/fs1.flac", HNL_FILE(56)},
    {"organ/c2.flac", HNL_FILE(57)},
    {"organ/fs2.flac", HNL_FILE(58)},
    {"organ/c3.flac", HNL_FILE(59)},
    {"organ/fs3.flac", HNL_FILE(60)},
    {"organ/c4.flac", HNL_FILE(61)},
    {"organ/fs4.flac", HNL_FILE(62)},
    {"organ/c5.flac", HNL_FILE(63)},
    {"organ/fs5.flac", HNL_FILE(64)},
    {"organ/c6.flac", HNL_FILE(65)},
    {"Pipe Organ.hnlsampler", HNL_FILE(66)},
};

const hnl::sampler::factory::Install kInstall(kItems, sizeof(kItems) / sizeof(kItems[0]));

} // namespace
