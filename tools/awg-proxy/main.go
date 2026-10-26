package main

import (
	"amnezia.org/awg-proxy/internal/netstack"
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	"github.com/amnezia-vpn/amneziawg-go/v3/device"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

type command struct {
	Version int      `json:"version"`
	Session string   `json:"session"`
	Type    string   `json:"type"`
	Native  string   `json:"native"`
	DNS     []string `json:"dns"`
	HTTP    int      `json:"httpPort"`
	SOCKS   int      `json:"socksPort"`
	Rules   rules    `json:"rules"`
}

var outputMu sync.Mutex

func emit(id, kind string, values map[string]any) {
	outputMu.Lock()
	defer outputMu.Unlock()
	if values == nil {
		values = map[string]any{}
	}
	values["version"] = 1
	values["session"] = id
	values["type"] = kind
	json.NewEncoder(os.Stdout).Encode(values)
}
func launch(ctx context.Context, c command) error {
	p, e := parseProfile(c.Native, c.DNS)
	if e != nil {
		return e
	}
	host, port, e := net.SplitHostPort(p.endpoint)
	if e != nil {
		return errors.New("invalid Endpoint")
	}
	n, e := strconv.ParseUint(port, 10, 16)
	if e != nil || n == 0 {
		return errors.New("invalid Endpoint port")
	}
	resolve, cancel := context.WithTimeout(ctx, 10*time.Second)
	ips, e := net.DefaultResolver.LookupNetIP(resolve, "ip", host)
	cancel()
	if e != nil || len(ips) == 0 {
		return errors.New("Endpoint resolution failed")
	}
	tun, tnet, e := netstack.CreateNetTUN(p.addresses, p.dns, p.mtu)
	if e != nil {
		return errors.New("netstack initialization failed")
	}
	// No upstream logs: UAPI validation errors may contain key/config values.
	guardedTun := newStartupTUN(tun)
	dev := device.NewDevice(guardedTun, conn.NewDefaultBind(), device.NewLogger(device.LogLevelSilent, ""))
	defer dev.Close()
	if e = dev.IpcSet(p.uapi + "endpoint=" + net.JoinHostPort(ips[0].Unmap().String(), port) + "\n"); e != nil {
		return errors.New("AWG rejected profile parameters")
	}
	if e = dev.Up(); e != nil {
		return errors.New("AWG UDP startup failed")
	}
	guardedTun.start()
	sessionCtx, stop := context.WithCancel(ctx)
	s := &session{ctx: sessionCtx, cancel: stop, profile: p, rules: c.Rules, tnet: tnet, connections: map[*trackedConn]bool{}, clients: map[net.Conn]bool{}}
	defer s.close()
	// A DNS packet initiates a handshake even with PersistentKeepalive=0.
	probe, probeCancel := context.WithTimeout(ctx, 25*time.Second)
	defer probeCancel()
	go func() { tnet.LookupContextHost(probe, "amnezia.org") }()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	connected := false
	probeDone := probe.Done()
	health := make(chan bool, 1)
	go func() {
		tick := time.NewTicker(15 * time.Second)
		defer tick.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-tick.C:
				check, stop := context.WithTimeout(ctx, 6*time.Second)
				_, err := tnet.LookupContextHost(check, "amnezia.org")
				stop()
				var dnsError *net.DNSError
				ok := err == nil || (errors.As(err, &dnsError) && dnsError.IsNotFound)
				select {
				case health <- ok:
				case <-ctx.Done():
					return
				}
			}
		}
	}()
	monitor := tunnelHealth{}
	var lastPeerRx uint64
	for {
		select {
		case <-ctx.Done():
			return nil
		case ok := <-health:
			if !connected {
				continue
			}
			changed, received := monitor.observe(ok, lastPeerRx)
			if !ok {
				emit(c.Session, "health", map[string]any{"dnsOk": false, "failedChecks": monitor.failures, "peerRxAdvanced": received})
			}
			if changed {
				kind := "available"
				if monitor.unavailable {
					kind = "unavailable"
				}
				emit(c.Session, kind, nil)
			}
		case <-probeDone:
			if !connected {
				return errors.New("AWG handshake timed out")
			}
			probeDone = nil
		case <-ticker.C:
			status, e := dev.IpcGet()
			if e != nil {
				return errors.New("AWG status failed")
			}
			handshake := false
			var peerRx, peerTx uint64
			for _, line := range strings.Split(status, "\n") {
				if strings.HasPrefix(line, "last_handshake_time_sec=") {
					v, _ := strconv.ParseInt(strings.TrimPrefix(line, "last_handshake_time_sec="), 10, 64)
					handshake = v > 0
				}
				if strings.HasPrefix(line, "rx_bytes=") {
					peerRx, _ = strconv.ParseUint(strings.TrimPrefix(line, "rx_bytes="), 10, 64)
				}
				if strings.HasPrefix(line, "tx_bytes=") {
					peerTx, _ = strconv.ParseUint(strings.TrimPrefix(line, "tx_bytes="), 10, 64)
				}
			}
			lastPeerRx = peerRx
			if !connected && handshake {
				if e = s.startListeners(c.HTTP, c.SOCKS); e != nil {
					return e
				}
				connected = true
				monitor.lastRx = peerRx
				emit(c.Session, "ready", nil)
			}
			if connected {
				emit(c.Session, "counters", map[string]any{"rx": s.rx.Load(), "tx": s.tx.Load(), "directRx": s.directRx.Load(), "directTx": s.directTx.Load(), "peerRx": peerRx, "peerTx": peerTx})
			}
		}
	}
}
func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	commands := make(chan command)
	go func() {
		defer cancel()
		scanner := bufio.NewScanner(os.Stdin)
		scanner.Buffer(make([]byte, 4096), 1<<20)
		for scanner.Scan() {
			var c command
			if json.Unmarshal(scanner.Bytes(), &c) != nil || c.Version != 1 {
				return
			}
			select {
			case commands <- c:
			case <-ctx.Done():
				return
			}
		}
	}()
	var started bool
	done := make(chan error, 1)
	id := ""
	for {
		select {
		case <-ctx.Done():
			return
		case c := <-commands:
			if c.Type == "stop" {
				cancel()
				return
			}
			if c.Type != "start" || started || c.Session == "" {
				return
			}
			started = true
			id = c.Session
			go func() { done <- launch(ctx, c) }()
		case e := <-done:
			if e != nil {
				emit(id, "error", map[string]any{"message": e.Error()})
			}
			return
		}
	}
}
