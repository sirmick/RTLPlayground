# om.mk - the object model (~/rtlpg-bench/object-model/DESIGN.md): its
# engine and handlers, and the tables generated from schema/. Included by the
# Makefile after sched.mk. The generator needs no SDCC: it reads the schema
# and the ID registry (schema/registry.txt) and fails while an object or
# field has no ID (tools/om_gen.py --register adds them; commit the result).

OM_GEN = $(BUILDDIR)/om
# OM_SIM=1: rtlsim measurements, with test/om_sim's schema (a 64-entry keyed
# object); use its own BUILDDIR
# state.sched's rows come from the scheduler's generator (sched.mk, --om)
OM_DIRS = schema $(SCHED_GEN)/om $(if $(OM_SIM),test/om_sim)
OM_SCHEMA = $(foreach d,schema $(if $(OM_SIM),test/om_sim),$(wildcard $(d)/*.py) $(d)/registry.txt) \
	$(SCHED_GEN)/om/state_sched.py
CC_FLAGS += -I$(OM_GEN)
# schedtest has its own `health`: no state.sched, state.health or state.con
# (the scheduler's words stay, state.sys.reset's)
OM_SKIP = $(if $(SCHEDTEST),state_health.py) $(if $(filter 0,$(OM_DBG)),state_dbg.py)
OM_SRCS = om.c om_port.c om_vlan.c om_lag.c om_stp.c om_sys.c om_svc.c $(if $(SCHEDTEST),,om_health.c) \
	$(if $(filter 0,$(OM_DBG)),,om_dbg.c) om_gen.c
SRCS += $(OM_SRCS)
$(shell mkdir -p $(OM_GEN))

$(OM_GEN)/gen.stamp: $(OM_SCHEMA) tools/om_gen.py
	python3 tools/om_gen.py $(addprefix -s ,$(OM_DIRS)) $(addprefix -x ,$(OM_SKIP)) -o $(OM_GEN) \
		$(if $(filter 1,$(OM_HELP)),--help-blob $(OM_GEN)/help.bin --help-room $$(($(HELP_END) - $(HELP_LOCATION))))
	touch $@
$(OM_GEN)/om_gen.h $(OM_GEN)/om_gen.c: $(OM_GEN)/gen.stamp ;
$(BUILDDIR)/om_gen.rel: $(OM_GEN)/om_gen.c $(BUILDDIR)/.ccflags | create_build_dir
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<
$(OBJS): | $(OM_GEN)/om_gen.h
-include $(patsubst %.c,$(BUILDDIR)/%.d,$(OM_SRCS))
