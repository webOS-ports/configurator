configurator test and hardening harness
=======================================

Four layers: unit tests on the build host, the sanitizers and valgrind over
them, static analysis and a warning gate over the sources, and a run against
the configurator on a device.

Every script takes a Yocto recipe workdir (`tmp/work/<arch>/<recipe>/<ver>`)
for the toolchain and the sysroot. Any recipe whose sysroot has db8, glib,
luna-service2, PmLogLib and googletest will do. With `rm_work`, the
configurator's own workdir is gone once it is built; imlibpurpleservice's or
activitymanager's have all five. The corei7-64 one is the one the host can
run.

Unit tests (`unit/`)
--------------------

gtest, built from the configurator's own sources (everything but `main()`)
with the cache directory moved to `CONFIGURATOR_TEST_LOCALSTATEDIR`
(default `/tmp/configurator-tests`; the stamps go under its `cache/`). They
need no bus: a `BusClient` is made but never opened, the configurators are
driven directly, and the replies to bus calls are counted on a
`MojServiceMessage` stand-in (`FakeMessage`). `ConfiguratorTestAccess`, a
friend of `Configurator`, `BusClient` and `BusClient::BusMethods`, reaches
the private parts.

| File | What it pins |
|---|---|
| `test_stamps.cpp` | a configuration counting as configured while its stamp records the SHA-256 of its contents, whatever the mtimes say (the reproducible image gives every file the same one); an old empty stamp, or a longer one, being replaced; the stamp being for the contents that were sent, not for what the file holds when the answer comes; no stamp for an answer with an error; no stamp written through a symlink; no descriptor left open |
| `test_scan.cpp` | every regular file under any depth taken once; the owning directory recorded; a dangling symlink no longer ending the scan of its directory; a symlink loop scanned once; FIFOs left alone (reading one blocks); a symlink to a file taken; configured files skipped unless reconfiguring; a missing directory |
| `test_run.cpp` | failures and files that need no request counted by path; a file that does not parse being a failure; each answer sending the next request; removal; 5000 failing files on a 256 KiB stack (Run() used to call itself once for each) |
| `test_requests.cpp` | what a scan, rescan or unconfigure entry must hold; an id being one path component (`../..` reached outside `usr/palm/`); a refused call not being kept for a second reply, and nothing in it acted on; run needing known types |
| `test_flags.cpp` | `Flags<>`, partly at compile time |

Build and run on the host:

    cmake -DBUILD_TESTING=ON ...      # with the recipe's toolchain.cmake
    ninja configurator-tests
    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path <sysroot>/usr/lib:<sysroot>/lib \
        tests/configurator-tests

Hardening (`hardening/`)
------------------------

- `run-sanitizers.sh <corei7-64 workdir>`: the tests under ASan+UBSan and
  TSan, each built with gcc and with clang, and under valgrind memcheck on
  the image toolchain's own build. The image toolchain ships no sanitizer
  runtimes, so the host's gcc and clang build against the sysroot
  (`host-sysroot.cmake`); the host's glibc is the sysroot's.
- `static-analysis.sh <corei7-64 workdir>`: gcc `-fanalyzer` at -O0 and -O2,
  the clang analyzer with the alpha checkers, clang-tidy, cppcheck
  (exhaustive, inconclusive), flawfinder and Coccinelle (`cocci/`). It fails
  on any finding not in `baseline.txt`, where each entry that was looked at
  and left has its reason. cppcheck's style notes are listed but do not
  gate. sparse is not run: it parses C only.
- `build-all.py [--clang <clang++>] <workdir>...`: the warning gate. The
  configurator and the tests are built for each architecture given, with
  the image's gcc and with clang, at `-Wall -Wextra`; a warning in `src/` or
  `tests/` fails it, one from db8's headers is listed. Run it with the
  armv7, aarch64 and corei7-64 workdirs, and the image's clang from
  `tmp/sysroots-components/x86_64/clang-native` (with its and llvm-native's
  `usr/lib` on `LD_LIBRARY_PATH`). clang builds with `-fgnuc-version=12`, as
  db8's atomics need a GCC above 4.3, and so without `_FORTIFY_SOURCE`,
  whose glibc wrappers then use a builtin only gcc has.
- `mutation-check.py <corei7-64 workdir>`: shows the tests can fail. Each
  mutant puts back one fault the configurator had: stamps by mtime, by
  another checksum, of the file as it is when the answer comes, written
  through a symlink, not truncated, or with the descriptor left open; a scan
  that stops at an entry it cannot stat, follows symlink loops, reads FIFOs
  or forgets a file's owner; Run() calling itself per failure, or counting
  by contents, or not at all; ids that are paths; and calls kept for the
  final reply before they were checked. Each must be caught by its test or
  by a timeout.

The Coccinelle rules flag a file created with `open(O_CREAT)` and no
`O_NOFOLLOW`, and a `break` in a `readdir()` loop. Both matched the code
before this harness. They are checked against `cocci/selftest.cpp`, which
has the patterns they look for, so a rule that silently matches nothing
fails the gate instead of passing it. spatch runs with `--c++=23`; without a
version it stops at the first `auto`.

On the device (`device/`)
-------------------------

`run-on-device.sh`, as root on the device, against the installed
configurator. It checks that every stamped configuration under `/etc/palm/`
has a stamp that records the SHA-256 of the file as it is, that the cache
holds no symlinks, that scan, rescan and unconfigure refuse a path as an id
(and answer the refusal once), and that run refuses unknown types and
accepts a known one.
