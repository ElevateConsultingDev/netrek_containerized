"""TCP+UDP connection with buffered reads for the Netrek protocol."""
import socket
import select
import struct
import sys
import time

from .constants import (
    CP_SPEED, CP_DIRECTION, CP_PHASER, CP_PLASMA, CP_TORP, CP_QUIT,
    CP_PRACTR, CP_SHIELD, CP_REPAIR, CP_ORBIT, CP_PLANLOCK, CP_PLAYLOCK,
    CP_BOMB, CP_BEAM, CP_CLOAK, CP_DET_TORPS, CP_DET_MYTORP, CP_REFIT,
    CP_TRACTOR, CP_REPRESS, CP_COUP, CP_DOCKPERM, CP_PING_RESPONSE,
    CP_UDP_REQ, SP_UDP_REPLY, SP_SEQUENCE, SP_SC_SEQUENCE,
    COMM_UDP, COMM_VERIFY, CONNMODE_PORT, CONNMODE_PACKET,
    SWITCH_UDP_OK, SWITCH_VERIFY, SWITCH_DENIED,
    UDP_HANDSHAKE_TIMEOUT,
)
from .protocol import PACKET_SIZES, decode_packet, cp_udp_req, cp_sequence
from .short_decode import VARIABLE_PACKET_TYPES, get_variable_size

# Packet types that should be sent over UDP when the channel is active.
# These are game-action packets where low latency matters more than
# guaranteed delivery.
_UDP_SEND_TYPES = frozenset({
    CP_SPEED, CP_DIRECTION, CP_PHASER, CP_PLASMA, CP_TORP, CP_QUIT,
    CP_PRACTR, CP_SHIELD, CP_REPAIR, CP_ORBIT, CP_PLANLOCK, CP_PLAYLOCK,
    CP_BOMB, CP_BEAM, CP_CLOAK, CP_DET_TORPS, CP_DET_MYTORP, CP_REFIT,
    CP_TRACTOR, CP_REPRESS, CP_COUP, CP_DOCKPERM, CP_PING_RESPONSE,
})


