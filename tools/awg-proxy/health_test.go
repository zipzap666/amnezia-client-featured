package main

import "testing"

func TestTunnelHealth(t *testing.T) {
	h := tunnelHealth{lastRx: 100}
	check := func(dnsOK bool, rx uint64, wantChanged, wantUnavailable bool) {
		t.Helper()
		changed, _ := h.observe(dnsOK, rx)
		if changed != wantChanged || h.unavailable != wantUnavailable {
			t.Fatalf("dnsOK=%t rx=%d: changed=%t unavailable=%t", dnsOK, rx, changed, h.unavailable)
		}
	}
	// The recorded incident: one failed probe, followed by a successful one.
	check(false, 100, false, false)
	check(true, 100, false, false)
	// A working tunnel must stay connected if just the probe's DNS is failing.
	for rx := uint64(101); rx < 110; rx++ {
		check(false, rx, false, false)
	}
	// A sustained blackhole is still reported; there is no direct fallback.
	check(false, 109, false, false)
	check(false, 109, false, false)
	check(false, 109, true, true)
	check(false, 109, false, true)
	// Fresh authenticated traffic alone proves the peer is reachable again.
	check(false, 110, true, false)
	check(false, 110, false, false)
	check(false, 110, false, false)
	check(false, 110, true, true)
	check(true, 110, true, false)
}
