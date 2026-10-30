.SUFFIXES:

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
CTRULIB ?= $(DEVKITPRO)/libctru

TARGET := AgitateESP3DS
BUILD := build
SOURCES := $(wildcard source/*.cpp)
OBJECTS := $(patsubst source/%.cpp,$(BUILD)/%.o,$(SOURCES))
ICON := resources/icon.png
BANNER := resources/banner.png
SILENCE := resources/silence.wav

APP_TITLE := agitate-esp
APP_DESCRIPTION := 3DS controller for agitate-esp
APP_AUTHOR := beegus
APP_PRODUCT_CODE := AGITATE3DS
# CIA version shown by installers; CI sets it from the vX.Y.Z release tag.
VERSION ?= 1.0.0
VERSION_PARTS := $(subst ., ,$(VERSION))

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CXX := $(DEVKITARM)/bin/arm-none-eabi-g++
HOST_CXX ?= c++
CPPFLAGS := -D__3DS__ -I$(CTRULIB)/include
CXXFLAGS := -g -Wall -Wextra -O2 -mword-relocations \
	-ffunction-sections -fdata-sections -fno-rtti -fno-exceptions \
	-std=gnu++11 $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib -Wl,--gc-sections
LIBS := -lctru -lm

.PHONY: all cia 3dsx test clean assets check-toolchain check-cia-tools check-3dsx-tools

all: cia

cia: $(TARGET).cia

3dsx: $(TARGET).3dsx

assets:
	python3 tools/generate_assets.py

check-toolchain:
	@test -x "$(CXX)" || { echo "Missing devkitARM compiler: $(CXX)"; exit 1; }

check-cia-tools: check-toolchain
	@command -v makerom >/dev/null || { echo "Missing makerom; get it from github.com/3DSGuy/Project_CTR/releases."; exit 1; }
	@command -v bannertool >/dev/null || { echo "Missing bannertool; get it from github.com/diasurgical/bannertool/releases."; exit 1; }

check-3dsx-tools: check-toolchain
	@command -v smdhtool >/dev/null || { echo "Missing smdhtool; install the devkitPro 3DS tools."; exit 1; }
	@command -v 3dsxtool >/dev/null || { echo "Missing 3dsxtool; install the devkitPro 3DS tools."; exit 1; }

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: source/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: check-toolchain $(OBJECTS)
	$(CXX) $(LDFLAGS) -Wl,-Map=$(TARGET).map -o $@ $(OBJECTS) $(LIBS)

$(TARGET).smdh: check-3dsx-tools assets
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" "$(ICON)" $@

$(TARGET).icn: check-cia-tools assets
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(ICON)" -o $@

$(TARGET).bnr: check-cia-tools assets
	bannertool makebanner -i "$(BANNER)" -a "$(SILENCE)" -o $@

$(TARGET).cia: check-cia-tools $(TARGET).elf $(TARGET).icn $(TARGET).bnr resources/agitate-esp.rsf
	makerom -f cia -o $@ -DAPP_ENCRYPTED=false -rsf resources/agitate-esp.rsf \
		-target t -exefslogo -elf $(TARGET).elf -icon $(TARGET).icn \
		-banner $(TARGET).bnr -major $(word 1,$(VERSION_PARTS)) -minor $(word 2,$(VERSION_PARTS)) \
		-micro $(word 3,$(VERSION_PARTS))

$(TARGET).3dsx: check-3dsx-tools $(TARGET).elf $(TARGET).smdh
	3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh

test: | $(BUILD)
	$(HOST_CXX) -std=c++11 -Wall -Wextra -Werror -pedantic -Isource \
		source/json.cpp tests/test_json.cpp -o $(BUILD)/test_json
	$(BUILD)/test_json
	$(HOST_CXX) -std=c++11 -Wall -Wextra -Werror -pedantic -Isource \
		source/json.cpp source/models.cpp tests/test_models.cpp \
		-o $(BUILD)/test_models
	$(BUILD)/test_models
	$(HOST_CXX) -std=c++11 -Wall -Wextra -Werror -pedantic -Isource \
		source/discovery.cpp tests/test_discovery.cpp \
		-o $(BUILD)/test_discovery
	$(BUILD)/test_discovery

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).map $(TARGET).smdh \
		$(TARGET).icn $(TARGET).bnr $(TARGET).cia $(TARGET).3dsx \
		$(ICON) $(BANNER) $(SILENCE)

-include $(OBJECTS:.o=.d)
