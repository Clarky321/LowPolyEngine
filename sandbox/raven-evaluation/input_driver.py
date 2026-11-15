"""Real OS input for the Raven prototype's --input-check (raven-final-validation).

Usage:
  python sandbox/raven-evaluation/input_driver.py EXE [--with-imgui]
      [--screenshot FILE.png] [--log FILE] [--size WxH]

Starts EXE (raven-evaluation) with --input-check and drives it with real
input through the operating system, phase by phase, as the program asks on
its standard output ("@@READY <phase> ...", see main.cpp):

- Windows: SendInput (keys with their scan codes, typed text through the
  active keyboard layout or as Unicode packets, mouse buttons),
  SetCursorPos, the Win32 clipboard, SetForegroundWindow for focus changes,
  a small Tk window of this script as the other window, WM_CLOSE.
- X11 (Linux, also under Xvfb): xdotool (XTEST keys, text and mouse,
  XSetInputFocus), setxkbmap for the Russian layout (xdotool's own typing of
  characters outside the layout remaps a key for each one, which races with
  the program reading the keymap), xclip for the clipboard, xmessage as the
  other window, SIGTERM to close.

The program checks what reached Raven and prints "@@CHECK" lines; this
script adds its own checks (the system clipboard, the keyboard layout, the
exit code) and prints a summary. Exit code 0 when every check passed.

While it runs it moves the mouse pointer, changes the foreground window and
types: do not touch the mouse or the keyboard. Keyboard input is only sent
while the prototype or the script's own window is in the foreground. On
Windows the text on the clipboard and the keyboard layout are restored at
the end.
"""

from __future__ import annotations

import argparse
import os
import queue
import re
import signal
import subprocess
import sys
import threading
import time

HELPER_TITLE = "LPE input helper"
KEY_DELAY = 0.012


class Failure(Exception):
    pass


# ---------------------------------------------------------------------------
# Windows
# ---------------------------------------------------------------------------


