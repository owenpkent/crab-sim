"""A virtual gamepad for the one-stick live test, straight to /dev/uinput, the
same way kbm.py makes its virtual mouse: no python-evdev (not installed here,
and it needs a C toolchain), just the ioctl and the packed input_event frames
kbm.py already uses, plus UI_ABS_SETUP for the stick's axis range.

Declares the left stick (ABS_X, ABS_Y) and a handful of the buttons and axes
of a wired Xbox 360 controller (vendor 045e, product 028e), the entry nearly
every gamepad mapping database, SDL2's included, keys off. That is the best
bet, without a real device to test against, that the crab's one-stick
controller sees this as a gamepad's left stick (EKeys::Gamepad_LeftX/Y) and
not just an unrecognised joystick. If the live run shows the stick is not
reaching the game, this mapping is the first thing to check.

Needs write access to /dev/uinput. Kernel 4.5 or newer for UI_DEV_SETUP and
UI_ABS_SETUP (the same baseline kbm.py already assumes).
"""
import fcntl
import os
import struct
import time

from kbm import _INPUT_EVENT, _ioc, _IOC_WRITE, EV_KEY, EV_SYN, SYN_REPORT

# struct uinput_abs_setup { __u16 code; /* 2 bytes padding */ struct input_absinfo { s32 value, minimum, maximum, fuzz, flat, resolution; } }
_UINPUT_ABS_SETUP = struct.Struct("H2x6i")
_UINPUT_SETUP = struct.Struct("HHHH80sI")

UI_DEV_CREATE = _ioc(0, "U", 1, 0)
UI_DEV_DESTROY = _ioc(0, "U", 2, 0)
UI_DEV_SETUP = _ioc(_IOC_WRITE, "U", 3, _UINPUT_SETUP.size)
UI_ABS_SETUP = _ioc(_IOC_WRITE, "U", 4, _UINPUT_ABS_SETUP.size)
UI_SET_EVBIT = _ioc(_IOC_WRITE, "U", 100, 4)
UI_SET_KEYBIT = _ioc(_IOC_WRITE, "U", 101, 4)
UI_SET_ABSBIT = _ioc(_IOC_WRITE, "U", 103, 4)

EV_ABS = 0x03
ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ = 0x00, 0x01, 0x02, 0x03, 0x04, 0x05
BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST = 0x130, 0x131, 0x133, 0x134
BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR = (
    0x136, 0x137, 0x13a, 0x13b, 0x13c, 0x13d, 0x13e)
BUS_USB = 0x03
STICK_RANGE = 32767  # matches a real pad's s16 axis range: set_stick(-1..1) scales into this


class Gamepad:
    """One virtual gamepad, alive from construction until close()."""

    def __init__(self, name="CrabSim live test gamepad", settle=2.0):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        try:
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
            for code in (BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                         BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR):
                fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)

            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
            for code in (ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ):
                fcntl.ioctl(self.fd, UI_SET_ABSBIT, code)
                is_trigger = code in (ABS_Z, ABS_RZ)
                minimum, maximum = (0, 255) if is_trigger else (-STICK_RANGE, STICK_RANGE)
                # value, minimum, maximum, fuzz, flat, resolution: centred and at rest until set_stick moves it.
                fcntl.ioctl(self.fd, UI_ABS_SETUP, _UINPUT_ABS_SETUP.pack(code, 0, minimum, maximum, 16, 128, 0))

            fcntl.ioctl(self.fd, UI_DEV_SETUP, _UINPUT_SETUP.pack(
                BUS_USB, 0x045e, 0x028e, 1, name.encode("utf-8")[:79], 0))
            fcntl.ioctl(self.fd, UI_DEV_CREATE)
        except Exception:
            os.close(self.fd)
            self.fd = -1
            raise
        time.sleep(settle)  # let X11/SDL notice and classify the device

    def _emit(self, events):
        if self.fd < 0:
            raise OSError("device is closed")
        frame = b"".join(_INPUT_EVENT.pack(0, 0, t, c, v) for t, c, v in events)
        os.write(self.fd, frame + _INPUT_EVENT.pack(0, 0, EV_SYN, SYN_REPORT, 0))

    def set_stick(self, x, y, code_x=ABS_X, code_y=ABS_Y):
        """Left stick XY, each -1 (or 0 for a trigger) to 1. UE convention: x right, y up.

        A real pad's ABS_Y usually reports up as the negative direction, but UE's
        GetInputAnalogStickState corrects for that (Gamepad_LeftY is already up-positive),
        so this sends y unflipped and trusts the engine's own convention.
        """
        self._emit([
            (EV_ABS, code_x, int(max(-1.0, min(1.0, x)) * STICK_RANGE)),
            (EV_ABS, code_y, int(max(-1.0, min(1.0, y)) * STICK_RANGE)),
        ])

    def centre_stick(self):
        self.set_stick(0.0, 0.0)

    def close(self):
        """Destroy the device. The kernel centres any axis and releases any button still held."""
        if self.fd < 0:
            return
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        except OSError:
            pass
        os.close(self.fd)
        self.fd = -1
