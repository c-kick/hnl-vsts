#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# HNL Patina, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Patina

FILES_DSP = PatinaPlugin.cpp
FILES_UI  = PatinaUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-patina
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3 clap

all: $(TARGETS)
