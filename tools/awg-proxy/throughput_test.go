package main

import (
	proxynet "amnezia.org/awg-proxy/internal/netstack"
	"context"
	"fmt"
	"io"
	"net"
	"net/netip"
	"os"
	"testing"
	"time"

	"github.com/amnezia-vpn/amneziawg-go/v3/tun"
	"github.com/amnezia-vpn/amneziawg-go/v3/tun/netstack"
)

// Opt-in diagnostic, not a speed threshold for CI. Simulates a WAN without
// touching the host's adapters, routes, DNS or an external server.
func TestThroughputDiagnostic(t *testing.T) {
	if os.Getenv("AWG_PROXY_THROUGHPUT") != "1" {
		t.Skip("set AWG_PROXY_THROUGHPUT=1 for the WAN throughput diagnostic")
	}
	for _, cubic := range []bool{false, true} {
		for _, loss := range []int{0, 1000} {
			for _, upload := range []bool{true, false} {
				t.Run(fmt.Sprintf("cubic%t/dropEvery%d/upload%t", cubic, loss, upload), func(t *testing.T) {
					throughputDiagnostic(t, 29*time.Millisecond, loss, upload, cubic)
				})
			}
		}
	}
}

type diagnosticPacket struct {
	data []byte
	at   time.Time
}

func diagnosticLink(ctx context.Context, src, dst tun.Device, delay time.Duration, dropEvery int) {
	queue := make(chan diagnosticPacket, 8192)
	go func() {
		defer close(queue)
		b := make([]byte, 65535)
		sizes := make([]int, 1)
		count := 0
		for {
			_, err := src.Read([][]byte{b}, sizes, 0)
			if err != nil {
				return
			}
			count++
			if dropEvery > 0 && count%dropEvery == 0 {
				continue
			}
			p := diagnosticPacket{append([]byte(nil), b[:sizes[0]]...), time.Now().Add(delay)}
			select {
			case queue <- p:
			case <-ctx.Done():
				return
			}
		}
	}()
	go func() {
		for p := range queue {
			if d := time.Until(p.at); d > 0 {
				timer := time.NewTimer(d)
				select {
				case <-timer.C:
				case <-ctx.Done():
					timer.Stop()
					return
				}
			}
			if ctx.Err() != nil {
				return
			}
			if _, err := dst.Write([][]byte{p.data}, 0); err != nil {
				return
			}
		}
	}()
}

func throughputDiagnostic(t *testing.T, delay time.Duration, loss int, upload, cubic bool) {
	create := netstack.CreateNetTUN
	// The two packages expose distinct Net types, so retain their shared dial API.
	var a tun.Device
	var an tunnelNet
	var err error
	if cubic {
		a, an, err = proxynet.CreateNetTUN([]netip.Addr{netip.MustParseAddr("10.77.0.2")}, nil, 1280)
	} else {
		a, an, err = create([]netip.Addr{netip.MustParseAddr("10.77.0.2")}, nil, 1280)
	}
	if err != nil {
		t.Fatal(err)
	}
	b, bn, err := netstack.CreateNetTUN([]netip.Addr{netip.MustParseAddr("10.77.0.1")}, nil, 1280)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer func() { cancel(); a.Close(); b.Close() }()
	diagnosticLink(ctx, a, b, delay, loss)
	diagnosticLink(ctx, b, a, delay, loss)
	l, err := bn.ListenTCPAddrPort(netip.MustParseAddrPort("10.77.0.1:80"))
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	const size = 16 << 20
	result := make(chan error, 1)
	go func() {
		c, err := l.Accept()
		if err != nil {
			result <- err
			return
		}
		defer c.Close()
		c.SetDeadline(time.Now().Add(45 * time.Second))
		if upload {
			_, err = io.CopyN(io.Discard, c, size)
		} else {
			_, err = io.CopyN(c, zeroReader{}, size)
		}
		result <- err
	}()
	c, err := an.DialContext(ctx, "tcp", "10.77.0.1:80")
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(45 * time.Second))
	// Include the real proxy relay and the Windows loopback socket.
	local, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer local.Close()
	go func() {
		downstream, err := local.Accept()
		if err == nil {
			relay(downstream, c, downstream)
		}
	}()
	app, err := net.Dial("tcp", local.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	defer app.Close()
	app.SetDeadline(time.Now().Add(45 * time.Second))
	start := time.Now()
	if upload {
		_, err = io.CopyN(app, zeroReader{}, size)
	} else {
		_, err = io.CopyN(io.Discard, app, size)
	}
	if err != nil {
		t.Fatal(err)
	}
	if err = <-result; err != nil {
		t.Fatal(err)
	}
	t.Logf("%.1f Mbit/s, %s", float64(size)*8/time.Since(start).Seconds()/1e6, time.Since(start))
}

type zeroReader struct{}

func (zeroReader) Read(p []byte) (int, error) { clear(p); return len(p), nil }
