# loopflags.mk - SDCC's loop optimizations off where they cost code space.
# On mcs51, --noinduction (no induction variables) and --noinvariant (no
# hoisted loop invariants) make these files smaller: 1286 B over the three
# banks (196 / 540 / 550 B on the 8x), and 13 B less DSEG and OSEG
# (evidence/es-code-space.txt, section 3, the best switch per file; the
# files where a switch grows the code, httpd.c, uip.c, uip_arp.c, syslog.c,
# are not here). Their loops recompute what the optimizer would have kept:
# CLI, print and JSON paths, checked against the step limits
# (evidence/es-loop-flags.txt).
# sched_bank.c (-6 B) is left out: its measurement loops run around every
# step. Michael, 2026-11-19: the only code-space option taken.
LOOP_NOIND = cmd_parser uip/uiplib crypto/chacha20 cmd_editor
LOOP_NOINV = rtl837x_port sfp rtl837x_lacp rtl837x_rldp
LOOP_BOTH  = rtl837x_leds_dump dns

$(LOOP_NOIND:%=$(BUILDDIR)/%.rel): OBJ_FLAGS += --noinduction
$(LOOP_NOINV:%=$(BUILDDIR)/%.rel): OBJ_FLAGS += --noinvariant
$(LOOP_BOTH:%=$(BUILDDIR)/%.rel): OBJ_FLAGS += --noinduction --noinvariant
$(foreach f,$(LOOP_NOIND) $(LOOP_NOINV) $(LOOP_BOTH),$(BUILDDIR)/$(f).rel): loopflags.mk
