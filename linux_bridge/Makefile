CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILD_DIR := build
PROTOCOL := ../bridge/frame_protocol.cpp
FRAME_SENDER := frame_sender.cpp
FRAME_CONVERT := frame_convert.cpp
PORTAL_PACKAGES := gio-2.0 gio-unix-2.0 libpipewire-0.3 libspa-0.2

.PHONY: all portal test clean

all: $(BUILD_DIR)/test_sender $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/frame_convert_tests $(BUILD_DIR)/aspect_fit_tests

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_sender: test_sender.cpp $(FRAME_SENDER) frame_sender.h $(PROTOCOL) ../bridge/frame_protocol.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) test_sender.cpp $(FRAME_SENDER) $(PROTOCOL) -pthread -o $@

portal: $(BUILD_DIR)/portal_sender

$(BUILD_DIR)/portal_sender: portal_sender.cpp $(FRAME_SENDER) frame_sender.h $(FRAME_CONVERT) frame_convert.h $(PROTOCOL) ../bridge/frame_protocol.h | $(BUILD_DIR)
	@pkg-config --exists $(PORTAL_PACKAGES) || { echo "Missing portal sender dependencies: $(PORTAL_PACKAGES)"; exit 1; }
	$(CXX) $(CXXFLAGS) -Wno-pedantic $$(pkg-config --cflags $(PORTAL_PACKAGES)) portal_sender.cpp $(FRAME_SENDER) $(FRAME_CONVERT) $(PROTOCOL) -pthread $$(pkg-config --libs $(PORTAL_PACKAGES)) -o $@

$(BUILD_DIR)/protocol_tests: protocol_tests.cpp $(PROTOCOL) ../bridge/frame_protocol.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) protocol_tests.cpp $(PROTOCOL) -o $@

$(BUILD_DIR)/frame_convert_tests: frame_convert_tests.cpp $(FRAME_CONVERT) frame_convert.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) frame_convert_tests.cpp $(FRAME_CONVERT) -o $@

$(BUILD_DIR)/aspect_fit_tests: aspect_fit_tests.cpp ../bridge/aspect_fit.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) aspect_fit_tests.cpp -o $@

test: $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/frame_convert_tests $(BUILD_DIR)/aspect_fit_tests
	$(BUILD_DIR)/protocol_tests
	$(BUILD_DIR)/frame_convert_tests
	$(BUILD_DIR)/aspect_fit_tests

clean:
	rm -f $(BUILD_DIR)/test_sender $(BUILD_DIR)/portal_sender $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/frame_convert_tests $(BUILD_DIR)/aspect_fit_tests
