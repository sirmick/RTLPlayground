# schedtest.mk - the scheduler test image (doc/sched/testing.md, layer 2;
# schedtest/README.md). Included by the Makefile when SCHEDTEST is set, which
# `make schedtest` does. The whole firmware is linked as usual, but for its
# tasks (tasks.c); SCHEDTEST switches rtlplayground.c's main() to the dummies.

ST_DIR = schedtest
SCHED_DEFS = $(ST_DIR)/st_main.def $(ST_DIR)/st_b1.def $(ST_DIR)/st_b2.def $(ST_DIR)/st_b3.def \
	$(if $(filter bad_spec,$(ST_NEG)),$(ST_DIR)/bad_spec.def)

CC_FLAGS += -DSCHEDTEST
ifneq ($(filter-out hang overrun bad_flash bad_spec,$(ST_NEG)),)
$(error ST_NEG is one of hang, overrun, bad_flash, bad_spec)
endif
ifneq ($(ST_NEG),)
CC_FLAGS += -DST_NEG_$(shell echo $(ST_NEG) | tr a-z A-Z)
endif
# ST_CRASH=8000: the crash record at xdata 0x8000 (silicon C), and `poke`
ifneq ($(ST_CRASH),)
CC_FLAGS += -DCRASH_ADDR=0x$(ST_CRASH) -DST_POKE
endif

# The scheduler itself comes from sched.mk, with these .def files. Banked,
# placed by place_banks.py: st_main.c, st_report.c. Pinned (listed as
# resident so place_banks.py takes their bank as given): one dummy file per
# bank.
# The firmware's console editor isn't linked: schedtest has its own con,
# and nothing else calls it (its tasks, tasks.c, are left out too).
SRCS := $(filter-out cmd_editor.c,$(SRCS))
ST_SRCS = $(ST_DIR)/st_main.c $(ST_DIR)/st_report.c $(ST_DIR)/st_b1.c $(ST_DIR)/st_b2.c $(ST_DIR)/st_b3.c
SRCS += $(ST_SRCS)
RESIDENT_SRCS += $(ST_DIR)/st_b1.c $(ST_DIR)/st_b2.c $(ST_DIR)/st_b3.c
$(BUILDDIR)/$(ST_DIR)/st_b1.rel: BANK_FLAGS = --codeseg BANK1 --constseg BANK1
$(BUILDDIR)/$(ST_DIR)/st_b2.rel: BANK_FLAGS = --codeseg BANK2 --constseg BANK2
$(BUILDDIR)/$(ST_DIR)/st_b3.rel: BANK_FLAGS = --codeseg BANK3 --constseg BANK3
$(shell mkdir -p $(BUILDDIR)/$(ST_DIR))

# The resource gate (doc/sched/memory.md), after the image: it fails the
# build on internal RAM growth, a short stack or a full common area.
ST_BASELINE = doc/sched/baseline/gate-schedtest.json
.PHONY: st-gate
all: st-gate
st-gate: $(OUTPUT)
	python3 tools/resource_gate.py $(BUILDDIR) --baseline $(ST_BASELINE)

# silicon.py with this build's constants (C): the symbol addresses, the
# states, sl_hang, the bank window's bytes, the version. Copy it to the bench.
all: $(BUILDDIR)/silicon.py
$(BUILDDIR)/silicon.py: $(BUILDDIR)/rtlplayground-boot-$(FILENAME_EXTENSION).bin $(ST_DIR)/silicon.py
	python3 $(ST_DIR)/silicon.py --embed $(BUILDDIR) > $@.tmp && chmod +x $@.tmp && mv $@.tmp $@
