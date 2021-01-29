# A set of experiments for the Raspberry Pi 4 camera subsystem

This isn't intended as production code, rather a series of different sketches to try things out.

These are intended to be built on a Raspberry Pi.

Tested on

OS: Raspbian GNU/Linux 10 (buster)
Kernel: Linux raspberrypi 5.4.83-v7l+ #1379 SMP Mon Dec 14 13:11:54 GMT 2020 armv7l GNU/Linux


## Capturing using V4L2

v4lcap is a simple example app for capturing frames from a MIPI-CSI2
camera over V4L2 (video for Linux version 2).

The kernel driver directly interfaces with the unicam hardware block.

Capturing 300 frames (10 seconds) gives a CPU usage of approximately 0.5% CPU.
No frames are dropped.

Instead of dropping, writing to /dev/null uses approximately 0.7% CPU.
No frames are dropped.

Instead writing to /dev/null, writing to the actual filesystem.. basically doesn't
work. The SD card just can't keep up and frames are dropped everywhere.

Doing some 'work' significantly increases CPU usage. For example, touching
each 8-th byte:

uint64_t
work(uint8_t *buf) {
    uint64_t s;
    for (unsigned i = 0; i < IMAGE_SIZE; i+=8) {
        s += buf[i];
    }
    return s;
}

Uses over 50% CPU usage. This is, relatively significant!

