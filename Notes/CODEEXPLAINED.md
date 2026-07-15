# OrderDB — Code Explained

A networked order-tracking system for a small office: a C++ server holds the
data, and a colored command-line client (Linux and Windows) connects to it
over the network to add, search, update, and report on orders. This document
explains how it's built, what every file does, what libraries it uses, and
how to build and run it.

---

## 1. Architecture, in one picture

```
                    TCP socket, port 5050
                    (plain text protocol)
 ┌──────────────┐  ─────────────────────▶  ┌──────────────────┐
 │ orderdb_client│                          │  orderdb_server   │
 │ (any PC on    │  ◀─────────────────────  │  (runs on one     │
 │  the network) │                          │   machine, owns   │
 └──────────────┘                          │   the data files) │
                                             └─────────┬─────────┘
       (up to 5 clients                                │
        connected at once)                    data/orders.dat
                                               data/users.dat
                                               data/connections.log
```

**Why client-server instead of everyone sharing a file over the network
drive:** file locking behaves differently on Windows vs Linux and is
notoriously unreliable over SMB/NFS shares — two people saving at once can
corrupt the file. A single server process that's the *only* thing touching
the data files sidesteps that entirely, and gives a natural place to put
login/password checks that a shared file can't.

**Why plain TCP text instead of HTTP/REST or a database:** this is a small,
fixed set of ~5 people on one office LAN. A lightweight line-based protocol
(see §7) keeps both the server and the client simple, dependency-free, and
easy to test by hand (you can literally `telnet` or `nc` into the server and
type commands). There's no HTTP framework, JSON library, or database engine
anywhere in this project — every dependency is either the C++ standard
library or one small vendored header (see §2).

---

## 2. Libraries and dependencies

This project deliberately has almost no external dependencies:

