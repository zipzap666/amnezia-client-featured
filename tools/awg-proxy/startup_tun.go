package main

import (
	"github.com/amnezia-vpn/amneziawg-go/v3/tun"
	"sync"
)

// AWG's reader snapshots S4 before blocking in Read. NewDevice starts that
// reader before IpcSet can apply S4. Complete its first read without a packet
// after configuration so the first real packet uses the configured offset.
type startupTUN struct {
	tun.Device
	ready chan struct{}
	once  sync.Once
	first bool
}

func newStartupTUN(device tun.Device) *startupTUN {
	return &startupTUN{Device: device, ready: make(chan struct{}), first: true}
}
func (t *startupTUN) start() { t.once.Do(func() { close(t.ready) }) }
func (t *startupTUN) Read(bufs [][]byte, sizes []int, offset int) (int, error) {
	if t.first {
		<-t.ready
		t.first = false
		sizes[0] = 0
		return 1, nil
	}
	return t.Device.Read(bufs, sizes, offset)
}
func (t *startupTUN) Close() error { t.start(); return t.Device.Close() }
