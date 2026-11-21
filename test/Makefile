# Host unit-test harness for RTLPlayground firmware logic.
#
# Compiles selected firmware translation units with host gcc + AddressSanitizer,
# force-including sdcc_shim.h to erase 8051 keywords, and links them against
# mocked hardware edges (support.c). No SDCC, no flashing, no hardware.
#
#   make        build + run all tests
#   make run    run tests
#   make clean

CC       = gcc
SRCROOT  = ..
# -include shim first so __xdata/__code/__banked vanish before any firmware header.
CFLAGS   = -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unknown-pragmas \
           -Wno-pointer-sign \
           -DRTLP_HOST_TEST \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -include sdcc_shim.h -Istub -I$(SRCROOT) -I$(SRCROOT)/httpd -I$(SRCROOT)/uip -I$(BUILDDIR)/om
ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS  += -Wno-builtin-declaration-mismatch
endif
LDFLAGS  = -fsanitize=address,undefined
# The preprocessor the scheduler sweep (tools/sched_sweep.py) runs: the
# flags as set here, not a test binary's own additions.
SWEEP_CPP := $(CC) -E $(CFLAGS)
# After each link: the binary depends on every header its sources include
# (build/<test>.d), so a firmware header change rebuilds the tests that use
# it. Not -MMD: with several sources in one gcc call, the last source's
# dependencies overwrite the others' (a stale crash.h struct passed quietly).
DEPEND   = $(CC) $(CFLAGS) -MM -MP -MT $@ $(filter %.c,$^) > $@.d

BUILDDIR = build

# One binary per subsystem under test.
TESTS = $(BUILDDIR)/test_cmd_editor \
        $(BUILDDIR)/test_om \
        $(BUILDDIR)/nohelp/test_om \
        $(BUILDDIR)/test_om_gen \
        $(BUILDDIR)/test_sfp_cal \
        $(BUILDDIR)/test_om_codec \
        $(BUILDDIR)/test_fwupdate \
        $(BUILDDIR)/test_ui_keys \
        $(BUILDDIR)/test_port_tables \
        $(BUILDDIR)/test_httpd_tx \
        $(BUILDDIR)/test_httpd_tx_compact \
        $(BUILDDIR)/test_tick_gate \
        $(BUILDDIR)/test_timer \
        $(BUILDDIR)/test_ntp \
        $(BUILDDIR)/test_cpu_vlan_full \
        $(BUILDDIR)/test_cpu_vlan_restricted \
        $(BUILDDIR)/test_cpu_vlan_8372 \
        $(BUILDDIR)/test_nic_rx \
        $(BUILDDIR)/test_lacp \
        $(BUILDDIR)/test_flash_dev \
        $(BUILDDIR)/test_update_apply \
        $(BUILDDIR)/test_update_apply_compact \
        $(BUILDDIR)/test_mdata_migrate \
        $(BUILDDIR)/test_sched_gen \
        $(BUILDDIR)/test_sched \
        $(BUILDDIR)/test_resource_gate \
        $(BUILDDIR)/test_place_banks \
        $(BUILDDIR)/test_check_cross_bank \
        $(BUILDDIR)/test_loader

# The table tests link the register mock and one environment file; stub/
# stands in for the two headers a firmware build generates.
TABLE_ENV = support.c hw_mock.c env_tables.c $(SRCROOT)/cmd_editor.c \
            $(SRCROOT)/rtl837x_port.c

