VERSION=0.1.0

# Flash layout: standard, a 512 KiB image (web updates stage a second copy, so
# need >=1 MiB flash), or LAYOUT=compact, a 252 KiB image that a 512 KiB flash
# can web-update. The addresses are defined once, in layout.h, and read from it
# here through the host C preprocessor.
LAYOUT ?= standard
ifeq ($(LAYOUT),compact)
LAYOUT_DEFS = -DLAYOUT_COMPACT
else ifneq ($(LAYOUT),standard)
$(error LAYOUT must be standard or compact)
endif
HOSTCC ?= gcc
# $(call layout_val,NAME): the value of NAME in layout.h, as a decimal number
layout_val = $(shell echo 'v=$(1)' | $(HOSTCC) -E -P -x c $(LAYOUT_DEFS) -include layout.h - | \
	sed -n 's/^v=//p' | { read x; echo $$((x)); })
IMAGESIZE := $(call layout_val,IMAGE_SIZE)
HTML_LOCATION := $(call layout_val,HTML_START)
DEFAULT_CONFIG_LOCATION := $(call layout_val,DEFAULT_CONFIG_START)
CONFIG_LOCATION := $(call layout_val,CONFIG_START)
ifeq ($(filter-out 0,$(IMAGESIZE)),)
$(error could not read the flash layout from layout.h with $(HOSTCC))
endif
# The standard image carries the saved config; a compact one keeps it outside.
CONFIG_IN_IMAGE := $(shell [ $(CONFIG_LOCATION) -lt $(IMAGESIZE) ] && echo 1)

# Startup configuration embedded in the image. Override it to build a specific
# machine with its own configuration: make MACHINE=<name> CONFIG=<file>
CONFIG ?= config.txt

# Web UI languages to keep (space/comma list, e.g. LANGS="en de"); empty = all
# six. English is always kept as the fallback. Trims the image; see the web UI.
LANGS ?=

