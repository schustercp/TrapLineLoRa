# Ping-pong bring-up sketch

The first firmware written for this project. Two boards transmit a counter and
answer each other, printing RSSI and SNR over serial and BLE.

It did its job: it proved the radio, the pin map, the TCXO voltage and the RF
switch configuration all work. It is **not** the survey tool — it has no packet
error rate, no path-loss maths, no CSV, and it only tests one spreading factor
at a time. Use the survey firmware in `src/` for Phase 0.

Kept here because a dead-simple two-board link test is the right thing to reach
for when a new board will not talk at all.

To build it, point `build_src_filter` at this directory in a scratch
environment — it is deliberately not part of the default build.
