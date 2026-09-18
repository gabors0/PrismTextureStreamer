<h1 align="center">PrismTextureStreamer</h1>

<p align="center">
A Prism3D plugin that takes over cabin screens (GPS, dashboard, or any custom accessory) in <b>Euro Truck Simulator 2</b> (or American Truck Simulator) and mirrors a live captured window onto them.
</p>

>[!NOTE]
>This fork adds support for the games running through Proton on Linux/Wayland and was mostly made by AI

---

## What it does

Press **Ctrl+F8** in game to open an ImGui overlay. From there you can:

- Add a **GPS** screen, **Dashboard** screen, or a **Custom** screen (The target `.tobj` MUST be a functional screen from a UI Script, such as `/ui/gps.sii`)
- Pick a running application window as the source for that screen
- Adjust target resolution and framerate
- Apply changes 

Whatever's rendering in the picked window gets captured and blitted onto the truck's screen every frame.

## Linux/Wayland testing (Proton/Wine only)

Linux support is experimental. Euro Truck Simulator 2 and the plugin remain Windows builds running through Proton; only the Wayland capture helper is a native Linux program. The helper uses xdg-desktop-portal and PipeWire, so it works with native Wayland windows that the DLL cannot see through Win32 APIs.

### Requirements

- ETS2 running its Windows DirectX 11 build through Proton (ATS is untested but should work)
- A working PipeWire and xdg-desktop-portal installation with the appropriate
  desktop backend (tested on KDE Plasma under NixOS and CachyOS)
- GNU Make, a C++17 compiler, and the GLib/GIO and PipeWire development files
- The Release x64 `PrismTextureStreamerFB.dll` from the same `dev` revision

Common build dependency packages are `base-devel`, `glib2`, and `pipewire` on Arch/CachyOS, or `build-essential`, `libglib2.0-dev`, and `libpipewire-0.3-dev` on Ubuntu/Debian. Do not run the helper with `sudo`.

### Install and run

Clone the test branch and build the helper:

```sh
git clone https://github.com/gabors0/PrismTextureStreamer.git
cd PrismTextureStreamer
make -C linux_bridge portal
```

If the repository is already cloned:

```sh
git pull
make -C linux_bridge portal
```

Download the `PrismTextureStreamerFB-win-x64` artifact from the
[latest successful Windows DLL build](https://github.com/gabors0/PrismTextureStreamer/actions?query=branch%3Amain+workflow%3A%22Windows%20DLL%20build%22)
Copy the DLL to:

```text
steamapps/common/Euro Truck Simulator 2/bin/win_x64/plugins/PrismTextureStreamerFB.dll
```

Then set ETS2's Steam Launch Options, replacing the path with the absolute path
to the clone:

```text
"/absolute/path/to/PrismTextureStreamer/linux_bridge/run_with_bridge.sh" %command%
```

Start ETS2 normally and select the 64-bit DirectX 11 option if prompted. Load a profile, press **Ctrl+F8**, add a screen and select **Linux bridge (localhost:27861)**. The portal chooser will then ask for one window or monitor. Click **Apply Unsaved Changes** after adding or changing a screen.

Use **Choose Window** in the Ctrl+F8 menu to change the shared source without restarting ETS2. Closing the captured application should also reopen the portal chooser. Cancelling the chooser stops automatic retries until the next game launch; the game itself can continue running.

Only one Linux bridge screen is supported at this stage. If performance is poor, test with one screen at 960x540 before adding other custom screens.

When reporting results, include the distribution, desktop/compositor, GPU, Proton version, selected helper FPS/resolution, approximate game FPS impact, and whether window reselection and closing the captured application work. Detailed protocol, manual-helper, test-pattern, and troubleshooting information is in [the Linux/Wayland bridge documentation](docs/linux-bridge.md).

## How it works

### 1. Redirecting the texture file
A hook on Prism3D's `memserver_texture_queue_processor` walks the engine's pending texture object queue every tick. If a queued `tobj`'s path matches a screen's `original_texture` (e.g. `/vehicle/truck/share/gps.tobj`), the path is swapped for `override_texture` before the engine loads it.

### 2. Catching the texture at creation time
The override `.tobj` still gets built into a real DirectX texture eventually, there's a hook on `ID3D11Device::CreateTexture2D`. Every creation call is checked against a fignerprint. The real game textures never match the fingerprint, so it's whats used to capture the screens texture.

*This method is a bit "jank"... but it does work ;)*

Once matched, the description is rewritten before the real `CreateTexture2D` is called:
```cpp
D3D11_TEXTURE2D_DESC modifiedDesc = *pDesc;
modifiedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
modifiedDesc.Usage = D3D11_USAGE_DYNAMIC;
modifiedDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
modifiedDesc.MiscFlags = 0;
modifiedDesc.Width = screen.targetLiveTextureWidth;
modifiedDesc.Height = screen.targetLiveTextureHeight;
```

This gives us a CPU writable arbitrarily sized texture instead of the default game texture -  then the resulting texture and its immediate context are cached on the `screen_t` for later.

### 3. Feeding it frames
Every present call, each screen with a live texture and an active source:
- Pulls the latest RGBA8 frame from its content source
- `Map()`s the live texture with `D3D11_MAP_WRITE_DISCARD`
- Copies row by row using nearest neighbor sampling if source and destination sizes differ, with an optional vertical flip
- `Unmap()`s it

### 4. Forcing the screens to actually render
The GPS/dashboard/custom screens are normally only drawn under specific in game conditions. `dllmain.cpp` patches the relevant conditional jump in the games process, flipping `JE` to `JMP` (and back) at runtime so the screen's render path is unconditionally taken whenever a screen of that type exists, and restored to normal when it doesn't. Addresses are found via pattern scanning, so it hopefully survives most game updates.

## Content sources
Windows applications can be captured with **`WindowSource`** (`PrintWindow` + `GetDIBits`) or **`WgcWindowSource`** (Windows Graphics Capture). The experimental **Linux bridge source** accepts bounded RGBA8 frames from the native portal/PipeWire sender over localhost, allowing the Windows DLL to remain inside an ETS2 Proton process.

The source uses the `IContentSource` interface, so other backends can be added
without replacing the DX11 or menu code.

See [Linux/Wayland bridge](docs/linux-bridge.md) for the protocol, test-pattern
sender, portal/PipeWire sender, manual helper mode, Steam launch wrapper, and
exact Proton test steps.

## Requirements
- MinHook
- Dear ImGui
- SCS Telemetry SDK
- DirectX 11

## Known issues
- Fingerprinting textures by dimensions/format means any other texture in the game that happens to match GPS/dashboard's size and format exactly would get caught too which is unlikely (if using unqiue dimentions), but not impossible.
- Custom screens have no override_texture_size_w/h wiring in the menu yet, unlike GPS/Dashboard.

## Usage

1. Drop the compiled plugin DLL into your ETS2/ATS `plugins` folder (not a injected DLL)
2. Launch the game, Ctrl+F8 to open the menu
3. Add a screen, pick a source window, hit Apply

## Contributing
PRs and issues are welcome, keep in mind:

- If a pattern scan fails to resolve on a newer patch, that's the first thing to check before assuming something else is broken. (Same with any Prism3D structures inside `prism/prism.h`)
- DO NOT use hard coded file offsets etc, keep with using pattern scans where possible.
- If you're adding a new `IContentSource` (video file, monitor capture, etc.), implement it against the existing interface in `sources/content_source.h`.
- Match existing code style so diffs stay reviewable and the project isnt a mess of multiple people.
- Test on an actual ETS2 install before opening a PR.
