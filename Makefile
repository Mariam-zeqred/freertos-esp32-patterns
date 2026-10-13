# Host-side checks (no board needed). `make check` is what CI runs first.
CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Werror -O1
CORE_SRC = $(wildcard lib/patterns_core/*.cpp)
SKETCHES = 01_queue 02_mutex 03_event_group 04_timer_vs_task 05_task_watchdog 06_core_pinning

.PHONY: check test-core pio-test build clean

check: test-core

test-core: build/test_core
	./build/test_core

build/test_core: test/test_native/test_core.cpp test/minitest.h $(CORE_SRC) lib/patterns_core/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Ilib/patterns_core -Itest test/test_native/test_core.cpp $(CORE_SRC) -o $@

# Same tests, run by PlatformIO (needs the registry reachable to fetch the native platform).
pio-test:
	pio test -e native

# All six sketches (needs the espressif32 platform; CI does this, the cloud sandbox cannot).
build:
	for s in $(SKETCHES); do pio run -e $$s || exit 1; done

clean:
	rm -rf build .pio
