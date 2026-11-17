#!/usr/bin/make -f
# HNL Master, built with DPF. Use ../../build.sh to cross-compile for Windows.

NAME = HNL-Master

FILES_DSP = MasterPlugin.cpp
FILES_UI  = MasterUI.cpp

DPF_BUILD_DIR  = ../../build/hnl-master
DPF_TARGET_DIR = ../../dist

include ../../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17

TARGETS = vst3 clap

all: $(TARGETS)
