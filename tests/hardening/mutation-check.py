#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# mutation-check.py [--work DIR] <corei7-64 recipe workdir>
#
# Shows the tests can fail: each mutant below breaks one behaviour they are
# meant to pin, in a copy of the tree, and the ASan+UBSan build (host gcc on
# the recipe's sysroot, as run-sanitizers.sh builds it) has to fail on it -
# through an assertion, a crash or a timeout. The
# unmutated copy has to pass first, or nothing below means anything.

import argparse, glob, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/configurator/mutants"))
ap.add_argument("workdir")
opts = ap.parse_args()
# Each verdict as it comes, also when the output is a file.
sys.stdout.reconfigure(line_buffering=True)

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONF = "src/Configurator.cpp"
BUS = "src/BusClient.cpp"

# name, file, text, replacement, what should catch it (file, text and
# replacement may be parallel lists, for a mutant that needs more than one
# edit)
MUTANTS = [
    # What it did before: a stamp newer than its file meant configured.
    ("stamp-checksum-ignored", CONF,
     "\treturn recorded == ContentChecksum(ReadFile(confFile));",
     "\treturn true;",
     "Stamps.ConfiguredAgainWhenTheContentsChangeButTheMtimeDoesNot"),
    ("stamp-checksum-sha1", "src/Configurator.cpp", "G_CHECKSUM_SHA256", "G_CHECKSUM_SHA1",
     "Stamps.TheStampRecordsTheSha256OfTheContents"),
    ("stamp-checksum-of-the-file-now", CONF,
     "\tconst std::string sum = ProcessedChecksum(confFile);",
     "\tconst std::string sum = ContentChecksum(ReadFile(confFile));",
     "Stamps.TheStampIsForTheContentsThatWereSent"),
    ("stamp-written-through-symlink", CONF, " | O_NOFOLLOW", "",
     "Stamps.AStampIsNotWrittenThroughASymlink"),
    ("stamp-not-truncated", CONF, " | O_TRUNC", "",
     "Stamps.ALongerOldStampIsOverwrittenWhole"),
    ("stamp-fd-not-closed", CONF, "\t(void)close(stampFd);\n", "",
     "Stamps.CheckingAndMarkingLeaveNoDescriptorOpen"),
    # The scan, as it was.
    ("scan-stops-at-a-bad-entry", CONF,
     "\t\t\tLOG_ERROR(MSGID_CONFIGURATOR_ERROR, 0, \"Failed to get file information on: %s\", filePath.c_str());\n\t\t\tcontinue;",
     "\t\t\tLOG_ERROR(MSGID_CONFIGURATOR_ERROR, 0, \"Failed to get file information on: %s\", filePath.c_str());\n\t\t\tbreak;",
     "Scan.ADanglingSymlinkDoesNotStopTheScan"),
    ("scan-follows-loops", CONF,
     "!visited.emplace(dirStat.st_dev, dirStat.st_ino).second",
     "!visited.emplace(dirStat.st_dev, dirStat.st_ino).second && false",
     "Scan.ASymlinkLoopIsScannedOnce"),
    ("scan-takes-a-fifo", CONF, "\t\tif (!S_ISREG(statBuf.st_mode)) {", "\t\tif (false) {",
     # Reading the FIFO blocks; the test run times out.
     "timeout"),
    ("subdirectory-owner-lost", CONF, "\t\tif (!parent.empty())\n\t\t\tm_parentDirMap[filePath] = parent;\n", "",
     "Scan.AFileIsOwnedByTheDirectoryItIsIn"),
    # Run(), as it was: it called itself once for each failing file.
    ("run-recurses-per-failure", CONF,
     "\t\tm_pendingConfigs.pop_back();\n\t}\n",
     "\t\tm_pendingConfigs.pop_back();\n\t\treturn Run();\n\t}\n",
     "RunConfigs.ManyFailuresDoNotGrowTheStack"),
    ("in-progress-counted-by-contents", CONF,
     "\t\t\tm_configureOk.push_back(filePath);", "\t\t\tm_configureOk.push_back(config);",
     "RunConfigs.OneThatNeedsNoRequestIsCountedByPath"),
    ("failure-not-counted", CONF, "\t\t\tm_configureFailed.push_back(filePath);\n", "",
     "RunConfigs.AFailureIsCountedByPathAndTheRestStillRun"),
    # The bus calls, as they were.
    ("id-may-be-a-path", BUS,
     "\treturn !name.empty() && name != \".\" && name != \"..\" && name.find('/') == std::string_view::npos;",
     "\treturn !name.empty();",
     "PackageRequests.AnIdIsOnePathComponent"),
    ("scan-kept-before-it-is-checked", BUS,
     ["\t\tstd::vector<PackageRequest> requests;\n\t\tMojErr err = ParsePackageRequests(payload, false, requests);\n\t\tMojErrCheck(err);\n\n\t\tm_client.m_msg.reset(msg);\n",
      "\t\tstd::vector<PackageRequest> requests;\n\t\tMojErr err = ParsePackageRequests(payload, true, requests);\n\t\tMojErrCheck(err);\n\n\t\tm_client.m_msg.reset(msg);\n"],
     ["\t\tstd::vector<PackageRequest> requests;\n\t\tm_client.m_msg.reset(msg);\n\t\tMojErr err = ParsePackageRequests(payload, false, requests);\n\t\tMojErrCheck(err);\n\n",
      "\t\tstd::vector<PackageRequest> requests;\n\t\tm_client.m_msg.reset(msg);\n\t\tMojErr err = ParsePackageRequests(payload, true, requests);\n\t\tMojErrCheck(err);\n\n"],
     "BusMethods.ARefusedCallIsNotKeptForASecondReply"),
    ("run-kept-before-it-is-checked", BUS,
     "\t\tMojObject types;\n\t\tMojErr err = payload.getRequired(\"types\", types);",
     "\t\tm_client.m_msg.reset(msg);\n\t\tMojObject types;\n\t\tMojErr err = payload.getRequired(\"types\", types);",
     "BusMethods.RunNeedsKnownTypes"),
    # Not here: taking the pending call off the queue by reference instead of
    # moving it. Nothing the call does reaches the queue before the
    # reference is last read, so no test can fail on it; the move stays, so
    # that a later change to that code cannot read a moved-from element.
]

