package main

// A DNS lookup is a liveness hint, not proof that the entire AWG tunnel failed.
// Require three failed checks without incoming authenticated peer traffic.
// A valid DNS response or fresh peer traffic restores availability immediately.
type tunnelHealth struct {
	lastRx      uint64
	failures    int
	unavailable bool
}

func (h *tunnelHealth) observe(dnsOK bool, peerRx uint64) (changed, received bool) {
	received = peerRx > h.lastRx
	h.lastRx = peerRx
	previous := h.unavailable
	if dnsOK || received {
		h.failures = 0
		h.unavailable = false
	} else {
		if h.failures < 3 {
			h.failures++
		}
		h.unavailable = h.failures >= 3
	}
	return previous != h.unavailable, received
}
