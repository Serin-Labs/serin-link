# SPDX-License-Identifier: GPL-3.0-only
# Serin dial UI simulator — builds the REAL main/ UI sources natively.
# Usage: make -j$(nproc) [BOARD=viewe15|viewe21]   Requires: libsdl2-dev
BOARD ?= viewe15

ifeq ($(BOARD),viewe21)
BOARD_DEF := CONFIG_SERIN_BOARD_VIEWE_21
else ifeq ($(BOARD),viewe15)
BOARD_DEF := CONFIG_SERIN_BOARD_VIEWE_15
else
$(error BOARD must be viewe15 or viewe21)
endif

ROOT  ?= ..
LVGL  ?= $(shell python3 prepare_lvgl.py .cache)
BUILD ?= build/$(BOARD)
BIN   := $(BUILD)/serin-sim

# Compile every main/ui*.c so new UI modules cannot be silently omitted.
# Shared UI helpers whose names do not begin with ui stay listed explicitly.
UI_SRCS   := $(wildcard $(ROOT)/main/ui*.c) $(ROOT)/main/child_lock.c $(ROOT)/main/factory_reset.c $(ROOT)/main/icons_serin.c $(wildcard $(ROOT)/main/fonts/*.c)
SIM_SRCS  := sim_main.c bsp_sim.c sim_time.c sim_shot.c sim_cmd.c stubs/esp_stubs.c
LVGL_SRCS := $(shell find $(LVGL)/src -name '*.c')
SRCS      := $(SIM_SRCS) $(UI_SRCS) $(LVGL_SRCS)

CFLAGS := -O2 -g -std=gnu17 -Wall -Wno-unused-function \
          -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE=1 -D$(BOARD_DEF)=1 -DSERIN_SIM=1 \
          -I. -Istubs -I$(ROOT)/main -I$(LVGL) \
          $(shell sdl2-config --cflags) -MMD -MP
LDLIBS := $(shell sdl2-config --libs) -lm

# Sources live above sim/ — flatten paths ('/' -> '~') into one object dir.
obj_of = $(BUILD)/$(subst /,~,$(1)).o
OBJS := $(foreach s,$(SRCS),$(call obj_of,$(s)))

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDLIBS) -o $@

define C_RULE
$(call obj_of,$(1)): $(1) | $(BUILD)
	$$(CC) $$(CFLAGS) -c $$< -o $$@
endef
$(foreach s,$(SRCS),$(eval $(call C_RULE,$(s))))

$(BUILD):
	mkdir -p $(BUILD)

-include $(OBJS:.o=.d)

clean:
	rm -rf build

.PHONY: all clean
