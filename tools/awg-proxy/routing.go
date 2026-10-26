package main

import (
	"context"
	"errors"
	"golang.org/x/net/idna"
	"net"
	"net/netip"
	"strings"
	"time"
)

type rules struct {
	Enabled bool     `json:"enabled"`
	Mode    int      `json:"mode"`
	Sites   []string `json:"sites"`
}
type tunnelNet interface {
	LookupContextHost(context.Context, string) ([]string, error)
	DialContext(context.Context, string, string) (net.Conn, error)
}

func domain(s string) string {
	s = strings.TrimSuffix(strings.ToLower(strings.TrimSpace(s)), ".")
	a, e := idna.Lookup.ToASCII(s)
	if e != nil {
		return ""
	}
	return a
}
func (r rules) route(host string, ips []netip.Addr) (bool, []netip.Addr) {
	if !r.Enabled || r.Mode == 0 {
		return true, ips
	}
	matched := false
	h := domain(host)
	for _, site := range r.Sites {
		s := domain(site)
		if _, e := netip.ParseAddr(s); e == nil {
			continue
		}
		if strings.Contains(s, "/") {
			continue
		}
		if s != "" && (h == s || strings.HasSuffix(h, "."+s)) {
			matched = true
			break
		}
	}
	selected := ips
	if !matched {
		var matches []netip.Addr
		for _, ip := range ips {
			for _, s := range r.Sites {
				prefix, e := netip.ParsePrefix(s)
				if e != nil {
					a, e := netip.ParseAddr(s)
					if e != nil {
						continue
					}
					prefix = netip.PrefixFrom(a, a.BitLen())
				}
				if prefix.Contains(ip) {
					matches = append(matches, ip)
					break
				}
			}
		}
		if len(matches) > 0 {
			matched = true
			selected = matches
		}
	}
	if r.Mode == 1 {
		return matched, selected
	}
	return !matched, selected
}
func (s *session) dial(ctx context.Context, network, address string) (net.Conn, error) {
	ctx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	host, port, e := net.SplitHostPort(address)
	if e != nil {
		return nil, e
	}
	var ips []netip.Addr
	if a, e := netip.ParseAddr(host); e == nil {
		ips = []netip.Addr{a}
	} else {
		host = domain(host)
		if host == "" {
			return nil, errors.New("invalid destination domain")
		}
		names, e := s.tnet.LookupContextHost(ctx, host)
		if e != nil {
			return nil, errors.New("tunnel DNS failed")
		}
		for _, name := range names {
			if a, e := netip.ParseAddr(name); e == nil {
				ips = append(ips, a)
			}
		}
	}
	awg, ips := s.rules.route(host, ips)
	var last error = errors.New("no usable address")
	for _, ip := range ips {
		target := net.JoinHostPort(ip.String(), port)
		var c net.Conn
		if awg {
			if !s.profile.covers(ip) {
				last = errors.New("destination outside AllowedIPs")
				continue
			}
			c, e = s.tnet.DialContext(ctx, network, target)
		} else {
			c, e = (&net.Dialer{}).DialContext(ctx, network, target)
		}
		if e == nil {
			return s.track(c, awg), nil
		}
		last = e
	}
	return nil, last
}
