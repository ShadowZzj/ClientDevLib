"""登录/封包加解密:game-DES、XOR、Blowfish、握手 xor 选择。"""
from __future__ import annotations

try:
    from Crypto.Cipher import Blowfish as PyCryptoBlowfish
except Exception:  # pragma: no cover - optional dependency fallback
    PyCryptoBlowfish = None

from .protocol import u32


XOR_KEYS = [
    bytes.fromhex("40 23 24 25"),
    bytes.fromhex("23 24 25 26"),
    bytes.fromhex("24 25 26 2a"),
    bytes.fromhex("25 26 2a 2b"),
    bytes.fromhex("26 2a 2b 21"),
    bytes.fromhex("2a 2b 21 40"),
    bytes.fromhex("2b 21 40 23"),
    bytes.fromhex("21 40 23 24"),
]
DEFAULT_XOR_INDEX = 7

KNOWN_ACCOUNT_XOR_INDEX = {
    # Fallback only. In normal "auto" mode the key is derived from the lobby
    # handshake for this TCP connection.
    "gongyu901121": 3,   # shadowpope sample, 2026-06-04 20:21
    "gongyu9011212": 4,  # shadowsing sample, 2026-06-04 22:27
    "gongyu9011213": 7,  # shadowdance sample, 2026-06-04 18:08
}

DATE_MAGIC = bytes.fromhex("32 30 31 37 31 31 31 37 00 00 00 00 67 5f 70 4c")
DATE_MAGIC_ENCRYPTED = bytes.fromhex("1b 35 be b7 4f 8d 24 39 10 5f 32 85 22 87 d2 2e")

# These are the observed SO3DPlus.exe values from the captured login. Passing
# --client-exe is better because the game sends the local file time/size.
FALLBACK_FILETIME_HIGH = 0x01DC3DEE
FALLBACK_FILETIME_LOW = 0x76664113
FALLBACK_CLIENT_SIZE = 0x007CC5C8

BILL_OPEN_DWORDS = (0x001AE890, 0x00B2B937, 0x00CDCEF0, 0x001AE828)

SO3D_GAME_BLOWFISH_KEY = bytes.fromhex(
    "74 6A 71 6A 73 6A 61 6E 61 6B 73 67 64 6B 64 79 "
    "21 5F 40 29 23 28 24 31 32 33 00"
)
SO3D_BLOWFISH_IV = b"\x00" * 8

CAPTURED_GAME1842_SELF_TEST = bytes.fromhex(
    "18 00 00 00 A1 45 06 00 "
    "E4 8C 74 AA 2A EB FE 5D 34 B3 B4 0C 1E DB FB 03"
)
CAPTURED_GAME1842_SELF_TEST_PROTO = 411041

IP = [
    58, 50, 42, 34, 26, 18, 10, 2,
    60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,
    64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1,
    59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,
    63, 55, 47, 39, 31, 23, 15, 7,
]
FP = [
    40, 8, 48, 16, 56, 24, 64, 32,
    39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30,
    37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28,
    35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26,
    33, 1, 41, 9, 49, 17, 57, 25,
]
EXP = [
    32, 1, 2, 3, 4, 5,
    4, 5, 6, 7, 8, 9,
    8, 9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32, 1,
]
PBOX = [
    16, 7, 20, 21, 29, 12, 28, 17,
    1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9,
    19, 13, 30, 6, 22, 11, 4, 25,
]
SBOX = [
    [14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 2, 14, 10, 0, 6, 13],
    [15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9],
    [10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12],
    [7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14],
    [2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3],
    [4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12],
    [12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13],
    [13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11],
]

K_ENC_BITS = (
    "001010010011111101010001101100110111111110111110"
    "001110110101000000011100100111110111111111010111"
    "000011000000000111111100101111111110001111110101"
    "100101100100100000111101111100111110111111000111"
    "100011110010101100100000111111101010011110011111"
    "100010100011111010101101111111110111011111001111"
    "110110010011011001001000011111101111001111101011"
    "010000001101111011101000111101101111110101101111"
    "101101001010000010110000011011111101110111011111"
    "100101100000111000110110110011111111010111111011"
    "111011100011001000010100111011111101111101101101"
    "000011101001011001101100110110101101111111111110"
    "110010100101000001111010110111111101111110111101"
    "101011001100101101101000110110110111111111111001"
    "100000100111101100001011111110111111101100111101"
    "001111100000001011000111101111111100111011111111"
)
K_ENC = [1 if ch == "1" else 0 for ch in K_ENC_BITS]