all: run

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# The object model's tables, from the firmware's schema (tools/om_gen.py).
# With state.sched's rows, as the firmware has them: here the scheduler
# generator's from test_sched.c's tasks (below).
OM_SCHEMA := $(wildcard $(SRCROOT)/schema/*.py) $(SRCROOT)/schema/registry.txt
SCHED_T = $(BUILDDIR)/sched_core
SCHED_OM = $(SCHED_T)/om/state_sched.py
$(BUILDDIR)/om/om_gen.c $(BUILDDIR)/om/om_gen.h &: $(OM_SCHEMA) $(SCHED_OM) $(SRCROOT)/tools/om_gen.py | $(BUILDDIR)
	python3 $(SRCROOT)/tools/om_gen.py -s $(SRCROOT)/schema -s $(SCHED_T)/om -o $(BUILDDIR)/om > /dev/null
$(filter-out $(BUILDDIR)/test_om_gen $(BUILDDIR)/test_sfp_cal,$(TESTS)): | $(BUILDDIR)/om/om_gen.h

# om.c, the engine, UNMODIFIED, over the real schema and om_schema/ (every
# kind and type no real object uses yet); the handlers are stubs in the test,
# but om_vlan.c's, om_lag.c's and om_stp.c's (the VLAN table, trunk, LACP and
# STP calls are).
OM_T_SCHEMA := $(wildcard om_schema/*.py) om_schema/registry.txt
# help's blob too (om.c reads it from the flash model): a room of its own, as
# the test schema's objects are extra; for the same reason no sector bound
# (--no-sector: the firmware's schema has its own, in build/om and
# test_om_codec's worst case). The compact layout's addresses: there a
# blob past HELP_END doesn't carry out of the low 16 bits (om.c h_addr()).
$(BUILDDIR)/om_t/om_gen.c $(BUILDDIR)/om_t/om_gen.h $(BUILDDIR)/om_t/help.bin &: $(OM_SCHEMA) $(SCHED_OM) $(OM_T_SCHEMA) $(SRCROOT)/tools/om_gen.py | $(BUILDDIR)
	python3 $(SRCROOT)/tools/om_gen.py -s $(SRCROOT)/schema -s $(SCHED_T)/om -s om_schema --no-sector -o $(BUILDDIR)/om_t \
		--help-blob $(BUILDDIR)/om_t/help.bin --help-room 8192 > /dev/null
$(BUILDDIR)/test_om $(BUILDDIR)/nohelp/test_om: CFLAGS := $(subst -I$(BUILDDIR)/om,-I$(BUILDDIR)/om_t,$(CFLAGS)) -DLAYOUT_COMPACT
OM_T_SRCS = test_om.c support.c flash_dev.c crc16_host.c $(SRCROOT)/cmd_editor.c $(SRCROOT)/om.c $(SRCROOT)/om_vlan.c $(SRCROOT)/om_lag.c $(SRCROOT)/om_stp.c $(BUILDDIR)/om_t/om_gen.c
$(BUILDDIR)/test_om: $(OM_T_SRCS) $(BUILDDIR)/om_t/help.bin | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OM_T_SRCS) -o $@ $(LDFLAGS)
	$(CC) $(CFLAGS) -MM -MP -MT $@ $(OM_T_SRCS) > $@.d
# The same with OM_HELP=0, in its own directory (test_om leaves its sectors there)
$(BUILDDIR)/nohelp/test_om: $(OM_T_SRCS) | $(BUILDDIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DOM_HELP=0 $(OM_T_SRCS) -o $@ $(LDFLAGS)
	$(CC) $(CFLAGS) -DOM_HELP=0 -MM -MP -MT $@ $(OM_T_SRCS) > $@.d

# The Python codec (tools/rtlpg_client.py) against om.c's: test_om leaves its
# saved sectors and their text in build/, so it runs after test_om passed.
$(BUILDDIR)/test_om_codec: test_om_codec.py $(SRCROOT)/tools/rtlpg_client.py $(SRCROOT)/tools/legacy_map.py \
		$(SRCROOT)/tools/om_gen.py $(OM_SCHEMA) $(OM_T_SCHEMA) $(BUILDDIR)/test_om.ok | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_om_codec.py"\n' "$(CURDIR)" > $@ && chmod +x $@

# The generator: broken schemas fail with their message, the registry holds.
# tools/fwupdate.py's upgrade flow against a fake switch (old firmware, then
# the object model)
$(BUILDDIR)/test_ui_keys: test_ui_keys.py $(SRCROOT)/html/app.js $(SRCROOT)/html/index.html | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_ui_keys.py"\n' "$(CURDIR)" > $@ && chmod +x $@

$(BUILDDIR)/test_fwupdate: test_fwupdate.py $(SRCROOT)/tools/fwupdate.py $(SRCROOT)/tools/rtlpg_client.py \
		$(SRCROOT)/tools/legacy_map.py $(SRCROOT)/tools/om_gen.py $(OM_SCHEMA) | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_fwupdate.py"\n' "$(CURDIR)" > $@ && chmod +x $@

# The bank placement (tools/place_banks.py) and its check on the linked
# image (tools/check_cross_bank.py), on made-up .rel files and listings
$(BUILDDIR)/test_place_banks: test_place_banks.py $(SRCROOT)/tools/place_banks.py | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_place_banks.py"\n' "$(CURDIR)" > $@ && chmod +x $@
$(BUILDDIR)/test_check_cross_bank: test_check_cross_bank.py $(SRCROOT)/tools/check_cross_bank.py | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_check_cross_bank.py"\n' "$(CURDIR)" > $@ && chmod +x $@

$(BUILDDIR)/test_om_gen: test_om_gen.py $(SRCROOT)/tools/om_gen.py | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_om_gen.py"\n' "$(CURDIR)" > $@ && chmod +x $@
# The web UI's SFP diagnostics math (html/app.js) in node, against known
# SFF-8472 calibration constants
$(BUILDDIR)/test_sfp_cal: test_sfp_cal.py $(SRCROOT)/html/app.js | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_sfp_cal.py"\n' "$(CURDIR)" > $@ && chmod +x $@

# cmd_editor.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_cmd_editor: test_cmd_editor.c support.c $(SRCROOT)/cmd_editor.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# rtl837x_port.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_port_tables: test_port_tables.c $(TABLE_ENV) hw_mock.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

$(BUILDDIR)/test_tick_gate: test_tick_gate.c support.c $(SRCROOT)/cmd_editor.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# timer.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_timer: test_timer.c support.c $(SRCROOT)/cmd_editor.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# state.ntp's getter, the real ntp.c (its statics: the DST rules, the date split)
$(BUILDDIR)/test_ntp: test_ntp.c $(SRCROOT)/ntp.c | $(BUILDDIR)
	$(CC) $(CFLAGS) test_ntp.c -o $@ $(LDFLAGS)
	$(DEPEND)

# Exercise the production RX ring reader; only the hardware DMA edge is mocked.
# Discard unused TX functions so this RX fixture needs no TX register mocks.
$(BUILDDIR)/test_nic_rx: test_nic_rx.c $(SRCROOT)/rtl837x_nic.c $(SRCROOT)/rtl837x_nic.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(filter %.c,$^) -o $@ $(LDFLAGS) -Wl,--gc-sections
	$(DEPEND)

# rtl837x_lacp.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_lacp: test_lacp.c $(SRCROOT)/rtl837x_lacp.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

$(BUILDDIR)/test_loader: test_loader.c $(SRCROOT)/loader/loader.c $(SRCROOT)/loader/net.c $(SRCROOT)/loader/loader.h $(SRCROOT)/loader/layout.h | $(BUILDDIR)
	$(CC) $(CFLAGS) test_loader.c -o $@ $(LDFLAGS)
	$(DEPEND)

# httpd/httpd.c and uip/uip.c are compiled UNMODIFIED from the firmware tree;
# this test carries the environment below them itself.
# Built for both layouts: /loader differs (a compact board without a loader
# is converted, a standard one refuses).
$(BUILDDIR)/test_httpd_tx_compact: CFLAGS += -DLAYOUT_COMPACT
$(BUILDDIR)/test_httpd_tx $(BUILDDIR)/test_httpd_tx_compact: test_httpd_tx.c $(SRCROOT)/httpd/httpd.c $(SRCROOT)/uip/uip.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)
# the login sessions httpd.c calls into
$(BUILDDIR)/test_httpd_tx $(BUILDDIR)/test_httpd_tx_compact: $(SRCROOT)/sessions.c

# CPU VLAN initialization with full and reduced user-port ranges.
$(BUILDDIR)/test_cpu_vlan_restricted: CFLAGS += -DMACHINE_MAX_PORT=7
$(BUILDDIR)/test_cpu_vlan_8372: CFLAGS += -DMACHINE_MAX_PORT=7 -DMACHINE_MIN_PORT=3 -DRTLP_TEST_IS8373=0
$(BUILDDIR)/test_cpu_vlan_full $(BUILDDIR)/test_cpu_vlan_restricted $(BUILDDIR)/test_cpu_vlan_8372: test_cpu_vlan.c $(TABLE_ENV) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# The flash device model (pure, no firmware symbols) and its self-test.
$(BUILDDIR)/test_flash_dev: test_flash_dev.c flash_dev.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# The real boot-time apply (update_apply.c, UNMODIFIED from the firmware tree)
# run against the modelled flash device - built for both flash layouts.
$(BUILDDIR)/test_update_apply_compact: CFLAGS += -DLAYOUT_COMPACT

# The one-time machine data move (compact layout) against the modelled flash.
$(BUILDDIR)/test_mdata_migrate: CFLAGS += -DLAYOUT_COMPACT
$(BUILDDIR)/test_mdata_migrate: test_mdata_migrate.c update_shim.c crc16_host.c flash_dev.c $(SRCROOT)/update_apply.c $(SRCROOT)/boot0.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)
$(BUILDDIR)/test_update_apply $(BUILDDIR)/test_update_apply_compact: test_update_apply.c update_shim.c crc16_host.c flash_dev.c $(SRCROOT)/update_apply.c $(SRCROOT)/boot0.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# The scheduler table generator (tools/sched_gen.py) is tested in Python; its
# "binary" is a wrapper, so it runs and logs like the others.
$(BUILDDIR)/test_sched_gen: test_sched_gen.py $(SRCROOT)/tools/sched_gen.py $(SRCROOT)/tools/sched_sweep.py $(SRCROOT)/sched.h \
		$(wildcard sched_gen/*/*) | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_sched_gen.py"\n' "$(CURDIR)" > $@ && chmod +x $@

