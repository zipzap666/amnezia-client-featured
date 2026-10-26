package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

type trackedConn struct {
	net.Conn
	s    *session
	awg  bool
	once sync.Once
}

func (c *trackedConn) Read(b []byte) (int, error) {
	n, e := c.Conn.Read(b)
	if c.awg {
		c.s.rx.Add(uint64(n))
	} else {
		c.s.directRx.Add(uint64(n))
	}
	return n, e
}
func (c *trackedConn) Write(b []byte) (int, error) {
	n, e := c.Conn.Write(b)
	if c.awg {
		c.s.tx.Add(uint64(n))
	} else {
		c.s.directTx.Add(uint64(n))
	}
	return n, e
}
func (c *trackedConn) Close() error {
	e := c.Conn.Close()
	c.once.Do(func() { c.s.mu.Lock(); delete(c.s.connections, c); c.s.mu.Unlock() })
	return e
}
func (c *trackedConn) CloseWrite() error {
	if half, ok := c.Conn.(interface{ CloseWrite() error }); ok {
		return half.CloseWrite()
	}
	return c.Close()
}
func (s *session) track(c net.Conn, awg bool) net.Conn {
	t := &trackedConn{Conn: c, s: s, awg: awg}
	s.mu.Lock()
	select {
	case <-s.ctx.Done():
		s.mu.Unlock()
		c.Close()
		return t
	default:
	}
	s.connections[t] = true
	s.mu.Unlock()
	return t
}
func relay(a net.Conn, b net.Conn, reader io.Reader) {
	done := make(chan struct{})
	finishWrite := func(c net.Conn) {
		if half, ok := c.(interface{ CloseWrite() error }); ok {
			if half.CloseWrite() == nil {
				return
			}
		}
		c.Close()
	}
	go func() { io.Copy(a, b); finishWrite(a); close(done) }()
	io.Copy(b, reader)
	finishWrite(b)
	<-done
	a.Close()
	b.Close()
}
func (s *session) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodConnect {
		if _, _, e := net.SplitHostPort(r.Host); e != nil {
			http.Error(w, "Invalid CONNECT target", 400)
			return
		}
		upstream, e := s.dial(r.Context(), "tcp", r.Host)
		if e != nil {
			http.Error(w, "Proxy connection failed", 502)
			return
		}
		defer upstream.Close()
		downstream, rw, e := w.(http.Hijacker).Hijack()
		if e != nil {
			return
		}
		defer downstream.Close()
		s.addClient(downstream)
		defer s.removeClient(downstream)
		if _, e = rw.WriteString("HTTP/1.1 200 Connection Established\r\n\r\n"); e != nil {
			return
		}
		if e = rw.Flush(); e != nil {
			return
		}
		relay(downstream, upstream, rw)
		return
	}
	if r.URL.Scheme != "http" || r.URL.Host == "" || r.URL.User != nil {
		http.Error(w, "Absolute HTTP URL required", 400)
		return
	}
	req := r.Clone(r.Context())
	req.RequestURI = ""
	stripHop(req.Header)
	resp, e := s.transport.RoundTrip(req)
	if e != nil {
		http.Error(w, "Proxy connection failed", 502)
		return
	}
	defer resp.Body.Close()
	stripHop(resp.Header)
	for k, values := range resp.Header {
		for _, v := range values {
			w.Header().Add(k, v)
		}
	}
	w.WriteHeader(resp.StatusCode)
	io.Copy(w, resp.Body)
}
func stripHop(h http.Header) {
	for _, v := range h.Values("Connection") {
		for _, k := range strings.Split(v, ",") {
			h.Del(strings.TrimSpace(k))
		}
	}
	for _, k := range []string{"Connection", "Proxy-Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization", "Te", "Trailer", "Transfer-Encoding", "Upgrade"} {
		h.Del(k)
	}
}
func (s *session) addClient(c net.Conn) {
	s.mu.Lock()
	defer s.mu.Unlock()
	select {
	case <-s.ctx.Done():
		c.Close()
	default:
		s.clients[c] = true
	}
}
func (s *session) removeClient(c net.Conn) { s.mu.Lock(); delete(s.clients, c); s.mu.Unlock() }
func (s *session) socks(c net.Conn) {
	defer c.Close()
	s.addClient(c)
	defer s.removeClient(c)
	c.SetDeadline(time.Now().Add(15 * time.Second))
	b := bufio.NewReader(c)
	version, e := b.ReadByte()
	if e != nil || version != 5 {
		return
	}
	n, e := b.ReadByte()
	if e != nil || n == 0 {
		return
	}
	methods := make([]byte, int(n))
	if _, e = io.ReadFull(b, methods); e != nil {
		return
	}
	supported := false
	for _, m := range methods {
		if m == 0 {
			supported = true
		}
	}
	if !supported {
		c.Write([]byte{5, 255})
		return
	}
	c.Write([]byte{5, 0})
	head := make([]byte, 4)
	if _, e = io.ReadFull(b, head); e != nil {
		return
	}
	reply := func(code byte) { c.Write([]byte{5, code, 0, 1, 0, 0, 0, 0, 0, 0}) }
	if head[0] != 5 || head[2] != 0 {
		reply(1)
		return
	}
	if head[1] != 1 {
		reply(7)
		return
	}
	var host string
	switch head[3] {
	case 1, 4:
		n := 4
		if head[3] == 4 {
			n = 16
		}
		addr := make([]byte, n)
		if _, e = io.ReadFull(b, addr); e != nil {
			return
		}
		host = net.IP(addr).String()
	case 3:
		n, e := b.ReadByte()
		if e != nil || n == 0 {
			return
		}
		addr := make([]byte, int(n))
		if _, e = io.ReadFull(b, addr); e != nil {
			return
		}
		host = string(addr)
	default:
		reply(8)
		return
	}
	port := make([]byte, 2)
	if _, e = io.ReadFull(b, port); e != nil {
		return
	}
	p := int(binary.BigEndian.Uint16(port))
	if p == 0 {
		reply(1)
		return
	}
	upstream, e := s.dial(s.ctx, "tcp", net.JoinHostPort(host, strconv.Itoa(p)))
	if e != nil {
		reply(4)
		return
	}
	defer upstream.Close()
	reply(0)
	c.SetDeadline(time.Time{})
	relay(c, upstream, b)
}

