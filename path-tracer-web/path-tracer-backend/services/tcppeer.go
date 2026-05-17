package services

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"sync"
	"time"
)

// Mirror the wire constants defined in path-tracer-core/src/cloud/tcp_peer.hpp.
// Both sides must agree on these byte-for-byte.
const (
	tcpMessageHeaderSize = 8
	tcpMaxMessageSize    = 50 * 1024 * 1024
)

// MessageType enum value (uint32, big-endian on the wire).
type tcpMessageType uint32

const (
	msgRayBatch   tcpMessageType = 1
	msgTerminate  tcpMessageType = 2
	msgHandshake  tcpMessageType = 3
	msgPixelBatch tcpMessageType = 4
)

// TCPPeerListener accepts a single inbound TCP connection from the master,
// reads framed messages, and dispatches pixel batches and termination signals
// through the supplied callbacks. It is the Go-side counterpart to the C++
// cloud::tcp_peer class and uses an identical wire format:
//
//	[4 bytes: msg_type big-endian uint32]
//	[4 bytes: msg_len  big-endian uint32]
//	[msg_len bytes:    payload]
//
// PIXEL_BATCH payloads are JSON arrays of pixel objects: [{X,Y,color,alpha}, ...].
type TCPPeerListener struct {
	port int

	onPixelBatch func(jsonBody string)
	onTerminate  func()

	listener net.Listener

	done    chan struct{} // closed when the master signals TERMINATE or the conn drops
	closeMu sync.Mutex
	closed  bool
}

// NewTCPPeerListener constructs a listener bound to the given port.
// onPixelBatch is invoked for every PIXEL_BATCH message body (a single JSON
// array string). onTerminate fires exactly once when the master sends
// TERMINATE or the connection ends.
func NewTCPPeerListener(port int, onPixelBatch func(string), onTerminate func()) *TCPPeerListener {
	return &TCPPeerListener{
		port:         port,
		onPixelBatch: onPixelBatch,
		onTerminate:  onTerminate,
		done:         make(chan struct{}),
	}
}

// Start binds the listener and accepts connections in a background goroutine.
// Returns the bound port (useful when port=0 / ephemeral) and any bind error.
func (p *TCPPeerListener) Start(ctx context.Context) (int, error) {
	addr := fmt.Sprintf(":%d", p.port)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return 0, fmt.Errorf("tcp listen %s: %w", addr, err)
	}
	p.listener = ln

	boundPort := ln.Addr().(*net.TCPAddr).Port
	log.Printf("TCP peer listener bound on port %d", boundPort)

	go p.acceptLoop(ctx)
	return boundPort, nil
}

// Done returns a channel that's closed when the master has signalled
// termination or the inbound connection has been torn down.
func (p *TCPPeerListener) Done() <-chan struct{} {
	return p.done
}

// Stop closes the listener and any inbound connection. Safe to call multiple
// times.
func (p *TCPPeerListener) Stop() {
	p.closeMu.Lock()
	defer p.closeMu.Unlock()
	if p.closed {
		return
	}
	p.closed = true

	if p.listener != nil {
		_ = p.listener.Close()
	}
	select {
	case <-p.done:
	default:
		close(p.done)
	}
}

func (p *TCPPeerListener) signalTerminate() {
	p.closeMu.Lock()
	defer p.closeMu.Unlock()
	select {
	case <-p.done:
		return
	default:
	}
	close(p.done)

	if p.onTerminate != nil {
		p.onTerminate()
	}
}

func (p *TCPPeerListener) acceptLoop(ctx context.Context) {
	for {
		conn, err := p.listener.Accept()
		if err != nil {
			if p.isClosed() {
				return
			}
			// Transient — log and back off briefly.
			log.Printf("TCP accept error: %v", err)
			select {
			case <-ctx.Done():
				return
			case <-time.After(100 * time.Millisecond):
			}
			continue
		}

		log.Printf("Accepted TCP connection from %s", conn.RemoteAddr())
		// Master only opens one connection; once it drops we treat the render
		// as finished.
		go func(c net.Conn) {
			defer c.Close()
			if err := p.handleConn(ctx, c); err != nil && !errors.Is(err, io.EOF) {
				log.Printf("TCP connection handler ended: %v", err)
			}
			p.signalTerminate()
		}(conn)
	}
}

func (p *TCPPeerListener) isClosed() bool {
	p.closeMu.Lock()
	defer p.closeMu.Unlock()
	return p.closed
}

func (p *TCPPeerListener) handleConn(ctx context.Context, conn net.Conn) error {
	header := make([]byte, tcpMessageHeaderSize)

	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}

		if _, err := io.ReadFull(conn, header); err != nil {
			return err
		}

		msgType := tcpMessageType(binary.BigEndian.Uint32(header[0:4]))
		msgLen := binary.BigEndian.Uint32(header[4:8])

		if msgLen > tcpMaxMessageSize {
			return fmt.Errorf("oversized message: type=%d len=%d", msgType, msgLen)
		}

		var body []byte
		if msgLen > 0 {
			body = make([]byte, msgLen)
			if _, err := io.ReadFull(conn, body); err != nil {
				return err
			}
		}

		switch msgType {
		case msgHandshake:
			log.Printf("Received handshake from peer: %s", string(body))
		case msgPixelBatch:
			if p.onPixelBatch != nil {
				p.onPixelBatch(string(body))
			}
		case msgTerminate:
			log.Printf("Received TERMINATE from master, stopping listener")
			p.signalTerminate()
			return nil
		case msgRayBatch:
			// Master never sends rays to the web peer; ignore defensively.
			log.Printf("Ignoring unexpected RAY_BATCH (%d bytes)", len(body))
		default:
			log.Printf("Unknown TCP message type: %d", msgType)
		}
	}
}
