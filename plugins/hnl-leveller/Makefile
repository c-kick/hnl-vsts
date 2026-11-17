#!/usr/bin/make -f
# HNL Leveller, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Leveller

FILES_DSP = LevellerPlugin.cpp
FILES_UI  = LevellerUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-leveller
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3 clap

all: $(TARGETS)
