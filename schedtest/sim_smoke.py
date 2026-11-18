#!/usr/bin/env python3
"""
sim_smoke.py - boot the schedtest image in rtlsim and run its commands once.

  schedtest/sim_smoke.py BUILDDIR [OVS_RTLPG]

BUILDDIR is a schedtest build (output/<MACHINE>-compact-schedtest);
OVS_RTLPG the ovs-rtlpg checkout (default ~/src/ovs-rtlpg), whose
tests/rtlsim_ctl.py and build/rtlsim are used. Boots the -boot image (the
loader, then schedtest) to its prompt, then health, status, print 20, jobs,
flash, slow, a paste, and health again, printing the console transcript, a
few symbols, and the simulator's checks. A smoke test, not B4's suite.
"""
import glob, os, sys, time

PROMPT = r"\n> $"


def main(argv):
    if not argv:
        sys.exit(__doc__)
    build = argv[0]
    ovs = argv[1] if len(argv) > 1 else os.path.expanduser("~/src/ovs-rtlpg")
    sys.path.insert(0, os.path.join(ovs, "tests"))
    from rtlsim_ctl import Sim, fmt_us

    image = sorted(glob.glob(os.path.join(build, "rtlplayground-boot-schedtest*.bin")), key=os.path.getmtime)[-1:]
    if not image:
        sys.exit(f"sim_smoke: no -boot schedtest image in {build}")
    board = os.path.join(ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    t0 = time.time()
    with Sim(image[0], board=board, map=os.path.join(build, "rtlplayground.map"),
             rtlsim=os.path.join(ovs, "build", "rtlsim")) as sim:
        out = []

        def cmd(line, ms=3000, until=PROMPT):
            out.append(sim.uart_text())		# take what came before, so the wait sees only the reply
            sim.uart_write(line + "\r")
            if sim.run_until_uart(until, max_ms=ms) is None:
                out.append(f"--- nothing matching {until!r} after {line!r} within {ms} ms\n")

        t = sim.run_until_uart(PROMPT, max_ms=10000)
        print(f"=== boot: prompt at {fmt_us(t) if t is not None else 'NEVER'} ms firmware time")
        if t is None:
            print(sim.uart_text())
            return 1
        cmd("health")
        cmd("status")
        cmd("print 20", until=r"print done\n")
        cmd("jobs")
        sim.run(200)				# the jobs end, freeing their slots
        cmd("flash", until=r"flash: (ok|verify FAILED|no free slot)\n")
        cmd("slow")
        sim.run(100)
        cmd("x" * 200)				# a paste: one long line, at the UART's rate
        sim.run(1000)
        cmd("status")
        cmd("health")
        out.append(sim.uart_text())
        print("".join(out))
        print("=== symbols")
        for name in ("st_jobs_claimed", "st_jobs_full", "st_jobs_done", "st_fl_state", "st_fl_bad",
                     "st_slow_runs", "sched_overruns", "sched_overrun_state", "st_con_rx_n",
                     "con_rx_overruns", "st_sp_step_max", "sched_fine_per_tick"):
            size = 1 if name in ("st_fl_state", "sched_overrun_state", "st_sp_step_max") else 2
            print(f"  {name} = {int.from_bytes(sim.peek_sym(name, size), 'little')}")
        log = sim.read_struct("st_log_sleep_a", [("n", "H"), ("gap_min", "H"), ("gap_max", "H")])
        print(f"  st_log_sleep_a: {log}")
        print("=== checks")
        for c in sim.checks():
            print(f"  {c}")
        print(f"=== firmware time {fmt_us(sim.time()[1])} ms, wall {time.time() - t0:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
