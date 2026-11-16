#!/usr/bin/make -f
# HNL House Piano, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-House-Piano

FILES_DSP = PianoPlugin.cpp
FILES_UI  = PianoUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-house-piano
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3

all: $(TARGETS)
