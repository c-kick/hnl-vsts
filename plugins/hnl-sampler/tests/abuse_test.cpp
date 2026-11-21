// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// What a host may do to this plugin without being wrong, and a little beyond. See common/tests/HostAbuse.hpp.
#include "../Params.hpp"

// An empty sampler makes no sound, and a silent plugin would pass every check: each host gets an instrument with a sawtooth.
#define HNL_ABUSE_SETUP(plugin) plugin.setState("instrument", "HNL Sampler instrument\nname = Saw\npart 1\np.osc_level = 100\n")

#include "../../../common/tests/HostAbuse.hpp"

int main() { return abuse::runAll(); }