# The resource gate (tools/resource_gate.py) on fixtures, then on the 8x
# schedtest and firmware builds against their baselines, when they exist.
GATE_BUILDS = $(wildcard $(SRCROOT)/output/KEEPLINK_KP_9000_9XHPML_V1_2-compact/rtlplayground.mem \
	$(SRCROOT)/output/KEEPLINK_KP_9000_9XHPML_V1_2-compact-schedtest/rtlplayground.mem)
$(BUILDDIR)/test_resource_gate: test_resource_gate.py $(wildcard gate/*) $(SRCROOT)/tools/resource_gate.py \
		$(SRCROOT)/tools/resource_report.py $(wildcard $(SRCROOT)/doc/sched/baseline/gate-*.json) $(GATE_BUILDS) | $(BUILDDIR)
	printf '#!/bin/sh\nexec python3 "%s/test_resource_gate.py"\n' "$(CURDIR)" > $@ && chmod +x $@

# The scheduler core, rings, watchdog and crash record, compiled UNMODIFIED,
# over the tables the generator makes from test_sched.c's TASK/STEP lines
# (swept with the host's preprocessor, as sched.mk does with SDCC's) and
# sched_core/sequence.def.
$(SCHED_T)/sched_gen.c $(SCHED_T)/sched_gen_names.c $(SCHED_T)/sched_gen.h $(SCHED_T)/sched_gen_tasks.c $(SCHED_T)/om/state_sched.py &: test_sched.c sched_core/hw.h sched_core/sequence.def \
		$(SRCROOT)/sched.h $(SRCROOT)/tools/sched_sweep.py $(SRCROOT)/tools/sched_gen.py | $(BUILDDIR)
	python3 $(SRCROOT)/tools/sched_sweep.py -o $(SCHED_T) --cpp '$(SWEEP_CPP) -include sched_core/hw.h' test_sched.c >/dev/null
	python3 $(SRCROOT)/tools/sched_gen.py --om $(SCHED_T)/om --slots 8 --sequence sched_core/sequence.def -o $(SCHED_T) $(SCHED_T)/test_sched.def >/dev/null
$(BUILDDIR)/test_sched: CFLAGS += -I$(SCHED_T) -include sched_core/hw.h \
	'-DCRASH_RESET()=host_reset()' '-DCRASH_JUMP()=crash_die()' '-DCRASH_IDATA(a)=host_idata[(uint8_t)(a)]'
$(BUILDDIR)/test_sched: test_sched.c $(SRCROOT)/sched.c $(SRCROOT)/sched_bank.c $(SRCROOT)/con_ring.c $(SRCROOT)/crash.c $(SRCROOT)/sched_health.c \
		$(SCHED_T)/sched_gen.c $(SCHED_T)/sched_gen_names.c $(SCHED_T)/sched_gen_tasks.c | $(SCHED_T)/sched_gen.h $(BUILDDIR)/om/om_gen.h
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
	$(DEPEND)

# Each test runs into its own log, so `make -j` runs them side by side; the
# logs are printed in order once all are done. A test's .ok marker is only
# written when it passes, so a rerun repeats exactly the failed or changed ones.
$(BUILDDIR)/%.ok: $(BUILDDIR)/%
	@if $< > $<.log 2>&1; then touch $@; else rm -f $@; fi

run: $(TESTS:=.ok)
run:
	@fail=0; for t in $(TESTS); do \
		echo "=== $$t ==="; cat $$t.log; echo; \
		[ -e $$t.ok ] || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; else echo "ALL TESTS PASSED"; fi

# Everything but the slow loader simulation (~3-7 min), for quick iteration.
QUICK = $(filter-out $(BUILDDIR)/test_loader,$(TESTS))
quick: $(QUICK:=.ok)
	@fail=0; for t in $(QUICK); do [ -e $$t.ok ] || { echo "=== $$t ==="; cat $$t.log; fail=1; }; done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; else echo "QUICK TESTS PASSED (test_loader skipped)"; fi

-include $(wildcard $(BUILDDIR)/*.d $(BUILDDIR)/nohelp/*.d)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all run quick clean
