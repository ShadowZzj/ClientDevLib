import ctypes
import struct
import time


# Named pipe used by sdl3_hook_dll
PIPE_NAME = r"\\.\pipe\redpass_sdl3_hook_input"

# Protocol constants (must match DLL)
MAGIC_RPS1 = 0x31535052  # 'RPS1' little-endian

CMD_MOUSE_LEFT_DOWN = 1
CMD_MOUSE_LEFT_UP = 2
CMD_MOUSE_LEFT_CLICK = 3

CMD_KEY_DOWN = 10
CMD_KEY_UP = 11
CMD_KEY_PRESS = 12


def _pack_header(cmd_type: int, payload: bytes) -> bytes:
    if len(payload) > 0xFFFF:
        raise ValueError("payload too large")
    # struct PipeMsgHeader { uint32 magic; uint16 type; uint16 size; }
    return struct.pack("<IHH", MAGIC_RPS1, cmd_type, len(payload))


def pack_mouse_left_click(x: float, y: float, interval_ms: int) -> bytes:
    # payload: float x, float y, uint32 interval_ms
    payload = struct.pack("<ffI", float(x), float(y), int(interval_ms) & 0xFFFFFFFF)
    return _pack_header(CMD_MOUSE_LEFT_CLICK, payload) + payload


def pack_mouse_left_down(x: float, y: float) -> bytes:
    payload = struct.pack("<ff", float(x), float(y))
    return _pack_header(CMD_MOUSE_LEFT_DOWN, payload) + payload


def pack_mouse_left_up(x: float, y: float) -> bytes:
    payload = struct.pack("<ff", float(x), float(y))
    return _pack_header(CMD_MOUSE_LEFT_UP, payload) + payload


def pack_key_press(name: str, interval_ms: int) -> bytes:
    # payload: uint8 name_len, char[name_len], uint32 interval_ms
    b = name.encode("utf-8")
    if len(b) == 0 or len(b) > 255:
        raise ValueError("key name must be 1..255 bytes")
    payload = struct.pack("<B", len(b)) + b + struct.pack("<I", int(interval_ms) & 0xFFFFFFFF)
    return _pack_header(CMD_KEY_PRESS, payload) + payload


def pack_key_down(name: str) -> bytes:
    b = name.encode("utf-8")
    if len(b) == 0 or len(b) > 255:
        raise ValueError("key name must be 1..255 bytes")
    payload = struct.pack("<B", len(b)) + b
    return _pack_header(CMD_KEY_DOWN, payload) + payload


def pack_key_up(name: str) -> bytes:
    b = name.encode("utf-8")
    if len(b) == 0 or len(b) > 255:
        raise ValueError("key name must be 1..255 bytes")
    payload = struct.pack("<B", len(b)) + b
    return _pack_header(CMD_KEY_UP, payload) + payload


# --- Win32 pipe I/O via ctypes (no external deps) ---
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80

INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


def _wait_pipe(name: str, timeout_ms: int) -> None:
    WaitNamedPipeW = kernel32.WaitNamedPipeW
    WaitNamedPipeW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32]
    WaitNamedPipeW.restype = ctypes.c_int
    if not WaitNamedPipeW(name, timeout_ms):
        err = ctypes.get_last_error()
        raise OSError(err, f"WaitNamedPipeW failed: {err}")


def _open_pipe_write(name: str):
    CreateFileW = kernel32.CreateFileW
    CreateFileW.argtypes = [
        ctypes.c_wchar_p,  # lpFileName
        ctypes.c_uint32,   # dwDesiredAccess
        ctypes.c_uint32,   # dwShareMode
        ctypes.c_void_p,   # lpSecurityAttributes
        ctypes.c_uint32,   # dwCreationDisposition
        ctypes.c_uint32,   # dwFlagsAndAttributes
        ctypes.c_void_p,   # hTemplateFile
    ]
    CreateFileW.restype = ctypes.c_void_p

    h = CreateFileW(name, GENERIC_WRITE, 0, None, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, None)
    if h == INVALID_HANDLE_VALUE:
        err = ctypes.get_last_error()
        raise OSError(err, f"CreateFileW failed: {err}")
    return h


def _write_all(h, data: bytes) -> None:
    WriteFile = kernel32.WriteFile
    WriteFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    WriteFile.restype = ctypes.c_int

    total = 0
    n = len(data)
    while total < n:
        written = ctypes.c_uint32(0)
        ok = WriteFile(h, ctypes.c_char_p(data[total:]), n - total, ctypes.byref(written), None)
        if not ok:
            err = ctypes.get_last_error()
            raise OSError(err, f"WriteFile failed: {err}")
        if written.value == 0:
            raise OSError("WriteFile wrote 0 bytes")
        total += written.value


def _close_handle(h) -> None:
    CloseHandle = kernel32.CloseHandle
    CloseHandle.argtypes = [ctypes.c_void_p]
    CloseHandle.restype = ctypes.c_int
    CloseHandle(h)


def main():
    # Reproduce previous demo behavior (in one while loop):
    # - KeyPress "Q" every 10 seconds, interval=100ms
    # - MouseLeftClick at (66,30) every 5 seconds, interval=50ms
    q_every = 10.0
    q_interval_ms = 100

    click_every = 5.0
    click_x, click_y = 66.0, 30.0
    click_interval_ms = 50

    next_q = time.monotonic()
    next_click = time.monotonic()

    h = None
    print(f"[pipe_client] connecting to {PIPE_NAME}")
    try:
        while True:
            # connect/reconnect
            if h is None:
                try:
                    _wait_pipe(PIPE_NAME, 2000)
                    h = _open_pipe_write(PIPE_NAME)
                    print("[pipe_client] connected")
                except OSError as e:
                    print(f"[pipe_client] wait/connect failed: {e}; retry...")
                    time.sleep(0.5)
                    continue

            now = time.monotonic()

            try:
                if now >= next_q:
                    msg = pack_key_press("Q", q_interval_ms)
                    _write_all(h, msg)
                    print(f"[pipe_client] sent KeyPress Q interval={q_interval_ms}ms")
                    next_q = now + q_every

                if now >= next_click:
                    msg = pack_mouse_left_click(click_x, click_y, click_interval_ms)
                    _write_all(h, msg)
                    print(f"[pipe_client] sent LClick ({click_x},{click_y}) interval={click_interval_ms}ms")
                    next_click = now + click_every

            except OSError as e:
                print(f"[pipe_client] write failed: {e}; reconnecting...")
                try:
                    _close_handle(h)
                except Exception:
                    pass
                h = None
                time.sleep(0.2)

            time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n[pipe_client] stopped")
    finally:
        if h is not None:
            _close_handle(h)


if __name__ == "__main__":
    main()