class WindowsInput:
    def __init__(self) -> None:
        import ctypes
        from ctypes import wintypes

        self.ctypes = ctypes
        self.w = wintypes
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        u, k = self.user32, self.kernel32
        # Physical pixels everywhere, as the program reports them.
        u.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
        u.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

        ulong_ptr = ctypes.c_size_t

        class KEYBDINPUT(ctypes.Structure):
            _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                        ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                        ("dwExtraInfo", ulong_ptr)]

        class MOUSEINPUT(ctypes.Structure):
            _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                        ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                        ("time", wintypes.DWORD), ("dwExtraInfo", ulong_ptr)]

        class HARDWAREINPUT(ctypes.Structure):
            _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD),
                        ("wParamH", wintypes.WORD)]

        class INPUTUNION(ctypes.Union):
            _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("hi", HARDWAREINPUT)]

        class INPUT(ctypes.Structure):
            _fields_ = [("type", wintypes.DWORD), ("u", INPUTUNION)]

        self.INPUT = INPUT
        u.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
        u.SendInput.restype = wintypes.UINT
        u.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
        u.MapVirtualKeyW.restype = wintypes.UINT
        u.VkKeyScanExW.argtypes = [wintypes.WCHAR, wintypes.HKL]
        u.VkKeyScanExW.restype = ctypes.c_short
        u.LoadKeyboardLayoutW.argtypes = [wintypes.LPCWSTR, wintypes.UINT]
        u.LoadKeyboardLayoutW.restype = wintypes.HKL
        u.GetKeyboardLayout.argtypes = [wintypes.DWORD]
        u.GetKeyboardLayout.restype = wintypes.HKL
        u.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        u.GetWindowThreadProcessId.restype = wintypes.DWORD
        u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        u.PostMessageW.restype = wintypes.BOOL
        u.GetForegroundWindow.restype = wintypes.HWND
        u.SetForegroundWindow.argtypes = [wintypes.HWND]
        u.BringWindowToTop.argtypes = [wintypes.HWND]
        u.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
        u.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
        u.FindWindowW.restype = wintypes.HWND
        u.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
        u.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
        u.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        u.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_int, wintypes.UINT]
        u.OpenClipboard.argtypes = [wintypes.HWND]
        u.GetClipboardData.argtypes = [wintypes.UINT]
        u.GetClipboardData.restype = wintypes.HANDLE
        u.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]
        u.SetClipboardData.restype = wintypes.HANDLE
        k.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
        k.GlobalAlloc.restype = wintypes.HGLOBAL
        k.GlobalLock.argtypes = [wintypes.HGLOBAL]
        k.GlobalLock.restype = ctypes.c_void_p
        k.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
        k.GetCurrentThreadId.restype = wintypes.DWORD

        self.app = None
        self.helper = None
        self.helper_process = None
        self.layouts = {"en": u.LoadKeyboardLayoutW("00000409", 0),
                        "ru": u.LoadKeyboardLayoutW("00000419", 0)}
        self.original_layout = None
        self.saved_clipboard = self.clipboard_get()

    # -- windows and focus --------------------------------------------------

    def attach(self, window: dict) -> None:
        self.app = self.w.HWND(int(window["hwnd"]))
        self.original_layout = self.layout()

    def start_helper(self) -> None:
        code = ("import tkinter as tk; r = tk.Tk(); r.title(%r); r.geometry('360x90+20+20'); "
                "tk.Label(r, text='Raven final validation: the other window').pack(expand=True); "
                "r.mainloop()" % HELPER_TITLE)
        self.helper_process = subprocess.Popen([sys.executable, "-c", code])
        for _ in range(100):
            hwnd = self.user32.FindWindowW(None, HELPER_TITLE)
            if hwnd:
                self.helper = self.w.HWND(hwnd)
                return
            time.sleep(0.05)
        raise Failure("the helper window did not appear")

    def stop_helper(self) -> None:
        if self.helper_process is not None:
            self.helper_process.terminate()
            self.helper_process.wait(timeout=10)

    def foreground(self):
        return self.user32.GetForegroundWindow()

    def _activate(self, hwnd) -> bool:
        u = self.user32
        target = hwnd.value if hasattr(hwnd, "value") else hwnd
        for _ in range(20):
            if self.foreground() == target:
                return True
            # Become the source of the last input event (a zero mouse move),
            # then attach to the foreground thread: Windows then allows the
            # change of the foreground window.
            self._mouse(0x0001)
            current = self.foreground()
            ours = self.kernel32.GetCurrentThreadId()
            theirs = u.GetWindowThreadProcessId(current, None) if current else 0
            if theirs and theirs != ours:
                u.AttachThreadInput(ours, theirs, True)
            u.BringWindowToTop(hwnd)
            u.SetForegroundWindow(hwnd)
            if theirs and theirs != ours:
                u.AttachThreadInput(ours, theirs, False)
            time.sleep(0.1)
        return self.foreground() == target

    def focus_app(self) -> None:
        if not self._activate(self.app):
            raise Failure("the prototype's window could not be brought to the foreground")
        time.sleep(0.15)

    def focus_helper(self) -> None:
        if not self._activate(self.helper):
            raise Failure("the helper window could not be brought to the foreground")
        time.sleep(0.15)

    def _ensure_ours(self) -> None:
        current = self.foreground()
        ours = {self.app.value, self.helper.value if self.helper else None}
        if current not in ours:
            raise Failure("another program's window is in the foreground; no input sent")

    # -- keyboard -------------------------------------------------------------

    VK = {"backspace": 0x08, "shift": 0xA0, "ctrl": 0xA2, "alt": 0xA4, "end": 0x23,
          "home": 0x24, "left": 0x25, "right": 0x27, "delete": 0x2E, "f9": 0x78}
    EXTENDED = {0x23, 0x24, 0x25, 0x27, 0x2E}

    def _send(self, inputs) -> None:
        array = (self.INPUT * len(inputs))(*inputs)
        sent = self.user32.SendInput(len(inputs), array, self.ctypes.sizeof(self.INPUT))
        if sent != len(inputs):
            raise Failure("SendInput sent %d of %d events" % (sent, len(inputs)))

    def _key_event(self, vk: int, up: bool):
        event = self.INPUT()
        event.type = 1
        event.u.ki.wVk = vk
        event.u.ki.wScan = self.user32.MapVirtualKeyW(vk, 0)
        flags = 0x0002 if up else 0
        if vk in self.EXTENDED:
            flags |= 0x0001
        event.u.ki.dwFlags = flags
        return event

    def _vk(self, name: str) -> int:
        if name in self.VK:
            return self.VK[name]
        if len(name) == 1 and name.isalnum():
            return ord(name.upper())
        raise Failure("no key " + name)

    def key(self, name: str, up: bool) -> None:
        self._ensure_ours()
        self._send([self._key_event(self._vk(name), up)])
        time.sleep(KEY_DELAY)

    def tap(self, name: str, times: int = 1) -> None:
        for _ in range(times):
            self.key(name, False)
            self.key(name, True)

    def combo(self, modifier: str, name: str) -> None:
        self.key(modifier, False)
        self.tap(name)
        self.key(modifier, True)

    def type_keys(self, text: str, layout: str = "en") -> None:
        """Types text with key presses of the given layout, as a person would."""
        hkl = self.layouts[layout]
        for ch in text:
            scan = self.user32.VkKeyScanExW(ch, hkl)
            if scan == -1:
                raise Failure("%r cannot be typed with the %s layout" % (ch, layout))
            vk, shift = scan & 0xFF, (scan >> 8) & 0xFF
            self._ensure_ours()
            events = []
            if shift & 1:
                events.append(self._key_event(0xA0, False))
            events += [self._key_event(vk, False), self._key_event(vk, True)]
            if shift & 1:
                events.append(self._key_event(0xA0, True))
            for event in events:
                self._send([event])
                time.sleep(KEY_DELAY)

    def type_unicode(self, text: str) -> None:
        """Types text as Unicode packets (KEYEVENTF_UNICODE: WM_CHAR without a layout)."""
        units = text.encode("utf-16-le")
        for i in range(0, len(units), 2):
            unit = int.from_bytes(units[i:i + 2], "little")
            self._ensure_ours()
            for up in (False, True):
                event = self.INPUT()
                event.type = 1
                event.u.ki.wVk = 0
                event.u.ki.wScan = unit
                event.u.ki.dwFlags = 0x0004 | (0x0002 if up else 0)
                self._send([event])
                time.sleep(KEY_DELAY)

    def layout(self):
        thread = self.user32.GetWindowThreadProcessId(self.app, None)
        return self.user32.GetKeyboardLayout(thread)

    def set_layout(self, name) -> bool:
        hkl = self.layouts[name] if name in self.layouts else name
        # LPARAM is signed: a handle with the top bit set wraps around.
        value = hkl if hkl < 2**63 else hkl - 2**64
        self.user32.PostMessageW(self.app, 0x0050, 0, value)  # WM_INPUTLANGCHANGEREQUEST
        for _ in range(30):
            if self.layout() == hkl:
                return True
            time.sleep(0.05)
        return False

    # -- mouse ----------------------------------------------------------------

    def _mouse(self, flags: int) -> None:
        event = self.INPUT()
        event.type = 0
        event.u.mi.dwFlags = flags
        self._send([event])

    def move(self, x: float, y: float) -> None:
        point = self.w.POINT(int(round(x)), int(round(y)))
        self.user32.ClientToScreen(self.app, self.ctypes.byref(point))
        self.user32.SetCursorPos(point.x, point.y)
        time.sleep(0.05)

    def button(self, which: str, up: bool) -> None:
        flags = {("left", False): 0x0002, ("left", True): 0x0004,
                 ("right", False): 0x0008, ("right", True): 0x0010}[(which, up)]
        self._mouse(flags)
        time.sleep(0.03)

    def click(self, x: float, y: float) -> None:
        self.move(x, y)
        self.button("left", False)
        self.button("left", True)

    # -- clipboard ------------------------------------------------------------

    def _open_clipboard(self) -> None:
        for _ in range(50):
            if self.user32.OpenClipboard(None):
                return
            time.sleep(0.02)
        raise Failure("the clipboard could not be opened")

    def clipboard_get(self) -> str | None:
        self._open_clipboard()
        try:
            handle = self.user32.GetClipboardData(13)  # CF_UNICODETEXT
            if not handle:
                return None
            pointer = self.kernel32.GlobalLock(handle)
            try:
                return self.ctypes.wstring_at(pointer)
            finally:
                self.kernel32.GlobalUnlock(handle)
        finally:
            self.user32.CloseClipboard()

    def clipboard_set(self, text: str | None) -> None:
        self._open_clipboard()
        try:
            self.user32.EmptyClipboard()
            if text is None:
                return
            data = text.encode("utf-16-le") + b"\0\0"
            handle = self.kernel32.GlobalAlloc(0x0002, len(data))  # GMEM_MOVEABLE
            pointer = self.kernel32.GlobalLock(handle)
            self.ctypes.memmove(pointer, data, len(data))
            self.kernel32.GlobalUnlock(handle)
            self.user32.SetClipboardData(13, handle)
        finally:
            self.user32.CloseClipboard()

    # -- window ---------------------------------------------------------------

    def resize_by(self, dw: int, dh: int) -> None:
        rect = self.w.RECT()
        self.user32.GetWindowRect(self.app, self.ctypes.byref(rect))
        self.user32.SetWindowPos(self.app, None, 0, 0, rect.right - rect.left + dw,
                                 rect.bottom - rect.top + dh, 0x0002 | 0x0004)

    def close_app(self, process: subprocess.Popen) -> None:
        self.user32.PostMessageW(self.app, 0x0010, 0, 0)  # WM_CLOSE

    def finish(self) -> None:
        if self.app is not None and self.original_layout:
            self.set_layout(self.original_layout)
        self.clipboard_set(self.saved_clipboard)
        self.stop_helper()

    def describe(self) -> str:
        return "Windows: SendInput, SetCursorPos, Win32 clipboard, SetForegroundWindow"


