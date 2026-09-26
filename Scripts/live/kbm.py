"""A virtual mouse for live tests, straight to /dev/uinput.

python-evdev is not installed here and needs a C toolchain, so this talks to
the kernel directly: an ioctl to describe the device, then packed input_event
frames. Only buttons are sent. Pointer placement is done with XWarpPointer in
focus.py, because relative uinput moves go through pointer acceleration and
land wherever the acceleration curve puts them.

The device still declares the relative axes a real mouse has, so the X server
classifies it as a pointer.

Needs write access to /dev/uinput. Kernel 4.5 or newer for UI_DEV_SETUP.
"""
import fcntl
import os
import struct
import time

# linux/uinput.h, asm-generic ioctl layout.
_IOC_WRITE, _IOC_NONE = 1, 0


def _ioc(direction, type_char, nr, size):
    return (direction << 30) | (ord(type_char) << 8) | (nr << 0) | (size << 16)


# struct input_event { struct timeval time; __u16 type; __u16 code; __s32 value; }
_INPUT_EVENT = struct.Struct("llHHi")
# struct uinput_setup { struct input_id id; char name[80]; __u32 ff_effects_max; }
_UINPUT_SETUP = struct.Struct("HHHH80sI")

UI_DEV_CREATE = _ioc(_IOC_NONE, "U", 1, 0)
UI_DEV_DESTROY = _ioc(_IOC_NONE, "U", 2, 0)
UI_DEV_SETUP = _ioc(_IOC_WRITE, "U", 3, _UINPUT_SETUP.size)
UI_SET_EVBIT = _ioc(_IOC_WRITE, "U", 100, 4)
UI_SET_KEYBIT = _ioc(_IOC_WRITE, "U", 101, 4)
UI_SET_RELBIT = _ioc(_IOC_WRITE, "U", 102, 4)

EV_SYN, EV_KEY, EV_REL = 0x00, 0x01, 0x02
SYN_REPORT = 0x00
REL_X, REL_Y, REL_WHEEL = 0x00, 0x01, 0x08
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE = 0x110, 0x111, 0x112
BUS_VIRTUAL = 0x06


class Mouse:
    """One virtual mouse, alive from construction until close()."""

    def __init__(self, name="CrabSim live test mouse", settle=2.0):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        try:
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
            for code in (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE):
                fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_REL)
            for code in (REL_X, REL_Y, REL_WHEEL):
                fcntl.ioctl(self.fd, UI_SET_RELBIT, code)
            fcntl.ioctl(self.fd, UI_DEV_SETUP, _UINPUT_SETUP.pack(
                BUS_VIRTUAL, 0x1209, 0xC01D, 1, name.encode("utf-8")[:79], 0))
            fcntl.ioctl(self.fd, UI_DEV_CREATE)
        except Exception:
            os.close(self.fd)
            self.fd = -1
            raise
        time.sleep(settle)  # let X11 notice and classify the device

    def _emit(self, events):
        """Write (type, code, value) events and the SYN_REPORT that ends the frame."""
        if self.fd < 0:
            raise OSError("device is closed")
        frame = b"".join(_INPUT_EVENT.pack(0, 0, t, c, v) for t, c, v in events)
        os.write(self.fd, frame + _INPUT_EVENT.pack(0, 0, EV_SYN, SYN_REPORT, 0))

    def button_down(self, code=BTN_LEFT):
        self._emit([(EV_KEY, code, 1)])

    def button_up(self, code=BTN_LEFT):
        self._emit([(EV_KEY, code, 0)])

    def hold(self, code=BTN_LEFT, seconds=1.0):
        """Press, wait, release. The release is sent even if the wait is interrupted."""
        self.button_down(code)
        try:
            time.sleep(seconds)
        finally:
            self.button_up(code)

    def click(self, code=BTN_LEFT, hold=0.12):
        self.hold(code, hold)

    def close(self):
        """Destroy the device. The kernel releases any button still held."""
        if self.fd < 0:
            return
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        except OSError:
            pass
        os.close(self.fd)
        self.fd = -1
