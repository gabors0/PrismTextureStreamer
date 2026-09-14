CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILD_DIR := build
PROTOCOL := ../bridge/frame_protocol.cpp

.PHONY: all test clean

all: $(BUILD_DIR)/test_sender $(BUILD_DIR)/protocol_tests

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_sender: test_sender.cpp $(PROTOCOL) ../bridge/frame_protocol.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) test_sender.cpp $(PROTOCOL) -o $@

$(BUILD_DIR)/protocol_tests: protocol_tests.cpp $(PROTOCOL) ../bridge/frame_protocol.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) protocol_tests.cpp $(PROTOCOL) -o $@

test: $(BUILD_DIR)/protocol_tests
	$(BUILD_DIR)/protocol_tests

clean:
	rm -f $(BUILD_DIR)/test_sender $(BUILD_DIR)/protocol_tests