# ---------------------------------------------------------------------------
# X11
# ---------------------------------------------------------------------------


class X11Input:
    KEYS = {"backspace": "BackSpace", "shift": "Shift_L", "ctrl": "Control_L",
            "alt": "Alt_L", "end": "End", "home": "Home", "left": "Left",
            "right": "Right", "delete": "Delete", "f9": "F9"}

    def __init__(self) -> None:
        if not os.environ.get("DISPLAY"):
            raise Failure("DISPLAY is not set")
        self.app = None
        self.helper = None
        self.helper_process = None

    def _run(self, *arguments: str, data: bytes | None = None) -> str:
        result = subprocess.run(list(arguments), input=data, capture_output=True, check=True,
                                timeout=20)
        return result.stdout.decode("utf-8", "replace")

    def attach(self, window: dict) -> None:
        self.app = window["x11"]

    def start_helper(self) -> None:
        self.helper_process = subprocess.Popen(
            ["xmessage", "-geometry", "360x90+0+0", "-name", "lpehelper",
             "Raven final validation: the other window"])
        for _ in range(100):
            found = subprocess.run(["xdotool", "search", "--classname", "lpehelper"],
                                   capture_output=True, text=True)
            ids = found.stdout.split()
            if ids:
                self.helper = ids[-1]
                return
            time.sleep(0.05)
        raise Failure("the helper window did not appear")

    def stop_helper(self) -> None:
        if self.helper_process is not None:
            self.helper_process.terminate()
            self.helper_process.wait(timeout=10)

    def _focus(self, window: str) -> None:
        self._run("xdotool", "windowraise", window)
        self._run("xdotool", "windowfocus", "--sync", window)
        time.sleep(0.15)

    def focus_app(self) -> None:
        self._focus(self.app)

    def focus_helper(self) -> None:
        self._focus(self.helper)

    def key(self, name: str, up: bool) -> None:
        self._run("xdotool", "keyup" if up else "keydown", self.KEYS.get(name, name))
        time.sleep(KEY_DELAY)

    def tap(self, name: str, times: int = 1) -> None:
        for _ in range(times):
            self.key(name, False)
            self.key(name, True)

    def combo(self, modifier: str, name: str) -> None:
        self.key(modifier, False)
        self.tap(name)
        self.key(modifier, True)

    LAYOUTS = {"en": "us", "ru": "ru"}

    def type_keys(self, text: str, layout: str = "en") -> None:
        # With the layout active, xdotool finds every character on a key of
        # the keymap and presses that key (with Shift where needed).
        self._run("xdotool", "type", "--delay", "25", text)

    def type_unicode(self, text: str) -> None:
        # X11 has no Unicode packets like Windows' KEYEVENTF_UNICODE: type
        # with the Russian layout.
        self.set_layout("ru")
        self.type_keys(text, "ru")
        self.set_layout("en")

    def set_layout(self, name) -> bool:
        self._run("setxkbmap", self.LAYOUTS[name])
        time.sleep(0.2)
        return True

    def move(self, x: float, y: float) -> None:
        self._run("xdotool", "mousemove", "--window", self.app, str(int(round(x))),
                  str(int(round(y))))
        time.sleep(0.05)

    def button(self, which: str, up: bool) -> None:
        self._run("xdotool", "mouseup" if up else "mousedown", "1" if which == "left" else "3")
        time.sleep(0.03)

    def click(self, x: float, y: float) -> None:
        self.move(x, y)
        self.button("left", False)
        self.button("left", True)

    def clipboard_get(self) -> str | None:
        result = subprocess.run(["xclip", "-selection", "clipboard", "-o"],
                                capture_output=True, timeout=10)
        return result.stdout.decode("utf-8", "replace") if result.returncode == 0 else None

    def clipboard_set(self, text: str | None) -> None:
        # xclip stays in the background to serve the selection; it must not
        # keep this script's output open (a pipe would never end).
        subprocess.run(["xclip", "-selection", "clipboard", "-i"],
                       input=(text or "").encode("utf-8"), stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=True, timeout=10)

    def resize_by(self, dw: int, dh: int) -> None:
        geometry = self._run("xdotool", "getwindowgeometry", self.app)
        match = re.search(r"Geometry: (\d+)x(\d+)", geometry)
        if not match:
            raise Failure("no window geometry: " + geometry)
        self._run("xdotool", "windowsize", self.app, str(int(match.group(1)) + dw),
                  str(int(match.group(2)) + dh))

    def close_app(self, process: subprocess.Popen) -> None:
        process.send_signal(signal.SIGTERM)

    def finish(self) -> None:
        self.stop_helper()
        subprocess.run(["setxkbmap", "us"], check=False, timeout=20)
        # The last xclip still serves the clipboard.
        subprocess.run(["pkill", "-x", "xclip"], check=False, timeout=20)

    def describe(self) -> str:
        return "X11: xdotool (XTEST), setxkbmap, xclip, XSetInputFocus"


