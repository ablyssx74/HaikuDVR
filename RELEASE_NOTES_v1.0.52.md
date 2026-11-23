# HaikuDVR 1.0.52

## Fixed
- **HaikuDVR starts on RenkuOS.** The program did not start on RenkuOS: it asked libbe for BMenuItem::BMenuItem(const char*, BMessage*, uint32, uint32), a constructor that only a newer libbe has ("Could not resolve symbol"). It is now built with the older MenuItem.h (compat/MenuItem.h, the same file hDesktop and hrecord use), which asks for the older constructor that both the older and the newer libbe have, so one package runs on both. `dvr_server` was not affected.

## Packages
- `HaikuDVR-1.0.52-1-x86_64.hpkg` for 64-bit Haiku. The 64-bit package was built on Haiku hrev60453; every symbol that HaikuDVR and dvr_server ask for is found in RenkuOS's libraries (hrev60072),, and a test build of 1.0.51 with the same change started there.
- `HaikuDVR-1.0.52-1-x86_gcc2.hpkg` for 32-bit Haiku (gcc 2 / hybrid). The 32-bit package was built but not run.

## Install
Install `HaikuDVR-1.0.52-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuDVR-1.0.52-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
