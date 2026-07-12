"""各类封包构造与解析(login/bill/chat/game/cg_enter 等)。"""
from __future__ import annotations

import socket
import struct
import time
import zlib
from pathlib import Path

from .crypto import PyCryptoBlowfish

from .protocol import (
    CG_ENTER,
    CHAT_LOGIN_MAGIC,
    CHAT_LOGIN_OPCODE,
    CHAT_STATUS_KIND,
    CHAT_STATUS_OPCODE,
    CL_CREATE_CHARACTER,
    CL_LOGIN,
    CL_LOGIN_SECONDARY,
    CREATE_CHAR_APPEARANCE,
    ChatStatus,
    ClientFileInfo,
    GAME_LOCAL_ID_PROTOS,
    GameServerInfo,
    PROTO_NAMES,
    SC_LOCAL_MAP_STATE,
    SC_MAP_HINT,
    ascii_preview,
    c_string,
    fixed_bytes,
    proto_name,
    short_hex,
    u32,
)
from .crypto import (
    BILL_OPEN_DWORDS,
    DATE_MAGIC,
    DEFAULT_XOR_INDEX,
    FALLBACK_CLIENT_SIZE,
    FALLBACK_FILETIME_HIGH,
    FALLBACK_FILETIME_LOW,
    SO3D_BLOWFISH_IV,
    SO3D_GAME_BLOWFISH_KEY,
    XOR_KEYS,
    game_des_encrypt,
    xor_payload,
)

from .logio import log_print as print


