#!/usr/bin/env python3
"""SO3D protect-layer (Blowfish-CBC) reverse implementation.

Reversed from so3dplus.exe (unpackd) via IDA + CE dynamic analysis:

  Protect_SealPacket (0x5CA910):
      memmove(body+8, body, size)          # make room for an 8-byte header
      body[0:4]  = GetTickCount()           # per-packet seed (LE)
      body[4:8]  = CRC32(body)              # integrity (reflected, poly EDB88320)
      Protect_BlowfishCBC_Encrypt(body, body, size+8)

  Protect_BlowfishCBC_Encrypt (0x5CB920):
      aligned = align8(size)                # if cbc: size+1 first
      pad with (aligned-size) value bytes   # PKCS#7-style when cbc
      for each 8-byte block:
          if cbc: lo ^= prevLo; hi ^= prevHi
          bswap(lo); bswap(hi)
          BlowfishEncryptBlock(lo, hi)      # 16-round, big-endian halves
          bswap(lo); bswap(hi)
          if cbc: prev = (lo, hi)           # CBC feedback = ciphertext block

  Blowfish key  (hardcoded): "tjqjsjanaksgdkdy!_@)#($123" passed with len=27
  g_protect_cbcMode = 1, g_protect_maxLen = 0x800

The 8-byte FRAME header (totalLen u32, protocolId u32) is NOT encrypted; the
protect layer only touches the body (offset 8..end).

This module uses pycryptodome's textbook Blowfish (ECB) as the block primitive
and drives CBC-with-ciphertext-feedback manually so it matches the game exactly.
The game loads each 8-byte block as two big-endian u32 halves (bswap before the
ECB call, bswap after), which is exactly the byte order pycryptodome's Blowfish
already uses internally -- so for ECB we can feed the little-endian block bytes
directly and the per-block bswap cancels out. We therefore XOR the CBC feedback
on the *little-endian block bytes* (i.e. on the raw 8 bytes), which is what the
game does once the bswaps on both sides are accounted for.
"""
from __future__ import annotations

import struct

from Crypto.Cipher import Blowfish as _BF

# Hardcoded key. SetKey passes length 27 (the 26 visible chars + NUL terminator).
KEY = b"tjqjsjanaksgdkdy!_@)#($123\x00"
KEY_LEN = 27
CBC = True
MASK = 0xFFFFFFFF


def _ecb():
    return _BF.new(KEY, _BF.MODE_ECB)


def _encrypt_block(ecb, block8: bytes) -> bytes:
    """Encrypt one 8-byte block the way the game does.

    Game: load lo,hi as LE u32 -> bswap each -> Blowfish(lo,hi) on big-endian
    halves -> bswap each -> store LE. pycryptodome treats the 8 input bytes as
    two big-endian halves directly, so passing the raw block bytes reproduces
    "bswap(load LE) == read BE" and the trailing bswap == "store BE". Net: feed
    the raw little-endian block bytes straight through ECB.
    """
    return ecb.encrypt(block8)


def _decrypt_block(ecb, block8: bytes) -> bytes:
    return ecb.decrypt(block8)


def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if (crc & 1) else (crc >> 1)
    return (~crc) & MASK


