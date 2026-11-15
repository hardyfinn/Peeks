# Peeks for VCV Rack.
# Build:  export RACK_DIR=/path/to/Rack-SDK && make && make install   (or make dist)
# Tests:  make test   (host build of the DSP engine, no Rack SDK needed)

RACK_DIR ?= ../..

FLAGS += -DPEEKS_RACK
CXXFLAGS += -Wall -Wextra $(EXTRA_CXXFLAGS)

SOURCES += $(wildcard src/*.cpp src/modules/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += README.md

ifneq ($(wildcard $(RACK_DIR)/plugin.mk),)
include $(RACK_DIR)/plugin.mk
endif

TEST_CXX ?= g++
build/peeks_tests: $(wildcard tests/*.cpp tests/*.hpp src/dsp/*.hpp)
	@mkdir -p build
	$(TEST_CXX) -std=c++17 -O2 -Wall -Wextra -o $@ $(wildcard tests/*.cpp)

build/render_demo: scripts/render_demo.cpp $(wildcard src/dsp/*.hpp)
	@mkdir -p build
	$(TEST_CXX) -std=c++17 -O2 -o $@ scripts/render_demo.cpp

.PHONY: test panels demo
test: build/peeks_tests
	./build/peeks_tests

# Regenerate res/*.svg and src/generated/Layout.hpp (needs Python 3 and fonttools).
panels:
	python3 scripts/make_panel.py

demo: build/render_demo
	./build/render_demo docs/peeks-demo.wav
