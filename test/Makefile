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
# -MMD -MP: each binary also depends on the headers it includes (build/*.d),
# so a firmware header change rebuilds the tests that use it.
CFLAGS   = -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unknown-pragmas -MMD -MP \
           -Wno-pointer-sign \
           -DRTLP_HOST_TEST \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -include sdcc_shim.h -Istub -I$(SRCROOT) -I$(SRCROOT)/httpd -I$(SRCROOT)/uip
ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS  += -Wno-builtin-declaration-mismatch
endif
LDFLAGS  = -fsanitize=address,undefined

BUILDDIR = build

# One binary per subsystem under test.
TESTS = $(BUILDDIR)/test_cmd_editor \
        $(BUILDDIR)/test_port_tables \
        $(BUILDDIR)/test_page_json \
        $(BUILDDIR)/test_httpd_tx \
        $(BUILDDIR)/test_httpd_tx_compact \
        $(BUILDDIR)/test_tick_gate \
        $(BUILDDIR)/test_timer \
        $(BUILDDIR)/test_cpu_vlan_full \
        $(BUILDDIR)/test_cpu_vlan_restricted \
        $(BUILDDIR)/test_cpu_vlan_8372 \
        $(BUILDDIR)/test_nic_rx \
        $(BUILDDIR)/test_lacp \
        $(BUILDDIR)/test_flash_dev \
        $(BUILDDIR)/test_update_apply \
        $(BUILDDIR)/test_update_apply_compact \
        $(BUILDDIR)/test_config_migrate \
        $(BUILDDIR)/test_mdata_migrate \
        $(BUILDDIR)/test_loader

# The table tests link the register mock and one environment file; stub/
# stands in for the two headers a firmware build generates.
TABLE_ENV = support.c hw_mock.c env_tables.c $(SRCROOT)/cmd_editor.c \
            $(SRCROOT)/rtl837x_port.c $(SRCROOT)/httpd/page_impl.c \
            $(SRCROOT)/httpd/html_out.c

all: run

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# cmd_editor.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_cmd_editor: test_cmd_editor.c support.c $(SRCROOT)/cmd_editor.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# rtl837x_port.c and httpd/page_impl.c are compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_port_tables: test_port_tables.c $(TABLE_ENV) hw_mock.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

$(BUILDDIR)/test_tick_gate: test_tick_gate.c support.c $(SRCROOT)/cmd_editor.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# timer.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_timer: test_timer.c support.c $(SRCROOT)/cmd_editor.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Exercise the production RX ring reader; only the hardware DMA edge is mocked.
# Discard unused TX functions so this RX fixture needs no TX register mocks.
$(BUILDDIR)/test_nic_rx: test_nic_rx.c $(SRCROOT)/rtl837x_nic.c $(SRCROOT)/rtl837x_nic.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(filter %.c,$^) -o $@ $(LDFLAGS) -Wl,--gc-sections

$(BUILDDIR)/test_page_json: test_page_json.c $(TABLE_ENV) hw_mock.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

# rtl837x_lacp.c is compiled UNMODIFIED from the firmware tree.
$(BUILDDIR)/test_lacp: test_lacp.c $(SRCROOT)/rtl837x_lacp.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/test_loader: test_loader.c $(SRCROOT)/loader/loader.c $(SRCROOT)/loader/net.c $(SRCROOT)/loader/loader.h $(SRCROOT)/loader/layout.h | $(BUILDDIR)
	$(CC) $(CFLAGS) test_loader.c -o $@ $(LDFLAGS)

# httpd/httpd.c and uip/uip.c are compiled UNMODIFIED from the firmware tree;
# this test carries the environment below them itself.
# Built for both layouts: /loader differs (a compact board without a loader
# is converted, a standard one refuses).
$(BUILDDIR)/test_httpd_tx_compact: CFLAGS += -DLAYOUT_COMPACT
$(BUILDDIR)/test_httpd_tx $(BUILDDIR)/test_httpd_tx_compact: test_httpd_tx.c $(SRCROOT)/httpd/httpd.c $(SRCROOT)/uip/uip.c $(SRCROOT)/timer.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
# the login sessions httpd.c calls into
$(BUILDDIR)/test_httpd_tx $(BUILDDIR)/test_httpd_tx_compact: $(SRCROOT)/sessions.c

# CPU VLAN initialization with full and reduced user-port ranges.
$(BUILDDIR)/test_cpu_vlan_restricted: CFLAGS += -DMACHINE_MAX_PORT=7
$(BUILDDIR)/test_cpu_vlan_8372: CFLAGS += -DMACHINE_MAX_PORT=7 -DMACHINE_MIN_PORT=3 -DRTLP_TEST_IS8373=0
$(BUILDDIR)/test_cpu_vlan_full $(BUILDDIR)/test_cpu_vlan_restricted $(BUILDDIR)/test_cpu_vlan_8372: test_cpu_vlan.c $(TABLE_ENV) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

# The flash device model (pure, no firmware symbols) and its self-test.
$(BUILDDIR)/test_flash_dev: test_flash_dev.c flash_dev.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# The real boot-time apply (update_apply.c, UNMODIFIED from the firmware tree)
# run against the modelled flash device - built for both flash layouts.
$(BUILDDIR)/test_update_apply_compact: CFLAGS += -DLAYOUT_COMPACT

# The one-time config move (compact layout) against the modelled flash.
$(BUILDDIR)/test_config_migrate $(BUILDDIR)/test_mdata_migrate: CFLAGS += -DLAYOUT_COMPACT
$(BUILDDIR)/test_mdata_migrate: test_mdata_migrate.c update_shim.c crc16_host.c flash_dev.c $(SRCROOT)/update_apply.c $(SRCROOT)/boot0.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
$(BUILDDIR)/test_config_migrate: test_config_migrate.c update_shim.c crc16_host.c flash_dev.c $(SRCROOT)/update_apply.c $(SRCROOT)/boot0.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
$(BUILDDIR)/test_update_apply $(BUILDDIR)/test_update_apply_compact: test_update_apply.c update_shim.c crc16_host.c flash_dev.c $(SRCROOT)/update_apply.c $(SRCROOT)/boot0.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

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

-include $(wildcard $(BUILDDIR)/*.d)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all run quick clean