def _align8(n: int) -> int:
    if CBC:
        n += 1
    return ((n + 7) // 8) * 8


def cbc_encrypt(data: bytes) -> bytes:
    ecb = _ecb()
    size = len(data)
    aligned = _align8(size)
    pad = aligned - size
    buf = bytearray(data) + bytes([pad if CBC else 0]) * pad
    out = bytearray()
    prev = b"\x00" * 8
    for off in range(0, aligned, 8):
        block = bytes(buf[off:off + 8])
        if CBC:
            block = bytes(a ^ b for a, b in zip(block, prev))
        ct = _encrypt_block(ecb, block)
        out += ct
        if CBC:
            prev = ct
    return bytes(out)


def cbc_decrypt(data: bytes) -> bytes:
    ecb = _ecb()
    out = bytearray()
    prev = b"\x00" * 8
    for off in range(0, len(data), 8):
        ct = bytes(data[off:off + 8])
        pt = _decrypt_block(ecb, ct)
        if CBC:
            pt = bytes(a ^ b for a, b in zip(pt, prev))
            prev = ct
        out += pt
    return bytes(out)


def seal_body(body: bytes, tick: int) -> bytes:
    """Reproduce Protect_SealPacket on a body (no frame header)."""
    inner = struct.pack("<II", tick & MASK, crc32(body)) + body
    return cbc_encrypt(inner)


def unseal_body(sealed: bytes, body_len: int | None = None) -> tuple[int, int, bytes]:
    """Inverse: returns (tick, crc, body) after stripping pad + 8B inner header.

    If body_len is given (from the frame), the body is sliced exactly; otherwise
    the trailing CBC pad byte count is used to trim.
    """
    dec = cbc_decrypt(sealed)
    tick, crc = struct.unpack_from("<II", dec, 0)
    inner = dec[8:]
    if body_len is not None:
        body = inner[:body_len]
    else:
        pad = dec[-1] if CBC and dec else 0
        body = inner[:len(inner) - pad] if 0 < pad <= 8 else inner
    return tick, crc, body


def seal_packet(proto_id: int, body: bytes, tick: int) -> bytes:
    """Build a full on-wire frame: u32 totalLen | u32 protoId | sealed_body."""
    sealed = seal_body(body, tick)
    total = 8 + len(sealed)
    return struct.pack("<II", total, proto_id & MASK) + sealed


def unseal_packet(frame: bytes) -> tuple[int, int, int, int, bytes]:
    """Parse a full frame. Returns (totalLen, protoId, tick, crc, body).

    The frame's totalLen counts header(8)+sealed; body length is unknown from
    the frame alone (it was padded), so body is trimmed by the CBC pad byte.
    """
    total, proto_id = struct.unpack_from("<II", frame, 0)
    sealed = frame[8:total]
    tick, crc, body = unseal_body(sealed)
    return total, proto_id, tick, crc, body


if __name__ == "__main__":
    # Self-test against captured 1842 packets (see net/sendlog WIRE_SEND).
    def _hx(s: str) -> bytes:
        return bytes.fromhex(s.replace(" ", ""))

    # Pair A: LOG len=52 (proto 411005) -> WIRE len=64
    log_a = _hx(
        "34 00 00 00 7D 45 06 00 47 AB 1B 20 EE 3D DC 01 13 41 66 76 C8 C5 7C "
        "00 00 00 00 00 00 00 00 00 00 00 00 00 73 68 61 64 6F 77 64 61 6E 63 "
        "65 00 00 00 00 00")
    wire_a = _hx(
        "40 00 00 00 7D 45 06 00 25 37 6D C8 5A 0F E8 B6 11 55 15 0E 69 2F B9 "
        "77 CB 03 7D 49 87 F6 5E 87 44 78 3E 2C 5E 39 F0 BA 03 6D 84 04 DA 57 "
        "38 FE 80 9D FE F7 D7 7B 7F 1A F5 53 64 F7 BD 81 76 18")

    # Pair B: LOG len=12 (proto 412039) -> WIRE len=24
    log_b = _hx("0C 00 00 00 87 49 06 00 87 49 06 00")
    wire_b = _hx(
        "18 00 00 00 87 49 06 00 F2 42 E5 A2 F6 9F 80 5A 2A 51 DC FF 27 89 82 2B")

    for name, log, wire in (("A", log_a, wire_a), ("B", log_b, wire_b)):
        log_total, log_proto = struct.unpack_from("<II", log, 0)
        log_body = log[8:log_total]
        w_total, w_proto, tick, crc, body = unseal_packet(wire)
        crc_ok = (crc == crc32(body))
        body_ok = (body == log_body)
        # Re-encrypt with recovered tick; should byte-match WIRE exactly.
        re = seal_packet(w_proto, body, tick)
        enc_ok = (re == wire)
        print(f"[pair {name}] proto={w_proto} tick=0x{tick:08X} "
              f"crc_ok={crc_ok} decrypt_body_ok={body_ok} reencrypt_ok={enc_ok}")
        if not body_ok:
            print(f"   want body={log_body.hex(' ')}")
            print(f"   got  body={body.hex(' ')}")
        if not enc_ok:
            print(f"   want wire={wire.hex(' ')}")
            print(f"   got  wire={re.hex(' ')}")
