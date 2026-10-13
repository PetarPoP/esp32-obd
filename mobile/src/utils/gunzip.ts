// Minimal gzip / DEFLATE (RFC 1951/1952) decoder so vehicle profiles can be
// downloaded compressed without an extra native dependency.

class Huffman {
  counts = new Uint16Array(16);
  symbols = new Uint16Array(288);
}

class BitReader {
  private readonly data: Uint8Array;
  private pos: number;
  private bitBuf = 0;
  private bitCount = 0;

  constructor(data: Uint8Array, start: number) {
    this.data = data;
    this.pos = start;
  }

  bit(): number {
    if (this.bitCount === 0) {
      if (this.pos >= this.data.length) throw new Error("gzip: unexpected end of data");
      this.bitBuf = this.data[this.pos++];
      this.bitCount = 8;
    }
    const b = this.bitBuf & 1;
    this.bitBuf >>= 1;
    this.bitCount--;
    return b;
  }

  bits(n: number): number {
    let value = 0;
    for (let i = 0; i < n; i++) value |= this.bit() << i;
    return value;
  }

  alignToByte(): void {
    this.bitCount = 0;
  }

  byte(): number {
    if (this.pos >= this.data.length) throw new Error("gzip: unexpected end of data");
    return this.data[this.pos++];
  }
}

const LENGTH_BASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
const LENGTH_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
const DIST_BASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
const DIST_EXTRA = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
const CODE_LENGTH_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];

function buildHuffman(lengths: ArrayLike<number>, offset: number, count: number): Huffman {
  const h = new Huffman();
  for (let i = 0; i < count; i++) h.counts[lengths[offset + i]]++;
  h.counts[0] = 0;
  const offs = new Uint16Array(16);
  for (let i = 1; i < 16; i++) offs[i] = offs[i - 1] + h.counts[i - 1];
  for (let i = 0; i < count; i++) {
    const len = lengths[offset + i];
    if (len) h.symbols[offs[len]++] = i;
  }
  return h;
}

