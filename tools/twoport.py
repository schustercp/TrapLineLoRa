#!/usr/bin/env python3
"""
Drive both survey radios at once and write a transcript Claude can read.

WHY THIS EXISTS
---------------
The Cowork bridge reaches this Mac's *files*, not its *devices*: it runs in a
Linux VM with only the project folder mounted, so /dev/cu.usbmodem* does not
exist there and no permission grant can conjure it. This script closes that gap
from the other side. You run it in Terminal; it drives both units and writes a
timestamped transcript into logs/, which IS visible across the bridge.

Side benefit: a scripted run is repeatable. Typing the §4-§6 procedure by hand
into two phones gets the timing subtly different every time, and the delivery
rate is sensitive to exactly that.

USAGE
-----
    ./tools/twoport.py check          # who are they, and are they alive
    ./tools/twoport.py desk           # the full desk procedure, both directions
    ./tools/twoport.py noise          # noise floor on both, antenna on vs off
    ./tools/twoport.py dump           # pull recorded marks -> logs/survey-*.csv
    ./tools/twoport.py cmd status     # send one command to both units
    ./tools/twoport.py cmd --a "role beacon" --b "role listen"

`dump` and `cmd` work with ONE unit connected, which is how you come back from
a range walk. `check`, `desk` and `noise` need two.

Ports are found automatically by Seeed's USB VID (0x2886). Override with
--port-a / --port-b if you have more than two XIAOs plugged in.

Everything lands in logs/twoport-<timestamp>.log, and logs/twoport-latest.log
is a copy of the most recent run. Any CSV the units emit is also split out into
logs/survey-<timestamp>.csv.

NOTE ON BAUD: this opens at 115200 and never at 1200. Opening a XIAO's port at
1200 baud is the magic handshake that reboots it into the UF2 bootloader, which
would end the run and look like a crash.
"""

import argparse
import os
import queue
import re
import sys
import threading
import time

# pyserial ships inside PlatformIO's virtualenv, so there is nothing to install
# -- but the system python3 usually cannot see it. Re-exec into that interpreter
# rather than making the user think about it.
try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    _pio = os.path.expanduser("~/.platformio/penv/bin/python")
    if os.path.exists(_pio) and os.environ.get("_TWOPORT_REEXEC") != "1":
        os.environ["_TWOPORT_REEXEC"] = "1"
        os.execv(_pio, [_pio] + sys.argv)
    sys.exit(
        "pyserial not found.\n"
        "  Install it:  pip3 install --user pyserial\n"
        "  or run with: ~/.platformio/penv/bin/python tools/twoport.py ..."
    )

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGDIR = os.path.join(REPO, "logs")
SEEED_VID = 0x2886
BAUD = 115200

# PID -> what firmware is on it. 0x80xx is the bootloader; see docs/bring-up.md §1b.
PIDS = {
    0x0044: "survey (plain XIAO nRF52840)",
    0x0045: "survey_sense (XIAO nRF52840 Sense)",
    0x8044: "BOOTLOADER, plain board -- not running firmware",
    0x8045: "BOOTLOADER, Sense board -- not running firmware",
}


def find_ports():
    hits = [p for p in list_ports.comports() if p.vid == SEEED_VID]
    hits.sort(key=lambda p: p.device)
    return hits


class Unit:
    """One radio: a serial port, a reader thread, and a label for the log."""

    def __init__(self, label, port, sink):
        self.label = label
        self.port = port
        self.sink = sink
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.lines = []
        self._stop = threading.Event()
        self._buf = ""
        self._t = threading.Thread(target=self._read, daemon=True)
        self._t.start()

    def _read(self):
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(256).decode("utf-8", "replace")
            except Exception as e:
                self.sink(self.label, "<serial error: %s>" % e)
                return
            if not chunk:
                continue
            self._buf += chunk
            while "\n" in self._buf:
                line, self._buf = self._buf.split("\n", 1)
                line = line.rstrip("\r")
                self.lines.append(line)
                self.sink(self.label, line)

    def send(self, cmd):
        self.sink(self.label, ">>> " + cmd)
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()

    def close(self):
        self._stop.set()
        time.sleep(0.2)
        try:
            self.ser.close()
        except Exception:
            pass


class Log:
    def __init__(self):
        os.makedirs(LOGDIR, exist_ok=True)
        self.stamp = time.strftime("%Y%m%d-%H%M%S")
        self.path = os.path.join(LOGDIR, "twoport-%s.log" % self.stamp)
        self.fh = open(self.path, "w")
        self.lock = threading.Lock()

    def line(self, label, text):
        s = "%s [%s] %s" % (time.strftime("%H:%M:%S"), label, text)
        with self.lock:
            print(s, flush=True)
            self.fh.write(s + "\n")
            self.fh.flush()

    def note(self, text):
        self.line("--", text)

    def finish(self, units):
        self.fh.close()
        latest = os.path.join(LOGDIR, "twoport-latest.log")
        with open(self.path) as src, open(latest, "w") as dst:
            dst.write(src.read())
        # Pull any CSV the units emitted into a real file.
        rows = []
        for u in units:
            grab = False
            for ln in u.lines:
                if ln.startswith("label,uptime_s"):
                    if not rows:
                        rows.append(ln)
                    grab = True
                    continue
                if grab:
                    if re.match(r"^\(\d+ marks?\)", ln) or not ln.strip():
                        grab = False
                        continue
                    rows.append(ln)
        if len(rows) > 1:
            csv = os.path.join(LOGDIR, "survey-%s.csv" % self.stamp)
            with open(csv, "w") as f:
                f.write("\n".join(rows) + "\n")
            print("\nCSV written: %s  (%d rows)" % (csv, len(rows) - 1))
        print("transcript:  %s" % self.path)
        print("             %s" % latest)


