package services

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"time"
)

// ResolveAdvertisedHost picks the IP / hostname that worker tasks should dial
// to reach this backend's TCP pixel listener.
//
// Resolution order:
//  1. Explicit WEB_ADVERTISED_HOST env var (operator override).
//  2. ECS task metadata v4 — the private IPv4 of this container's eni.
//     Only used when ECS_CONTAINER_METADATA_URI_V4 is set, which it always
//     is on Fargate.
//  3. First non-loopback IPv4 on any local interface (good for local dev
//     when the workers run on the same docker network).
//  4. 127.0.0.1, which will only work if the master is somehow in this same
//     network namespace.
//
// The chosen value is logged so the failure mode is obvious in CloudWatch.
func ResolveAdvertisedHost() string {
	if v := os.Getenv("WEB_ADVERTISED_HOST"); v != "" {
		log.Printf("WebAdvertisedHost = %q (from WEB_ADVERTISED_HOST)", v)
		return v
	}

	if uri := os.Getenv("ECS_CONTAINER_METADATA_URI_V4"); uri != "" {
		if ip, err := ipFromECSMetadata(uri); err == nil && ip != "" {
			log.Printf("WebAdvertisedHost = %q (from ECS task metadata)", ip)
			return ip
		} else if err != nil {
			log.Printf("ECS metadata lookup failed, falling back: %v", err)
		}
	}

	if ip := firstNonLoopbackIPv4(); ip != "" {
		log.Printf("WebAdvertisedHost = %q (from local interfaces)", ip)
		return ip
	}

	log.Printf("WebAdvertisedHost = %q (last-resort fallback — workers likely cannot reach this)", "127.0.0.1")
	return "127.0.0.1"
}

// ipFromECSMetadata hits the ECS task metadata v4 endpoint and pulls out the
// first private IPv4 attached to the container.
func ipFromECSMetadata(containerURI string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	// {ECS_CONTAINER_METADATA_URI_V4}/task gives the whole task view; the
	// container's own URI returns just its slice, which also has Networks.
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, containerURI, nil)
	if err != nil {
		return "", err
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("ecs metadata: status %d", resp.StatusCode)
	}

	var payload struct {
		Networks []struct {
			IPv4Addresses []string `json:"IPv4Addresses"`
		} `json:"Networks"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&payload); err != nil {
		return "", err
	}

	for _, n := range payload.Networks {
		for _, ip := range n.IPv4Addresses {
			if ip != "" {
				return ip, nil
			}
		}
	}
	return "", fmt.Errorf("ecs metadata: no IPv4 addresses found")
}

// firstNonLoopbackIPv4 returns the first usable IPv4 from the host's network
// interfaces (skipping loopback / link-local). Mostly useful for local dev.
func firstNonLoopbackIPv4() string {
	ifaces, err := net.Interfaces()
	if err != nil {
		return ""
	}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			ip = ip.To4()
			if ip == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
				continue
			}
			return ip.String()
		}
	}
	return ""
}
