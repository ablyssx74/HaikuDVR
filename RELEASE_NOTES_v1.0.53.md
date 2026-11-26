# HaikuDVR 1.0.53

## Fixed
- **Other programs can search for tuners while HaikuDVR's server runs.** `dvr_server` held the network port that Universal Plug'n'Play searches use (UDP 1900) for itself, so a program such as VLC under Wine (11.20-87 or newer) found no HDHomeRun tuners. The server now shares the port, and still answers searches itself.

## Packages
- `HaikuDVR-1.0.53-1-x86_64.hpkg` for 64-bit Haiku and RenkuOS. Built on RenkuOS (hrev60072). The change in `dvr_server` was tested there with VLC under Wine.
- `HaikuDVR-1.0.53-1-x86_gcc2.hpkg` for 32-bit Haiku (gcc 2 / hybrid). Built but not run.

## Install
Install `HaikuDVR-1.0.53-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuDVR-1.0.53-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
