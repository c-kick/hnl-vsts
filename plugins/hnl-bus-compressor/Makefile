#!/usr/bin/make -f
# HNL Bus Compressor, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Bus-Compressor

FILES_DSP = BusCompPlugin.cpp
FILES_UI  = BusCompUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-bus-compressor
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3

all: $(TARGETS)