def settle(log, seconds, why):
    """Wait, saying why and counting down, and notice if the Mac slept.

    A run went from '90 s to go' straight to eight minutes later, because the
    laptop suspended mid-measurement. Wall-clock waiting caught up instantly on
    wake and said nothing, so the log looked like a normal 130 s settle that had
    somehow gathered 253 packets. Sleep is prevented below, and detected here in
    case it happens anyway.
    """
    log.note("waiting %d s -- %s" % (seconds, why))
    end = time.time() + seconds
    last = time.time()
    slept = 0.0
    while time.time() < end:
        now = time.time()
        if now - last > 5.0:          # a 1 s loop cannot legitimately jump 5 s
            slept += now - last
        last = now
        left = int(end - now)
        if left % 30 == 0 and left > 0:
            log.note("  %d s to go" % left)
        time.sleep(1)
    if slept > 5.0:
        log.note("!! host appears to have SLEPT for ~%d s during this wait." % slept)
        log.note("!! The beacon kept running, so the packet count covers a much")
        log.note("!! longer window than %d s. Treat this measurement with care." % seconds)


def keep_awake(log):
    """Stop macOS suspending mid-run. Dies with us; no cleanup needed."""
    if sys.platform != "darwin":
        return None
    try:
        import subprocess
        p = subprocess.Popen(["caffeinate", "-i", "-w", str(os.getpid())])
        log.note("caffeinate active — display may sleep, the machine will not")
        return p
    except Exception as e:
        log.note("could not start caffeinate (%s); avoid letting this Mac sleep" % e)
        return None


# ------------------------------------------------------------- procedures ---

NEEDS_TWO = {"check", "desk", "noise"}


def proc_check(a, b, log, args):
    for u in (a, b):
        u.send("id")
    time.sleep(1.5)
    # Compare these. Two units on different builds behave differently in ways
    # that look like hardware faults; it has already happened once.
    for u in (a, b):
        u.send("ver")
    time.sleep(1.5)
    for u in (a, b):
        u.send("status")
    time.sleep(2.5)
    va = [l for l in a.lines if l.startswith("build ")]
    vb = [l for l in b.lines if l.startswith("build ")]
    if va and vb and va[-1] != vb[-1]:
        log.note("")
        log.note("!! MISMATCHED FIRMWARE — A is %r, B is %r" % (va[-1], vb[-1]))
        log.note("!! Reflash both before trusting any measurement.")
    elif va and vb:
        log.note("both units on %s" % va[-1])


def proc_noise(a, b, log, args):
    log.note("Measuring with whatever is fitted right now.")
    # `role listen`, NOT `role idle`. Idle puts the SX1262 in standby, where
    # GetRssiInst rails at 0xFF and reports -127.5 dBm -- which looks like a
    # spectacularly quiet site instead of a non-measurement. This procedure
    # did exactly that on its first run.
    for u in (a, b):
        u.send("role listen")
    time.sleep(1.5)
    for u in (a, b):
        u.send("noise")
    # The slow window is one beacon interval + guard, and both run in sequence.
    time.sleep(12.0)
    for u in (a, b):
        u.send("status")
    time.sleep(3.0)
    log.note("")
    log.note("To calibrate the instrument floor, unplug the antenna from a unit")
    log.note("that is in `role idle` or `role listen` -- NEVER one in `role beacon`,")
    log.note("an open PA port can destroy it -- and run this again.")
    log.note("Bench reference 2026-08-22: -113 dBm bare, -89 dBm on a desk antenna.")


def proc_desk(a, b, log, args):
    log.note("Desk procedure -- docs/bring-up.md §4-§6, both directions.")
    log.note("Units should be 1-3 m apart with antennas FITTED on both.")

    for u in (a, b):
        u.send("power -9")
        time.sleep(0.4)
        u.send("antenna 2")
        time.sleep(0.4)
        u.send("sf 10")
        time.sleep(0.4)
    time.sleep(1.0)

    for direction, (tx, rx) in enumerate(((a, b), (b, a))):
        name = "%s->%s" % (tx.label, rx.label)
        log.note("")
        log.note("=== direction %d of 2: %s beacons, %s listens ===" % (direction + 1, tx.label, rx.label))
        # Role entry now resets the counters and measures the noise floor, so
        # this is a genuinely fresh measurement at each direction.
        tx.send("role beacon")
        time.sleep(0.5)
        rx.send("role listen")
        settle(log, args.settle, "need 50+ beacons at %d s spacing for a real delivery rate" % 2)

        rx.send("status")
        time.sleep(3.0)
        rx.send("mark desk-%s" % name)
        time.sleep(1.0)

    log.note("")
    log.note("=== marks ===")
    for u in (a, b):
        u.send("log")
        time.sleep(3.0)

    for u in (a, b):
        u.send("role idle")
    time.sleep(0.5)


