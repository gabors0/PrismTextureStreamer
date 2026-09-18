# Linux/Wayland bridge

Euro Truck Simulator 2 and `PrismTextureStreamerFB.dll` remain Windows binaries
running inside Proton. Native Wayland windows are not Win32 windows, so the
existing `EnumWindows`, Windows Graphics Capture, and `PrintWindow` sources
cannot capture them. The localhost frame boundary remains independent of how
Linux obtains a frame. A generated test pattern is the stable diagnostic source;
the experimental portal sender adds one native Wayland capture source.

## Frame transport protocol v1

The DLL is a TCP server bound only to `127.0.0.1:27861`. One sender connection is
handled at a time. The Linux-to-DLL stream is a sequence of a 32-byte header
followed immediately by its RGBA8 payload. All integers are little-endian.

| Offset | Size | Field | v1 value |
| ---: | ---: | --- | --- |
| 0 | 4 | Magic bytes | ASCII `PTSB` |
| 4 | 2 | Version | `1` |
| 6 | 2 | Header size | `32` |
| 8 | 4 | Width | `1..1280` |
| 12 | 4 | Height | `1..720` |
| 16 | 4 | Pixel format | `1` = RGBA8, top row first |
| 20 | 4 | Payload size | exactly `width * height * 4` |
| 24 | 8 | Sequence | sender-defined, normally increasing |

The receiver rejects an invalid magic, version, header size, format, dimension,
or payload size and closes that connection. It then waits for a new sender. TCP
fragmentation and multiple frames in one read are supported. Resolution may
change on any frame. A disconnected source stops publishing frames rather than
holding stale pixels, and can reconnect without reselecting it in the menu.

Protocol v1 deliberately caps frames at 1280x720 (3.52 MiB each). The test sender
also caps its rate at 30 FPS and defaults to 640x360 at 15 FPS (about 13.8 MB/s
before TCP overhead). The portal sender scales captured frames to these limits
before transmission. Compression, authentication, and multiple bridge screens
are outside this stage.

### Helper control protocol v1

The same TCP connection has an optional DLL-to-Linux control direction. A
headless helper waits for this fixed eight-byte message before opening the portal
chooser. Existing frame-only senders may ignore it.

| Offset | Size | Field | v1 value |
| ---: | ---: | --- | --- |
| 0 | 4 | Magic bytes | ASCII `PTSC` |
| 4 | 2 | Version | `1` |
| 6 | 2 | Command | `1` = start capture |

The helper parser supports fragmented and combined messages and rejects unknown
magic, versions, and commands. Future commands must retain explicit versioning.

## Build the native senders and tests

No extra libraries are required beyond a C++17 compiler, GNU Make, and standard
Linux/POSIX headers:

```sh
make -C linux_bridge
make -C linux_bridge test
```

Run the pattern sender with defaults:

```sh
./linux_bridge/build/test_sender
```

Optional positional arguments are width, height, FPS, and port. For example:

```sh
./linux_bridge/build/test_sender 960 540 20 27861
```

The sender connects only to `127.0.0.1` and retries after startup ordering or a
disconnect. Stop it with Ctrl+C.

### Experimental Wayland portal sender

The native capture sender requires development files for GLib/GIO and PipeWire
(`gio-2.0`, `gio-unix-2.0`, `libpipewire-0.3`, and `libspa-0.2` in pkg-config).
On CachyOS/Arch these are supplied by the `glib2` and `pipewire` packages. Build
it separately so missing capture dependencies never break the test sender:

```sh
make -C linux_bridge portal
./linux_bridge/build/portal_sender
```

The default command is the manually started mode: it opens the chooser
immediately. To start the helper manually but let the mod UI trigger the chooser,
use:

```sh
./linux_bridge/build/portal_sender --wait 60
```

It connects while idle. Select `Linux bridge (localhost:27861)` in the mod UI to
open the chooser. With the automatic wrapper running, use the `Choose Window`
button beside `Flip Screen` to end the current portal session and open a fresh
chooser without restarting ETS2.

The portal dialog allows one monitor or window. The sender consumes one raw
PipeWire stream, converts RGBx/RGBA/BGRx/BGRA to RGBA8, scales it to fit within
1280x720, caps publication to 15 FPS by default, and drops old frames instead of
blocking capture on TCP. It drains queued PipeWire buffers to the newest frame
and bounds TCP buffering to reduce end-to-end latency. Optional arguments are
FPS, port, and a maximum output size:

```sh
./linux_bridge/build/portal_sender 20 27861
./linux_bridge/build/portal_sender 60
./linux_bridge/build/portal_sender 60 27861 960 540
```

