#!/usr/bin/make -f
# HNL Transient Shaper, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Transient-Shaper

FILES_DSP = ShaperPlugin.cpp
FILES_UI  = ShaperUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-transient-shaper
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3 clap

all: $(TARGETS)