type session struct {
	ctx                        context.Context
	cancel                     context.CancelFunc
	profile                    profile
	rules                      rules
	tnet                       tunnelNet
	mu                         sync.Mutex
	connections                map[*trackedConn]bool
	clients                    map[net.Conn]bool
	transport                  *http.Transport
	server                     *http.Server
	listeners                  []net.Listener
	rx, tx, directRx, directTx atomic.Uint64
}

func (s *session) startListeners(httpPort, socksPort int) error {
	if httpPort < 1 || httpPort > 65535 || socksPort < 1 || socksPort > 65535 || httpPort == socksPort {
		return errors.New("invalid or equal listener ports")
	}
	for _, port := range []int{httpPort, socksPort} {
		l, e := net.Listen("tcp4", net.JoinHostPort("127.0.0.1", strconv.Itoa(port)))
		if e != nil {
			for _, v := range s.listeners {
				v.Close()
			}
			return errors.New("listener port unavailable")
		}
		s.listeners = append(s.listeners, l)
	}
	s.transport = &http.Transport{Proxy: nil, DialContext: s.dial, ResponseHeaderTimeout: 30 * time.Second, IdleConnTimeout: 60 * time.Second, MaxIdleConns: 100, ForceAttemptHTTP2: false}
	s.server = &http.Server{Handler: s, ReadHeaderTimeout: 15 * time.Second, MaxHeaderBytes: 1 << 20, BaseContext: func(net.Listener) context.Context { return s.ctx }}
	go s.server.Serve(s.listeners[0])
	go func() {
		for {
			c, e := s.listeners[1].Accept()
			if e != nil {
				return
			}
			go s.socks(c)
		}
	}()
	return nil
}
func (s *session) close() {
	s.cancel()
	for _, l := range s.listeners {
		l.Close()
	}
	if s.server != nil {
		s.server.Close()
	}
	if s.transport != nil {
		s.transport.CloseIdleConnections()
	}
	s.mu.Lock()
	var all []net.Conn
	for c := range s.connections {
		all = append(all, c)
	}
	for c := range s.clients {
		all = append(all, c)
	}
	s.mu.Unlock()
	for _, c := range all {
		c.Close()
	}
}
