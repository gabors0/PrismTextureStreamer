# Linux/Wayland bridge: stage 1

Euro Truck Simulator 2 and `PrismTextureStreamerFB.dll` remain Windows binaries
running inside Proton. Native Wayland windows are not Win32 windows, so the
existing `EnumWindows`, Windows Graphics Capture, and `PrintWindow` sources
cannot capture them. This stage adds a localhost frame boundary; it deliberately
does not add portal or PipeWire capture yet.

## Frame transport protocol v1

The DLL is a TCP server bound only to `127.0.0.1:27861`. One sender connection is
handled at a time. There is no handshake: the stream is a sequence of a 32-byte
header followed immediately by its RGBA8 payload. All integers are little-endian.

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
before TCP overhead). A future PipeWire sender must scale captured frames to
these limits before transmission. Compression, authentication, and multiple
bridge screens are outside this stage.

## Build the native test sender and tests

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

## Next step

Replace the generated pattern in the native sender with one portal/PipeWire
capture session: request a window through `org.freedesktop.portal.ScreenCast`,
consume PipeWire frames, convert them to top-down RGBA8, scale to at most
1280x720, and feed the existing protocol writer. The DLL and its DX11 path do not
need to change for that step.
