# sched.mk - the cooperative scheduler (doc/sched/) in every build: its
# sources, the tables generated from the tasks' .def files, the ISR frame
# check. Included by the Makefile after schedtest.mk. The firmware's tasks
# are tasks.def (tasks.c); schedtest.mk names its own dummies instead.

SCHED_GEN = $(BUILDDIR)/sched
SCHED_DEFS ?= tasks.def

# What the ISR prologues push before SWDT_ISR / CRASH_FROM_ISR read SP
# (crash.h); checked against the compiled rtlplayground.asm below.
ISR_T2_PUSHES = 8
ISR_SERIAL_PUSHES = 7
CC_FLAGS += -I$(SCHED_GEN) -DISR_T2_PUSHES=$(ISR_T2_PUSHES) -DISR_SERIAL_PUSHES=$(ISR_SERIAL_PUSHES)

# Resident: the core, the crash record, the dispatch tables. Banked, placed
# by place_banks.py: the rest of the scheduler, health and the names, the
# tasks' steps.
SCHED_SRCS = sched.c sched_bank.c sched_health.c crash.c sched_gen.c sched_gen_names.c \
	$(if $(SCHEDTEST),,tasks.c)
SRCS += $(SCHED_SRCS)
RESIDENT_SRCS += sched.c crash.c sched_gen.c
$(shell mkdir -p $(SCHED_GEN))

# The generated tables, from the .def files
$(SCHED_GEN)/sched_gen.h $(SCHED_GEN)/sched_gen.c $(SCHED_GEN)/sched_gen_names.c &: $(SCHED_DEFS) $(SCHED_DEFS:.def=.c) tools/sched_gen.py
	python3 tools/sched_gen.py -o $(SCHED_GEN) $(SCHED_DEFS) > $(SCHED_GEN)/sequence.txt || { cat $(SCHED_GEN)/sequence.txt; exit 1; }
	cat $(SCHED_GEN)/sequence.txt
	touch $(SCHED_GEN)/sched_gen.h $(SCHED_GEN)/sched_gen.c $(SCHED_GEN)/sched_gen_names.c
$(BUILDDIR)/sched_gen.rel: $(SCHED_GEN)/sched_gen.c $(BUILDDIR)/.ccflags | create_build_dir
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<
$(BUILDDIR)/sched_gen_names.rel: $(SCHED_GEN)/sched_gen_names.c $(BUILDDIR)/.ccflags | create_build_dir
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<
$(OBJS): | $(SCHED_GEN)/sched_gen.h
# The Makefile reads DEPS (:=) before this file adds to SRCS: read these
# objects' header dependencies here, or a regenerated sched_gen.h (a slot
# more) leaves them compiled against the old one.
-include $(patsubst %.c,$(BUILDDIR)/%.d,$(SCHED_SRCS) $(ST_SRCS))

# The PC in the crash record is only right if the push counts are
$(BUILDDIR)/isr_frame.ok: $(BUILDDIR)/rtlplayground.rel tools/isr_frame.py
	python3 tools/isr_frame.py $(BUILDDIR)/rtlplayground.asm \
		isr_timer2=$(ISR_T2_PUSHES) isr_serial=$(ISR_SERIAL_PUSHES)
	@touch $@
$(BUILDDIR)/placed.stamp: $(BUILDDIR)/isr_frame.ok
