include NickelHook/NickelHook.mk

override LIBRARY  := src/libtouchremote.so
override SOURCES  += src/main.cc src/touchremote.cc src/gesture.cc
override MOCS     += src/touchremote.h
override PKGCONF  += Qt5Widgets Qt5DBus
override CFLAGS   += -Wall -Wextra -Werror -fvisibility=hidden
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden -fvisibility-inlines-hidden
ifeq ($(TR_CONFIG_DIR),)
override TR_CONFIG_DIR := /mnt/onboard/.adds/touchremote
endif
override CPPFLAGS += -Isrc -DTR_CONFIG_DIR='"$(TR_CONFIG_DIR)"'
override KOBOROOT += res/doc:$(TR_CONFIG_DIR)/doc res/config.sample.ini:$(TR_CONFIG_DIR)/config.sample.ini

override SKIPCONFIGURE += strip test
strip:
	$(STRIP) --strip-unneeded src/libtouchremote.so
.PHONY: strip

# Host build of the gesture translator tests (no Qt, no cross compiler).
test:
	$(HOST_CXX) -std=c++11 -Wall -Wextra -Werror -Isrc tests/gesture_test.cc src/gesture.cc -o tests/gesture_test
	tests/gesture_test
.PHONY: test
HOST_CXX ?= g++

include NickelHook/NickelHook.mk
