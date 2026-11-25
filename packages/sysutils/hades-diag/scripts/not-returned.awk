# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: the drivers that a start was starting when its log ends. With initcall_debug the kernel writes
# "calling  <function> @ <pid>" when it calls the start of a driver, built in or a module, and
# "initcall <function> returned <status> after <time> usecs" when that returns (init/main.c, do_one_initcall()).
# This lists the ones that were called and had not returned by the last line of a journal or of a copy of the kernel
# log: in a start that hung, what it was in (issue 14 of LibreELEC Hades: the journal of a start that hung named
# snd_virtuoso, nvidia, cfg80211 and snd_usb_audio that way). For each, its "calling" line and the last kernel line
# after it that a module's driver begins with its name ("<module> <device>: ..." or "<module>: ..."): how far it got.
# A probe that a driver runs on its own after its start returned (asynchronous probing) has no such line, nor has a
# driver that the kernel log says nothing of: those are not seen. Nothing is written where the log has no "calling"
# line (initcall_debug was off). Read by collect.sh (previous_boot() and early_log()); BusyBox awk.
# Only a line that begins with "calling" or "initcall" after the prefix of the kernel's lines counts: in a journal
# "kernel: ", in a copy of the kernel log (dmesg) "] ". The PCI fixups that initcall_debug also shows
# ("pci <device>: calling  <fixup> @ <pid>", drivers/pci/quirks.c) say when they end only when that took 10 ms or more,
# so they are not counted.
#
# -v what=<what the log is, for the first line>
# The first line is a summary that collect.sh also writes to the kernel log.

function name(k) {
	sub(/\+0x[0-9a-f]+\/0x[0-9a-f]+/, "", k)
	return k
}

# where the line has "calling  " or "initcall " right after the prefix of a kernel line: the place after it, or 0
function after(s,    i) {
	if ((i = index($0, "kernel: " s)) > 0)
		return i + 8 + length(s)
	if ((i = index($0, "] " s)) > 0)
		return i + 2 + length(s)
	return 0
}

{ last = $0 }

(i = after("calling  ")) > 0 && $0 ~ / @ [0-9]+$/ {
	k = substr($0, i)
	sub(/ @ [0-9]+$/, "", k)
	calls++
	if (!(k in pend)) {
		n++
		order[n] = k
	}
	pend[k]++
	at[k] = NR
	text[k] = $0
	if (match(k, / \[[A-Za-z0-9_-]+\]$/)) {
		m = substr(k, RSTART + 2, RLENGTH - 3)
		mod[k] = m
		live[m]++
		mat[m] = 0
	}
	next
}

(i = after("initcall ")) > 0 && (j = index($0, " returned ")) > i {
	k = substr($0, i, j - i)
	if (pend[k] > 0) {
		pend[k]--
		if (k in mod) {
			m = mod[k]
			if (--live[m] <= 0)
				delete live[m]
		}
	}
	next
}

# (a kernel line of a module being started: in a journal after "kernel: ", in a copy of the kernel log after "] ")
{
	for (m in live)
		if (index($0, "kernel: " m " ") || index($0, "kernel: " m ": ") ||
		    index($0, "] " m " ") || index($0, "] " m ": ")) {
			mat[m] = NR
			mline[m] = $0
		}
}

END {
	if (!calls)
		exit
	left = 0
	list = ""
	for (x = 1; x <= n; x++) {
		k = order[x]
		if (pend[k] > 0) {
			left++
			if (left <= 6)
				list = list (left > 1 ? ", " : "") name(k)
		}
	}
	if (!left) {
		printf "Every driver that %s shows starting had returned by its last line (%d initcalls, %d lines).\n", what, calls, NR
		exit
	}
	if (left > 6)
		list = list ", and " (left - 6) " more"
	printf "%d of the %d initcalls in %s started and had not returned by its last line (line %d): %s.\n", left, calls, what, NR, list
	print ""
	print "With initcall_debug the kernel writes a line when it calls the start of each driver (\"calling\") and one when"
	print "that returns (\"initcall ... returned\"). The drivers below had been called and had not returned when the log"
	print "ends: the start hung or lost its power while they were starting, or the log was cut there. A driver that probes"
	print "its device on its own after its start returned (asynchronous probing) has no such line and is not listed."
	for (x = 1; x <= n; x++) {
		k = order[x]
		if (pend[k] <= 0)
			continue
		print ""
		print name(k)
		printf "  called at line %d: %s\n", at[k], text[k]
		if (k in mod) {
			m = mod[k]
			if (mat[m] > at[k])
				printf "  the last line of %s after it, line %d: %s\n", m, mat[m], mline[m]
			else
				printf "  no line of %s after it\n", m
		}
	}
	print ""
	printf "The last line, %d: %s\n", NR, last
}
