
# HaikuDVR

HaikuDVR is a native GUI digital video recorder application and background scheduling service designed explicitly for Haiku.

⚠️ **Hardware Requirement:** This software strictly requires an active **SiliconDust HDHomeRun Network Tuner** connected to your local network to scan channels and record video streams.

The project is split into a lightweight, headless background recorder daemon and an intuitive graphical desktop frontend packed with advanced diagnostic instrumentation.

## Key Architecture Highlights

* **Dual-Layer Guide Parsing Engine** – Leverages a unified local hardware parser (`lineup.json`) for absolute 100% accurate HD/SD stream filtering, seamlessly combined with SiliconDust's cloud EPG API for deep, look-ahead program scheduling lineups.
* **Asynchronous Vector Icon Cache** – Automatically downloads station logos on an isolated background serial thread queue. Images are cached in-memory and rendered at 60 FPS using high-quality bilinear blending.
* **Real-Time Bandwidth Instrumentation** – Features a non-blocking `libcurl` transfer info callback that calculates and prints live file download size metrics right to your dashboard status panel as streams write to disk.
* **Proactive Storage Protection** – An automated background disk monitor queries Haiku Storage Kit partition volumes every 5 seconds, flashing high-visibility warnings if storage space drops below 5GB.
* **Decoupled Lifecycle Architecture** – Cleanly separates process ownership using `setsid()`. Hitting the Restart Backend toggle recycles the background daemon instantly while letting you close or manage the GUI client completely independently without interrupting active streams.
* **Adaptive Theme Contrast** – Utilizes YIQ brightness calculations and native Haiku system color tokens (`ui_color`) to hot-swap background layers and layout text contrast instantly when switching desktop system appearances.



## Licensing & Intellectual Property

* **HaikuDVR Application & Service** – Released under the permissive **MIT License**. You are free to modify, distribute, and utilize the source code for personal or commercial application suites.
* **HDHomeRun Interface Layer (`libhdhomerun`)** – This project dynamically links against SiliconDust's official device interface library, which is governed under the **GNU Lesser General Public License (LGPL v2.1)**. 

*Disclaimer: HaikuDVR is a third-party open-source application and is not officially affiliated with or endorsed by Silicondust.*


## System Compatibility

* **Operating System Baseline** – This software was built and verified exclusively on **Haiku Beta x86_64 hrev59783**. Compatibility with older revisions or nightly builds is not guaranteed.



## Installation & Build Requirements

Ensure your Haiku system feature layers include the required development libraries:
- libcurl_devel nlohmann_json

A reboot is required for the backend service worker to register with the Haiku Launcher Roster.
To build.
```
make release
```

### Screenshots
<img align="left" width="889" height="508" alt="Image" src="https://github.com/user-attachments/assets/c8a3d465-3bcc-4562-ab09-6fdddf7f2254" />
