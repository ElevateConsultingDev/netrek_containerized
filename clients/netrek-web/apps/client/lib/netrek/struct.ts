// Minimal big-endian struct pack/unpack, enough for the netrek packet formats
// in packets.h (the "py-struct" comments). Same subset python's struct uses:
// b B h H i I l L x and <n>s, all network byte order.

type Tok = { count: number; char: string };

const SIZES: Record<string, number> = {
  b: 1,
  B: 1,
  x: 1,
  s: 1,
  h: 2,
  H: 2,
  i: 4,
  I: 4,
  l: 4,
  L: 4,
};

function parse(fmt: string): Tok[] {
  const toks: Tok[] = [];
  for (const m of fmt.replace(/^!/, "").matchAll(/(\d*)([bBhHiIlLxs])/g)) {
    toks.push({ count: m[1] ? parseInt(m[1], 10) : 1, char: m[2]! });
  }
  return toks;
}

export function calcsize(fmt: string): number {
  return parse(fmt).reduce((n, t) => n + t.count * SIZES[t.char]!, 0);
}

/** Values are numbers, except 's' fields which are Uint8Array. */
export type Field = number | Uint8Array;

export function unpack(fmt: string, buf: Uint8Array): Field[] {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const out: Field[] = [];
  let o = 0;
  for (const { count, char } of parse(fmt)) {
    if (char === "x") {
      o += count;
      continue;
    }
    if (char === "s") {
      out.push(buf.subarray(o, o + count));
      o += count;
      continue;
    }
    for (let i = 0; i < count; i++) {
      switch (char) {
        case "b":
          out.push(dv.getInt8(o));
          break;
        case "B":
          out.push(dv.getUint8(o));
          break;
        case "h":
          out.push(dv.getInt16(o, false));
          break;
        case "H":
          out.push(dv.getUint16(o, false));
          break;
        case "i":
        case "l":
          out.push(dv.getInt32(o, false));
          break;
        default:
          out.push(dv.getUint32(o, false));
          break;
      }
      o += SIZES[char]!;
    }
  }
  return out;
}

export function pack(fmt: string, ...values: Field[]): Uint8Array {
  const buf = new Uint8Array(calcsize(fmt));
  const dv = new DataView(buf.buffer);
  let o = 0;
  let v = 0;
  for (const { count, char } of parse(fmt)) {
    if (char === "x") {
      o += count;
      continue;
    }
    if (char === "s") {
      const src = values[v++] as Uint8Array;
      buf.set(src.subarray(0, count), o); // rest stays zero-padded
      o += count;
      continue;
    }
    for (let i = 0; i < count; i++) {
      const n = values[v++] as number;
      switch (char) {
        case "b":
          dv.setInt8(o, n);
          break;
        case "B":
          dv.setUint8(o, n & 0xff);
          break;
        case "h":
          dv.setInt16(o, n, false);
          break;
        case "H":
          dv.setUint16(o, n & 0xffff, false);
          break;
        case "i":
        case "l":
          dv.setInt32(o, n, false);
          break;
        default:
          dv.setUint32(o, n >>> 0, false);
          break;
      }
      o += SIZES[char]!;
    }
  }
  return buf;
}

/** Fixed-length ASCII field, NUL padded. */
export function str(s: string, len: number): Uint8Array {
  const out = new Uint8Array(len);
  for (let i = 0; i < Math.min(s.length, len); i++)
    out[i] = s.charCodeAt(i) & 0xff;
  return out;
}

/** Decode a NUL-terminated fixed field back to a string. */
export function cstr(b: Uint8Array): string {
  const end = b.indexOf(0);
  return String.fromCharCode(...(end === -1 ? b : b.subarray(0, end)));
}
