#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-on-device.sh
#
# Run on the device, as root, against the installed configurator. It checks:
#
# - the stamps: every configuration under /etc/palm/ that has a stamp in the
#   cache records the SHA-256 of the file as it is now. An empty stamp is one
#   from before the checksum, and a mismatch is a file that changed without
#   being configured again; both are reported. (Right after an image update
#   and a reboot there should be neither.)
# - the bus calls: scan, rescan and unconfigure refuse an id that is not one
#   path component ("../.." reached outside usr/palm/), and answer the
#   refusal once; run refuses an unknown type; run with a known type
#   succeeds (it is lazy: what is configured already is skipped).
#
# configurator is a dynamic service, started by the hub on the first call.
# Needs luna-send and busybox (find, sha256sum, tr).
set -u

C=luna://com.palm.configurator
CACHE=/var/cache/configurator
fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }
skip() { echo "SKIP $*"; }

# Up to two replies, so a call answered twice shows; -w ends the wait for
# the second one.
send() { luna-send -w 10000 -n 2 "$C/$1" "$2" 2>&1; }
replies() { printf '%s\n' "$1" | grep -c '"returnValue"'; }

# ---- stamps ----------------------------------------------------------------------
if [ ! -d "$CACHE" ]; then
	skip "no $CACHE"
else
	checked=0 empty=0 stale=0
	for f in $(find /etc/palm -type f 2>/dev/null); do
		stamp=$CACHE/$(printf '%s' "$f" | tr / _)
		[ -f "$stamp" ] || continue
		checked=$((checked + 1))
		recorded=$(cat "$stamp")
		if [ -z "$recorded" ]; then
			empty=$((empty + 1))
			echo "     empty stamp (no checksum): $f"
		elif [ "$recorded" != "$(sha256sum "$f" | cut -d' ' -f1)" ]; then
			stale=$((stale + 1))
			echo "     stamp does not match the file: $f"
		fi
	done
	if [ $checked -eq 0 ]; then
		skip "no stamped configuration under /etc/palm"
	elif [ $((empty + stale)) -eq 0 ]; then
		pass "$checked stamps record their file's SHA-256"
	else
		bad "$checked stamps: $empty without a checksum, $stale not matching"
	fi
	links=$(find "$CACHE" -type l | wc -l)
	[ "$links" -eq 0 ] && pass "no symlinks in $CACHE" || bad "$links symlinks in $CACHE"
fi

# ---- bus calls ---------------------------------------------------------------------
refused_once() {
	r=$(send "$1" "$2")
	case "$r" in
		*'"returnValue"'*false*)
			n=$(replies "$r")
			[ "$n" -eq 1 ] && pass "$1 refuses $2" || bad "$1 $2: $n replies: $r" ;;
		*) bad "$1 $2: $r" ;;
	esac
}
for m in scan rescan unconfigure; do
	refused_once $m '[{"id":"../..","type":"app","location":"system"}]'
	refused_once $m '[{"id":"com.example.none","type":"app","location":"system"},{"id":"a/b","type":"app","location":"system"}]'
	refused_once $m '[{"id":"com.example.none","type":"widget","location":"system"}]'
done
refused_once run '{"types":["watches"]}'
refused_once run '{}'

r=$(send run '{"types":["dbkinds"]}')
case "$r" in
	*'"returnValue"'*true*) pass "run dbkinds" ;;
	*) bad "run dbkinds: $r" ;;
esac

exit $fail
