// Netrek TCP byte stream -> packets. Packets are laid end to end with no
// framing; the first byte gives the type, which gives the length.
// ponytail: fixed-size (long) packets only. We never send CP_S_REQ, so the
// server never sends the SP_S_* variable-length compressed forms. Add
// short-packet support only if bandwidth ever matters.
import { PACKET_SIZES, decodePacket, type Packet } from "./protocol";

export class PacketStream {
  private buf = new Uint8Array(0);

  feed(chunk: Uint8Array): Packet[] {
    const merged = new Uint8Array(this.buf.length + chunk.length);
    merged.set(this.buf);
    merged.set(chunk, this.buf.length);

    const out: Packet[] = [];
    let pos = 0;
    while (pos < merged.length) {
      const size = PACKET_SIZES[merged[pos]!];
      if (size === undefined) {
        // Desync: nothing sane to do but drop the byte and hope to realign.
        console.warn(`netrek: unknown packet type ${merged[pos]} at ${pos}`);
        pos += 1;
        continue;
      }
      if (pos + size > merged.length) break;
      const p = decodePacket(merged[pos]!, merged.subarray(pos, pos + size));
      if (p) out.push(p);
      pos += size;
    }
    this.buf = merged.subarray(pos);
    return out;
  }
}
