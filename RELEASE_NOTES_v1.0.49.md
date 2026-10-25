# HaikuDVR v1.0.49

## Highlights
- **The version comes from the `VERSION` file.** The program's version text (the About box, and what the update check compares with the `VERSION` file on GitHub), the resource and the package all take it from there now, as hDesktop and Cricket do, so a release changes one file. The number was typed into the code, the Makefile and the resource file before, and one of them could be left behind, which would have the program offering an update to itself.
- **The resource's version numbers are in the right fields.** The program read as 1.48.0 to the system (the third number was in the second place); it reads as 1.0.49 now.
- **The server leaves the SSDP multicast group cleanly.** `dvr_server`'s DLNA discovery now drops its multicast membership explicitly before it closes the SSDP socket, and stops discovery when the server quits, so the socket is not left to be cleaned up when the program is torn down. On current Haiku kernels that late cleanup hit an assertion in the network stack (`MulticastFilter` leaving the group) and panicked the system. It still joins the multicast group by default; setting `HAIKUDVR_SSDP_JOIN_MULTICAST=0` skips the join and answers unicast SSDP only, in case a kernel problem comes back. (These are the changes of pull requests #1 and #2.)
- The recorder and the guide are as they were in v1.0.48. The server keeps its own version.

## Install
Install `HaikuDVR-1.0.49-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuDVR-1.0.49-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.

## Tested on
- Haiku R1/beta6: the 64-bit and 32-bit packages were built (on 64-bit Haiku and on 32-bit Haiku) and show v1.0.49 in the program and in the package. They were not run.
