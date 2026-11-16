#!/usr/bin/make -f
# HNL Channel Spacer, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Channel-Spacer

FILES_DSP = SpacerPlugin.cpp
FILES_UI  = SpacerUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-channel-spacer
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3

all: $(TARGETS)