| What | Where it comes from | What it's used for |
|---|---|---|
| C++17 standard library | Compiler-provided | Everything: strings, containers, file I/O, threads, mutexes, `<chrono>` timing |
| **picosha2** (`include/picosha2.h`) | Vendored single header, MIT license, by okdshin | SHA-256 hashing for password storage (see §5.4) |
| Winsock2 (`ws2_32`) | Windows SDK, linked only when `_WIN32` is defined | TCP sockets on Windows |
| POSIX sockets (`sys/socket.h` etc.) | Linux libc | TCP sockets on Linux |
| pthreads (via CMake's `Threads::Threads`) | System | Backs `std::thread`/`std::mutex` on Linux; on Windows this resolves to MinGW's winpthreads (see `README_WINDOWS_BUILD.md`) |

That's it — no Boost, no JSON library, no ORM, no database. The entire
"database" is a hand-rolled append-only text file format (§6), which is
enough for the actual scale here (a handful of users, an office's worth of
orders) and is trivial to back up (it's just files — copy the `data/`
folder).

---

## 3. Project structure

```
orderdb2/
├── CMakeLists.txt              Build configuration (native + cross-compile aware)
├── cmake/
│   ├── toolchain-mingw64.cmake  Cross-compile to 64-bit Windows
│   └── toolchain-mingw32.cmake  Cross-compile to 32-bit Windows
├── include/
│   ├── Order.h                  The core data record + its (de)serialization
│   ├── Protocol.h                The wire protocol spec + helper functions
│   ├── Socket.h                  Cross-platform TCP socket wrapper (declarations)
│   ├── StorageEngine.h           On-disk storage + in-memory search indexes
│   ├── AuthManager.h             User accounts, password hashing, admin roles
│   ├── Colors.h                  ANSI terminal colors for the CLI client
│   └── picosha2.h                Vendored SHA-256 implementation (third-party)
├── src/
│   ├── Socket.cpp                Socket wrapper implementation
│   ├── StorageEngine.cpp         Storage engine implementation
│   ├── AuthManager.cpp           Auth manager implementation
│   ├── server_main.cpp           The server program (protocol dispatch, main())
│   └── client_main.cpp           The CLI client program (menu, prompts, main())
├── dist/                        Native Linux build output (gitignored in spirit)
├── dist-win64/                  Windows x86_64 cross-build output
├── dist-win32/                  Windows x86 cross-build output
└── README_WINDOWS_BUILD.md      Step-by-step Windows cross-compilation guide
```

---

## 4. Data model — `Order.h`

`Order` is a plain struct — every field on an order, plus the logic to turn
one into a line of text and back again.

**Fields:** `id`, `customer`, `supplier`, `description`, `partNumber`,
`website`, `contactName`/`contactNumber` (the contact *at the supplier*),
`orderedBy` (which login placed it — server-controlled, see §5.4),
`dateOrdered`, `status`, `clientName` (who *requested* the order — distinct
from the supplier contact), and the pricing trio `quantity` / `unitPrice` /
`subtotal` / `total`.

**`calculateTotals()`** is one line of actual logic: `subtotal = quantity *
unitPrice`, `total = subtotal`. It's called by the storage engine on every
add/update so the stored total is always mathematically derived, never
trusted as client input — this is what fixed a real bug earlier in
development where a missing quantity silently zeroed out every total.

**`serialize()`/`deserialize()`** turn an `Order` into one line of a text
file and back. The format is pipe-delimited (`|`), with backslash-escaping
for any `|`, `\`, or newline that appears inside a field value (so a
description containing a literal pipe character doesn't corrupt the file).
`deserialize()` explicitly handles **three generations** of the file format
side by side, so old data keeps loading correctly after the schema grew:

```
OLDEST:  id|customer|...|status|total
MIDDLE:  id|customer|...|status|quantity|unitPrice|subtotal|total
CURRENT: id|customer|...|status|quantity|unitPrice|subtotal|total|clientName
```

This matters because the data file is never migrated or rewritten when the
schema changes — old rows just get read with sensible defaults for fields
that didn't exist yet (e.g. `clientName` defaults to `""` for rows written
before that field existed).

---

## 5. The server — `StorageEngine`, `AuthManager`, `server_main.cpp`

### 5.1 `StorageEngine` — how orders are actually stored and searched

The storage format is **append-only**: adding, updating, or deleting an
order never rewrites existing bytes in `orders.dat` — it only appends a new
line.

- **Add**: append the new order's serialized line.
- **Update**: append a *new* line with the same `id` but updated fields. The
  old line for that id is still physically in the file, but is superseded.
- **Delete**: append a tombstone line (`D|<id>`) marking that id as gone.

Why append-only instead of rewriting the file in place: it's crash-safe (a
half-written rewrite could corrupt the whole file; a half-written append
just leaves one truncated line, which is easy to detect and ignore) and
it's cheap (no need to rewrite gigabytes of file to change one row — not
that this project is anywhere near that scale, but the pattern costs
nothing and avoids a whole class of bugs).

**Startup (`load()`)** does one linear pass over the file: for every line,
if it's a tombstone, mark that id deleted; otherwise remember the *byte
offset* of that line as the current location of that id (later lines for
the same id simply overwrite the earlier remembered offset). After that
pass, every surviving order gets indexed (see below). This means a single
order's current data is always a **direct file seek**, not a scan — you
pay the O(n) cost once at startup, not on every read afterward.

**In-memory indexes**, rebuilt from scratch every startup, never persisted:

| Index | Type | Used for |
|---|---|---|
| `idToOffset_` | `id → byte offset` | Direct lookup/seek for any order by id |
| `byDate_` | `date string → set<id>`, a **sorted** `std::map` | Date-range and monthly-report queries, via `lower_bound`/`upper_bound` — this works because ISO dates (`YYYY-MM-DD`) sort correctly as plain strings |
| `bySupplier_`, `byOrderedBy_`, `byContactName_`, `byContactNumber_`, `byClientName_` | `lowercased key → set<id>` | Exact or substring search on each field |
| `byDescWord_`, `byPartWord_` | `word → set<id>` | Word-level search on description/part number, so "bolt" matches "bolts" |

**Search implementations** mostly follow one of two patterns:
1. **Exact-key lookup** (supplier, ordered-by): lowercase the query, look it
   up directly in the hash map.
2. **Substring scan over index keys** (description, part number, contact
   name/number, client name): for each stored key, check if it *contains*
   the query. This is what lets a search for "bolt" find "bolts", or the
   last four digits of a phone number find the full number. It's an O(unique
   keys) scan rather than O(1), but for an office's worth of distinct
   suppliers/contacts/part numbers, that's a trivial cost.

**`advancedSearch()`** is the odd one out: rather than using the indexes, it
does a single linear pass over *every* order, checking each of 8 optional
filters (date range, customer, supplier, part number, ordered-by, contact
number, client name) and keeping only orders that pass all of them. This is
simpler to reason about for a combined multi-field query than trying to
intersect multiple indexes, and at this data scale the performance
difference is irrelevant.

**`searchByMonth(year, month)`** just computes the first and last calendar
day of that month (correctly handling leap years for February) and delegates
to `searchByDateRange()`.

### 5.2 `AuthManager` — accounts and passwords

Users are stored the same append-only way as orders: one line per account
change, `username|salt|hash|isAdmin`, and the *latest* line for a given
username wins on load.

- **Passwords are never stored in plain text.** Each user gets a random
  32-character hex salt; the stored hash is `SHA256(salt + password)` (via
  the vendored `picosha2` library). This is adequate for a small internal
  office tool. It is explicitly *not* what you'd want for anything
  internet-facing — a proper deployment there should use a slow,
  purpose-built password hash like bcrypt or Argon2 instead of a single
  fast SHA-256 round, to resist offline brute-forcing if the file ever
  leaks. That tradeoff is intentional and documented in the header comment.
- **`isAdmin`** is a simple boolean flag per account, gating three server
  commands: `REGISTER` (creating new accounts), `ADMIN_RESET_PASSWORD`
  (resetting someone else's password without knowing the old one), and
  `SHUTDOWN`.
- **Self-service password change** (`changePassword`) requires the correct
  current password. **Admin reset** (`resetPassword`) doesn't — that's the
  point, it's for recovering a locked-out account.

### 5.3 `Socket` / `ServerSocket` — cross-platform networking

A thin RAII wrapper so the rest of the code never has to `#ifdef _WIN32`
for networking. `Socket` owns one connected TCP socket; `ServerSocket` owns
the listening socket and produces new `Socket`s from `accept()`.

Two details worth knowing:
- **`sendLine`/`recvLine`** implement the wire protocol's framing: every
  message is one line terminated by `\n`. `recvLine` reads one byte at a
  time until it sees that terminator — simple and correct at this traffic
  scale, though not the most efficient possible approach for high-throughput
  scenarios (not a concern here).
- **`peerAddress()`** captures the connecting client's IP at `accept()` time
  (via `inet_ntop`), used purely for the connection log (§5.4). The
  move-constructor/move-assignment operators were a real bug source early
  on: the first version forgot to carry `peerAddress_` across a move, so
  every "disconnected" log line showed a blank IP — fixed by making the
  move operations copy that field too.

### 5.4 `server_main.cpp` — tying it together

`main()` does four things at startup: opens the storage engine and auth
manager (creating `data/` files if they don't exist), bootstraps a default
`admin`/`changeme` account with admin rights if no users exist yet, starts
listening on the given port (default 5050), then loops forever accepting
connections.

**Connection handling**: each accepted connection is handed to
`handleClient()` running in its own `std::thread`, detached (the thread
cleans itself up when the client disconnects; nothing needs to `join()` it).
Two safeguards sit around this:

- **A hard cap of 5 concurrent connections** (`MAX_CONNECTIONS`). The 6th
  simultaneous connection gets an immediate `ERR|server is full...` message
  and is closed, without ever incrementing the counter or spawning a thread.
  The check-then-increment happens only in `main()`'s single thread, so
  there's no race there even though decrements happen concurrently from
  multiple client-handler threads.
- **Every connect/disconnect is logged** with timestamp and IP, to both the
  console and `data/connections.log` (append-only, so it's a permanent
  audit trail), guarded by a mutex so log lines from different threads
  don't interleave into garbage.

**`handleClient()`** is a straightforward loop: read a line, split it into
`|`-delimited fields, dispatch on the first field (the command name), and
respond according to the wire protocol (§7). `LOGIN` and `QUIT` work before
authentication; every other command requires `loggedIn == true` first, and
`REGISTER`/`ADMIN_RESET_PASSWORD`/`SHUTDOWN` additionally require
`g_auth->isAdmin(currentUser)`.

A few defaulting behaviors live here rather than in `StorageEngine`, since
they're about *interpreting client input*, not storage mechanics:
- **Blank date → today.** Leaving the date blank on `ADD`/`UPDATE`, or
  leaving either side of a date-range search blank, defaults to today's
  date rather than being stored/treated as empty.
- **Blank or garbage quantity → 1**, via `parseQuantity()` — handles both
  an empty string and non-numeric input gracefully rather than throwing.
- **`orderedBy` is never taken from client input.** It's always set to
  whichever account is logged in on that connection. This is deliberate:
  making it client-editable would let anyone claim any order was placed by
  anyone. If you want orders to correctly show who placed them, give that
  person their own login via `REGISTER` rather than trying to make this
  field editable.

**`SHUTDOWN`** is intentionally blunt rather than trying to gracefully
unwind the accept loop: it replies `OK`, waits 300ms (so that reply
actually reaches the client before the process dies), then calls
`std::exit(0)`. This is safe because every write is already flushed to disk
immediately on append (see `StorageEngine::appendLine`), so there's no
buffered, at-risk data at the moment of exit.

---

## 6. On-disk file formats

All three files live in `data/` next to the executable, and are plain text
— you can open them in any text editor if you ever need to inspect or hand-
edit something (not recommended for routine use, but useful for debugging).

**`orders.dat`** — one order per line (see §4's serialize format), plus
tombstone lines `D|<id>` for deletions. Append-only; never rewritten.

**`users.dat`** — one line per account creation/change:
`username|salt|hash|isAdmin`. Append-only; the latest line for a username
wins.

**`connections.log`** — one line per connect/disconnect/rejected-connection
event, human-readable, timestamped. Purely an audit trail; nothing reads
this file back in on startup.

---

## 7. Wire protocol reference

Every message, in both directions, is **one line of text terminated by
`\n`, with fields separated by `|`**. The authoritative reference lives as
a comment block at the top of `include/Protocol.h` — this section is a
readable summary of the same thing.

### Client → Server

| Command | Fields | Notes |
|---|---|---|
| `LOGIN` | `username\|password` | Works before authentication |
| `REGISTER` | `username\|password` | **Admin only.** Creates a new account (non-admin by default) |
| `ADD` | `customer\|supplier\|description\|partNumber\|website\|contactName\|contactNumber\|dateOrdered\|status\|quantity\|unitPrice\|clientName` | `dateOrdered` blank → today. `quantity` blank → 1. `clientName` optional entirely (backward compat) |
| `UPDATE` | `id\|` + same fields as `ADD` | Same defaulting rules |
| `DELETE` | `id` | |
| `FIND` | `id` | Returns 0 or 1 row |
| `SEARCH_DESC` / `SEARCH_PART` | `keyword` | Substring match, word-indexed |
| `SEARCH_SUPPLIER` / `SEARCH_WHO` | `value` | Exact match (case-insensitive) |
| `SEARCH_CONTACT_NAME` / `SEARCH_CONTACT_NUM` / `SEARCH_CLIENT` | `value` | Substring match |
| `SEARCH_DATE` | `startDate\|endDate` | Either side blank → today |
| `REPORT_MONTH` | `YYYY-MM` | Every order in that calendar month |
| `ADVANCED_SEARCH` | `startDate\|endDate\|customer\|supplier\|partNumber\|orderedBy\|contactNumber\|clientName` | Dates blank → today; everything else blank → unfiltered |
| `REPORT` | *(none)* | Every order, no filter |
| `CHANGE_PASSWORD` | `oldPassword\|newPassword` | Self-service |
| `ADMIN_RESET_PASSWORD` | `username\|newPassword` | **Admin only** |
| `SHUTDOWN` | *(none)* | **Admin only.** Stops the server |
| `QUIT` | *(none)* | Closes the connection cleanly |

### Server → Client

| Response | Meaning |
|---|---|
| `OK\|<message>` | Success |
| `ERR\|<message>` | Failure, with a human-readable reason |
| `ROW\|id\|customer\|supplier\|description\|partNumber\|website\|contactName\|contactNumber\|orderedBy\|date\|status\|total\|clientName\|quantity` | One order, sent once per matching row |
| `END` | Marks the end of a (possibly zero-row) list of `ROW` lines |

A design note worth knowing if you ever extend the protocol: the client's
`printRows()` function used to only recognize `END` as a terminator, so if
the server ever replied with a single `ERR`/`OK` line for a command that
was *supposed* to stream rows (e.g. an unrecognized command name), the
client would sit there waiting forever for an `END` that was never coming.
That's fixed now — `printRows()` also stops cleanly on an `ERR`/`OK` line —
but it's the reason every row-returning command handler needs to actually
send `END`, and why that defensive check exists on the client side too.

---

## 8. The CLI client — `client_main.cpp`

A menu-driven terminal program. On startup it connects, prompts for
username/password, then loops showing a numbered menu (grouped and
color-coded: green for add/update/delete, cyan for searches, amber for
reports, pink for your own account, red for admin actions) until you quit.

**Colors** (`Colors.h`) are plain ANSI escape codes, applied consistently:
amber for field labels, green for successful (`OK`) responses, red for
errors, bold cyan section banners (`== ADD ORDER ==`) so it's always visually
obvious which screen you're on. On Windows, `enableAnsiOnWindows()` is
called once at startup to turn on ANSI interpretation in the console
(`ENABLE_VIRTUAL_TERMINAL_PROCESSING`) — without it, Windows 10/11 consoles
would show the raw escape codes as junk text instead of colors.

**Cancellable multi-step forms**: Add Order and Update Order collect eleven
fields each. Typing `cancel` at *any* prompt (matched case-insensitively,
via `isCancelWord()`) aborts the whole form immediately — nothing is sent
to the server, since the `ADD`/`UPDATE` command is only ever transmitted
once, after every field has been collected. There's no server-side
"transaction" to roll back because nothing partial ever left the client.

**Status is a fixed menu, not free text** (`promptStatusOrCancel()`): six
numbered options (Pending / Ordered / Back ordered / Shipped / Received /
Cancelled), looping on invalid input rather than accepting arbitrary text.
This exists because free-text status entry previously let inconsistent
spellings like `"ORDERD"` and `"ordered"` slip in as data, which then
silently broke anything trying to group or filter by status.

**Every report/search funnels through one `printRows()` function**, which
means two things apply uniformly everywhere without needing to be
implemented separately per screen: a running sum of the `Total` column
printed at the end of any multi-row result, and the defensive "stop on
`ERR`/`OK` instead of hanging forever" behavior mentioned in §7.

**Robustness note**: the main menu loop reads the choice with `std::cin >>
choice`. If that stream ever hits genuine EOF (stdin closed/piped input
exhausted), the loop deliberately exits cleanly instead of looping forever
— an earlier version called `cin.clear()` unconditionally on any read
failure, which silently masked EOF forever and spun the CPU at 100% with
no way out.

---

## 9. Build system — `CMakeLists.txt` and cross-compilation

One `CMakeLists.txt` builds three logically separate things: a static
library `orderdb_core` (Storage engine, Auth manager, Socket — everything
shared between server and client), and the two executables
`orderdb_server`/`orderdb_client`, each linked against that library.

**Output directory is target-aware**: native Linux builds land in `dist/`;
cross-compiling for Windows lands in `dist-win64/` or `dist-win32/`
depending on target architecture, so building one target never silently
overwrites another sitting in the same source tree (this used to happen
before that logic was added — 32-bit and 64-bit Windows builds shared the
same `dist/` folder and each one clobbered the other).

**`find_package(Threads REQUIRED)`** plus **`if(WIN32) target_link_libraries(...
ws2_32)`** are the only platform-conditional pieces — everything else in the
build is identical across Linux and Windows targets.

**Cross-compiling for Windows** uses the two toolchain files in `cmake/`.
The short version: they auto-detect whether your MinGW installation ships a
single compiler binary (openSUSE's packaging) or separate `-posix`/`-win32`
variants (Debian/Ubuntu's packaging), and specifically prefer/require the
POSIX threading variant, because the server's one-thread-per-connection
design needs real `std::thread` support that MinGW's older "win32" threading
model doesn't fully provide. Full step-by-step instructions, including the
exact `zypper` commands for openSUSE Tumbleweed, are in
**`README_WINDOWS_BUILD.md`** — that file also documents what was actually
verified (genuine PE executables, correct static linking so no MinGW
runtime DLLs need to ship alongside the `.exe`) versus what wasn't
(full runtime behavior on an actual Windows machine).

---

## 10. How to build

### Linux (native)

```bash
mkdir build && cd build
cmake ..
make -j4
```

Output: `dist/orderdb_server` and `dist/orderdb_client`.

### Windows (cross-compiled from Linux)

See `README_WINDOWS_BUILD.md` for the full walkthrough (installing the
MinGW-w64 toolchain, etc). Short version once the toolchain is installed:

```bash
mkdir build-win64 && cd build-win64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-mingw64.cmake
make -j4
```

Output: `dist-win64/orderdb_server.exe` and `dist-win64/orderdb_client.exe`
— copy the whole `dist-win64/` folder (it includes an empty `data/`
subfolder the server expects) to the target Windows machine.

---

## 11. How to use

### Start the server

On the machine that will hold the data:

```bash
./orderdb_server 5050
```

(Port is optional, defaults to 5050.) First run creates `data/orders.dat`,
`data/users.dat`, and a default account — **`admin` / `changeme`** — with
admin rights. Change that password immediately (menu option 16, once
logged in).

### Connect a client

From any machine on the same network:

```bash
./orderdb_client <server-ip> 5050
```

You'll be prompted for username and password, then dropped into the main
menu.

### First things to do on a fresh install

1. **Log in as `admin`/`changeme`**, then use menu option **16** to change
   that password to something real.
2. **Create a login for each real person** who'll use this (menu option
   **18**, admin only) — orders they add will then correctly show their own
   username under "Ordered By" instead of everyone appearing as `admin`.
3. Start adding orders (option **1**) and searching/reporting (options
   3–9, 12–15) as needed.

### Menu reference

| # | Action | # | Action |
|---|---|---|---|
| 1 | Add Order | 12 | Report (all orders) |
| 2 | Find Order by ID | 13 | Advanced Search Report |
| 3 | Search Description | 14 | Search Client Name |
| 4 | Search Part Number | 15 | Monthly Report |
| 5 | Search Supplier | 16 | Change My Password |
| 6 | Search Ordered By | 17 | Admin: Reset User Password |
| 7 | Search Contact Name | 18 | Admin: Register New User |
| 8 | Search Contact Number | 19 | Admin: Shutdown Server |
| 9 | Search Date Range | 20 | Quit |

Every search and report screen shows a running total of the `Total` column
at the bottom of its results.

### Backing up

Everything that matters is the `data/` folder next to the server binary —
`orders.dat`, `users.dat`, `connections.log`. Stopping the server and
copying that folder elsewhere is a complete backup; there's no separate
database engine or external state to worry about.

---

## 12. Known limitations (by design, not oversights)

- **No TLS.** Traffic, including login credentials, is plaintext on the
  wire. Appropriate for a trusted internal office LAN; not something to
  expose beyond that without adding encryption.
- **Password hashing is a single SHA-256 round with a per-user salt** —
  fine for this use case, not what you'd choose for an internet-facing
  service (see §5.2).
- **No file-locking coordination beyond the server itself owning the
  files** — this is the whole point of the client-server design (§1), not
  a gap: nothing outside the server process should ever touch `data/*.dat`
  directly while the server is running.
- **Hard cap of 5 concurrent connections**, matching the small-office scale
  this was built for — raising `MAX_CONNECTIONS` in `server_main.cpp` is a
  one-line change if that ever needs to grow.