function decodeSymbol(r: BitReader, h: Huffman): number {
  let code = 0;
  let first = 0;
  let index = 0;
  for (let len = 1; len < 16; len++) {
    code |= r.bit();
    const count = h.counts[len];
    if (code - first < count) return h.symbols[index + (code - first)];
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  throw new Error("gzip: bad Huffman code");
}

const FIXED_LIT = (() => {
  const lengths = new Uint8Array(288);
  lengths.fill(8, 0, 144);
  lengths.fill(9, 144, 256);
  lengths.fill(7, 256, 280);
  lengths.fill(8, 280, 288);
  return buildHuffman(lengths, 0, 288);
})();
const FIXED_DIST = buildHuffman(new Uint8Array(30).fill(5), 0, 30);

class Output {
  buf: Uint8Array;
  len = 0;
  constructor(sizeHint: number) {
    this.buf = new Uint8Array(Math.max(1024, sizeHint));
  }
  push(b: number): void {
    if (this.len === this.buf.length) {
      const next = new Uint8Array(this.buf.length * 2);
      next.set(this.buf);
      this.buf = next;
    }
    this.buf[this.len++] = b;
  }
}

function inflateBlock(r: BitReader, out: Output, lit: Huffman, dist: Huffman): void {
  for (;;) {
    const sym = decodeSymbol(r, lit);
    if (sym < 256) {
      out.push(sym);
    } else if (sym === 256) {
      return;
    } else {
      const li = sym - 257;
      const length = LENGTH_BASE[li] + r.bits(LENGTH_EXTRA[li]);
      const di = decodeSymbol(r, dist);
      const distance = DIST_BASE[di] + r.bits(DIST_EXTRA[di]);
      if (distance > out.len) throw new Error("gzip: bad distance");
      for (let i = 0; i < length; i++) out.push(out.buf[out.len - distance]);
    }
  }
}

function readDynamicTrees(r: BitReader): [Huffman, Huffman] {
  const hlit = r.bits(5) + 257;
  const hdist = r.bits(5) + 1;
  const hclen = r.bits(4) + 4;
  const codeLengths = new Uint8Array(19);
  for (let i = 0; i < hclen; i++) codeLengths[CODE_LENGTH_ORDER[i]] = r.bits(3);
  const codeTree = buildHuffman(codeLengths, 0, 19);
  const lengths = new Uint8Array(hlit + hdist);
  let n = 0;
  while (n < hlit + hdist) {
    const sym = decodeSymbol(r, codeTree);
    if (sym < 16) {
      lengths[n++] = sym;
    } else {
      let repeat = 0;
      let value = 0;
      if (sym === 16) {
        if (n === 0) throw new Error("gzip: bad repeat");
        value = lengths[n - 1];
        repeat = 3 + r.bits(2);
      } else if (sym === 17) {
        repeat = 3 + r.bits(3);
      } else {
        repeat = 11 + r.bits(7);
      }
      while (repeat-- > 0) lengths[n++] = value;
    }
  }
  return [buildHuffman(lengths, 0, hlit), buildHuffman(lengths, hlit, hdist)];
}

/** Decompresses a gzip file (as produced by Python's gzip module). */
export function gunzip(data: Uint8Array): Uint8Array {
  if (data.length < 18 || data[0] !== 0x1f || data[1] !== 0x8b || data[2] !== 8) {
    throw new Error("gzip: not a gzip file");
  }
  const flags = data[3];
  let pos = 10;
  if (flags & 4) pos += 2 + (data[pos] | (data[pos + 1] << 8));
  if (flags & 8) while (data[pos++] !== 0);
  if (flags & 16) while (data[pos++] !== 0);
  if (flags & 2) pos += 2;
  const n = data.length;
  const isize = (data[n - 4] | (data[n - 3] << 8) | (data[n - 2] << 16) | (data[n - 1] << 24)) >>> 0;

  const r = new BitReader(data, pos);
  const out = new Output(isize);
  let last = 0;
  while (!last) {
    last = r.bit();
    const type = r.bits(2);
    if (type === 0) {
      r.alignToByte();
      const len = r.byte() | (r.byte() << 8);
      r.byte();
      r.byte(); // one's complement of len
      for (let i = 0; i < len; i++) out.push(r.byte());
    } else if (type === 1) {
      inflateBlock(r, out, FIXED_LIT, FIXED_DIST);
    } else if (type === 2) {
      const [lit, dist] = readDynamicTrees(r);
      inflateBlock(r, out, lit, dist);
    } else {
      throw new Error("gzip: bad block type");
    }
  }
  return out.buf.subarray(0, out.len);
}

/** UTF-8 bytes to string (Hermes has no TextDecoder on all versions). */
export function utf8Decode(bytes: Uint8Array): string {
  let out = "";
  const chunk: number[] = [];
  for (let i = 0; i < bytes.length; ) {
    const b = bytes[i++];
    let cp = b;
    if (b >= 0xf0) {
      cp = ((b & 0x07) << 18) | ((bytes[i++] & 0x3f) << 12) | ((bytes[i++] & 0x3f) << 6) | (bytes[i++] & 0x3f);
    } else if (b >= 0xe0) {
      cp = ((b & 0x0f) << 12) | ((bytes[i++] & 0x3f) << 6) | (bytes[i++] & 0x3f);
    } else if (b >= 0xc0) {
      cp = ((b & 0x1f) << 6) | (bytes[i++] & 0x3f);
    }
    if (cp > 0xffff) {
      cp -= 0x10000;
      chunk.push(0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff));
    } else {
      chunk.push(cp);
    }
    if (chunk.length > 8192) {
      out += String.fromCharCode(...chunk);
      chunk.length = 0;
    }
  }
  return out + String.fromCharCode(...chunk);
}
