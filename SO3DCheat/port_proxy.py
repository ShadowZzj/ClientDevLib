import ctypes
import sys
import os
import time

ACCESS_KEY = '{"access_key":"b9f241db50a25b4f4f4e9c877e86641c"}'
DLL_NAME = "protect.dll"


def main():
    daemon = "-d" in sys.argv

    if daemon:
        hwnd = ctypes.windll.kernel32.GetConsoleWindow()
        if hwnd:
            ctypes.windll.user32.ShowWindow(hwnd, 0)

    dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), DLL_NAME)
    if not os.path.exists(dll_path):
        dll_path = DLL_NAME

    print(f"[*] Loading {dll_path}")
    try:
        dll = ctypes.CDLL(dll_path)
    except OSError as e:
        print(f"[-] Failed to load {DLL_NAME}: {e}", file=sys.stderr)
        if not daemon:
            os.system("pause")
        return -1

    dll.protect_start.restype = ctypes.c_uint32
    dll.protect_start.argtypes = [ctypes.c_char_p]
    dll.protect_stop.restype = ctypes.c_uint64
    dll.protect_stop.argtypes = []

    print("[*] Calling protect_start...")
    ret = dll.protect_start(ACCESS_KEY.encode("utf-8"))

    if ret != 0:
        print(f"[-] protect_start failed with code {ret}", file=sys.stderr)
        dll.protect_stop()
        if not daemon:
            os.system("pause")
        return -1

    print("[+] protect_start OK")

    if daemon:
        while True:
            time.sleep(1)
    else:
        print("[*] Type 'exit' to stop")
        while True:
            try:
                cmd = input("> ").strip()
                if cmd == "exit":
                    break
            except (EOFError, KeyboardInterrupt):
                break

    print("[*] Stopping...")
    dll.protect_stop()
    print("[*] Done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
