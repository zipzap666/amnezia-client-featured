package main

import (
	"bufio"
	"context"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	"github.com/amnezia-vpn/amneziawg-go/v3/device"
	"github.com/amnezia-vpn/amneziawg-go/v3/tun/netstack"
	"golang.org/x/net/dns/dnsmessage"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"net/url"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestRules(t *testing.T) {
	ips := []netip.Addr{netip.MustParseAddr("10.77.0.1")}
	for _, tc := range []struct {
		host string
		mode int
		want bool
	}{{"example.com", 1, true}, {"www.example.com", 1, true}, {"otherexample.com", 1, false}, {"www.example.com", 2, false}, {"otherexample.com", 2, true}} {
		r := rules{true, tc.mode, []string{"example.com"}}
		got, _ := r.route(tc.host, ips)
		if got != tc.want {
			t.Fatalf("%s mode %d", tc.host, tc.mode)
		}
	}
	r := rules{true, 1, []string{"10.77.0.0/24"}}
	got, _ := r.route("other.test", ips)
	if !got {
		t.Fatal("CIDR ignored")
	}
}
func TestStrictProfile(t *testing.T) {
	key := base64.StdEncoding.EncodeToString(make([]byte, 32))
	native := "[Interface]\nAddress=10.77.0.2/32\nPrivateKey=" + key + "\n[Peer]\nAllowedIPs=0.0.0.0/0\nEndpoint=127.0.0.1:1234\nPublicKey=" + key + "\n"
	p, e := parseProfile(native, []string{"10.77.0.1"})
	if e != nil {
		t.Fatal(e)
	}
	if strings.Index(p.uapi, "public_key=") > strings.Index(p.uapi, "allowed_ip=") {
		t.Fatal("UAPI key ordering")
	}
	for _, bad := range []string{strings.Replace(native, "Address=", "PostUp=secret\nAddress=", 1), native + "[Peer]\n", strings.Replace(native, "/32", "/999", 1)} {
		if _, e = parseProfile(bad, []string{"10.77.0.1"}); e == nil {
			t.Fatal("invalid profile accepted")
		}
	}
	if _, e = parseProfile(strings.Replace(native, "0.0.0.0/0", "192.168.0.0/24", 1), []string{"10.77.0.1"}); e == nil {
		t.Fatal("DNS outside AllowedIPs accepted")
	}
}
func TestNativeBooleanFields(t *testing.T) {
	key := base64.StdEncoding.EncodeToString(make([]byte, 32))
	native := "[Interface]\nAddress=10.77.0.2/32\nPrivateKey=" + key + "\n%s=%s\n[Peer]\nPublicKey=" + key + "\nAllowedIPs=0.0.0.0/0\nEndpoint=127.0.0.1:1234\n"
	for _, field := range []string{"RandomTrailers", "DisableCookies"} {
		for _, tc := range []struct{ input, want string }{
			{"on", "1"}, {"ON", "1"}, {" On ", "1"}, {"true", "1"}, {"1", "1"},
			{"t", "1"}, {"yes", "1"},
			{"off", "0"}, {"OFF", "0"}, {" Off ", "0"}, {"false", "0"}, {"0", "0"}, {"", "0"},
			{"f", "0"}, {"no", "0"},
		} {
			t.Run(field+"/"+tc.input, func(t *testing.T) {
				p, err := parseProfile(fmt.Sprintf(native, field, tc.input), []string{"10.77.0.1"})
				if err != nil {
					t.Fatal(err)
				}
				if !strings.Contains(p.uapi, deviceFields[field]+"="+tc.want+"\n") {
					t.Fatal("incorrect boolean conversion")
				}
			})
		}
		if _, err := parseProfile(fmt.Sprintf(native, field, "invalid"), []string{"10.77.0.1"}); err == nil {
			t.Fatal("invalid boolean accepted")
		}
	}
}
func freePort(t *testing.T) int {
	l, e := net.Listen("tcp4", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port
}
func TestLocalAWGProxy(t *testing.T) {
	for _, version := range []int{1, 2, 3} {
		t.Run(fmt.Sprintf("AWG%d", version), func(t *testing.T) { testLocalAWGProxy(t, version) })
	}
}
func testLocalAWGProxy(t *testing.T, version int) {

	// Both peers and services live in userspace; no system adapter, DNS or route changes.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clientKey, _ := ecdh.X25519().GenerateKey(rand.Reader)
	serverKey, _ := ecdh.X25519().GenerateKey(rand.Reader)
	serverTun, serverNet, e := netstack.CreateNetTUN([]netip.Addr{netip.MustParseAddr("10.77.0.1"), netip.MustParseAddr("fd77::1")}, nil, 1280)
	if e != nil {
		t.Fatal(e)
	}
	guardedServerTun := newStartupTUN(serverTun)
	server := device.NewDevice(guardedServerTun, conn.NewDefaultBind(), device.NewLogger(device.LogLevelSilent, ""))
	defer server.Close()
	serverConfig := fmt.Sprintf("private_key=%s\nlisten_port=0\npublic_key=%s\nallowed_ip=10.77.0.2/32\n", hex.EncodeToString(serverKey.Bytes()), hex.EncodeToString(clientKey.PublicKey().Bytes()))
	serverConfig += "allowed_ip=fd77::2/128\n"
	interfaceNative := "Jc=2\nJmin=20\nJmax=40\nS1=10\nS2=20\nH1=12345\nH2=23456\nH3=34567\nH4=45678\n"
	deviceUAPI := "jc=2\njmin=20\njmax=40\ns1=10\ns2=20\nh1=12345\nh2=23456\nh3=34567\nh4=45678\n"
	if version >= 2 {
		interfaceNative += "S3=5\nS4=6\n"
		deviceUAPI += "s3=5\ns4=6\n"
	}
	if version >= 3 {
		interfaceNative += "ContentPaddingAddition=0-5\nRekeyAfterTime=120\nRekeyTimeout=5\nRejectAfterTime=180\nKeepaliveTimeout=10\nMaxHandshakeAttempts=18\nRandomTrailers=on\nDisableCookies=off\n"
		deviceUAPI += "content_padding_addition=0-5\nrekey_after_time=120\nrekey_timeout=5\nreject_after_time=180\nkeepalive_timeout=10\nmax_handshake_attempts=18\nrandom_trailers=1\ndisable_cookies=0\n"
		protection := make([]byte, 32)
		for _, padding := range [][2]string{{"S1=10", "S1=20"}, {"S2=20", "S2=30"}, {"S3=5", "S3=40"}, {"S4=6", "S4=50"}} {
			interfaceNative = strings.Replace(interfaceNative, padding[0], padding[1], 1)
			deviceUAPI = strings.Replace(deviceUAPI, strings.ToLower(padding[0]), strings.ToLower(padding[1]), 1)
		}
		rand.Read(protection)
		interfaceNative += "HeaderProtectionKey=" + base64.StdEncoding.EncodeToString(protection) + "\n"
		deviceUAPI += "header_protection_key=" + hex.EncodeToString(protection) + "\n"
		for i := 1; i <= 5; i++ {
			pattern := "<b 0xf6ab3267fa><r 10>"
			interfaceNative += fmt.Sprintf("I%d=%s\n", i, pattern)
			deviceUAPI += fmt.Sprintf("i%d=%s\n", i, pattern)
		}
		interfaceNative = strings.Replace(interfaceNative, "H1=12345", "H1=12345-12350", 1)
		deviceUAPI = strings.Replace(deviceUAPI, "h1=12345", "h1=12345-12350", 1)
	}
	serverConfig = deviceUAPI + serverConfig
	if e = server.IpcSet(serverConfig); e != nil {
		t.Fatal(e)
	}
	if e = server.Up(); e != nil {
		t.Fatal(e)
	}
	guardedServerTun.start()
	status, _ := server.IpcGet()
	port := ""
	for _, l := range strings.Split(status, "\n") {
		if strings.HasPrefix(l, "listen_port=") {
			port = strings.TrimPrefix(l, "listen_port=")
		}
	}
	if port == "" {
		t.Fatal("no peer UDP port")
	}
	listener, e := serverNet.ListenTCPAddrPort(netip.MustParseAddrPort("10.77.0.1:80"))
	if e != nil {
		t.Fatal(e)
	}
	defer listener.Close()
	httpServer := &http.Server{Handler: uploadTestHandler("inside-awg")}
	defer httpServer.Close()
	go httpServer.Serve(listener)
	listener6, e := serverNet.ListenTCPAddrPort(netip.MustParseAddrPort("[fd77::1]:80"))
	if e != nil {
		t.Fatal(e)
	}
	defer listener6.Close()
	go httpServer.Serve(listener6)
	halfListener, e := serverNet.ListenTCPAddrPort(netip.MustParseAddrPort("10.77.0.1:81"))
	if e != nil {
		t.Fatal(e)
	}
	defer halfListener.Close()
	go func() {
		c, e := halfListener.Accept()
		if e != nil {
			return
		}
		defer c.Close()
		body, _ := io.ReadAll(c)
		if string(body) == "request" {
			io.WriteString(c, "after-eof")
		}
	}()
	secureListener, e := serverNet.ListenTCPAddrPort(netip.MustParseAddrPort("10.77.0.1:443"))
	if e != nil {
		t.Fatal(e)
	}
	secureServer := httptest.NewUnstartedServer(uploadTestHandler("tls-inside-awg"))
	secureServer.Listener.Close()
	secureServer.Listener = secureListener
	secureServer.StartTLS()
	defer secureServer.Close()
	dns, e := serverNet.ListenUDPAddrPort(netip.MustParseAddrPort("10.77.0.1:53"))
	if e != nil {
		t.Fatal(e)
	}
	defer dns.Close()
	go func() {
		b := make([]byte, 4096)
		for {
			n, addr, e := dns.ReadFrom(b)
			if e != nil {
				return
			}
			var query dnsmessage.Message
			if query.Unpack(b[:n]) != nil || len(query.Questions) == 0 {
				continue
			}
			reply := dnsmessage.Message{Header: dnsmessage.Header{ID: query.ID, Response: true, RecursionAvailable: true}, Questions: query.Questions}
			if query.Questions[0].Type == dnsmessage.TypeA {
				reply.Answers = []dnsmessage.Resource{{Header: dnsmessage.ResourceHeader{Name: query.Questions[0].Name, Type: dnsmessage.TypeA, Class: dnsmessage.ClassINET, TTL: 30}, Body: &dnsmessage.AResource{A: [4]byte{10, 77, 0, 1}}}}
			}
			if query.Questions[0].Type == dnsmessage.TypeAAAA {
				reply.Answers = []dnsmessage.Resource{{Header: dnsmessage.ResourceHeader{Name: query.Questions[0].Name, Type: dnsmessage.TypeAAAA, Class: dnsmessage.ClassINET, TTL: 30}, Body: &dnsmessage.AAAAResource{AAAA: netip.MustParseAddr("fd77::1").As16()}}}
			}
			data, _ := reply.Pack()
			dns.WriteTo(data, addr)
		}
	}()
	native := fmt.Sprintf("[Interface]\nAddress=10.77.0.2/32\nPrivateKey=%s\n[Peer]\nPublicKey=%s\nAllowedIPs=10.77.0.0/24\nEndpoint=127.0.0.1:%s\n", base64.StdEncoding.EncodeToString(clientKey.Bytes()), base64.StdEncoding.EncodeToString(serverKey.PublicKey().Bytes()), port)
	native = strings.Replace(native, "[Interface]\n", "[Interface]\n"+interfaceNative, 1)
	native = strings.Replace(native, "Address=10.77.0.2/32", "Address=10.77.0.2/32, fd77::2/128", 1)
	native = strings.Replace(native, "AllowedIPs=10.77.0.0/24", "AllowedIPs=10.77.0.0/24, fd77::/64", 1)
	hp, sp := freePort(t), freePort(t)
	for sp == hp {
		sp = freePort(t)
	}
	done := make(chan error, 1)
	go func() {
		done <- launch(ctx, command{Session: "test", Native: native, DNS: []string{"10.77.0.1"}, HTTP: hp, SOCKS: sp})
	}()
	var c net.Conn
	deadline := time.Now().Add(12 * time.Second)
	for time.Now().Before(deadline) {
		c, e = net.DialTimeout("tcp", fmt.Sprintf("127.0.0.1:%d", hp), 100*time.Millisecond)
		if e == nil {
			break
		}
		select {
		case e := <-done:
			t.Fatal(e)
		default:
		}
		time.Sleep(50 * time.Millisecond)
	}
	if e != nil {
		status, _ := server.IpcGet()
		for _, line := range strings.Split(status, "\n") {
			if strings.HasPrefix(line, "last_handshake") || strings.HasPrefix(line, "tx_bytes") || strings.HasPrefix(line, "rx_bytes") {
				t.Log(line)
			}
		}
		t.Fatal("helper readiness:", e)
	}
	c.SetDeadline(time.Now().Add(5 * time.Second))
	fmt.Fprint(c, "GET http://service.test/ HTTP/1.1\r\nHost: service.test\r\nConnection: close\r\n\r\n")
	response, e := http.ReadResponse(bufio.NewReader(c), nil)
	if e != nil {
		t.Fatal(e)
	}
	body, _ := io.ReadAll(response.Body)
	response.Body.Close()
	c.Close()
	if string(body) != "inside-awg" {
		t.Fatalf("forward: %s", body)
	}
	// CONNECT is an opaque byte tunnel; send HTTP through it to the local service.
	c, e = net.Dial("tcp", fmt.Sprintf("127.0.0.1:%d", hp))
	if e != nil {
		t.Fatal(e)
	}
	c.SetDeadline(time.Now().Add(5 * time.Second))
	fmt.Fprint(c, "CONNECT service.test:80 HTTP/1.1\r\nHost: service.test:80\r\n\r\n")
	b := bufio.NewReader(c)
	response, e = http.ReadResponse(b, nil)
	if e != nil || response.StatusCode != 200 {
		t.Fatal("CONNECT", e)
	}
	fmt.Fprint(c, "GET / HTTP/1.1\r\nHost: service.test\r\nConnection: close\r\n\r\n")
	response, e = http.ReadResponse(b, nil)
	if e != nil {
		t.Fatal(e)
	}
	body, _ = io.ReadAll(response.Body)
	c.Close()
	if string(body) != "inside-awg" {
		t.Fatal("CONNECT payload")
	}
	c, e = net.Dial("tcp", fmt.Sprintf("127.0.0.1:%d", sp))
	if e != nil {
		t.Fatal(e)
	}
	c.SetDeadline(time.Now().Add(5 * time.Second))
	c.Write([]byte{5, 1, 0})
	ack := make([]byte, 2)
	io.ReadFull(c, ack)
	host := "service.test"
	c.Write(append(append([]byte{5, 1, 0, 3, byte(len(host))}, []byte(host)...), 0, 80))
	ack = make([]byte, 10)
	if _, e = io.ReadFull(c, ack); e != nil || ack[1] != 0 {
		t.Fatal("SOCKS", e, ack)
	}
	fmt.Fprint(c, "GET / HTTP/1.1\r\nHost: service.test\r\nConnection: close\r\n\r\n")
	response, e = http.ReadResponse(bufio.NewReader(c), nil)
	if e != nil {
		t.Fatal(e)
	}
	body, _ = io.ReadAll(response.Body)
	c.Close()
	if string(body) != "inside-awg" {
		t.Fatal("SOCKS payload")
	}
	proxyURL, _ := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", hp))
	plainTransport := &http.Transport{Proxy: http.ProxyURL(proxyURL)}
	defer plainTransport.CloseIdleConnections()
	plainClient := &http.Client{Transport: plainTransport, Timeout: 5 * time.Second}
	for _, target := range []string{"http://10.77.0.1/", "http://[fd77::1]/"} {
		response, e = plainClient.Get(target)
		if e != nil {
			t.Fatal("numeric IP forwarding", e)
		}
		body, _ = io.ReadAll(response.Body)
		response.Body.Close()
		if string(body) != "inside-awg" {
			t.Fatal("numeric IP payload")
		}
	}
	tlsTransport := &http.Transport{Proxy: http.ProxyURL(proxyURL), TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
	defer tlsTransport.CloseIdleConnections()
	tlsClient := &http.Client{Transport: tlsTransport, Timeout: 5 * time.Second}
	response, e = tlsClient.Get("https://service.test/")
	if e != nil {
		t.Fatal("HTTPS CONNECT:", e)
	}
	body, _ = io.ReadAll(response.Body)
	response.Body.Close()
	if string(body) != "tls-inside-awg" {
		t.Fatal("TLS payload")
	}
	// Exercise sustained HTTP forwarding and HTTPS CONNECT uploads, including
	// integrity across many netstack packets and pooled packet buffers.
	payload := strings.Repeat("upload-integrity-0123456789abcdef", 32768)
	expected := fmt.Sprintf("%x", sha256.Sum256([]byte(payload)))
	for _, tc := range []struct {
		client *http.Client
		target string
	}{
		{plainClient, "http://service.test/upload"},
		{tlsClient, "https://service.test/upload"},
	} {
		resp, err := tc.client.Post(tc.target, "application/octet-stream", strings.NewReader(payload))
		if err != nil {
			t.Fatal("upload:", err)
		}
		got, err := io.ReadAll(resp.Body)
		resp.Body.Close()
		if err != nil || string(got) != expected {
			t.Fatalf("upload integrity: %s, %v", got, err)
		}
	}
	c, e = net.Dial("tcp", fmt.Sprintf("127.0.0.1:%d", hp))
	if e != nil {
		t.Fatal(e)
	}
	c.SetDeadline(time.Now().Add(5 * time.Second))
	fmt.Fprint(c, "CONNECT service.test:81 HTTP/1.1\r\nHost: service.test:81\r\n\r\n")
	reader := bufio.NewReader(c)
	response, e = http.ReadResponse(reader, nil)
	if e != nil || response.StatusCode != 200 {
		t.Fatal("half-close CONNECT", e)
	}
	fmt.Fprint(c, "request")
	c.(*net.TCPConn).CloseWrite()
	body, e = io.ReadAll(reader)
	c.Close()
	if e != nil || string(body) != "after-eof" {
		t.Fatal("half-close lost response", e, string(body))
	}
	cancel()
	select {
	case e := <-done:
		if e != nil {
			t.Fatal(e)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("helper did not stop")
	}
}

func uploadTestHandler(defaultBody string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/upload" {
			hash := sha256.New()
			if _, err := io.Copy(hash, r.Body); err != nil {
				http.Error(w, "body read failed", 400)
				return
			}
			fmt.Fprintf(w, "%x", hash.Sum(nil))
			return
		}
		io.WriteString(w, defaultBody)
	}
}

type unreachableTunnel struct{}

func (unreachableTunnel) LookupContextHost(context.Context, string) ([]string, error) {
	return nil, errors.New("DNS unavailable")
}
func (unreachableTunnel) DialContext(context.Context, string, string) (net.Conn, error) {
	return nil, errors.New("tunnel unavailable")
}
func TestDirectRouteAndNoFallback(t *testing.T) {
	var hits atomic.Int32
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { hits.Add(1); io.WriteString(w, "direct") }))
	defer target.Close()
	u, _ := url.Parse(target.URL)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s := &session{ctx: ctx, cancel: cancel, profile: profile{allowed: []netip.Prefix{netip.MustParsePrefix("0.0.0.0/0")}}, tnet: unreachableTunnel{}, connections: map[*trackedConn]bool{}, clients: map[net.Conn]bool{}}
	defer s.close()
	if c, e := s.dial(ctx, "tcp", u.Host); e == nil {
		c.Close()
		t.Fatal("tunnel failure fell back directly")
	}
	if hits.Load() != 0 {
		t.Fatal("direct fallback")
	}
	s.rules = rules{true, 2, []string{"127.0.0.1"}}
	c, e := s.dial(ctx, "tcp", u.Host)
	if e != nil {
		t.Fatal(e)
	}
	fmt.Fprint(c, "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
	response, e := http.ReadResponse(bufio.NewReader(c), nil)
	if e != nil {
		t.Fatal(e)
	}
	body, _ := io.ReadAll(response.Body)
	response.Body.Close()
	c.Close()
	if string(body) != "direct" {
		t.Fatal("explicit direct route failed")
	}
}
func TestSOCKSUnsupportedAndPortConflict(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s := &session{ctx: ctx, cancel: cancel, connections: map[*trackedConn]bool{}, clients: map[net.Conn]bool{}}
	defer s.close()
	for _, command := range []byte{2, 3} {
		client, server := net.Pipe()
		go s.socks(server)
		client.SetDeadline(time.Now().Add(time.Second))
		client.Write([]byte{5, 1, 0})
		ack := make([]byte, 2)
		io.ReadFull(client, ack)
		client.Write([]byte{5, command, 0, 1})
		reply := make([]byte, 10)
		_, e := io.ReadFull(client, reply)
		client.Close()
		if e != nil || reply[1] != 7 {
			t.Fatal("unsupported SOCKS command accepted")
		}
	}
	busy, e := net.Listen("tcp4", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	defer busy.Close()
	hp := freePort(t)
	if e = s.startListeners(hp, busy.Addr().(*net.TCPAddr).Port); e == nil {
		t.Fatal("busy port accepted")
	}
	l, e := net.Listen("tcp4", fmt.Sprintf("127.0.0.1:%d", hp))
	if e != nil {
		t.Fatal("partial startup left a listener open")
	}
	l.Close()
}