def encrypted_body_size(size: int) -> int:
    # Game mode is CBC + PKCS-ish padding, and sub_5CBA50 adds one byte before
    # rounding so a naturally aligned body still gets a full padding block.
    adjusted = size + 1
    return ((adjusted + 7) // 8) * 8


def game1841_plain_body(body: bytes, tick_ms: int | None = None) -> bytes:
    tick = (int(time.monotonic() * 1000) if tick_ms is None else tick_ms) & 0xFFFFFFFF
    crc = zlib.crc32(body) & 0xFFFFFFFF
    plain_len = len(body) + 8
    padded_len = encrypted_body_size(plain_len)
    pad_value = padded_len - plain_len
    return struct.pack("<II", tick, crc) + body + bytes([pad_value]) * pad_value


def game1841_encrypt_body(body: bytes, tick_ms: int | None = None) -> bytes:
    plain = game1841_plain_body(body, tick_ms=tick_ms)
    if PyCryptoBlowfish is not None:
        cipher = PyCryptoBlowfish.new(SO3D_GAME_BLOWFISH_KEY, PyCryptoBlowfish.MODE_CBC, iv=SO3D_BLOWFISH_IV)
        return cipher.encrypt(plain)
    raise RuntimeError("pycryptodome is required for 1842 Blowfish/CBC packets; run: python -m pip install pycryptodome")


def game1841_decrypt_body(encrypted: bytes) -> bytes:
    if PyCryptoBlowfish is None:
        raise RuntimeError("pycryptodome is required for 1842 decrypt diagnostics")
    cipher = PyCryptoBlowfish.new(SO3D_GAME_BLOWFISH_KEY, PyCryptoBlowfish.MODE_CBC, iv=SO3D_BLOWFISH_IV)
    return cipher.decrypt(encrypted)


def game1841_packet(proto: int, body: bytes, tick_ms: int | None = None) -> bytes:
    encrypted = game1841_encrypt_body(body, tick_ms=tick_ms)
    return struct.pack("<II", len(encrypted) + 8, proto) + encrypted


def send_game1841(
    sock: socket.socket,
    proto: int,
    body: bytes,
    label: str | None = None,
    *,
    key_index: int,
    debug: bool,
) -> None:
    name = proto_name(proto) if label is None else label
    plain_packet = game_packet(proto, body)
    packet = game1841_packet(proto, body)
    debug_game_send(name, plain_packet, xor=False, key_index=key_index, enabled=debug)
    if debug:
        print(f"[debug][wire:{name}] len={len(packet)} raw={short_hex(packet)}")
    sock.sendall(packet)
    print(f"[game] send {name}")


def iter_framed_packets(data: bytes):
    offset = 0
    data_len = len(data)
    while offset + 8 <= data_len:
        size = u32(data, offset)
        if 8 <= size <= data_len - offset:
            frame = data[offset:offset + size]
            yield frame
            offset += size
            continue
        offset += 1


# 单帧上限:实测最大整桶包约 4112 字节,留足余量防对齐错乱时无限等待。
MAX_GAME_FRAME = 1 << 20


def take_complete_frames(buf: bytearray) -> bytes:
    """从累积缓冲里弹出所有完整帧(拼接成一段返回),把尾部不完整的帧留在 buf 里等下次 recv。
    game socket 自连接起就帧对齐,所以正常只会走"完整/等待"两条路;size 非法时滑 1 字节自愈。"""
    out = bytearray()
    offset = 0
    n = len(buf)
    while offset + 8 <= n:
        size = u32(buf, offset)
        if size < 8 or size > MAX_GAME_FRAME:
            offset += 1            # 对齐错乱,滑 1 字节重找
            continue
        if offset + size > n:
            break                  # 帧未收全,留到下次
        out += buf[offset:offset + size]
        offset += size
    del buf[:offset]
    return bytes(out)


def extract_local_player_id(data: bytes) -> int | None:
    status = extract_chat_status(data)
    if status and status.player_id:
        return status.player_id
    for frame in iter_framed_packets(data):
        if len(frame) < 12:
            continue
        proto = u32(frame, 4)
        if proto not in GAME_LOCAL_ID_PROTOS:
            continue
        candidate = u32(frame, 8)
        if 0 < candidate < 0x10000:
            return candidate
    return None


def extract_chat_status(data: bytes) -> ChatStatus | None:
    status = ChatStatus()
    for frame in iter_framed_packets(data):
        if len(frame) < 12:
            continue
        proto = u32(frame, 4)
        if proto == SC_LOCAL_MAP_STATE and len(frame) >= 24:
            player_id = u32(frame, 12)
            map_id = u32(frame, 20)
            if 0 < player_id < 0x100000:
                status.player_id = player_id
            if 0 < map_id < 0x10000:
                status.map_id = map_id
        elif proto == SC_MAP_HINT and len(frame) >= 12:
            map_id = u32(frame, 8)
            if 0 < map_id < 0x10000:
                status.map_id = map_id
    if status.player_id or status.map_id:
        return status
    return None


def merge_chat_status(current: ChatStatus, chunk: bytes, *, label: str = "") -> bool:
    found = extract_chat_status(chunk)
    if not found:
        return False
    changed = False
    if found.player_id and found.player_id != current.player_id:
        current.player_id = found.player_id
        changed = True
    if found.map_id and found.map_id != current.map_id:
        current.map_id = found.map_id
        changed = True
    if changed:
        suffix = f" from {label}" if label else ""
        print(f"[chat] status source{suffix}: player_id=0x{current.player_id:08X} map_id={current.map_id}")
    return changed


def game_packet(proto: int, body: bytes = b"", *, xor: bool = False, key_index: int = DEFAULT_XOR_INDEX) -> bytes:
    packet = struct.pack("<II", len(body) + 8, proto) + body
    return xor_payload(packet, key_index) if xor else packet


def debug_game_send(label: str, packet: bytes, *, xor: bool, key_index: int, enabled: bool) -> None:
    if not enabled:
        return
    decoded = xor_payload(packet, key_index) if xor else packet
    proto = u32(decoded, 4) if len(decoded) >= 8 else 0
    print(f"[debug][send:{label}] len={len(packet)} proto={proto_name(proto)} xor={xor}")
    print(f"[debug][send:{label}] raw={short_hex(packet)}")
    if xor:
        print(f"[debug][send:{label}] dec={short_hex(decoded)}")


def debug_game_recv(label: str, raw: bytes, decoded: bytes, *, key_index: int, enabled: bool) -> None:
    if not enabled:
        return
    proto = u32(decoded, 4) if len(decoded) >= 8 else 0
    print(f"[debug][recv:{label}] len={len(raw)} proto={proto_name(proto)} key={key_index}")
    print(f"[debug][recv:{label}] raw={short_hex(raw)}")
    print(f"[debug][recv:{label}] dec={short_hex(decoded)}")
    if proto not in PROTO_NAMES:
        known_candidates: list[str] = []
        print(f"[debug][recv:{label}] ascii={ascii_preview(decoded)}")
        for i in range(len(XOR_KEYS)):
            cand = xor_payload(raw, i)
            if len(cand) >= 8:
                cand_proto = u32(cand, 4)
                first_body_u32 = u32(cand, 8) if len(cand) >= 12 else 0
                name = proto_name(cand_proto)
                if cand_proto in PROTO_NAMES:
                    known_candidates.append(f"key={i} {name}")
                print(
                    f"[debug][recv:{label}] xor-candidate key={i} "
                    f"proto={name} firstBody=0x{first_body_u32:08X} "
                    f"body={short_hex(cand[8:40], 32)}"
                )
        if not known_candidates:
            print(
                f"[debug][recv:{label}] no XOR key produced a known SO3D proto; "
                "the SO3D client dispatcher would not handle this as a normal game packet"
            )
        else:
            print(f"[debug][recv:{label}] known-candidates {'; '.join(known_candidates)}")


def login_packet(account: str, password: str, encoding: str, key_index: int) -> bytes:
    account_block = fixed_bytes(account, 16, encoding)
    password_block = fixed_bytes(password, 16, encoding)
    body = (
        struct.pack("<I", 0)
        + game_des_encrypt(account_block)
        + game_des_encrypt(password_block)
        + game_des_encrypt(DATE_MAGIC)
    )
    return game_packet(CL_LOGIN, body, xor=True, key_index=key_index)


def login_secondary_packet(account: str, password: str, encoding: str, key_index: int) -> bytes:
    body = fixed_bytes(account, 16, encoding) + fixed_bytes(password, 16, encoding)
    return game_packet(CL_LOGIN_SECONDARY, body, xor=True, key_index=key_index)


def create_character_body(character: str, server_id: int, encoding: str) -> bytes:
    return struct.pack(
        "<I16s" + "I" * len(CREATE_CHAR_APPEARANCE),
        server_id,
        fixed_bytes(character, 16, encoding),
        *CREATE_CHAR_APPEARANCE,
    )


def create_character_packet(character: str, server_id: int, encoding: str, key_index: int) -> bytes:
    return game_packet(
        CL_CREATE_CHARACTER,
        create_character_body(character, server_id, encoding),
        xor=True,
        key_index=key_index,
    )


def chat_login_packet(account: str, password: str, character: str, encoding: str, player_id: int) -> bytes:
    return struct.pack(
        "<HI17s17s17sIII",
        69,
        CHAT_LOGIN_OPCODE,
        fixed_bytes(account, 17, encoding),
        fixed_bytes(password, 17, encoding),
        fixed_bytes(character, 17, encoding),
        1,
        CHAT_LOGIN_MAGIC,
        player_id & 0xFFFFFFFF,
    )


def chat_status_packet(character: str, encoding: str, map_id: int, player_id: int, extra_id: int = 0) -> bytes:
    return struct.pack(
        "<HIII16sxII",
        39,
        CHAT_STATUS_OPCODE,
        CHAT_STATUS_KIND,
        map_id & 0xFFFFFFFF,
        fixed_bytes(character, 16, encoding),
        extra_id & 0xFFFFFFFF,
        player_id & 0xFFFFFFFF,
    )


def bill_op1(account: str, encoding: str) -> bytes:
    return struct.pack("<HI16sIIII", 38, 1, fixed_bytes(account, 16, encoding), *BILL_OPEN_DWORDS)


def bill_account_op(opcode: int, account: str, encoding: str) -> bytes:
    return struct.pack("<HI16s", 22, opcode, fixed_bytes(account, 16, encoding))


def bill_u32_op(opcode: int, value: int) -> bytes:
    return struct.pack("<HII", 10, opcode, value)


def get_client_file_info(path: Path | None) -> ClientFileInfo:
    if path and path.exists():
        stat = path.stat()
        filetime = int(stat.st_mtime_ns // 100 + 116444736000000000)
        return ClientFileInfo((filetime >> 32) & 0xFFFFFFFF, filetime & 0xFFFFFFFF, stat.st_size)
    return ClientFileInfo(FALLBACK_FILETIME_HIGH, FALLBACK_FILETIME_LOW, FALLBACK_CLIENT_SIZE)


def parse_game_server_info(decoded: bytes) -> GameServerInfo:
    body = decoded[8:]
    if len(body) < 52:
        raise ValueError(f"LC_GSERV_CONNECT_SUCCESS body too short: {len(body)}")
    character = c_string(body[4:20])
    host = c_string(body[20:36])
    return GameServerInfo(
        host=host,
        port=u32(body, 36),
        character=character,
        enter_seed=u32(body, 40),
        current_server_type=u32(body, 44),
        channel_id=u32(body, 48),
    )


def parse_charinfo(decoded: bytes) -> tuple[int, str]:
    """Return (character_count, first_character_name) from LC_CHARINFO_SUCCESS.

    Body layout after the 8-byte len+proto header: server(u32), count(u32),
    flag(u32), then the first character's 16-byte name. This mirrors the client
    handler (sub_8CDD00), which reads the count at body+4 (g_LobbyCharacterSize)
    and the first name at body+12.
    """
    body = decoded[8:]
    if len(body) < 28:
        return 0, ""
    count = u32(body, 4)
    first_name = c_string(body[12:28])
    return count, first_name


def cg_enter_packet(character: str, encoding: str, server: GameServerInfo, file_info: ClientFileInfo) -> bytes:
    return game_packet(CG_ENTER, cg_enter_body(character, encoding, server, file_info))


def cg_enter_body(character: str, encoding: str, server: GameServerInfo, file_info: ClientFileInfo) -> bytes:
    return struct.pack(
        "<IIIIIII16s",
        server.enter_seed,
        file_info.filetime_high,
        file_info.filetime_low,
        file_info.size,
        0,
        0,
        0,
        fixed_bytes(character, 16, encoding),
    )
