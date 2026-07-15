# Cross-Compiling OrderDB for Windows

Cross-compiles the C++ server and CLI client from your openSUSE Tumbleweed
box to native Windows `.exe` files - no Windows machine needed to build,
just to run the result. This was tested end-to-end in a Linux sandbox
before being handed to you: both a 64-bit and 32-bit build were produced,
confirmed to be genuine Windows PE executables, and confirmed to have no
external DLL dependencies beyond core Windows system DLLs.

## 1. Install the MinGW-w64 cross toolchain

openSUSE ships this through separate repos per target architecture:

```bash
# 64-bit Windows (this is what you want unless you have a specific need
# for 32-bit - all Windows 10/11 machines are 64-bit)
sudo zypper addrepo https://download.opensuse.org/repositories/windows:mingw:win64/openSUSE_Tumbleweed/windows:mingw:win64.repo
sudo zypper refresh
sudo zypper install mingw64-cross-gcc-c++ mingw64-cross-pkgconf

# 32-bit Windows (only if you specifically need it)
sudo zypper addrepo https://download.opensuse.org/repositories/windows:mingw:win32/openSUSE_Tumbleweed/windows:mingw:win32.repo
sudo zypper refresh
sudo zypper install mingw32-cross-gcc-c++ mingw32-cross-pkgconf
```

Verify it installed correctly:
```bash
x86_64-w64-mingw32-g++ --version
```

**A threading detail worth knowing:** the server uses `std::thread` (one
thread per client connection), which requires MinGW's "posix" threading
model - the older "win32" model doesn't fully implement C++11 threading.
openSUSE's `mingw64-cross-gcc-c++` package is already built with posix
threading by default, so this isn't something you need to configure - it's
just why `cmake/toolchain-mingw64.cmake` looks for the compiler the way it
does (see the comments in that file if you're curious, or if you ever move
this to a different Linux distro where MinGW is packaged differently, e.g.
Debian/Ubuntu ship both variants side by side and default to the wrong
one).

## 2. Build

From the project root:

```bash
# 64-bit Windows build
mkdir build-win64 && cd build-win64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-mingw64.cmake
make -j4
cd ..

# 32-bit Windows build (only if you installed the mingw32 toolchain above)
mkdir build-win32 && cd build-win32
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-mingw32.cmake
make -j4
cd ..
```

Output lands in `dist-win64/` and `dist-win32/` respectively - kept
separate from each other and from your native Linux `dist/` build, so
building one target never overwrites another.

Each `dist-winXX/` folder already contains an empty `data/` subfolder
(created automatically by `CMakeLists.txt`), matching what the Linux build
does. **When you copy the build to a Windows machine, copy the whole
`dist-winXX/` folder, not just the `.exe` files** - the server expects a
`data/` folder next to it to store `orders.dat`, `users.dat`, and
`connections.log`.

## 3. What you get

```
dist-win64/
├── orderdb_server.exe
├── orderdb_client.exe
└── data/                  (empty - populated on first run)
```

These are statically linked - confirmed by inspecting the binary's DLL
imports, which are just `KERNEL32.dll`, `msvcrt.dll`, and `WS2_32.dll`
(Winsock, needed for networking). All three ship with every Windows
install; there's no MinGW runtime DLL (`libstdc++-6.dll`,
`libwinpthread-1.dll`, etc.) to remember to copy alongside the `.exe`,
which is a common gotcha with MinGW builds that this project avoids via
`-static -static-libgcc -static-libstdc++` in the toolchain files.

## 4. Running on Windows

Copy `dist-win64/` (or `dist-win32/`) to the Windows machine, then from a
Command Prompt or PowerShell in that folder:

```
orderdb_server.exe 5050
```

and on client machines:

```
orderdb_client.exe <server-ip> 5050
```

The colored CLI output (from the recent Colors.h work) works automatically
on Windows 10/11 - the client calls `enableAnsiOnWindows()` at startup,
which turns on ANSI escape sequence support in the console. Older Windows
versions (7/8) don't support this and would show raw escape codes instead
of colors; not a concern for a modern install, but worth knowing if this
ever needs to run somewhere older.

## 5. Testing without a Windows machine (optional)

I attempted to actually *run* the built `.exe` under Wine here as a
functional smoke test, not just confirm it compiled - but Wine's first-run
initialization didn't complete reliably in this sandbox (no display, and
its prefix setup hung rather than finishing). So to be accurate about
what's actually verified: I confirmed the binaries are genuine Windows PE
executables (`file` reports `PE32+ executable ... for MS Windows`) and that
static linking worked correctly (the only DLL imports are `KERNEL32.dll`,
`msvcrt.dll`, `WS2_32.dll` - all standard Windows system DLLs, confirmed
via `objdump -p`). That's solid evidence the cross-compile itself is
correct, but it's not the same as watching the server actually accept a
connection and add an order the way I tested the native Linux build.

If you want that last-mile confirmation before trusting this in production,
either:
- Copy `dist-win64/` to an actual Windows machine and run it there (the
  real target environment anyway), or
- Try Wine yourself with a proper display available:
  ```bash
  sudo zypper install wine
  wine dist-win64/orderdb_server.exe 5050
  ```
  This may work fine in a normal desktop session even though it didn't
  complete here.

## Known limitations

- **No installer/packaging** - this hands you `.exe` files and a `data/`
  folder, not an MSI or similar. Fine for internal office use; something
  to revisit if this ever needs wider distribution.
- **Firewall prompt on first run** - Windows will likely prompt to allow
  `orderdb_server.exe` through the firewall the first time it listens on a
  port. Normal and expected; just needs an admin to click "Allow" once.
- **Not yet runtime-tested on Windows itself** - verified as correct,
  properly statically-linked Windows binaries (see section 5), but not
  yet confirmed to actually run and behave correctly the way I tested the
  native Linux build end-to-end. Worth doing that check - either on real
  Windows or via Wine - before relying on this for real data.