S = os.path.join(opts.workdir, "recipe-sysroot")
N = os.path.join(opts.workdir, "recipe-sysroot-native")
env = dict(os.environ, PATH=os.path.join(N, "usr/bin") + os.pathsep + os.environ["PATH"],
           PKG_CONFIG_SYSROOT_DIR=S,
           PKG_CONFIG_LIBDIR=S + "/usr/lib/pkgconfig:" + S + "/usr/share/pkgconfig",
           PKG_CONFIG_PATH=S + "/usr/lib/pkgconfig",
           ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_malloc=0",
           UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1",
           G_SLICE="always-malloc", DEBUGINFOD_URLS="")

rt = os.path.join(opts.work, "hostrt")
os.makedirs(rt, exist_ok=True)
for lib in ("libasan.so.8", "libubsan.so.1", "libstdc++.so.6", "libgcc_s.so.1"):
    f = subprocess.run(["gcc", "-print-file-name=" + lib], capture_output=True, text=True).stdout.strip()
    if os.path.exists(f):
        dst = os.path.join(rt, lib)
        if os.path.lexists(dst):
            os.remove(dst)
        os.symlink(os.path.realpath(f), dst)


def build_and_run(name, mutate=None):
    tree = os.path.join(opts.work, name, "src")
    build = os.path.join(opts.work, name, "build")
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(SRC, tree, ignore=shutil.ignore_patterns(".git"))
    if mutate:
        paths, olds, news = mutate
        # A mutant is one edit, or several given as parallel lists; the file
        # may be one for all of them, or a list of its own.
        if isinstance(olds, str):
            olds, news = [olds], [news]
        if isinstance(paths, str):
            paths = [paths] * len(olds)
        for path, old, new in zip(paths, olds, news):
            p = os.path.join(tree, path)
            text = open(p).read()
            if text.count(old) != 1:
                return None, "mutation does not apply in %s (%d matches)" % (path, text.count(old))
            open(p, "w").write(text.replace(old, new))
    cfg = subprocess.run(["cmake", "-S", tree, "-B", build, "-G", "Ninja",
                          "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(tree, "tests/hardening/host-sysroot.cmake"),
                          "-DCONFIGURATOR_SYSROOT=" + S, "-DCONFIGURATOR_HOST_CC=gcc", "-DWEBOS_INSTALL_ROOT=/",
                          "-DCMAKE_INSTALL_LOCALSTATEDIR=/var",
                          "-DBUILD_TESTING=ON",
                          "-DCONFIGURATOR_SANITIZE=address,undefined", "-DCMAKE_CXX_FLAGS=-g -O1",
                          "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--dynamic-linker=%s/usr/lib/ld-linux-x86-64.so.2 "
                          "-Wl,--disable-new-dtags -Wl,-rpath,%s:%s/usr/lib" % (S, rt, S)],
                         capture_output=True, text=True, env=env)
    if cfg.returncode:
        return None, "configure failed:\n" + cfg.stdout[-2000:] + cfg.stderr[-2000:]
    b = subprocess.run(["ninja", "-C", build, "configurator-tests"], capture_output=True, text=True, env=env)
    if b.returncode:
        return None, "build failed:\n" + b.stdout[-2000:]
    try:
        r = subprocess.run([os.path.join(build, "tests/configurator-tests")], capture_output=True, text=True,
                           env=env, timeout=120)
    except subprocess.TimeoutExpired:
        return -1, "timeout: the tests did not finish"
    return r.returncode, r.stdout + r.stderr


fail = False
rc, out = build_and_run("unmutated")
if rc != 0:
    print("FAIL the unmutated tree does not pass:\n" + (out or "")[-3000:])
    sys.exit(1)
print("ok   unmutated tree passes")

for name, path, old, new, catcher in MUTANTS:
    rc, out = build_and_run(name, (path, old, new))
    if rc is None:
        print("FAIL %-26s %s" % (name, out))
        fail = True
    elif rc == 0:
        print("FAIL %-26s survived: no test failed" % name)
        fail = True
    elif catcher not in out:
        print("ok   %-26s killed, but not by %s" % (name, catcher))
    else:
        print("ok   %-26s killed by %s" % (name, catcher))

sys.exit(1 if fail else 0)
