CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -pedantic -O2

NANO_FQBN ?= arduino:avr:nano:cpu=atmega328old
TEST_BINARY := build/test_speed_estimator

.PHONY: all test arduino clean

all: test arduino

test: $(TEST_BINARY)
	./$(TEST_BINARY)

$(TEST_BINARY): tests/test_speed_estimator.cpp firmware/speed/SpeedEstimator.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Ifirmware/speed $< -o $@

arduino:
	arduino-cli compile --fqbn "$(NANO_FQBN)" firmware/speed

clean:
	rm -rf build