def resolve_xor_index(account: str, requested: object) -> int | None:
    if isinstance(requested, int):
        value = requested
    else:
        text = str(requested or "auto").strip().lower()
        if text in ("", "auto"):
            return None
        else:
            value = int(text, 0)
    if not 0 <= value < len(XOR_KEYS):
        raise ValueError(f"xor_index must be 0..{len(XOR_KEYS) - 1}, got {value}")
    return value


def bytes_to_bits(block: bytes) -> list[int]:
    bits: list[int] = []
    for value in block:
        bits.extend((value >> shift) & 1 for shift in range(7, -1, -1))
    return bits


def bits_to_bytes(bits: list[int]) -> bytes:
    out = bytearray()
    for i in range(0, len(bits), 8):
        value = 0
        for bit in bits[i:i + 8]:
            value = (value << 1) | int(bit)
        out.append(value)
    return bytes(out)


def feistel_bits(right: list[int], round_no: int) -> list[int]:
    expanded = [right[i - 1] for i in EXP]
    mixed = [expanded[i] ^ K_ENC[round_no * 48 + i] for i in range(48)]
    sbox_bits: list[int] = []
    for box in range(8):
        base = box * 6
        index = (
            32 * mixed[base]
            + 16 * mixed[base + 5]
            + 8 * mixed[base + 1]
            + 4 * mixed[base + 2]
            + 2 * mixed[base + 3]
            + mixed[base + 4]
        )
        value = SBOX[box][index]
        sbox_bits.extend([(value >> 3) & 1, (value >> 2) & 1, (value >> 1) & 1, value & 1])
    return [sbox_bits[i - 1] for i in PBOX]


def encrypt_block(block: bytes) -> bytes:
    bits = bytes_to_bits(block)
    permuted = [bits[i - 1] for i in IP]
    left = permuted[:32]
    right = permuted[32:]

    for round_no in range(16):
        old_right = right[:]
        pbox = feistel_bits(right, round_no)
        right = [pbox[i] ^ left[i] for i in range(32)]
        left = old_right

    # This matches sub_5CC2E0's final stack layout after sub_5CCB60.
    preoutput = left + right
    return bits_to_bytes([preoutput[i - 1] for i in FP])


def decrypt_block(block: bytes) -> bytes:
    bits = bytes_to_bits(block)
    permuted = [bits[i - 1] for i in IP]
    left = permuted[:32]
    right = permuted[32:]

    for round_no in range(15, -1, -1):
        pbox = feistel_bits(left, round_no)
        previous_left = [right[i] ^ pbox[i] for i in range(32)]
        previous_right = left
        left = previous_left
        right = previous_right

    preoutput = left + right
    return bits_to_bytes([preoutput[i - 1] for i in FP])


def game_des_encrypt(data: bytes) -> bytes:
    out = bytearray()
    full = (len(data) // 8) * 8
    for offset in range(0, full, 8):
        out.extend(encrypt_block(data[offset:offset + 8]))
    out.extend(data[full:])
    return bytes(out)


def game_des_decrypt(data: bytes) -> bytes:
    out = bytearray()
    full = (len(data) // 8) * 8
    for offset in range(0, full, 8):
        out.extend(decrypt_block(data[offset:offset + 8]))
    out.extend(data[full:])
    return bytes(out)


def handshake_xor_index(raw: bytes) -> tuple[int, bytes]:
    if len(raw) < 12:
        raise ValueError(f"handshake packet too short: {len(raw)}")
    decoded = game_des_decrypt(raw[4:12])
    marker = u32(decoded, 0)
    key_seed = u32(decoded, 4)
    low_parity = sum((key_seed >> bit) & 1 for bit in range(0, 9)) & 1
    high_parity = sum((key_seed >> bit) & 1 for bit in range(16, 24)) & 1
    if low_parity != ((key_seed >> 24) & 1) or high_parity != ((key_seed >> 28) & 1):
        raise ValueError(f"bad lobby handshake key checksum: {decoded.hex(' ')}")
    key_index = (key_seed >> 25) & 7
    if key_index == 0:
        key_index = DEFAULT_XOR_INDEX
    if not 0 <= key_index < len(XOR_KEYS):
        raise ValueError(f"bad lobby handshake key index {key_index}")
    return key_index, decoded


def xor_payload(packet: bytes, key_index: int = DEFAULT_XOR_INDEX) -> bytes:
    key = XOR_KEYS[key_index]
    data = bytearray(packet)
    for offset in range(4, len(data)):
        data[offset] ^= key[(offset - 4) % 4]
    return bytes(data)
