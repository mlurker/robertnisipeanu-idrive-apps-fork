# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 mlurker@gmail.com
# mlurker-hud-signals for the unit: the cluster-render toolchain (Ubuntu 18.04 amd64, gcc
# 7.5, the unit's libstdc++; ../cluster-render/dcsm/DCSM.md "Build and ABI"), linked
# against the ABI stub of the unit's libvsomeip3 (the real one is on the unit).
#
#   make test      the flasher, arrow and warning tests, on this machine
#   make target    build/target/mlurker-hud-signals for the unit (Docker)
CR      := ../cluster-render
IMAGE   := cluster-render-build:$(shell cksum < $(CR)/docker/Dockerfile | cut -d' ' -f1)
PYTHON  ?= python3
B       := build
VERSION := $(shell sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' component.json | head -1)
CXXFLAGS = -std=c++14 -O2 -g -Wall -Wextra -Werror
INC     = -Isrc -I$(CR)/include -I$(CR) -I$(CR)/src -isystem $(CR)/dcsm/third_party/vsomeip/interface
SRC     = src/main.cpp src/indicators.cpp src/arrows.cpp src/readouts.cpp src/blindspot.cpp $(CR)/client/client.cpp $(CR)/client/protocol.cpp \
          $(CR)/src/config.cpp $(CR)/src/log.cpp

.PHONY: all test target target-in-docker image clean
all: test

test: $(B)/host/hud_signals_test
	$(B)/host/hud_signals_test

$(B)/host/hud_signals_test: test/hud_signals_test.cpp src/indicators.cpp src/arrows.cpp src/readouts.cpp src/blindspot.cpp src/indicators.h src/arrows.h src/readouts.h src/blindspot.h
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ test/hud_signals_test.cpp src/indicators.cpp src/arrows.cpp src/readouts.cpp src/blindspot.cpp

image:
	docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -q --platform linux/amd64 -t $(IMAGE) $(CR)/docker

$(B)/vsomeip_stub.S: $(CR)/dcsm/abi/libvsomeip3.so.3.symbols
	mkdir -p $(B)
	$(PYTHON) $(CR)/dcsm/tools/abi.py stub $< $@

target: $(B)/vsomeip_stub.S | image
	docker run --rm --platform linux/amd64 --user "$$(id -u):$$(id -g)" -v "$(CURDIR)/..:/w" -w /w/mlurker-hud-signals $(IMAGE) \
	  make -s target-in-docker

target-in-docker: $(B)/target/mlurker-hud-signals

$(B)/target/mlurker-hud-signals: $(SRC) src/arrows.h src/indicators.h src/blindspot.h
	mkdir -p $(@D)
	g++ -shared -nostdlib -Wl,-soname,libvsomeip3.so.3 -o $(B)/libvsomeip3.so.3 $(B)/vsomeip_stub.S
	g++ $(CXXFLAGS) $(INC) -DHS_VERSION='"$(VERSION)"' -o $@ $(SRC) $(B)/libvsomeip3.so.3 -lpthread \
	  -Wl,--dynamic-linker=/lib/ld-linux-x86-64.so.2

clean:
	rm -rf $(B)
