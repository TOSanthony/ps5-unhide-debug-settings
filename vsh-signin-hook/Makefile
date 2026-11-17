PS5_HOST ?= ps5
PS5_PORT ?= 9021

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BUILD_DIR ?= build
INTERMEDIATE_DIR ?= hook-build
ELFLDR_DIR ?= third_party/elfldr

VSHSIG_DEBUG ?= 0
VSHSIG_LOGGING ?= $(VSHSIG_DEBUG)
BUILD_CONFIG := $(INTERMEDIATE_DIR)/.vshsig-config-$(VSHSIG_DEBUG)-$(VSHSIG_LOGGING)
BUILD_FLAGS := -O0 -DVSHSIG_LOGGING=$(VSHSIG_LOGGING)
ifeq ($(VSHSIG_DEBUG),1)
BUILD_FLAGS += -g
else
BUILD_FLAGS += -g0 -Wl,--strip-debug
endif

CXXFLAGS := -Wall -Werror $(BUILD_FLAGS) -std=c++17 -Isrc
CFLAGS := -Wall -Werror $(BUILD_FLAGS) -I$(ELFLDR_DIR)
# -nostartfiles is redundant with -shared on this SDK's current clang (it warns
# "argument unused"), which -Werror then turns into a build failure -- so it's
# dropped here versus ps5-syslang's otherwise-identical flag set.
HOOK_FLAGS := -nostdlib -shared -fPIC -ffreestanding -fno-exceptions -fno-rtti -fvisibility=hidden -Wl,-T,src/hook.ld -Wl,-Bsymbolic -Wl,--no-undefined
LDLIBS := -lSceSystemService -lkernel_sys

all: $(BUILD_DIR)/vsh-signin-diag.elf

$(BUILD_DIR):
	mkdir -p $@

$(INTERMEDIATE_DIR):
	mkdir -p $@

$(BUILD_CONFIG): | $(INTERMEDIATE_DIR)
	rm -f $(INTERMEDIATE_DIR)/.vshsig-config-*
	touch $@

$(INTERMEDIATE_DIR)/shellui-hook.elf: src/hook.cpp src/hook_args.h src/vshsig_log.h src/hook.ld Makefile $(BUILD_CONFIG) | $(INTERMEDIATE_DIR)
	$(CXX) $(CXXFLAGS) $(HOOK_FLAGS) -o $@ src/hook.cpp

$(INTERMEDIATE_DIR)/shellui-hook.c: $(INTERMEDIATE_DIR)/shellui-hook.elf
	xxd -i -n shellui_hook_elf $< > $@

$(BUILD_DIR)/vsh-signin-diag.elf: src/loader.c src/hook_args.h src/vshsig_log.h $(INTERMEDIATE_DIR)/shellui-hook.c \
                                   $(ELFLDR_DIR)/elfldr.c $(ELFLDR_DIR)/elfldr.h $(ELFLDR_DIR)/log.h \
                                   $(ELFLDR_DIR)/pt.c $(ELFLDR_DIR)/pt.h \
                                   $(ELFLDR_DIR)/thread_bootstrap.S $(ELFLDR_DIR)/thread_bootstrap.h \
                                   Makefile $(BUILD_CONFIG) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(filter %.c %.S,$^) $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR) $(INTERMEDIATE_DIR)

test: $(BUILD_DIR)/vsh-signin-diag.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
