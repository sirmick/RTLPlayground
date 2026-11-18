# schedtest.mk - the scheduler test image (doc/sched/testing.md, layer 2;
# schedtest/README.md). Included by the Makefile when SCHEDTEST is set, which
# `make schedtest` does. The whole firmware is linked as usual; SCHEDTEST
# switches rtlplayground.c's main() and its console ISRs to the scheduler's.

ST_DIR = schedtest
ST_GEN = $(BUILDDIR)/sched
ST_DEFS = $(ST_DIR)/st_main.def $(ST_DIR)/st_b1.def $(ST_DIR)/st_b2.def $(ST_DIR)/st_b3.def \
	$(if $(filter bad_spec,$(ST_NEG)),$(ST_DIR)/bad_spec.def)

# What the ISR prologues push before SWDT_ISR / CRASH_FROM_ISR read SP
# (crash.h); checked against the compiled rtlplayground.asm below.
ST_ISR_T2_PUSHES = 8
ST_ISR_SERIAL_PUSHES = 9

CC_FLAGS += -DSCHEDTEST -I$(ST_GEN) \
	-DST_ISR_T2_PUSHES=$(ST_ISR_T2_PUSHES) -DST_ISR_SERIAL_PUSHES=$(ST_ISR_SERIAL_PUSHES)
ifneq ($(filter-out hang overrun bad_flash bad_spec,$(ST_NEG)),)
$(error ST_NEG is one of hang, overrun, bad_flash, bad_spec)
endif
ifneq ($(ST_NEG),)
CC_FLAGS += -DST_NEG_$(shell echo $(ST_NEG) | tr a-z A-Z)
endif

# Resident: the core, the rings, the crash record, the dispatch tables.
# Banked, placed by place_banks.py: the rest of the scheduler, the names,
# st_main.c. Pinned (listed as resident so place_banks.py takes their bank
# as given): one dummy file per bank.
SRCS += sched.c sched_bank.c sched_health.c con_ring.c crash.c sched_gen.c sched_gen_names.c \
	$(ST_DIR)/st_main.c $(ST_DIR)/st_b1.c $(ST_DIR)/st_b2.c $(ST_DIR)/st_b3.c
RESIDENT_SRCS += sched.c con_ring.c crash.c sched_gen.c \
	$(ST_DIR)/st_b1.c $(ST_DIR)/st_b2.c $(ST_DIR)/st_b3.c
$(BUILDDIR)/$(ST_DIR)/st_b1.rel: BANK_FLAGS = --codeseg BANK1 --constseg BANK1
$(BUILDDIR)/$(ST_DIR)/st_b2.rel: BANK_FLAGS = --codeseg BANK2 --constseg BANK2
$(BUILDDIR)/$(ST_DIR)/st_b3.rel: BANK_FLAGS = --codeseg BANK3 --constseg BANK3
$(shell mkdir -p $(BUILDDIR)/$(ST_DIR) $(ST_GEN))

# The generated tables, from the dummies' .def files
$(ST_GEN)/sched_gen.h $(ST_GEN)/sched_gen.c $(ST_GEN)/sched_gen_names.c &: $(ST_DEFS) $(ST_DEFS:.def=.c) tools/sched_gen.py
	python3 tools/sched_gen.py -o $(ST_GEN) $(ST_DEFS) > $(ST_GEN)/sequence.txt || { cat $(ST_GEN)/sequence.txt; exit 1; }
	cat $(ST_GEN)/sequence.txt
	touch $(ST_GEN)/sched_gen.h $(ST_GEN)/sched_gen.c $(ST_GEN)/sched_gen_names.c
$(BUILDDIR)/sched_gen.rel: $(ST_GEN)/sched_gen.c $(BUILDDIR)/.ccflags | create_build_dir
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<
$(BUILDDIR)/sched_gen_names.rel: $(ST_GEN)/sched_gen_names.c $(BUILDDIR)/.ccflags | create_build_dir
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<
$(OBJS): | $(ST_GEN)/sched_gen.h

# The PC in the crash record is only right if the push counts are
$(BUILDDIR)/isr_frame.ok: $(BUILDDIR)/rtlplayground.rel tools/isr_frame.py
	python3 tools/isr_frame.py $(BUILDDIR)/rtlplayground.asm \
		isr_timer2=$(ST_ISR_T2_PUSHES) isr_serial=$(ST_ISR_SERIAL_PUSHES)
	@touch $@
$(BUILDDIR)/placed.stamp: $(BUILDDIR)/isr_frame.ok

# The resource report, after the image
.PHONY: st-report
all: st-report
st-report: $(OUTPUT)
	python3 tools/resource_report.py $(BUILDDIR)
