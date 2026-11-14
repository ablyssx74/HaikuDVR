# HaikuDVR v1.0.51

## Fixed
- **Check Tuner Firmware now tells you when a tuner is out of date.** The check always said "up to date": it asked silicondust.com's home page for the newest firmware, which is not data the program can read, so it never found a newer version and treated the tuner as current (a failed check looked the same). It now asks the tuner itself, which reports its running firmware and the newer one when there is one (for a CONNECT QUATRO and a CONNECT 4K running 20260326, the newer build 20261107). If the tuner can not be read, you get an error, not "up to date".
- **The button in the update message opens the tuner's web page.** It was called "Download Manually" and opened a blank browser window. It is now "Open Tuner Interface" and opens `http://` and the address of the tuner that was checked, where the firmware can be updated.

## Install
Install `HaikuDVR-1.0.51-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuDVR-1.0.51-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
