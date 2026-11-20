CC      ?= cc
SRCROOT  = ../..
CFLAGS   = -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unknown-pragmas \
           -Wno-pointer-sign \
           -fpack-struct=1 \
           -DRTLP_HOST_TEST \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -include ../sdcc_shim.h -I../stub -I$(SRCROOT) -I$(SRCROOT)/httpd -I$(SRCROOT)/uip -Ibuild/om
ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS  += -Wno-builtin-declaration-mismatch
endif
LDFLAGS  = -fsanitize=address,undefined

SRCS = stp_sim.c $(SRCROOT)/rtl837x_stp.c $(SRCROOT)/timer.c

all: build/stp_sim
	./build/stp_sim

verbose: build/stp_sim
	./build/stp_sim -v

# the object model's tables (om_get_stp's state structs), state.sched's rows
# from test/'s scheduler build, as test_om has them
SCHED_OM = ../build/sched_core/om/state_sched.py
$(SCHED_OM):
	$(MAKE) -C .. build/sched_core/om/state_sched.py
build/om/om_gen.h: $(wildcard $(SRCROOT)/schema/*.py) $(SRCROOT)/schema/registry.txt $(SRCROOT)/tools/om_gen.py $(SCHED_OM)
	mkdir -p build/om
	python3 $(SRCROOT)/tools/om_gen.py -s $(SRCROOT)/schema -s ../build/sched_core/om -o build/om > /dev/null

build/stp_sim: $(SRCS) $(SRCROOT)/rtl837x_stp.h build/om/om_gen.h
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -rf build

.PHONY: all verbose clean
