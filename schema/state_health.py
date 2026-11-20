# state.health, state.con (DESIGN.md 7.3), and do sched.clear. state.sched's
# own rows come from the scheduler's generator (sched.mk: tools/sched_gen.py
# --om writes them into the build).
#
# Deferred for room (P5-2), to return when the banks have it: state.web
# (sessions, tcp.{st,tmr,rtx,rto,len,mss,lport,rport,rip},
# session.1-4.idle_s; +1,055 B with its getter and the IPv4 type), the
# HEALTH=1 build's health output. state.health.rx_frames went with
# HEALTH=1 (it was counted only there).

state("health", get="om_get_health")
st("hw_timeouts", U16(), tip="Bounded hardware polls that gave up")
st("hw_timeout_reg", U16(), tip="The last one's register (65281: the flash's WEL)")
st("stack_headroom", U8(), tip="Bytes of the stack never used since the boot")

state("con", get="om_get_con")
st("rx_overruns", U16(), tip="Console bytes lost to a full ring")
st("rx_used", U8())
st("tx_free", U16())

obj("sched", order=0, tip="The scheduler")
action("clear", "om_do_sched_clear",
       tip="Zero the step statistics, overruns and hardware timeouts")