FPS may be 1-60. At 31-60 FPS, omitting the size automatically selects 960x540;
at 30 FPS and below it defaults to 1280x720. Explicit settings that exceed a
128 MiB/s raw-frame budget are rejected, so 1280x720 at 60 FPS is not allowed.
The DLL preserves the source aspect ratio for Linux bridge frames and centers
the image with opaque black bars when the target game texture has a different
shape. Existing Windows capture sources retain their original stretch behavior.
Linux bridge textures are marked as sRGB so captured desktop midtones are
linearized by the GPU instead of appearing washed out; Windows source texture
formats remain unchanged.

The default portal cursor mode is used. Capture permission is requested each
time; persistent restore tokens are intentionally not implemented yet. See the
[ScreenCast portal lifecycle](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
and the [PipeWire video capture tutorial](https://docs.pipewire.org/page_tutorial5.html).

### Start it automatically with ETS2

After building the portal sender, set ETS2's Steam Launch Options to the absolute
path of the included wrapper followed by `%command%`:

```text
"/absolute/path/to/PrismTextureStreamer/linux_bridge/run_with_bridge.sh" %command%
```

The wrapper supervises `portal_sender --wait 60`, launches the unchanged Proton
game command, and stops the helper when the game exits. If the shared window
closes or `Choose Window` requests another selection, the helper exits normally
and the wrapper starts a fresh portal session. Cancelling the portal chooser or
a helper error stops automatic retries until the next game launch. The wrapper
does not install a service or change system configuration. The portal dialog
appears only after selecting the Linux bridge source in the mod UI.

## Build the Windows DLL

On Windows, open `PrismTextureStreamerFB.sln` in Visual Studio with the Desktop
development with C++ workload and a current Windows 10 or Windows 11 SDK installed.
Select `Release | x64` and build the solution. The project uses its bundled
MinHook and ImGui libraries and links the system Winsock library.

Equivalent Developer Command Prompt command:

```bat
msbuild PrismTextureStreamerFB.sln /m /p:Configuration=Release /p:Platform=x64
```

The output is normally `x64/Release/PrismTextureStreamerFB.dll`. The included
Windows GitHub Actions workflow performs the same build and publishes the DLL as
the `PrismTextureStreamerFB-win-x64` artifact when the repository is pushed.

## Manual ETS2/Proton test

1. In Steam, open ETS2 Properties > Compatibility, enable “Force the use of a
   specific Steam Play compatibility tool,” and select the Proton version you
   normally use. Do not install or launch a native Linux ETS2 build.
2. Copy the Release x64 DLL into the Windows game's plugin directory:
   `steamapps/common/Euro Truck Simulator 2/bin/win_x64/plugins/`. Create only
   the `plugins` directory if it is absent. A custom Steam library has the same
   path beneath its `steamapps` directory.
3. Build the sender with `make -C linux_bridge` on the Linux host.
4. Start `./linux_bridge/build/test_sender`. It will print a waiting message
   until the DLL opens the listener.
5. Launch ETS2 through Steam. In the Launchpad, choose the 64-bit DirectX 11
   option if Steam presents launch choices.
6. Load a profile and press Ctrl+F8. Add a GPS, Dashboard, or Custom screen.
7. In that screen's existing source combo, select
   `Linux bridge (localhost:27861)`. Leave “Legacy Capture” unchanged; it applies
   only to Win32 window sources.
8. The sender should report that it connected. Click “Apply Unsaved Changes” if
   the screen itself was newly added or its target texture settings changed.
9. Enter the cab and verify the moving white square/color gradient appears on
   the selected game screen. “Flip Screen” remains available if orientation is
   inverted.
10. Stop and restart the sender to verify disconnect/reconnection. To exercise a
    resolution change, restart it with, for example,
    `./linux_bridge/build/test_sender 800 450 15`; the menu source does not need
    to be selected again.

If the menu reports “Source Error,” another screen/process probably already owns
TCP port 27861. Stage 1 intentionally supports only one Linux bridge source.

## Portal/PipeWire manual test

1. Complete the DLL setup above and select `Linux bridge (localhost:27861)` in
   ETS2.
2. Run `make -C linux_bridge portal` from the repository root.
3. Run `./linux_bridge/build/portal_sender` in the active Wayland desktop
   session, not over SSH or with `sudo`.
4. Choose exactly one native window or monitor in the system portal dialog.
5. Verify the selected content appears in ETS2. Stop with Ctrl+C; start it again
   and make another selection to test reconnecting.

When using the Steam wrapper, press Ctrl+F8 and click `Choose Window` to select a
different source. Closing the captured application should also cause a new
chooser to appear after the helper restarts.

If D-Bus reports that the ScreenCast interface is unavailable, verify the normal
desktop portal and the KDE or Niri-compatible portal backend are running. The
sender does not modify portal configuration.

## Next step

Test the experimental sender on Niri and across more KDE setups, then add format
support only for formats actually observed there (DMA-BUF/modifier negotiation
or additional raw formats if required). After that, persist the portal restore
token to avoid showing the chooser on every launch.