def proc_cmd(a, b, log, args):
    sent = False
    if args.a:
        a.send(args.a)
        sent = True
    if args.b:
        b.send(args.b)
        sent = True
    if args.words:
        both = " ".join(args.words)
        for u in (a, b):
            u.send(both)
        sent = True
    if not sent:
        log.note("nothing to send -- give a command, or --a/--b")
    time.sleep(args.wait)


def proc_dump(a, b, log, args):
    """Pull recorded marks off whatever is connected.

    Marks live in RAM. A unit that lost power since the walk has none, and this
    will say so rather than writing a confidently empty CSV.
    """
    units = [u for u in (a, b) if u]
    log.note("pulling marks from %d unit(s)" % len(units))
    for u in units:
        u.send("log")

    # Wait for the "(N marks)" sentinel rather than guessing a duration — 48
    # marks is a lot of lines, and a fixed sleep either truncates or dawdles.
    deadline = time.time() + 30
    done = set()
    while time.time() < deadline and len(done) < len(units):
        for u in units:
            if u.label not in done and any(re.match(r"^\(\d+ marks?\)", l) for l in u.lines):
                done.add(u.label)
        time.sleep(0.3)

    for u in units:
        n = None
        for l in u.lines:
            m = re.match(r"^\((\d+) marks?\)", l)
            if m:
                n = int(m.group(1))
        if u.label not in done:
            log.note("!! %s did not finish dumping within 30 s" % u.label)
        elif not n:
            log.note("!! %s reports 0 marks." % u.label)
            log.note("!! Either nothing was recorded, or it lost power since the walk —")
            log.note("!! marks are held in RAM and do not survive a power cycle.")
        else:
            log.note("%s: %d mark(s) recovered" % (u.label, n))


PROCS = {"check": proc_check, "dump": proc_dump, "desk": proc_desk, "noise": proc_noise, "cmd": proc_cmd}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("proc", choices=sorted(PROCS), help="which procedure to run")
    ap.add_argument("words", nargs="*", help="for `cmd`: the command to send to both")
    ap.add_argument("--port-a")
    ap.add_argument("--port-b")
    ap.add_argument("--a", help="for `cmd`: send only to unit A")
    ap.add_argument("--b", help="for `cmd`: send only to unit B")
    ap.add_argument("--settle", type=int, default=130,
                    help="seconds to gather beacons per direction (default 130)")
    ap.add_argument("--wait", type=float, default=4.0,
                    help="for `cmd`: seconds to keep listening after sending")
    args = ap.parse_args()

    pa, pb = args.port_a, args.port_b
    # Scan only when we actually lack a port we need. An explicit --port-a for a
    # single-unit procedure must be honoured, not overridden by discovery.
    need_scan = (not pa) or (args.proc in NEEDS_TWO and not pb)
    if need_scan:
        found = find_ports()
        print("Seeed devices found:")
        for p in found:
            what = PIDS.get(p.pid, "unknown PID 0x%04x" % (p.pid or 0))
            print("  %-24s %s" % (p.device, what))
            if p.pid in (0x8044, 0x8045):
                print("      ^ in the bootloader, not running firmware. Reflash it.")
        if len(found) == 0:
            sys.exit("\nNo XIAO found. Check the cable carries data, not just power.")
        if len(found) > 2 and not (pa and pb):
            sys.exit("\nMore than two found -- name them with --port-a and --port-b.")
        if len(found) == 1:
            # One board is the normal case coming back from a range walk: you
            # carried one and left the other in the garden.
            if args.proc in NEEDS_TWO:
                sys.exit("\n`%s` needs two units; only one is connected." % args.proc)
            pa, pb = found[0].device, None
            print("\none unit connected — running %s against it alone\n" % args.proc)
        else:
            pa, pb = found[0].device, found[1].device
            print()

    log = Log()
    keep_awake(log)
    log.note("A = %s" % pa)
    log.note("B = %s" % (pb or "(none)"))
    log.note("procedure: %s" % args.proc)

    a = b = None
    try:
        a = Unit("A", pa, log.line)
        b = Unit("B", pb, log.line) if pb else None
        # Opening the port raises DTR, which is what makes the firmware
        # re-announce itself. Give it a moment to land before commanding.
        time.sleep(2.0)
        PROCS[args.proc](a, b, log, args)
    except KeyboardInterrupt:
        log.note("interrupted")
    finally:
        units = [u for u in (a, b) if u]
        for u in units:
            u.close()
        log.finish(units)


if __name__ == "__main__":
    main()
