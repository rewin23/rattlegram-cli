CXX ?= g++
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
DESTDIR ?=
PACKAGE = com.aicodix.rattlegram
GRADLE = JAVA_HOME=$(HOME)/Android/Studio/jbr/ ./gradlew
ADB = adb

CPPFLAGS += -isystem app/src/main/cpp $(shell $(PKG_CONFIG) --cflags libpulse-simple)
CXXFLAGS ?= -O3 -ffast-math
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -MMD -MP
LDLIBS += $(shell $(PKG_CONFIG) --libs libpulse-simple)

TARGET := rattlegram
SOURCES := cli/main.cpp cli/rattlegram.cpp cli/wav.cpp cli/pulse_audio.cpp
OBJECTS := $(patsubst cli/%.cpp,build/cli/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean install uninstall android android-build android-install android-remove android-start

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/cli/%.o: cli/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -rf build/cli $(TARGET)

android:
	$(GRADLE) installDebug
	$(ADB) shell am start -n $(PACKAGE)/$(PACKAGE).MainActivity

android-build:
	$(GRADLE) assembleDebug

android-install:
	$(ADB) -d install app/build/outputs/apk/debug/app-debug.apk

android-remove:
	$(ADB) uninstall $(PACKAGE)

android-start:
	$(ADB) shell am start -n $(PACKAGE)/$(PACKAGE).MainActivity

-include $(DEPS)