class Connection:
    def __init__(self):
        self.sock = None
        self.buf = b""

        # UDP state
        self.udp_sock = None
        self.udp_active = False
        self.udp_buf = b""
        self.server_host = None
        self.server_udp_port = 0
        self.local_udp_port = 0
        self._udp_state = "off"           # off / req_sent / verify / active
        self._udp_req_time = 0.0          # when we sent CP_UDP_REQ
        self._sc_sequence = 0             # server->client sequence counter
        self._sc_got_first = False        # have we seen the first sequence?

        # Desync detection state (persists across _parse_buffer calls)
        self._consecutive_unknown = 0
        self._desync_logged = False
        self._total_packets = 0
        self._logged_summary = False

    def connect(self, host, port):
        self.server_host = host
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.setblocking(False)

    def send(self, data):
        if not data:
            return
        if self.udp_active and data[0] in _UDP_SEND_TYPES:
            try:
                self.udp_sock.send(data)
                return
            except OSError:
                # UDP send failed -- fall through to TCP
                pass
        if self.sock:
            ptype = data[0]
            if ptype in _UDP_SEND_TYPES:
                print(f"TCP send: type={ptype} len={len(data)} data={list(data[:8])}")
            self.sock.sendall(data)

    def recv_packets(self, timeout=0.0):
        """Non-blocking receive from TCP and UDP. Returns list of (type, decoded_dict)."""
        packets = []

        # Build the read-set: always TCP, plus UDP if socket exists
        read_fds = []
        if self.sock:
            read_fds.append(self.sock)
        if self.udp_sock:
            read_fds.append(self.udp_sock)

        if not read_fds:
            return packets

        try:
            r, _, _ = select.select(read_fds, [], [], timeout)
        except (ValueError, OSError):
            return packets

        # --- TCP ---
        if self.sock in r:
            try:
                data = self.sock.recv(65536)
            except BlockingIOError:
                data = b""
            except (ConnectionResetError, OSError):
                print("Server disconnected.")
                sys.exit(1)
            if not data:
                print("Server disconnected.")
                sys.exit(1)
            self.buf += data

        # --- UDP ---
        if self.udp_sock and self.udp_sock in r:
            self._recv_udp()

        # Parse TCP buffer
        self._parse_buffer(self.buf, packets, is_udp=False)

        # Parse UDP buffer
        if self.udp_buf:
            self._parse_buffer(self.udp_buf, packets, is_udp=True)

        # Log packet summary once we've received enough
        self._total_packets += len(packets)
        if not self._logged_summary and self._total_packets > 100:
            from collections import Counter
            types = Counter(p[0] for p in packets)
            print(f"Packet summary ({self._total_packets} total): "
                  f"{dict(sorted(types.items()))}")
            self._logged_summary = True

        return packets

    # ------------------------------------------------------------------
    # Internal: buffer parsing
    # ------------------------------------------------------------------

    def _parse_buffer(self, buf, packets, is_udp):
        """Parse complete packets from buf, append to packets list.
        Modifies self.buf or self.udp_buf in place via reassignment at the end."""
        pos = 0
        unknown_run = 0  # count of consecutive unknown bytes in this call
        while pos < len(buf):
            ptype = buf[pos]

            if ptype in PACKET_SIZES:
                # Fixed-size packet
                size = PACKET_SIZES[ptype]
                if pos + size > len(buf):
                    break
                raw = buf[pos:pos + size]
                pos += size
                if unknown_run:
                    # False sync — a garbage byte matched a valid type.
                    # Don't reset, keep counting toward desync threshold.
                    self._consecutive_unknown += size
                    unknown_run += size
                    if self._consecutive_unknown >= 32:
                        if not self._desync_logged:
                            print(f"Desync: flushing {len(buf) - pos} bytes "
                                  f"(first unknown at byte {pos - size - unknown_run + size})")
                            self._desync_logged = True
                        pos = len(buf)
                        break
                    continue
                decoded = decode_packet(ptype, raw)
                if decoded is not None:
                    packets.append((ptype, decoded))

            elif ptype in VARIABLE_PACKET_TYPES:
                # Variable-length short packet — need at least 4 bytes for size calc
                if pos + 4 > len(buf):
                    break
                size = get_variable_size(buf[pos:])
                if size == 0:
                    self._consecutive_unknown += 1
                    unknown_run += 1
                    if self._consecutive_unknown >= 32:
                        if not self._desync_logged:
                            print(f"Desync: flushing {len(buf) - pos} bytes")
                            self._desync_logged = True
                        pos = len(buf)
                        break
                    pos += 1
                    continue
                if pos + size > len(buf):
                    break
                if unknown_run:
                    self._consecutive_unknown += size
                    unknown_run += size
                    pos += size
                    if self._consecutive_unknown >= 32:
                        if not self._desync_logged:
                            print(f"Desync: flushing {len(buf) - pos} bytes")
                            self._desync_logged = True
                        pos = len(buf)
                        break
                    continue
                raw = buf[pos:pos + size]
                pos += size
                # Short packets are decoded by gamestate handlers directly
                # from raw bytes — pass raw as the "decoded" value
                packets.append((ptype, raw))

            else:
                if not unknown_run and not is_udp:
                    print(f"Unknown packet type {ptype} at pos {pos} "
                          f"(buf {len(buf)} bytes, context: {list(buf[pos:pos+8])})")
                self._consecutive_unknown += 1
                unknown_run += 1
                if self._consecutive_unknown >= 32:
                    if not self._desync_logged:
                        print(f"Desync: flushing {len(buf) - pos} bytes")
                        self._desync_logged = True
                    pos = len(buf)
                    break
                pos += 1
                continue

        # Reset desync state when we parsed cleanly (no unknowns)
        if unknown_run == 0:
            self._consecutive_unknown = 0
            self._desync_logged = False

        remainder = buf[pos:]
        if is_udp:
            self.udp_buf = remainder
        else:
            self.buf = remainder

    # ------------------------------------------------------------------
    # Internal: UDP receive
    # ------------------------------------------------------------------

    def _recv_udp(self):
        """Read all available UDP datagrams."""
        # COW: receiving ANY data on the UDP socket while in "verify"
        # state completes the handshake.  The server sets commMode=COMM_UDP
        # before sending SWITCH_VERIFY, so the reply arrives over UDP
        # (not TCP).  COW doesn't even look for SWITCH_VERIFY specifically;
        # it just transitions on first UDP activity.
        if self._udp_state == "verify":
            self._udp_state = "active"
            self.udp_active = True
            print("UDP: handshake complete, channel active")

        while True:
            try:
                data = self.udp_sock.recv(65536)
            except BlockingIOError:
                break
            except OSError:
                break
            if not data:
                break
            self._process_udp_datagram(data)

    def _process_udp_datagram(self, data):
        """Validate sequence header and queue payload for parsing."""
        if len(data) < 4:
            return

        # Most UDP datagrams start with an SP_SEQUENCE header (4 bytes:
        # type, flag8, sequence_u16).  The very first datagram after the
        # handshake may lack it (the server sends SP_UDP_REPLY directly).
        if data[0] != SP_SEQUENCE:
            # No sequence header — queue the entire datagram as-is
            self.udp_buf += data
            return

        seq = struct.unpack("!xBH", data[:4])[1]

        if not self._sc_got_first:
            self._sc_sequence = seq
            self._sc_got_first = True
        else:
            # 16-bit wrap-aware comparison: is seq newer than _sc_sequence?
            diff = (seq - self._sc_sequence) & 0xFFFF
            if diff == 0 or diff > 0x7FFF:
                # Duplicate or old packet -- drop
                return
            self._sc_sequence = seq

        # Payload is everything after the 4-byte SP_SEQUENCE header
        payload = data[4:]
        if payload:
            self.udp_buf += payload

    # ------------------------------------------------------------------
    # UDP handshake
    # ------------------------------------------------------------------

    def start_udp_negotiation(self):
        """Begin the UDP handshake after login succeeds.

        Uses CONNMODE_PORT so the server tells us its UDP port and we
        connect() to it.  This works through Docker NAT / firewalls
        because the client initiates the UDP connection to a known
        mapped port, rather than the server trying to reach back to
        the client's ephemeral port.
        """
        if self._udp_state != "off":
            return
        try:
            self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_sock.bind(("", 0))
            self.udp_sock.setblocking(False)
            self.local_udp_port = self.udp_sock.getsockname()[1]
        except OSError as e:
            print(f"UDP: failed to create socket: {e}")
            self._udp_cleanup()
            return

        # Send the request over TCP using CONNMODE_PORT
        self.sock.sendall(cp_udp_req(COMM_UDP, CONNMODE_PORT, self.local_udp_port))
        self._udp_state = "req_sent"
        self._udp_req_time = time.monotonic()
        print(f"UDP: sent CP_UDP_REQ CONNMODE_PORT (local port {self.local_udp_port})")

    def handle_udp_reply(self, pkt):
        """Handle SP_UDP_REPLY -- advance the handshake state machine."""
        reply = pkt.get("reply", -1)

        if self._udp_state == "req_sent":
            if reply == SWITCH_UDP_OK:
                # Server tells us its UDP port; connect to it
                server_port = pkt.get("port", 0)
                if not server_port:
                    print("UDP: server sent no port, falling back to TCP")
                    self._udp_cleanup()
                    return
                self.server_udp_port = server_port
                self.udp_sock.connect((self.server_host, server_port))
                print(f"UDP: connected to server UDP port {server_port}")

                # Send COMM_VERIFY over the connected UDP socket
                self._udp_state = "verify"
                verify_pkt = cp_udp_req(COMM_VERIFY, CONNMODE_PORT,
                                        self.local_udp_port)
                self.udp_sock.send(verify_pkt)
                print("UDP: sent COMM_VERIFY")

            elif reply == SWITCH_DENIED:
                print("UDP: server denied request")
                self._udp_cleanup()

        elif self._udp_state == "verify":
            if reply == SWITCH_VERIFY:
                # Handshake complete
                self._udp_state = "active"
                self.udp_active = True
                print("UDP: handshake complete, channel active")

            elif reply == SWITCH_DENIED:
                print("UDP: server denied during verify")
                self._udp_cleanup()

    def check_udp_timeout(self):
        """Check for UDP handshake timeout. Called each tick."""
        if self._udp_state in ("req_sent", "verify"):
            elapsed = time.monotonic() - self._udp_req_time
            if elapsed > UDP_HANDSHAKE_TIMEOUT:
                print("UDP: handshake timed out, continuing TCP-only")
                self._udp_cleanup()

    def _udp_cleanup(self):
        """Tear down UDP state, revert to TCP-only."""
        if self.udp_sock:
            try:
                self.udp_sock.close()
            except OSError:
                pass
        self.udp_sock = None
        self.udp_active = False
        self.udp_buf = b""
        self._udp_state = "off"
        self._sc_sequence = 0
        self._sc_got_first = False

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def close(self):
        self._udp_cleanup()
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