SDCC_TOOLCHAIN = sdcc-$(shell cat sdcc/* | cksum | cut -d' ' -f1)
SDCC_PREFIX = toolchain/$(SDCC_TOOLCHAIN)
ifeq ($(origin CC),default)
SDCC = $(SDCC_PREFIX)/bin/sdcc
CC = $(SDCC)
ASM = $(SDCC_PREFIX)/bin/sdas8051
SDCC_REV := $(shell git log -1 --format=%h -- sdcc 2>/dev/null)
ifneq ($(SDCC_REV),)
ifneq ($(shell git status --porcelain --untracked-files=no -- sdcc 2>/dev/null),)
SDCC_REV := $(SDCC_REV)-dirty
endif
endif
SDCC_BANNER = $(if $(SDCC_REV),sdcc/ at $(SDCC_REV),$(SDCC_TOOLCHAIN)) ($(words $(wildcard sdcc/*.patch)) patches)
else
SDCC_BANNER = system
endif
CC_FLAGS = -mmcs51 -I. -Imachine -Ihttpd -Iuip
ASM ?= sdas8051
AFLAGS= -plosgff

SUBDIRS := tools
SUBDIRSCLEAN=$(addsuffix clean,$(SUBDIRS))

# $MACHINE selects machine/$(MACHINE).c as the board definition and is passed
# on as -DMACHINE_NAME, which machine.h uses to include machine/$(MACHINE).h.
# Board selection itself needs no MACHINE_* symbol for #if chains, so the
# definition is all the compiler has to know.
ifneq ($(MACHINE),)
	CC_FLAGS += -DMACHINE_NAME=$(MACHINE)
endif

# PCB information
MACHINE_LINK_TARGET := $(shell readlink machine/$(MACHINE).c 2>/dev/null)
ifneq ($(MACHINE_LINK_TARGET),)
	CC_FLAGS += -DMACHINE_PCB_NAME=$(basename $(notdir $(MACHINE_LINK_TARGET)))
endif
# Health instrumentation and the "health" console command: HEALTH=1 gmake ...
ifneq ($(HEALTH),)
	CC_FLAGS += -DHEALTH
endif
ifneq ($(DEBUG),)
	CC_FLAGS += -DDEBUG
endif

ifeq ($(CI),1)
	CC_FLAGS += --Werror
endif
CC_FLAGS += $(LAYOUT_DEFS)

# The board definition being built: one file per MACHINE_* symbol in machine.h.
MACHINE_SRC = machine/$(MACHINE).c

MACHINE_INIT_DEFAULT := machine/default_machine_init.c
MACHINE_INIT_SRC := $(wildcard machine/$(MACHINE)_init.c)
ifeq ($(MACHINE_INIT_SRC),)
	MACHINE_INIT_SRC := $(MACHINE_INIT_DEFAULT)
endif

ifndef BUILDDIR
	BUILDDIR = output/$(MACHINE)$(if $(filter compact,$(LAYOUT)),-compact)
endif

CONFIG_STAMP := $(BUILDDIR)/.config
VERSION_HEADER := version.h

GIT_VERSION := $(shell git rev-parse --short HEAD)
ifeq ($(shell git status --porcelain --untracked-files=no),)
else
	GIT_VERSION := $(GIT_VERSION)-dirty
endif

VERSION_EXTENSION = v$(VERSION)-$(GIT_VERSION)
FILENAME_EXTENSION = $(VERSION_EXTENSION)-$(MACHINE)$(if $(filter compact,$(LAYOUT)),-compact)

# Path and file name of the final image. BUILDDIR still holds the intermediate
# objects and tools. OUTPUT_DIR is the destination directory. OUTPUT_PREFIX is
# prepended to the automatic file name (version, git hash and machine);
# OUTPUT_NAME replaces that whole name and takes precedence, so use only one of
# OUTPUT_PREFIX and OUTPUT_NAME. Both refer to OUTPUT_DIR. OUTPUT sets a
# complete path and overrides all three:
# make MACHINE=<name> OUTPUT_DIR=/path/to/firmware
# make MACHINE=<name> OUTPUT_DIR=/path/to/firmware OUTPUT_PREFIX=my-switch-
# make MACHINE=<name> OUTPUT_DIR=/path/to/firmware OUTPUT_NAME=my-switch.bin
# make MACHINE=<name> OUTPUT=/path/to/firmware.bin
OUTPUT_DIR ?= $(BUILDDIR)
OUTPUT_PREFIX ?= rtlplayground-
OUTPUT_NAME ?= $(OUTPUT_PREFIX)$(FILENAME_EXTENSION).bin
OUTPUT ?= $(OUTPUT_DIR)/$(OUTPUT_NAME)

# Deterministic build date: honor SOURCE_DATE_EPOCH, else the HEAD commit date,
# else wall-clock (no-git fallback). Keeps same-commit builds byte-identical
# (BUILD_DATE is baked into the image and covered by the trailing CRC).
SOURCE_DATE_EPOCH ?= $(shell git show -s --format=%ct HEAD 2>/dev/null)
ifeq ($(SOURCE_DATE_EPOCH),)
BUILD_DATE := $(shell date +"%Y-%m-%d %H:%M:%S")
else
BUILD_DATE := $(shell date -u -d @$(SOURCE_DATE_EPOCH) +"%Y-%m-%d %H:%M:%S" 2>/dev/null \
	|| date -u -r $(SOURCE_DATE_EPOCH) +"%Y-%m-%d %H:%M:%S")
endif

# $(MACHINE_SRC) and $(CONFIG) are listed here so an unknown $MACHINE or a
# missing config file fails before anything else is built, including the tools/
# subdirectory.
all: create_build_dir $(VERSION_HEADER) $(MACHINE_SRC) $(CONFIG) $(SUBDIRS) $(OUTPUT)

bank_size:
	tools/bank_occupancy.sh "$(MACHINE)"

# Images for rtlsim's flash tests (github.com/sirmick/ovs-rtlpg): every board
# and layout tools/rtlsim_images.py lists, built one after another (builds
# share files in the tree), then assembled into OUT with its manifest.json.
RTLSIM_BOARDS = SWGT024_V2_0_UNMANAGED AMPCOM_WAM902_SWTG018AS POE_2G040210GSM
RTLSIM_BUILD = output/rtlsim
.PHONY: rtlsim-images
rtlsim-images:
	@test -n "$(OUT)" || { echo "usage: make rtlsim-images OUT=dir" >&2; false; }
	for m in $(RTLSIM_BOARDS); do \
		$(MAKE) MACHINE=$$m BUILDDIR=$(RTLSIM_BUILD)/$$m && \
		$(MAKE) MACHINE=$$m LAYOUT=compact BUILDDIR=$(RTLSIM_BUILD)/$$m-compact && \
		$(MAKE) MACHINE=$$m LAYOUT=compact LOADER=1 BUILDDIR=$(RTLSIM_BUILD)/$$m-compact-loader || exit 1; \
	done
	python3 tools/rtlsim_images.py $(RTLSIM_BUILD) "$(OUT)"

# Fail if any direct call crosses a code bank (needs a prior build of $MACHINE).
check-banks:
	python3 tools/check_cross_bank.py $(BUILDDIR)

create_build_dir: $(SDCC)
	mkdir -p "$(BUILDDIR)"
	mkdir -p "$(BUILDDIR)/uip"
	mkdir -p "$(BUILDDIR)/httpd"
	mkdir -p "$(BUILDDIR)/machine"
	mkdir -p "$(BUILDDIR)/crypto"

# Every machine is a pair machine/<MACHINE>.c + machine/<MACHINE>.h. Selecting
# them by name means no #if/#elif chain, so an unknown or empty $MACHINE is
# caught here instead of by a #error further down the build.
$(MACHINE_SRC):
	@echo "ERROR: MACHINE='$(MACHINE)' has no definition (expected $(MACHINE_SRC))." >&2
	@echo "       Every machine is one file pair under machine/, named after the" >&2
	@echo "       MACHINE_* symbol it defines; see machine/*.c for the full list." >&2
	@echo "       Example: make MACHINE=KP_9000_6XHML_X2" >&2
	@false

# Fail with a clear message when CONFIG names a file that does not exist.
$(CONFIG):
	@echo "ERROR: CONFIG='$(CONFIG)' does not exist." >&2
	@echo "       Point CONFIG at a configuration file, or omit it to use config.txt." >&2
	@false

# Keep the per-machine definition in first position to fail immediately on an
# invalid $MACHINE value
SRCS = \
	$(MACHINE_SRC) \
	$(MACHINE_INIT_SRC) \
	cmd_editor.c \
	cmd_parser.c \
	dhcp.c \
	html_data.c \
	rtlplayground.c \
	boot0.c \
	update_apply.c \
	boot.c \
	sfp.c \
	dns.c \
	dns_cli.c \
	sessions.c \
	ntp.c \
	ntp_cli.c \
	syslog.c \
	timer.c \
	udp_apps.c \
	crypto/chacha20.c

# RTL837x
SRCS += \
	rtl837x_bandwidth.c \
	rtl837x_cabletest.c \
	rtl837x_flash.c \
	rtl837x_igmp.c \
	rtl837x_init.c \
	rtl837x_nic.c \
	rtl837x_leds.c \
	rtl837x_leds_dump.c \
	rtl837x_phy.c \
	rtl837x_pins.c\
	rtl837x_port.c \
	rtl837x_stp.c \
	rtl837x_lldp.c \
	rtl837x_storm.c \
	rtl837x_lacp.c \
	rtl837x_trap.c \
	rtl837x_stp_cli.c
SRCS += \
	httpd/httpd.c \
	httpd/page_impl.c \
	httpd/html_out.c \
	httpd/page_stp.c
SRCS += \
	uip/uip.c \
	uip/uiplib.c \
	uip/uip_arp.c \
	uip/uip-fw.c \
	uip/uip-neighbor.c \
	uip/uip-split.c

OBJS = ${SRCS:%.c=$(BUILDDIR)/%.rel}
DEPS := ${SRCS:%.c=$(BUILDDIR)/%.d}

# Code banks. These sources stay in the home bank: interrupt handlers and the
# code they call, the flash and update code (it rewrites the banks), the board
# definition, and the generated web UI index (html_data names its own bank).
# Every other C source is compiled into the area BANKED, and each build places
# it in BANK1-3 after compiling (tools/place_banks.py, result in
# $(BUILDDIR)/banks.txt): no bank placement in the sources or anywhere else.
RESIDENT_SRCS = $(MACHINE_SRC) html_data.c rtlplayground.c boot0.c update_apply.c timer.c \
	udp_apps.c rtl837x_flash.c rtl837x_nic.c httpd/html_out.c
BANKED_SRCS = $(filter-out $(RESIDENT_SRCS),$(SRCS))
BANKED_OBJS = $(BANKED_SRCS:%.c=$(BUILDDIR)/%.rel)
RESIDENT_OBJS = $(filter-out $(BANKED_OBJS),$(OBJS))
ASM_OBJS = $(BUILDDIR)/crtbank.rel $(BUILDDIR)/crc16.rel $(BUILDDIR)/crypto/chacha_8051.rel
# What is linked: every object in SRCS order, a banked one as its placed copy.
LINK_OBJS = $(foreach o,$(OBJS),$(if $(filter $(o),$(BANKED_OBJS)),$(BUILDDIR)/placed/$(patsubst $(BUILDDIR)/%,%,$(o)),$(o))) $(ASM_OBJS)
BANKED_FLAGS = --codeseg BANKED --constseg BANKED
$(BANKED_OBJS): BANK_FLAGS = $(BANKED_FLAGS)
# The first code after a reset, in sector 0 right after the interrupt vectors
# (rtlplayground.c's HOME area, linked before it); see boot0.c.
$(BUILDDIR)/boot0.rel: BANK_FLAGS = --codeseg HOME --constseg HOME
HTML := $(shell find html -name '*.html' -or -name '*.svg' -or -name '*.css' -or -name '*.ico')

# Minified copy of the web UI sources, used as the fileadder input.
# The raw html/ sources stay untouched for development; the minified
# copy is a build artifact under output/.
HTML_MIN := $(BUILDDIR)/html_min
HTML_MIN_LIST := $(BUILDDIR)/html_min.list
HTML_MIN_STAMP := $(BUILDDIR)/html_min.stamp
.PHONY: html_min
html_min: $(HTML_MIN_STAMP)

$(HTML_MIN_LIST): FORCE
	@mkdir -p $(BUILDDIR)
	@echo $(sort $(HTML)) LANGS=$(LANGS) > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@

$(HTML_MIN_STAMP): $(HTML) html/app.js $(HTML_MIN_LIST) tools/minify.py tools/rjsmin.py tools/filter_langs.py $(if $(wildcard $(HTML_MIN)),,FORCE)
	rm -rf $(HTML_MIN)
	mkdir -p $(HTML_MIN) $(BUILDDIR)/_langs
	@for f in $(HTML); do \
		python3 tools/filter_langs.py "$(LANGS)" $$f $(BUILDDIR)/_langs/$$(basename $$f) && \
		python3 tools/minify.py $(BUILDDIR)/_langs/$$(basename $$f) $(HTML_MIN)/$$(basename $$f) || exit 1; \
	done
	python3 tools/filter_langs.py "$(LANGS)" html/app.js $(BUILDDIR)/_langs/app.js && \
		python3 tools/rjsmin.py < $(BUILDDIR)/_langs/app.js > $(HTML_MIN)/app.js || exit 1
	@touch $@

# html_data.c is generated in the source tree with the web UI's addresses, so
# it must be regenerated whenever the layout changes, even across BUILDDIRs.
LAYOUT_STAMP := .layout
$(LAYOUT_STAMP): FORCE
	@echo '$(HTML_LOCATION) $(IMAGESIZE)' | cmp -s - $@ 2>/dev/null || echo '$(HTML_LOCATION) $(IMAGESIZE)' > $@

html_data.c html_data.h &: $(HTML_MIN_STAMP) $(LAYOUT_STAMP) | tools
	tools/$(BUILDDIR)/fileadder -a $(HTML_LOCATION) -s $(IMAGESIZE) -b BANK1 -z -d $(HTML_MIN) -p html_data

$(VERSION_HEADER): FORCE
	@printf '%s\n' "#ifndef VERSION_H" "#define VERSION_H" \
		"#define VERSION_SW \"$(VERSION_EXTENSION)\"" \
		"#define BUILD_DATE \"$(BUILD_DATE)\"" \
		"#define SDCC_TOOLCHAIN \"$(SDCC_BANNER)\"" \
		"#endif" > $(VERSION_HEADER).tmp
	@cmp -s $(VERSION_HEADER).tmp $(VERSION_HEADER) \
		&& rm -f $(VERSION_HEADER).tmp \
		|| mv $(VERSION_HEADER).tmp $(VERSION_HEADER)

httpd: html_data.h

$(SUBDIRS):
	$(MAKE) -C $@ BUILDDIR=$(BUILDDIR)

clean: $(SUBDIRSCLEAN)
	-rm -f html_data.c html_data.h $(VERSION_HEADER) $(HTML_MIN_LIST) $(HTML_MIN_STAMP) $(LAYOUT_STAMP)
	-if [ -d $(BUILDDIR) ]; then find $(BUILDDIR) -type f ! -name "*.bin" -delete; fi

distclean: $(SUBDIRSCLEAN)
	-rm -f html_data.c html_data.h $(VERSION_HEADER) $(HTML_MIN_LIST) $(HTML_MIN_STAMP) $(LAYOUT_STAMP)
	-rm -rf $(BUILDDIR)
	-rm -rf toolchain

ifneq ($(SDCC),)
$(SDCC):
	sh sdcc/build.sh $(abspath $(SDCC_PREFIX))
endif

$(SUBDIRSCLEAN):
	$(MAKE) -C $(@:clean=) clean

# Objects depend on the flags they were built with, through a stamp file that
# is rewritten only when CC_FLAGS actually changes.
CCFLAGS_STAMP := $(BUILDDIR)/.ccflags

.PHONY: FORCE
FORCE:

$(CCFLAGS_STAMP): FORCE | create_build_dir
	@echo '$(CC_FLAGS)' | cmp -s - $@ 2>/dev/null || echo '$(CC_FLAGS)' > $@

# The same for $(CONFIG): its path is stamped so switching to a config file
# older than the image still rebuilds it, while $(CONFIG) itself as a normal
# prerequisite covers edits to the selected file.
$(CONFIG_STAMP): FORCE | create_build_dir
	@echo '$(CONFIG)' | cmp -s - $@ 2>/dev/null || echo '$(CONFIG)' > $@

$(BUILDDIR)/%.rel: %.c $(CCFLAGS_STAMP) | create_build_dir html_data.h
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<

$(BUILDDIR)/%.rel: %.asm $(CCFLAGS_STAMP) | create_build_dir
	${ASM} ${AFLAGS} -o $@ $<
#	mv -f $(addprefix $(basename $^), .lst .rel .sym) .

# Place the banked objects into the banks: copies under placed/ for the link.
$(BUILDDIR)/placed.stamp: $(OBJS) $(ASM_OBJS) tools/place_banks.py
	python3 tools/place_banks.py $(BUILDDIR) $(BANKED_OBJS) -- $(RESIDENT_OBJS) $(ASM_OBJS)
	@touch $@

$(BUILDDIR)/rtlplayground.ihx: $(BUILDDIR)/placed.stamp
	$(CC) $(CC_FLAGS) --xram-size 49151 -Wl-bHOME=0x00000 -Wl-bBANK1=0x14000 -Wl-bBANK2=0x24000 -Wl-bBANK3=0x34000 -Wl-r -o $@ \
		$(LINK_OBJS)
	python3 tools/check_linked.py $(BUILDDIR) $(BUILDDIR)/rtlplayground.map || { rm -f $@; exit 1; }
	python3 tools/check_cross_bank.py $(BUILDDIR) || { rm -f $@; exit 1; }

$(BUILDDIR)/rtlplayground.img: $(BUILDDIR)/rtlplayground.ihx
	objcopy --input-target=ihex -O binary $< $@

$(OUTPUT): $(BUILDDIR)/rtlplayground.img $(CONFIG) $(CONFIG_STAMP) | tools
	@mkdir -p "$(dir $@)"
	if [ -e "$@" ]; then rm "$@"; fi
	tools/$(BUILDDIR)/imagebuilder -i $(BUILDDIR)/rtlplayground.img "$@"
	python3 tools/check_layout.py --code "$@" --html-defs html_data.h --html $(HTML_LOCATION) \
		--default-config $(DEFAULT_CONFIG_LOCATION) --size $(IMAGESIZE)
	tools/$(BUILDDIR)/fileadder -a $(DEFAULT_CONFIG_LOCATION) -s $(IMAGESIZE) -d "$(CONFIG)" "$@"
	$(if $(CONFIG_IN_IMAGE),tools/$(BUILDDIR)/fileadder -a $(CONFIG_LOCATION) -s $(IMAGESIZE) -d "$(CONFIG)" "$@")
	tools/$(BUILDDIR)/fileadder -a $(HTML_LOCATION) -s $(IMAGESIZE) -z -d $(HTML_MIN) -b BANK1 "$@"
	tools/$(BUILDDIR)/crc_calculator -u "$@"
	python3 tools/check_layout.py --image "$@" --html $(HTML_LOCATION) \
		--default-config $(DEFAULT_CONFIG_LOCATION) --size $(IMAGESIZE)
ifeq ($(LAYOUT),compact)
	# Whole-chip copy for a programmer: the image, then erased (0xff) flash up
	# to 512 KiB - empty staging area and a blank saved config, seeded at boot.
	{ cat "$@"; head -c $$((524288 - $(IMAGESIZE))) /dev/zero | tr '\000' '\377'; } > "$(basename $@)-flash512k.bin"
endif
	if [ "$(OUTPUT)" = "$(BUILDDIR)/rtlplayground-$(FILENAME_EXTENSION).bin" ]; then \
		ln -sf $(MACHINE)/rtlplayground-$(FILENAME_EXTENSION).bin "$(BUILDDIR)/../rtlplayground.bin"; \
	fi

# A compact build always builds the loader: the programmer image (-boot), the
# /loader image (-loader) and the install image (-install: the application,
# then the loader, which its upload stages at LDR_STAGE; a board without the
# loader converts to it at the next boot). LOADER=1 is kept for old commands.
ifneq ($(or $(LOADER),$(filter compact,$(LAYOUT))),)
all: $(BUILDDIR)/rtlplayground-boot-$(FILENAME_EXTENSION).bin $(BUILDDIR)/rtlplayground-install-$(FILENAME_EXTENSION).bin
endif

LOADER_OBJS = $(BUILDDIR)/loader/loader.rel $(BUILDDIR)/loader/net.rel $(BUILDDIR)/loader/loader_alias.rel $(BUILDDIR)/loader/rtl837x_flash.rel

LOADER_HDRS = layout.h loader/layout.h loader/loader.h rtl837x_sfr.h rtl837x_regs.h rtl837x_common.h rtl837x_flash.h

$(BUILDDIR)/loader/%.rel: loader/%.c $(LOADER_HDRS) $(CCFLAGS_STAMP) | create_build_dir
	mkdir -p "$(BUILDDIR)/loader"
	$(CC) $(CC_FLAGS) -o $@ -c $<

$(BUILDDIR)/loader/rtl837x_flash.rel: rtl837x_flash.c $(LOADER_HDRS) $(CCFLAGS_STAMP) | create_build_dir
	mkdir -p "$(BUILDDIR)/loader"
	$(CC) $(CC_FLAGS) -DLDR_BUILD -o $@ -c $<

$(BUILDDIR)/loader/loader.ihx: $(LOADER_OBJS)
	@if grep -E '^ +[0-9A-F]{6} ' $(BUILDDIR)/loader/loader_alias.lst | grep -E '(ljmp|lcall|ajmp|acall)' | grep -vq 'ljmp 0x0000'; then echo "app_start must only use relative jumps"; exit 1; fi
	$(CC) -mmcs51 --xram-size 49151 -Wl-bHOME=0x00000 -Wl-bALIAS=0xb800 -Wl-r -o $@ $^
	python3 tools/check_linked.py $(BUILDDIR) $(BUILDDIR)/loader/loader.map || { rm -f $@; exit 1; }

$(BUILDDIR)/rtlplayground-boot-$(FILENAME_EXTENSION).bin: $(BUILDDIR)/loader/loader.ihx $(BUILDDIR)/rtlplayground-$(FILENAME_EXTENSION).bin tools/bootpack.py tools/layout.py layout.h
	LAYOUT_DEFS="$(LAYOUT_DEFS)" HOSTCC="$(HOSTCC)" python3 tools/bootpack.py $(BUILDDIR)/loader/loader.ihx $(BUILDDIR)/rtlplayground-$(FILENAME_EXTENSION).bin $@ $(BUILDDIR)/rtlplayground-loader-$(FILENAME_EXTENSION).bin

$(BUILDDIR)/rtlplayground-install-$(FILENAME_EXTENSION).bin: $(BUILDDIR)/rtlplayground-boot-$(FILENAME_EXTENSION).bin
	cat $(BUILDDIR)/rtlplayground-$(FILENAME_EXTENSION).bin $(BUILDDIR)/rtlplayground-loader-$(FILENAME_EXTENSION).bin > $@

.PHONY: clean distclean all $(SUBDIRS) $(SUBDIRSCLEAN) create_build_dir

# Every machine/ definition is compiled, so a machine added to machine/ is
# covered without registering it anywhere. A board only counts once both its
# <machine>.c and its <machine>.h are present: the header is what the rest of
# the build includes, so a .c without one is an incomplete board. The _init.c
# files are excluded here: they are the boot hooks, compiled separately below
# (the board's own if it exists, otherwise default_machine_init.c). The
# machine-specific defines are filtered out of $(CC_FLAGS) and supplied per
# iteration instead, so this also works as "make machine_check MACHINE=...".
MACHINE_DEFS := $(sort $(filter $(patsubst machine/%.h,%,$(wildcard machine/*.h)),$(patsubst machine/%.c,%,$(filter-out machine/%_init.c,$(wildcard machine/*.c)))))
MACHINE_LESS_CC_FLAGS = $(filter-out -DMACHINE_%,$(CC_FLAGS))

.PHONY: machine_check
machine_check: $(SDCC)
	@mkdir -p $(BUILDDIR)/tmp
	@set -eo pipefail; \
	for MACHINE in $(MACHINE_DEFS); \
	do \
	echo "Checking $${MACHINE}"; \
	$(CC) $(MACHINE_LESS_CC_FLAGS) -DMACHINE_NAME=$${MACHINE} -MMD -o $(BUILDDIR)/tmp/machine_check -c machine/$${MACHINE}.c; \
	INIT=machine/$${MACHINE}_init.c; \
	if [ ! -e $$INIT ] && [ ! -L $$INIT ]; then INIT=$(MACHINE_INIT_DEFAULT); fi; \
	$(CC) $(MACHINE_LESS_CC_FLAGS) $(BANKED_FLAGS) -DMACHINE_NAME=$${MACHINE} -MMD -o $(BUILDDIR)/tmp/machine_check -c $$INIT; \
	done
	@rm -rf $(BUILDDIR)/tmp

-include $(DEPS)
