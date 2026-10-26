package main

import (
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"net/netip"
	"strconv"
	"strings"
)

// Native profile is the source of truth. Never print its contents in errors.
type profile struct {
	addresses, dns []netip.Addr
	allowed        []netip.Prefix
	endpoint       string
	mtu            int
	uapi           string
}

var deviceFields = map[string]string{
	"Jc": "jc", "Jmin": "jmin", "Jmax": "jmax", "S1": "s1", "S2": "s2", "S3": "s3", "S4": "s4",
	"H1": "h1", "H2": "h2", "H3": "h3", "H4": "h4", "I1": "i1", "I2": "i2", "I3": "i3", "I4": "i4", "I5": "i5",
	"HeaderProtectionKey": "header_protection_key", "ContentPaddingAddition": "content_padding_addition",
	"RekeyAfterTime": "rekey_after_time", "RekeyTimeout": "rekey_timeout", "RejectAfterTime": "reject_after_time",
	"KeepaliveTimeout": "keepalive_timeout", "MaxHandshakeAttempts": "max_handshake_attempts", "RandomTrailers": "random_trailers", "DisableCookies": "disable_cookies",
}

func parseProfile(native string, dns []string) (profile, error) {
	p := profile{mtu: 1280}
	section := ""
	peers := 0
	seen := map[string]bool{}
	var dev, peer []string
	key := func(v string) (string, error) {
		b, e := base64.StdEncoding.DecodeString(v)
		if e != nil || len(b) != 32 {
			return "", errors.New("invalid key")
		}
		return hex.EncodeToString(b), nil
	}
	for _, line := range strings.Split(native, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}
		if strings.HasPrefix(line, "[") {
			if line != "[Interface]" && line != "[Peer]" {
				return p, errors.New("unsupported profile section")
			}
			section = line
			if line == "[Peer]" {
				peers++
				if peers > 1 {
					return p, errors.New("only one peer is supported")
				}
			}
			continue
		}
		name, value, ok := strings.Cut(line, "=")
		name = strings.TrimSpace(name)
		value = strings.TrimSpace(value)
		if !ok || section == "" {
			return p, errors.New("invalid profile line")
		}
		id := section + name
		if seen[id] {
			return p, fmt.Errorf("duplicate field: %s", name)
		}
		seen[id] = true
		if section == "[Interface]" {
			switch name {
			case "PrivateKey":
				v, e := key(value)
				if e != nil {
					return p, e
				}
				dev = append(dev, "private_key="+v)
			case "Address":
				for _, a := range strings.Split(value, ",") {
					prefix, e := netip.ParsePrefix(strings.TrimSpace(a))
					if e != nil {
						return p, errors.New("invalid interface address")
					}
					p.addresses = append(p.addresses, prefix.Addr())
				}
			case "DNS": // Effective DNS is supplied by the existing Qt configuration pipeline.
			case "MTU":
				n, e := strconv.Atoi(value)
				if e != nil || n < 576 || n > 65535 {
					return p, errors.New("invalid MTU")
				}
				p.mtu = n
			case "ListenPort":
				n, e := strconv.ParseUint(value, 10, 16)
				if e != nil {
					return p, errors.New("invalid ListenPort")
				}
				dev = append(dev, fmt.Sprintf("listen_port=%d", n))
			default:
				field, ok := deviceFields[name]
				if !ok {
					return p, fmt.Errorf("unsupported interface field: %s", name)
				}
				if name == "HeaderProtectionKey" {
					v, e := key(value)
					if e != nil {
						return p, e
					}
					value = v
				}
				if name == "RandomTrailers" || name == "DisableCookies" {
					switch strings.ToLower(value) {
					case "on", "true", "1", "t", "yes":
						value = "1"
					case "off", "false", "0", "f", "no", "":
						value = "0"
					default:
						return p, fmt.Errorf("invalid boolean field: %s", name)
					}
				}
				dev = append(dev, field+"="+value)
			}
		} else {
			switch name {
			case "PublicKey", "PresharedKey":
				v, e := key(value)
				if e != nil {
					return p, e
				}
				field := "public_key"
				if name == "PresharedKey" {
					field = "preshared_key"
				}
				peer = append(peer, field+"="+v)
			case "Endpoint":
				p.endpoint = value
			case "AllowedIPs":
				for _, a := range strings.Split(value, ",") {
					prefix, e := netip.ParsePrefix(strings.TrimSpace(a))
					if e != nil {
						return p, errors.New("invalid AllowedIPs")
					}
					p.allowed = append(p.allowed, prefix)
					peer = append(peer, "allowed_ip="+prefix.String())
				}
			case "PersistentKeepalive":
				peer = append(peer, "persistent_keepalive_interval="+value)
			default:
				return p, fmt.Errorf("unsupported peer field: %s", name)
			}
		}
	}
	if !seen["[Interface]PrivateKey"] || !seen["[Peer]PublicKey"] || len(p.addresses) == 0 || len(p.allowed) == 0 || p.endpoint == "" {
		return p, errors.New("incomplete AWG profile")
	}
	// Public key must precede all peer fields in UAPI, regardless of INI ordering.
	for i, s := range peer {
		if strings.HasPrefix(s, "public_key=") {
			peer[0], peer[i] = peer[i], peer[0]
			break
		}
	}
	for _, s := range dns {
		if s == "" {
			continue
		}
		a, e := netip.ParseAddr(s)
		if e != nil {
			return p, errors.New("DNS must be an IP address")
		}
		if !p.covers(a) {
			return p, errors.New("DNS is outside AllowedIPs")
		}
		p.dns = append(p.dns, a)
	}
	if len(p.dns) == 0 {
		return p, errors.New("no tunnel DNS configured")
	}
	p.uapi = strings.Join(append(dev, peer...), "\n") + "\n"
	return p, nil
}
func (p profile) covers(a netip.Addr) bool {
	for _, prefix := range p.allowed {
		if prefix.Contains(a) {
			return true
		}
	}
	return false
}
