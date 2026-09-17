# Thin convenience wrapper around this project's own real build workflow
# (README.md's own four commands). Nothing here reimplements anything --
# every target below just shells out to the exact same cmake/ctest
# invocations README.md already documents. If this file and README.md
# ever disagree, trust README.md and fix this file to match it.

BUILD_DIR := build
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: all build clean test run

all: build

# Configure (if needed) and build -- README.md's own first two commands.
# Safe to run repeatedly: `cmake -B -S` only reconfigures when the cache
# is missing or stale, it does not force a full rebuild every time.
build:
	cmake -B $(BUILD_DIR) -S .
	cmake --build $(BUILD_DIR) -j$(JOBS)

# A real clean: removes the whole build directory (the CMake cache and
# every compiled artifact), not just test output -- `cmake --build
# $(BUILD_DIR) --target clean` alone only clears compiled objects, it
# leaves the CMake cache and CTest's own bookkeeping in place, which is
# not what "clean" means here.
clean:
	rm -rf $(BUILD_DIR)

# README.md's own third command. Depends on `build` so a fresh checkout
# can run `make test` directly without a separate manual build step.
test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

# README.md's own fourth command: build, then boot the driver against
# the bundled mudlib's own canonical config.
run: build
	./$(BUILD_DIR)/amlp etc/driver.cfg
