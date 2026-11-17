#!/usr/bin/make -f
# HNL Thickener, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Thickener

FILES_DSP = ThickenerPlugin.cpp
FILES_UI  = ThickenerUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-thickener
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3 clap

all: $(TARGETS)
