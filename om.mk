# om.mk - the object model (~/rtlpg-bench/object-model/DESIGN.md): its
# engine and handlers, and the tables generated from schema/. Included by the
# Makefile after sched.mk. The generator needs no SDCC: it reads the schema
# and the ID registry (schema/registry.txt) and fails while an object or
# field has no ID (tools/om_gen.py --register adds them; commit the result).

OM_GEN = $(BUILDDIR)/om
OM_SCHEMA = $(wildcard schema/*.py) schema/registry.txt
CC_FLAGS += -I$(OM_GEN)
OM_SRCS = om.c om_port.c om_gen.c om_fail.c
SRCS += $(OM_SRCS)
RESIDENT_SRCS += om_fail.c	# a handler's message is in the handler's bank
$(shell mkdir -p $(OM_GEN))

$(OM_GEN)/gen.stamp: $(OM_SCHEMA) tools/om_gen.py
	python3 tools/om_gen.py -s schema -o $(OM_GEN)
	touch $@
$(OM_GEN)/om_gen.h $(OM_GEN)/om_gen.c: $(OM_GEN)/gen.stamp ;
$(BUILDDIR)/om_gen.rel: $(OM_GEN)/om_gen.c $(BUILDDIR)/.ccflags | create_build_dir
	$(CC) -MMD $(CC_FLAGS) $(BANK_FLAGS) -o $@ -c $<
$(OBJS): | $(OM_GEN)/om_gen.h
-include $(patsubst %.c,$(BUILDDIR)/%.d,$(OM_SRCS))