# ---------------------------------------------------------------------------
# The phases
# ---------------------------------------------------------------------------


def centre(payload: dict) -> tuple[float, float]:
    x, y, w, h = (float(v) for v in payload["target"].split(","))
    return x + w / 2.0, y + h / 2.0


class Driver:
    def __init__(self, system, process: subprocess.Popen, log) -> None:
        self.system = system
        self.process = process
        self.log = log
        self.lines: queue.Queue[str] = queue.Queue()
        self.checks: list[tuple[str, bool, str]] = []
        self.infos: list[str] = []
        self.window: dict = {}
        self.scale = ""
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        assert self.process.stdout is not None
        for raw in self.process.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            self.log.write(line + "\n")
            self.log.flush()
            self.lines.put(line)
        self.lines.put("@@EOF")

    def expect(self, name: str, passed: bool, detail: str) -> None:
        self.checks.append((name, passed, "(driver) " + detail))
        print("@@CHECK %s %s (driver) %s" % (name, "PASS" if passed else "FAIL", detail))

    def next_ready(self, timeout: float = 90.0) -> tuple[str, dict]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise Failure("the program printed nothing to act on for %.0f s" % timeout)
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                continue
            if line == "@@EOF":
                raise Failure("the program ended before the check was done")
            if line.startswith("@@WINDOW "):
                self.window = dict(item.split("=", 1) for item in line.split()[1:])
            elif line.startswith("@@SCALE "):
                self.scale = line[len("@@SCALE "):]
            elif line.startswith("@@INFO "):
                self.infos.append(line[len("@@INFO "):])
            elif line.startswith("@@CHECK "):
                name, verdict, detail = (line[len("@@CHECK "):].split(" ", 2) + ["", ""])[:3]
                self.checks.append((name, verdict == "PASS", detail))
            elif line.startswith("@@READY "):
                parts = line.split()
                payload = dict(item.split("=", 1) for item in parts[2:] if "=" in item)
                return parts[1], payload

    def wait_done(self, phase: str) -> None:
        while True:
            line = self.lines.get(timeout=60)
            if line == "@@EOF":
                raise Failure("the program ended during " + phase)
            if line.startswith("@@CHECK "):
                name, verdict, detail = (line[len("@@CHECK "):].split(" ", 2) + ["", ""])[:3]
                self.checks.append((name, verdict == "PASS", detail))
            elif line.startswith("@@INFO "):
                self.infos.append(line[len("@@INFO "):])
            elif line == "@@DONE " + phase:
                return

    def finish_phase(self, phase: str) -> None:
        self.system.focus_app()
        self.system.tap("f9")
        self.wait_done(phase)

    def act(self, phase: str, payload: dict) -> None:
        s = self.system
        if phase == "focus":
            s.focus_app()
        elif phase in ("click-input", "click-button", "click-after-resize",
                       "raven-click-with-imgui"):
            s.focus_app()
            s.click(*centre(payload))
        elif phase == "type-ascii":
            s.focus_app()
            s.set_layout("en")
            s.type_keys("wasd Hello, World 123.!?;:-_()")
        elif phase == "type-layout":
            s.focus_app()
            if s.set_layout("ru"):
                self.expect("type-layout.layout", True, "the Russian layout is active in the "
                            "program's window")
                s.type_keys("Привет, мир!", "ru")
                s.set_layout("en")
            else:
                self.expect("type-layout.layout", False, "the Russian layout could not be "
                            "activated")
        elif phase == "type-unicode":
            s.type_unicode("Тестовый текст: 123.")
        elif phase == "editing":
            s.tap("home")
            for _ in range(8):
                s.combo("shift", "right")
            s.combo("ctrl", "x")
            time.sleep(0.3)
            clip = s.clipboard_get()
            self.expect("editing.os-clipboard", clip == "Тестовый",
                        "system clipboard %r after Ctrl+X" % clip)
            s.tap("end")
            s.combo("ctrl", "v")
            s.tap("backspace", 2)
            s.tap("home")
            s.tap("delete")
            s.tap("right", 5)
            s.type_keys("X")
        elif phase == "select-all-copy":
            s.combo("ctrl", "a")
            s.combo("ctrl", "c")
            time.sleep(0.3)
            clip = s.clipboard_get()
            self.expect("select-all-copy.os-clipboard", clip == "Привет, мир!",
                        "system clipboard %r after Ctrl+A, Ctrl+C" % clip)
        elif phase == "paste-external":
            s.clipboard_set("Вставка ✓ αβγ 🙂")
            time.sleep(0.2)
            s.combo("ctrl", "v")
        elif phase == "paste-multiline-input":
            s.clipboard_set("Первая строка\r\nВторая строка\nТретья")
            time.sleep(0.2)
            s.combo("ctrl", "v")
        elif phase == "paste-multiline-editor":
            s.combo("ctrl", "v")
        elif phase == "paste-empty":
            s.clipboard_set(None)
            time.sleep(0.2)
            s.combo("ctrl", "v")
        elif phase == "switch-and-paste":
            s.focus_helper()
            time.sleep(0.3)
            s.clipboard_set("После переключения")
            time.sleep(0.2)
            s.focus_app()
            time.sleep(0.3)
            s.combo("ctrl", "v")
        elif phase == "focus-loss":
            s.focus_app()
            s.move(*centre(payload))
            for name in ("w", "shift", "ctrl", "alt"):
                s.key(name, False)
            s.button("right", False)
            time.sleep(0.5)
            s.focus_helper()
            time.sleep(0.3)
            for name in ("alt", "ctrl", "shift", "w"):
                s.key(name, True)
            s.button("right", True)
            time.sleep(0.3)
            s.focus_app()
            time.sleep(0.6)
        elif phase == "resize":
            s.resize_by(240, 120)
            time.sleep(0.6)
        elif phase == "imgui-click":
            s.focus_app()
            s.move(*centre(payload))
            time.sleep(0.25)
            s.click(*centre(payload))
        elif phase == "imgui-type":
            s.focus_app()
            s.click(*centre(payload))
            time.sleep(0.25)
            s.type_keys("imgui")
        else:
            raise Failure("unknown phase " + phase)

    def run(self) -> None:
        while True:
            phase, payload = self.next_ready()
            if not self.window:
                raise Failure("the program did not tell its window")
            if self.system.app is None:
                self.system.attach(self.window)
                self.system.start_helper()
            if phase == "close":
                self.system.close_app(self.process)
                return
            print("-- phase %s %s" % (phase, payload))
            self.act(phase, payload)
            self.finish_phase(phase)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("exe")
    parser.add_argument("--with-imgui", action="store_true")
    parser.add_argument("--screenshot")
    parser.add_argument("--size")
    parser.add_argument("--log", default="raven-input-check.log")
    args = parser.parse_args()

    system = WindowsInput() if sys.platform == "win32" else X11Input()
    command = [args.exe, "--input-check"]
    if args.with_imgui:
        command.append("--with-imgui")
    if args.screenshot:
        command += ["--screenshot", args.screenshot]
    if args.size:
        command += ["--size", args.size]
    print("driver:", system.describe())
    print("command:", " ".join(command))
    print("Do not use the mouse or the keyboard until the check has finished.")
    with open(args.log, "w", encoding="utf-8") as log:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        driver = Driver(system, process, log)
        error = None
        try:
            driver.run()
        except Failure as failure:
            error = str(failure)
            process.kill()
        finally:
            try:
                code = process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait()
            # The program's last lines (its close checks), up to its end.
            while True:
                try:
                    line = driver.lines.get(timeout=10)
                except queue.Empty:
                    break
                if line == "@@EOF":
                    break
                if line.startswith("@@CHECK "):
                    name, verdict, detail = (line[len("@@CHECK "):].split(" ", 2) + ["", ""])[:3]
                    driver.checks.append((name, verdict == "PASS", detail))
            system.finish()
    if error is None:
        driver.expect("close.exit-code", code == 0, "the program exited with %d" % code)
    else:
        driver.expect("driver.completed", False, error)
    print()
    print("window:", driver.window)
    print("scale:", driver.scale)
    for info in driver.infos:
        print("info:", info)
    passed = sum(1 for _, ok, _ in driver.checks if ok)
    for name, ok, detail in driver.checks:
        print("%-4s %-38s %s" % ("PASS" if ok else "FAIL", name, detail))
    print("%d of %d checks passed" % (passed, len(driver.checks)))
    return 0 if error is None and passed == len(driver.checks) and driver.checks else 1


if __name__ == "__main__":
    sys.exit(main())
