# SPDX-License-Identifier: GPL-2.0-or-later
# Hades diagnostics: what tells who someone is, where they live or which machine this is, in what the system
# says of itself, replaced by a marker (busybox awk, LC_ALL=C; run by redact.sh, which says where each file
# goes). A marker names what was there and is the same for the same value everywhere on this system: a
# secret of this system (KEYFILE) and the value give it, through SHA-256, and without that secret it cannot
# be turned back. Two lines that name the same address name the same marker; nothing in it is the address.
#   - IPv4 addresses, with the class of address in the marker (lan-ip, linklocal-ip, cgnat-ip, ip) and two
#     parts: the first the same for every address of one /24 network, the second for the address itself.
#     Left as they are: 0.0.0.0/8, loopback, multicast, 240/4 and 255.x (netmasks, broadcast), and a few
#     public resolvers (they tell which DNS is set, and nobody by it).
#   - IPv6 addresses likewise (the first part for the /64 network), but ::, ::1 and multicast.
#   - MAC and Bluetooth addresses (also in the names connman gives its services, and in the names BlueZ gives
#     its devices, dev_AA_BB_CC_DD_EE_FF), but all-zero, broadcast and the group addresses of IPv4 and IPv6
#     multicast and of IEEE 802.1 (01:00:5e, 33:33, 01:80:c2).
#   - UUIDs (partitions, the graphics card), and the serial numbers of FAT and NTFS volumes where a UUID
#     goes; the Bluetooth base UUIDs stay (they name profiles, not devices).
#   - Serial numbers that hardware reports (displays, graphics cards, USB devices), the GPU PDI of NVIDIA,
#     and in an EDID that is printed as rows of hex, its serial number and serial string (see edid()).
#   - SSH fingerprints, public keys and the "randomart" of a key.
#   - Network names (SSID) as iwd, connman and wpa_supplicant write them.
#   - Names of computers of the home network in addresses (no dot, or .local, .lan, .home and the like),
#     and the name of this system (HOST) wherever it stands as a word.
#   - The names of the Bluetooth devices that BlueZ keeps (NAMES), [bt-name-...]: macOS names the keyboard of
#     a Mac "Magic Keyboard de Juan", and the kernel, systemd-logind and Kodi write that name in their logs.
#     Each is found by its bytes, never as a regular expression, as a word of its own, the longest first. One
#     that would take ordinary words of the logs with it (shorter than 6 bytes, or one word of the letters A to
#     Z alone, such as "TV", "K" or "Keyboard"), or that a marker could hold, is taken only where a line names
#     an input device (btctx()).
#   - Each name in a path of a drive, of a media folder of /storage and of a share of the network, and the
#     labels of drives: [volume-...], [share-...] and [name-...], with the extension of a file kept. A drive
#     is often named after its owner, and the folder of a film after the film (mpaths()).
# What stays, because it says what the machine is and not whose: makes and models, the names that devices
# and drivers give themselves, public server names, ports, prefix lengths, and the time zone.
# Variables: KEYFILE (its first line is the secret; without it a marker has no second part), TMP (a file
# for the hashing, that nobody else writes), HOST (this system's network name), NAMES (a file with the names
# of the Bluetooth devices, one a line, as MODE "btlist" prints them), MODE ("edid": the input is an EDID as
# `od -An -v -tx1` prints it, and what comes out is that EDID without its serial numbers, as octal escapes
# for printf; "btlist": the input is files of BlueZ, and what comes out is the names in them, one a line;
# "btmark": the same, as "Name: <marker>" and "Alias: <marker>").
# The time stays linear in the length of a line: each rule looks for its next match after the last one.
# A line of 32767 characters or more is left out, as redact.sed leaves it out (and says so in its place).

BEGIN {
	KEY = ""
	if (KEYFILE != "" && (getline KEY < KEYFILE) <= 0) KEY = ""
	if (KEYFILE != "") close(KEYFILE)
	if (KEY !~ /^[0-9a-f]+$/ || TMP == "") KEY = ""
	if (HOST !~ /^[A-Za-z0-9][A-Za-z0-9.-]*$/ || tolower(HOST) == "libreelec" || tolower(HOST) == "localhost" || length(HOST) < 2) HOST = ""
	for (i = 0; i < 256; i++) HX[sprintf("%02x", i)] = i
	for (i = 1; i < 256; i++) ORD[sprintf("%c", i)] = i
	split("82 69 68 65 67 84 69 68", RED, " ")
	# (public resolvers: which DNS is set is worth knowing, and it says nothing of anybody)
	split("1.1.1.1 1.0.0.1 8.8.8.8 8.8.4.4 9.9.9.9 149.112.112.112 208.67.222.222 208.67.220.220", P4, " ")
	for (i in P4) KEEP4[P4[i]] = 1
	NROWS = 0; NB = 0; ND = 0
	# (the names in paths that are kept as they are: those that the structure of a Blu-ray, a DVD or an HD DVD
	# gives its folders and files, and the folders of /storage that the system makes and names)
	split("bdmv playlist stream clipinf jar backup bdjo auxdata meta ssif tn dl certificate aacs bdsvm duplicate makemkv video_ts audio_ts hvdvd_ts adv_obj", T, " ")
	for (i in T) DISCDIR[T[i]] = 1
	split("index.bdmv movieobject.bdmv id.bdmv sound.bdmv app.discroot.crt bu.discroot.crt unit_key_ro.inf mkb_ro.inf contentrevocation.lst", T, " ")
	for (i in T) DISCFILE[T[i]] = 1
	split("videos music pictures tvshows movies downloads recordings emulators roms picons backup screenshots", T, " ")
	for (i in T) STOREDIR[T[i]] = 1
	split("logfiles lost+found", T, " ")
	for (i in T) STOREKEEP[T[i]] = 1
	# (the labels of the partitions of LibreELEC and the ones that Windows and its tools give: no one's name)
	split("libreelec|storage|system|efi|esp|boot|recovery|winre_drv|windows re|efi system partition|basic data partition|microsoft reserved partition", T, "|")
	for (i in T) LABELKEEP[T[i]] = 1
	# (the extensions that a name keeps, those of subtitles, and the languages before one of them; the classes and
	# words of the markers, see ismarker(). Looked up in arrays: busybox awk compiles a regular expression that is
	# held in a string each time it is used, 150 us for one this long)
	split("mkv mk3d mka mp4 m4v m4a mov avi wmv asf flv webm ts m2ts mts m2t tp trp mpg mpeg vob ifo bup iso img bdmv mpls clpi bdjo jar crt inf cer tbl lst evo divx ogv ogm ogg rmvb 3gp wtv strm mp3 flac aac ac3 eac3 dts thd opus wav wma ape wv dsf dff srt ass ssa sub idx sup vtt smi jpg jpeg png gif bmp tbn webp nfo cue m3u m3u8 pls xsp zip rar 7z xml txt bin properties otf ttf fnt json log tar gz xz zst", T, " ")
	for (i in T) EXT[T[i]] = 1
	split("srt ass ssa sub idx sup vtt smi", T, " ")
	for (i in T) SUBX[T[i]] = 1
	split("en eng es spa fr fre fra de ger deu it ita pt por pb nl dut nld sv swe no nor nb nob da dan fi fin pl pol ru rus ja jpn zh chi zho ko kor ar ara he heb hu hun cs cze ces el gre ell tr tur ro rum ron hi hin th tha vi vie id ind ms may msa uk ukr bg bul hr hrv sr srp sk slo slk sl slv et est lv lav lt lit fa per fas ca cat eu baq eus gl glg is ice isl und forced sdh cc default", T, " ")
	for (i in T) LANG[T[i]] = 1
	split("lan-ip linklocal-ip cgnat-ip ip ip6 ula-ip6 linklocal-ip6 mac uuid serial ssid ssh-key lan-name bt-name volume share name", T, " ")
	for (i in T) MKCLS[T[i]] = 1
	split("hostname randomart redacted redacted-long redacted-line email", T, " ")
	for (i in T) MKWORD[T[i]] = 1
	# (the digests: made in batches where markers can wait for them, and kept across the files of one run)
	DEFER = (MODE == "log" || MODE == ""); NQ = 0; NBUF = 0; PHN = 0; NDP = 0
	if (DGFILE != "" && KEY != "") dgload()
	# (the names of Bluetooth devices: 256 at most, of which 64 are looked for in every line)
	NBC = 0; NBS = 0; NBW = 0
	if (NAMES != "") {
		while (NBC < 256 && (getline v < NAMES) > 0) btadd(v)
		close(NAMES)
		btsort()
	}
}

# ---- markers
# The first 8 hex digits of SHA-256 of the secret, the class and the value; "" without the secret. They are made
# in batches, not with a process each: in a log (MODE "log") or a file of the system (MODE ""), a value seen for
# the first time gets a placeholder, "\034<number>:", the lines that come out are held (emit()), and once 256
# values wait, or 4096 lines, one sha256sum hashes them all and the digests take the places of the placeholders
# (flushq()). The digests made are kept for the other files of the same run of redact.sh (DGFILE). Two budgets,
# each of 20000 new values a run of awk: the names in paths (volume, share, name), and all the rest; past it, a
# marker has no second part. In the modes that print a marker at once (btmark), a value is hashed by itself.
function digest(cls, v,   k, p) {
	k = cls SUBSEP v
	if (k in DG) return DG[k]
	if (KEY == "") return ""
	p = (cls == "volume" || cls == "share" || cls == "name")
	if (p ? NDP >= 20000 : ND >= 20000) return ""
	if (p) NDP++; else ND++
	NDG++
	if (!DEFER) return DG[k] = hashnow(cls, v)
	PHN++; NQ++
	QI[NQ] = PHN; PK[PHN] = k
	printf "%s\n%s\n%s\n", KEY, cls, v > (TMP "." NQ)
	close(TMP "." NQ)
	return DG[k] = "\034" PHN ":"
}
function hashnow(cls, v,   c, h) {
	printf "%s\n%s\n%s\n", KEY, cls, v > TMP
	close(TMP)
	c = "sha256sum " TMP
	h = ""
	c | getline h
	close(c)
	h = substr(h, 1, 8)
	return (h ~ /^[0-9a-f]{8}$/) ? h : ""
}
# The first n digits of a digest, or the place of them while it waits: "\034<number>:<n>\034"
function part(h, n) { return (substr(h, 1, 1) == "\034") ? h n "\034" : substr(h, 1, n) }
function mark(cls, v, n,   h) {
	h = digest(cls, v)
	return "[" cls (h == "" ? "" : "-" part(h, n)) "]"
}
# An address: [class-NNNN.HHHH], NNNN the same for its whole network, HHHH its own.
function pair(cls, net, a,   h1, h2) {
	h1 = digest(cls "/net", net)
	h2 = digest(cls, a)
	if (h1 == "" || h2 == "") return "[" cls "]"
	return "[" cls "-" part(h1, 4) "." part(h2, 4) "]"
}
# The values that wait, hashed by one sha256sum (a file each, TMP.1 to TMP.NQ), and the held lines, with the
# digests in the places of their placeholders, printed. A digest that could not be made is "x" (no hex digit:
# it cannot be taken for one).
function flushq(   i, c, line, f, id, files, nf) {
	if (NQ) {
		files = ""
		for (i = 1; i <= NQ; i++) files = files " " TMP "." i
		c = "sha256sum" files
		while ((c | getline line) > 0) {
			f = line; sub(/^[0-9a-f]+  /, "", f)
			i = substr(f, length(TMP) + 2) + 0
			if (substr(f, 1, length(TMP) + 1) == TMP "." && i >= 1 && i <= NQ && line ~ /^[0-9a-f]{64}  /) {
				id = QI[i]; PH[id] = substr(line, 1, 8); DG[PK[id]] = PH[id]; NEW[PK[id]] = 1
			}
		}
		close(c)
		nf = 0
		for (i = 1; i <= NQ; i++) if (!(QI[i] in PH)) { PH[QI[i]] = "failedfa"; DG[PK[QI[i]]] = PH[QI[i]]; nf++ }
		# (the lines of this batch say "failed" where a digest was to be: one line before them says why. It is the
		# only line that the filter adds, and shutdown.sh, which counts the lines of the filter, knows it)
		if (nf) { out1("[the filter could not make " nf " digests: sha256sum gave none for them; their markers say failed]"); NFAIL += nf }
		NQ = 0
	}
	for (i = 1; i <= NBUF; i++) out1(index(BUF[i], "\034") ? resolve(BUF[i]) : BUF[i])
	NBUF = 0; HBY = 0
}
function resolve(s,   out, t, j) {
	out = ""
	while (match(s, /\034[0-9]+:[0-9]+\034/)) {
		t = substr(s, RSTART + 1, RLENGTH - 2); j = index(t, ":")
		out = out substr(s, 1, RSTART - 1) substr(PH[substr(t, 1, j - 1)], 1, substr(t, j + 1) + 0)
		s = substr(s, RSTART + RLENGTH)
	}
	return out s
}
# What the files before this one in the same run of redact.sh worked out, and what this one adds for those after
# it, a line each: "D<class and value>\035<digest>" and "P<key of PNC>\035<the result of the name>" (a key with a
# line end or a byte 035 in it is not kept; the file is in the folder of the run, readable by root only, in RAM)
function dgload(   line, i, k) {
	while ((getline line < DGFILE) > 0) {
		if (!(i = index(line, "\035"))) continue
		k = substr(line, 2, i - 2)
		if (substr(line, 1, 1) == "D") { if (substr(line, i + 1) ~ /^[0-9a-f]{8}$/) DG[k] = substr(line, i + 1) }
		else if (substr(line, 1, 1) == "P" && NPNC < 50000) { NPNC++; PNC[k] = substr(line, i + 1) }
	}
	close(DGFILE)
}
function dgsave(   k, r) {
	for (k in NEW) if (DG[k] ~ /^[0-9a-f]{8}$/ && !index(k, "\n") && !index(k, "\035")) print "D" k "\035" DG[k] >> DGFILE
	for (k in NEWP) {
		r = index(PNC[k], "\034") ? resolve(PNC[k]) : PNC[k]
		if (!index(k, "\n") && !index(k, "\035") && !index(r, "\n") && r !~ /-fail(ed)?[].]/) print "P" k "\035" r >> DGFILE
	}
	close(DGFILE)
}
# Each rule looks at a line again from where its last match ended, and each look costs the length of what
# is left: a line with thousands of matches would cost their number times its length. After 1024 matches in
# one line, the line is left out (and says so in its place): no log line of a system has that many
# addresses, and the time stays within the length of the line times 1024.
function more() {
	if (++NM <= 1024) return 1
	TOOMANY = 1
	return 0
}
# The names in the paths of a line count apart, 4096 at most, more than the 1024 matches of more(): a path of
# thousands of names ("/var/media/USB/a b/a b/...") was marked to the last of them, 8 such lines in 7 s, to be left
# out after for its length; a line with a few hundred paths of a few names each stays as it was. Past them the line
# is left out, and says so in its place.
function mname() {
	if (++NMN <= 4096) return 1
	TOOMANY = 2
	return 0
}
# Whether v is a marker of this filter (or a redaction of redact.sed), in the very form that they write it: it is
# not marked again. A class of theirs with a digest of 6 hex digits, or of 4 and 4, or with none (or the
# placeholder of a digest), and the words that redact.sed and the rules write. A name of a folder such as
# "[juan]" or "[4k]" is no marker.
function ismarker(v,   t) {
	if (substr(v, 1, 1) != "[" || v !~ /^\[[a-z0-9.:\034-]+\]$/) return 0
	t = substr(v, 2, length(v) - 2)
	if ((t in MKWORD) || (t in MKCLS)) return 1
	if (!match(t, /-[^-]*$/)) return 0
	return (substr(t, 1, RSTART - 1) in MKCLS) && substr(t, RSTART + 1) ~ /^([0-9a-f]{6}|failed|\034[0-9]+:6\034|([0-9a-f]{4}|fail|\034[0-9]+:4\034)\.([0-9a-f]{4}|fail|\034[0-9]+:4\034))$/
}
function lastc(s) { return substr(s, length(s), 1) }
function hexof(v,   i, r) {
	r = ""
	for (i = 1; i <= length(v); i++) r = r sprintf("%02x", ORD[substr(v, i, 1)])
	return r
}

# ---- IPv4
function ip4(s,   out, m, prv, nxt, n, P, i, ok) {
	if (s !~ /[0-9]\.[0-9]+\.[0-9]+\.[0-9]/) return s
	out = ""
	while (more() && match(s, /[0-9]+(\.[0-9]+)+/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		prv = lastc(out); nxt = substr(s, 1, 1)
		# (a version: "v1.2.3.4", "alsa-lib-1.2.15.1", or five parts)
		if (prv ~ /[A-Za-z0-9_.-]/ || nxt ~ /[A-Za-z_]/ || split(m, P, "[.]") != 4) { out = out m; continue }
		ok = 1
		for (i = 1; i <= 4; i++) if (length(P[i]) > 3 || P[i] + 0 > 255) ok = 0
		out = out (ok ? ip4mark(m, P) : m)
	}
	return out s
}
function ip4mark(a, P,   x, y, c) {
	x = P[1] + 0; y = P[2] + 0
	if (x == 0 || x == 127 || x >= 224 || (a in KEEP4)) return a
	if (x == 10 || (x == 172 && y >= 16 && y <= 31) || (x == 192 && y == 168)) c = "lan-ip"
	else if (x == 169 && y == 254) c = "linklocal-ip"
	else if (x == 100 && y >= 64 && y <= 127) c = "cgnat-ip"
	else c = "ip"
	return pair(c, (P[1] + 0) "." (P[2] + 0) "." (P[3] + 0), (P[1] + 0) "." (P[2] + 0) "." (P[3] + 0) "." (P[4] + 0))
}

# ---- IPv6
function ip6(s,   out, m, prv, nxt) {
	if (s !~ /::|[0-9A-Fa-f]:[0-9A-Fa-f]+:[0-9A-Fa-f]+:[0-9A-Fa-f]+:[0-9A-Fa-f]+:[0-9A-Fa-f]+:[0-9A-Fa-f]+:[0-9A-Fa-f]/) return s
	out = ""
	while (more() && match(s, /[0-9A-Fa-f]*:[0-9A-Fa-f:.]*:[0-9A-Fa-f:.]*(%[A-Za-z0-9_.-]+)?/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		prv = lastc(out); nxt = substr(s, 1, 1)
		# (not a word that ends in "::" or goes on after the address: C++ names, labels)
		if (prv ~ /[A-Za-z0-9_]/ || nxt ~ /[A-Za-z0-9_]/) out = out m
		else out = out ip6tok(m)
	}
	return out s
}
# An address as netstat or ip prints it: with a zone after "%", or a port after the last colon.
function ip6tok(t,   z, i, r, a, p) {
	z = ""
	if ((i = index(t, "%")) > 0) { z = substr(t, i); t = substr(t, 1, i - 1) }
	if (substr(t, 1, 3) == ":::") return t z
	# (netstat writes the port after the address with a colon: there, a last group of digits is the port
	# where what is before it is an address; elsewhere, it is a part of the address where that makes one)
	if (NETSTAT && match(t, /:[0-9]+$/)) {
		a = substr(t, 1, RSTART - 1); p = substr(t, RSTART)
		r = ip6val(a)
		if (r != "") return r p z
	}
	r = ip6val(t)
	if (r != "") return r z
	if (match(t, /:[0-9]+$/)) {
		a = substr(t, 1, RSTART - 1); p = substr(t, RSTART)
		r = ip6val(a)
		if (r != "") return r p z
	}
	return t z
}
# The marker of an IPv6 address, the address itself where it stays, or "" when t is none.
function ip6val(t,   n, G, i, g, dbl, full, E, k, f, v4, P, z) {
	if (t ~ /:::/ || t ~ /^:[^:]/ || t ~ /[^:]:$/) return ""
	if (index(t, ".")) {
		if (tolower(substr(t, 1, 7)) != "::ffff:") return ""
		v4 = substr(t, 8)
		if (v4 !~ /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ || split(v4, P, "[.]") != 4) return ""
		for (i = 1; i <= 4; i++) if (length(P[i]) > 3 || P[i] + 0 > 255) return ""
		return substr(t, 1, 7) ip4mark(v4, P)
	}
	dbl = 0
	for (i = 1; i < length(t); i++) if (substr(t, i, 2) == "::") dbl++
	if (dbl > 1) return ""
	n = split(t, G, ":")
	g = 0
	for (i = 1; i <= n; i++) {
		if (G[i] == "") continue
		if (G[i] !~ /^[0-9A-Fa-f]+$/ || length(G[i]) > 4) return ""
		g++
	}
	if (dbl == 0 && (n != 8 || g != 8)) return ""
	if (dbl == 1 && g > 7) return ""
	# the eight groups, as numbers ("::" stands for as many zero groups as are missing)
	k = 0; z = 0
	for (i = 1; i <= n; i++) {
		if (G[i] == "") {
			if (!z) for (f = 0; f < 8 - g; f++) E[++k] = 0
			z = 1
		} else E[++k] = ("0x" G[i]) + 0
	}
	if (k != 8) return ""
	full = ""
	for (i = 1; i <= 8; i++) full = full (i > 1 ? ":" : "") sprintf("%x", E[i])
	# (unspecified, loopback, multicast, and fe80:: itself: the prefix of every link-local address)
	if (full == "0:0:0:0:0:0:0:0" || full == "0:0:0:0:0:0:0:1" || E[1] >= 65280 || full == "fe80:0:0:0:0:0:0:0") return t
	if (E[1] >= 65152 && E[1] <= 65215) return mark("linklocal-ip6", full, 6)
	return pair((E[1] >= 64512 && E[1] <= 65023) ? "ula-ip6" : "ip6", sprintf("%x:%x:%x:%x", E[1], E[2], E[3], E[4]), full)
}

# ---- MAC and Bluetooth addresses: six groups of two hex digits, not a part of a longer such run
function macs(s,   out, m, prv, left, right, n, w, j) {
	if (s !~ /[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:/) return s
	out = ""
	# (a run of groups of two hex digits, as long as it goes to the right)
	while (more() && match(s, /[0-9A-Fa-f][0-9A-Fa-f](:[0-9A-Fa-f][0-9A-Fa-f])+/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		prv = lastc(out)
		left = 0
		if (prv ~ /[A-Za-z0-9_]/) {
			# its first two digits end a word, as in "mac:aa:bb:...": they stay
			out = out substr(m, 1, 3)
			m = substr(m, 4)
		} else if (prv == ":") {
			# a group of hex digits before it that the run did not take: an IPv6 address
			# ("2001:db8:aa:bb:cc:dd:ee:ff"), a key, a dump. A word before it, as in "bssid:aa:...", has a
			# letter that is no hex digit.
			w = ""
			for (j = length(out) - 1; j > 0 && length(w) < 6 && substr(out, j, 1) ~ /[0-9A-Za-z_]/; j--) w = substr(out, j, 1) w
			left = w ~ /^[0-9A-Fa-f]+$/ && length(w) <= 4
		}
		right = substr(s, 1, 1) ~ /[0-9A-Fa-f]/ || (substr(s, 1, 1) == ":" && substr(s, 2, 1) ~ /[0-9A-Fa-f]/)
		n = (length(m) + 1) / 3
		out = out ((n == 6 && !left && !right) ? macmark(m) : m)
	}
	return out s
}
function macmark(m,   l) {
	l = tolower(m)
	if (l == "00:00:00:00:00:00" || l == "ff:ff:ff:ff:ff:ff") return m
	# (the group addresses of IPv4 and IPv6 multicast and of IEEE 802.1 bridges: of a multicast, not of a device.
	# Not every address with the group bit: a Bluetooth address is written with its most significant byte
	# first, and half of the random addresses of Bluetooth LE devices would look like a group address)
	if (l ~ /^(01:00:5e|33:33|01:80:c2):/) return m
	return mark("mac", l, 6)
}
function colons(h,   i, r) {
	r = ""
	for (i = 1; i <= 11; i += 2) r = r (i > 1 ? ":" : "") substr(h, i, 2)
	return r
}

# ---- BlueZ's names of its devices (on D-Bus, and in its log and in the log of the settings add-on), the
# address with "_" for ":" (/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF): the marker of the address
function bluez(s,   out, m) {
	if (s !~ /dev_[0-9A-Fa-f][0-9A-Fa-f]_[0-9A-Fa-f][0-9A-Fa-f]_/) return s
	out = ""
	while (more() && match(s, /dev_[0-9A-Fa-f][0-9A-Fa-f](_[0-9A-Fa-f][0-9A-Fa-f])+/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		# (six groups, not the end of a longer word nor a part of a longer run of hex digits)
		if (lastc(out) ~ /[A-Za-z0-9]/ || length(m) != 21 || substr(s, 1, 1) ~ /[0-9A-Fa-f]/) { out = out m; continue }
		m = substr(m, 5)
		gsub(/_/, ":", m)
		out = out "dev_" macmark(m)
	}
	return out s
}

# ---- UUIDs
function uuids(s,   out, m, prv, nxt, l) {
	if (s !~ /[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]-/) return s
	out = ""
	while (more() && match(s, /[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		prv = lastc(out); nxt = substr(s, 1, 1)
		l = tolower(m)
		if (prv ~ /[0-9A-Fa-f]/ || nxt ~ /[0-9A-Fa-f-]/ || l == "00000000-0000-0000-0000-000000000000" || l ~ /-0000-1000-8000-00805f9b34fb$/) out = out m
		else out = out mark("uuid", l, 6)
	}
	return out s
}
# After UUID=, PARTUUID= and by-uuid/ (the kernel's command line, the init, mounts): what is not marked yet,
# as the serial number of a FAT volume ("1234-ABCD"), of an NTFS volume (16 hex digits) or the PARTUUID of
# an MBR partition ("1234abcd-01").
function uuidvals(s,   out, m, i, v) {
	out = ""
	while (more() && match(s, /([Pp][Aa][Rr][Tt])?[Uu][Uu][Ii][Dd][=\/][0-9A-Fa-f][0-9A-Fa-f-]*/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		i = match(m, /[=\/]/)
		v = substr(m, i + 1)
		if (substr(s, 1, 1) ~ /[A-Za-z0-9_]/ || length(v) < 8) out = out m
		else out = out substr(m, 1, i) mark("uuid", tolower(v), 6)
	}
	return out s
}
# The same in the names of systemd units, where "-" is written "\x2d": dev-disk-by\x2duuid-1234\x2dABCD.device
function escuuids(s,   out, m, i, v) {
	out = ""
	while (more() && match(s, /by\\x2d(part)?uuid-[0-9A-Fa-f]+(\\x2d[0-9A-Fa-f]+)*/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		i = index(m, "uuid-") + 4
		v = substr(m, i + 1)
		gsub(/\\x2d/, "-", v)
		out = out substr(m, 1, i) mark("uuid", tolower(v), 6)
	}
	return out s
}
# lsblk -f: the serial number of a FAT or NTFS volume is a column of its own
function lsblk(s,   out, m) {
	out = ""
	while (more() && match(s, / [0-9A-F][0-9A-F][0-9A-F][0-9A-F]-[0-9A-F][0-9A-F][0-9A-F][0-9A-F] | [0-9A-F]{16} /)) {
		out = out substr(s, 1, RSTART)
		m = substr(s, RSTART + 1, RLENGTH - 2)
		s = substr(s, RSTART + RLENGTH - 1)
		out = out mark("uuid", tolower(m), 6)
	}
	return out s
}

# ---- serial numbers that hardware reports
function serials(s,   l, out, k, v, r) {
	l = tolower(s); out = ""
	while (more() && match(l, /(serial[ _-]?(number|num|no\.?)|id_serial(_short)?|gpu pdi)[ \t]*[:=][ \t]*/)) {
		k = RSTART + RLENGTH
		out = out substr(s, 1, k - 1)
		s = substr(s, k); l = substr(l, k)
		v = ""
		if (match(s, /^\[[a-z0-9-]+(-[0-9a-f]+(\.[0-9a-f]+)?)?\]/) || match(s, /^[^] \t,;)]+/)) v = substr(s, 1, RLENGTH)
		r = tolower(v)
		if (v == "" || ismarker(v) || r ~ /^(n\/a|\[n\/a\]|none|null|\(null\)|unknown|not|0x0+|0+)$/) out = out v
		else out = out mark("serial", v, 6)
		s = substr(s, length(v) + 1); l = substr(l, length(v) + 1)
	}
	return out s
}

# ---- connman's names of its services: wifi_<MAC>_<SSID in hex>_managed_psk, ethernet_<MAC>_cable,
# bluetooth_<MAC>_<MAC>
function connman(s,   out, m, n, F, i, r) {
	if (s !~ /(wifi|ethernet|bluetooth)_[0-9A-Fa-f]{12}/) return s
	out = ""
	while (more() && match(s, /(wifi|ethernet|bluetooth)_[0-9A-Fa-f]{12}(_[0-9A-Za-z]+)*/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		if (lastc(out) ~ /[A-Za-z0-9]/) { out = out m; continue }
		n = split(m, F, "_")
		r = F[1] "_" macmark(colons(tolower(F[2])))
		for (i = 3; i <= n; i++) {
			if (F[1] == "wifi" && i == 3 && F[i] != "hidden" && F[i] ~ /^[0-9A-Fa-f]+$/ && length(F[i]) % 2 == 0) r = r "_" mark("ssid", tolower(F[i]), 6)
			else if (F[i] ~ /^[0-9A-Fa-f]+$/ && length(F[i]) == 12) r = r "_" macmark(colons(tolower(F[i])))
			else r = r "_" F[i]
		}
		out = out r
	}
	return out s
}
# ---- network names (SSID): iwd's "ssid: <name>, bss: ...", and "SSID 'name'" or "SSID \"name\"". The
# marker of a name is the one of its hex, which is what connman's names of its services hold.
function ssids(s,   i, j, rest, v, k, q) {
	if (match(s, /(^|[^A-Za-z])ssid: /)) {
		i = RSTART + RLENGTH - 6
		rest = substr(s, i + 6)
		k = 0
		while (more() && (j = index(substr(rest, k + 1), ", bss: ")) > 0) k += j
		v = (k > 0) ? substr(rest, 1, k - 1) : rest
		if (v != "" && !ismarker(v)) s = substr(s, 1, i + 5) mark("ssid", hexof(v), 6) substr(rest, length(v) + 1)
	}
	if (match(s, /SSID ['"]/)) {
		q = substr(s, RSTART + 5, 1)
		rest = substr(s, RSTART + 6)
		j = index(rest, q)
		if (j > 1 && !ismarker(substr(rest, 1, j - 1))) s = substr(s, 1, RSTART + 5) mark("ssid", hexof(substr(rest, 1, j - 1)), 6) substr(rest, j)
	}
	return s
}

# ---- SSH: fingerprints, public keys, and the picture of a key ("randomart")
function ssh(s,   out, m, i) {
	out = ""
	while (more() && match(s, /(SHA256:[A-Za-z0-9+\/]{43}[A-Za-z0-9+\/=]*|MD5(:[0-9a-f][0-9a-f]){16})/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		i = index(m, ":")
		out = out substr(m, 1, i) mark("ssh-key", m, 6)
	}
	s = out s; out = ""
	while (more() && match(s, /(ssh-(rsa|ed25519|dss)|ecdsa-sha2-nistp[0-9]+|sk-ssh-ed25519@openssh\.com|sk-ecdsa-sha2-nistp256@openssh\.com) AAAA[A-Za-z0-9+\/]+=*/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		i = index(m, " ")
		out = out substr(m, 1, i) mark("ssh-key", substr(m, i + 1), 6)
	}
	s = out s
	if (match(s, /\|[ .o+=*BOX@%&#\/^SE]{17}\|[ \t]*$/)) s = substr(s, 1, RSTART - 1) "[randomart]"
	return s
}

# ---- names of computers of the home network in addresses, and the name of this system
function lannames(s,   out, m, h, i) {
	out = ""
	while (more() && match(s, /(smb|nfs|ftps?|sftp|https?|davs?|webdavs?|rtsp|rtmp[a-z]*|mms[a-z]*|ssh|afp|udp|rtp):(\\?\/)(\\?\/)([^\/@ "'<>\\]*@)?[A-Za-z0-9][A-Za-z0-9_-]*(\.[A-Za-z0-9_-]+)*/)) {
		out = out substr(s, 1, RSTART - 1)
		m = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		# the name: after the slashes, and after the "user@" of an address that has one
		i = match(m, /\/[^\/]*$/)
		h = substr(m, i + 1)
		if ((i = index(h, "@")) > 0) h = substr(h, i + 1)
		# (not the user of an address whose host is a marker already: "smb://user:pw@[lan-ip-...]")
		if (lanname(h) && substr(s, 1, 1) != "@" && s !~ /^:[^\/ ]*@/) out = out substr(m, 1, length(m) - length(h)) mark("lan-name", tolower(h), 6)
		else out = out m
	}
	return out s
}
function lanname(h,   l) {
	l = tolower(h)
	if (l == "localhost") return 0
	if (index(l, ".") == 0) return 1
	return l ~ /\.(local|lan|home|home\.arpa|localdomain|internal|intranet|private|fritz\.box)$/
}
# The name of this system where a line names it as such: the field of the journal and of syslog that holds
# it, "Linux NAME ..." of uname, after "hostname", "host name is", "hostname set to <", "netbios name =",
# "@" or "//", and before ".local", ".lan" and the like. Not as any word of a line: a name such as "kodi",
# "media" or "tv" would take every ".kodi/", "/var/media" or "LG TV" with it.
function host(s,   out, i, L, pre, nxt) {
	L = length(HOST)
	if (match(s, /^[A-Z][a-z][a-z] [ 0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9] /) && substr(s, RLENGTH + 1, L + 1) == HOST " ")
		s = substr(s, 1, RLENGTH) "[hostname]" substr(s, RLENGTH + L + 1)
	if (substr(s, 1, L + 7) == "Linux " HOST " ") s = "Linux [hostname]" substr(s, L + 7)
	out = ""
	while (more() && (i = index(s, HOST)) > 0) {
		pre = out substr(s, 1, i - 1)
		nxt = substr(s, i + L, 1)
		if (nxt !~ /[A-Za-z0-9_-]/ && (pre ~ /([Hh]ost ?name[:=]? ?|[Hh]ost name is |[Hh]ostname set to <|[Nn]etbios name = |@|\/\/)$/ ||
		    (substr(pre, length(pre), 1) !~ /[A-Za-z0-9_.-]/ && substr(s, i + L) ~ /^\.(local|lan|home|localdomain|home\.arpa|internal)([^A-Za-z0-9-]|$)/)))
			out = pre "[hostname]"
		else out = pre HOST
		s = substr(s, i + L)
	}
	return out s
}

# ---- in a log (MODE "log"): an address or a path of media whose name has spaces in it, up to the last name
# of a media file in its part of the line (between quotes or angle brackets), with that name's extension:
# "nfs://[lan-ip-...]/Movies/A Film (2001)/A Film.mkv" becomes "nfs:{//}[lan-ip-...]/[redacted].mkv". The
# braces keep redact.sed, which runs next, from taking it again as an address (it would cut the extension
# off), and redact.sed puts the slashes back. Of each part of the line, the first address or path before its
# last name of a media file is taken, and nothing else: the time stays linear in the length of the line,
# which it would not be with a rule of sed that looked for a name of a media file after each address. What
# has no such name after it is left to redact.sed, which cuts an address at the first space (at the end of
# the line for one with a query).
function paths(s,   out, p) {
	if (s !~ /:(\\?\/)(\\?\/)|\/(var\/media|media|mnt|run\/media|storage)\//) return s
	out = ""
	while (more() && match(s, /["'<>]/)) {
		# (pathpart() matches too: where this match was is kept first)
		p = RSTART
		out = out pathpart(substr(s, 1, p - 1)) substr(s, p, 1)
		s = substr(s, p + 1)
	}
	return out pathpart(s)
}
function pathpart(g,   l, x, e, c, u, k, t, h, rest) {
	l = tolower(g)
	# the last name of a media file: its extension at x, and its end at e
	x = 0; e = 0; k = 0
	while (more() && match(substr(l, k + 1), /\.(mkv|mk3d|mka|mp4|m4v|m4a|mov|avi|wmv|asf|flv|webm|ts|m2ts|mts|m2t|tp|trp|mpg|mpeg|vob|ifo|iso|img|bdmv|mpls|evo|divx|ogv|ogm|ogg|rmvb|3gp|wtv|strm|mp3|flac|aac|ac3|eac3|dts|thd|opus|wav|wma|ape|wv|dsf|dff|srt|ass|ssa|sub|idx|sup|vtt|smi|jpg|jpeg|png|gif|bmp|tbn|webp|nfo|cue|m3u|m3u8|pls|xsp|zip|rar|7z)([^a-z0-9]|$)/)) {
		x = k + RSTART; e = k + RSTART + RLENGTH - 1
		if (substr(l, e, 1) !~ /[a-z0-9]/) e--
		k = e
	}
	if (!x) return g
	# the first address before it, or the first path of media
	c = 0; u = 0
	if (match(l, /(https?|rtmp[a-z]*|rtsp[a-z]*|mms[a-z]*|smb|nfs|s?ftps?|ssh|davs?|webdavs?|wss?|udp|rtp|upnp|plugin):(\\?\/)(\\?\/)/) && RSTART < x) { c = RSTART; u = 1 }
	if (match(l, /(^|[^a-z0-9_.-])\/(var\/media\/[^\/]+|media\/[^\/]+|mnt\/[^\/]+|run\/media\/[^\/]+\/[^\/]+|storage\/(videos|music|pictures|tvshows|movies|downloads|recordings|emulators|roms))\//) && RSTART < x && (!c || RSTART < c)) {
		t = RSTART + RLENGTH - 1
		# (where the path starts: after the character before it, unless it starts the line)
		c = (substr(l, RSTART, RLENGTH) ~ /^\/(var\/media|media|mnt|run\/media|storage)\//) ? RSTART : RSTART + 1
		u = 0
	}
	if (!c) return g
	# what follows the extension up to a space goes too (a query, a part after a dot)
	rest = substr(g, e + 1)
	match(rest, /^[^ ]*/)
	rest = substr(rest, RLENGTH + 1)
	if (u) {
		# the scheme and the server stay, the user and the password of an address go (as in redact.sed)
		h = substr(g, c)
		if (!match(h, /^[A-Za-z0-9+.-]+:(\\?\/)(\\?\/)([^\/@ \\]*@)?[^\/:? \\]+/)) return g
		k = index(h, ":")
		t = substr(h, k, RLENGTH - k + 1)
		sub(/^:(\\?\/)(\\?\/)([^\/@ \\]*@)?/, "", t)
		return substr(g, 1, c - 1) substr(h, 1, k - 1) ":{//}" t "/[redacted]." substr(g, x + 1, e - x) rest
	}
	return substr(g, 1, c - 1) "{/}" substr(g, c + 1, t - c) "[redacted]." substr(g, x + 1, e - x) rest
}

# ---- paths of media, in every mode: each name in a path of a drive (/var/media, /media, /mnt, /run/media),
# of a media folder of /storage (videos, music, ...; not the folders of the system, such as .kodi or
# logfiles) and of a share of the network (smb, nfs, ftp, sftp, dav, afp) becomes a marker: [volume-...] for the
# drive, [share-...] for the share, [name-...] for each folder and file below, with the extension of a file
# after it ("[name-1a2b3c].mkv", "[name-1a2b3c].eng.srt"). The same name gives the same marker wherever it
# stands: a folder and the file in it that is named after it, a path and the same path inside another address.
# Kept as they are: the names that the structure of a disc gives (BDMV, PLAYLIST, STREAM, CLIPINF, JAR, BACKUP,
# VIDEO_TS, ...) and its standard files (index.bdmv, VTS_01_1.VOB, ...), names of digits (00000, 00001.m2ts),
# "." and "..", and what is a marker already (a second pass changes nothing). The same in a path that is
# percent-encoded inside another address (bluray://%2fvar%2fmedia%2f...), in the names that systemd gives the
# mounts of drives (var-media-Juan\x27s\x20Drive.mount), and in the labels that blkid and udev print.
# A path ends at a double quote, an angle bracket, a tab, a "|" or the end of the line, or where another such
# path starts after a space or a quote; at a space only in a line of the table of mounts, or where the path
# writes a space as \040 before its first space. A path that starts after a "'" ("Skipping dir '/var/...' due
# to ...") ends at the "'" that closes it: the first one followed by a space, the end of the line or one of
# ),;:] after which the line goes on with no more of a path (no "/" and no name of a file before the next such
# "'", or another path), so that the "'" of "Ana's" or of "Rock 'n' Roll/" stays in it. Its last name ends
# after its last extension of a known kind that is followed by none of a letter, a digit, ".", "-" or "_"
# ("... a.mkv took 3 ms": " took 3 ms" stays; "Film.Sub.Name" has no such end, and is one name); without one,
# the rest of the path is that name. In a log (MODE "log"), the path is written with "{/}" or ":{//}" where its
# first "/" or "://" goes: the rules of redact.sed for addresses and paths do not take it again (they would cut
# it down), and redact.sed puts the slashes back.
# (Each part looked for first with index(), which costs next to nothing: a regular expression is run only on a
# line that has what it looks for. The result of a name, its marker or itself, is kept in PNC by the name, its
# class and how it is written, for the other lines and the other files of the run.)
function mpaths(s,   loc, net) {
	loc = index(s, "/media/") || index(s, "/mnt/") || index(s, "/storage/")
	net = index(s, "://") && (index(s, "smb://") || index(s, "nfs://") || index(s, "ftp") || index(s, "dav") || index(s, "afp://"))
	if (loc || net) {
		MNT = s ~ /^[^ ]+ \/[^ ]* [^ ]+ [^ ]+ [0-9]+ [0-9]+$/ || s ~ /^[0-9]+ [0-9]+ [0-9]+:[0-9]+ /
		s = plainp(s)
	}
	if ((index(s, "%2") || index(s, "%252")) && s ~ /%(25)*2[Ff](var|media|mnt|run|storage)/) s = encp(s)
	if (index(s, "mount") && (index(s, "media-") || index(s, "mnt-"))) s = unitp(s)
	if (index(s, "LABEL") || index(s, "label")) s = labels(s)
	return s
}
# A name by its class c, as it is written n, and how that is meant to be read d ("" plain, "net" with percent
# escapes, "enc" percent escapes alone, "unit" the \x escapes of systemd): pname() once, then from PNC
function pnamec(n, c, d,   k, v, r) {
	k = d SUBSEP c SUBSEP n
	if (k in PNC) { NPC++; return PNC[k] }
	NPN++
	v = (d == "") ? poct(n) : (d == "net") ? pdec(poct(n)) : (d == "enc") ? pdec(n) : pxdec(n)
	r = pname(n, v, c)
	# (not a marker that has no digest, past the budget of the names: another file of the run may have one for it)
	if (NPNC < 50000 && r !~ /\[(volume|share|name)\]/) { NPNC++; NEWP[k] = 1; PNC[k] = r }
	return r
}
# Whether a name of a path is kept as it is (v: as it is meant, decoded)
function pkeep(n, v,   l, c) {
	if (n == "" || n == "." || n == ".." || pmarker(n)) return 1
	l = tolower(v)
	# (each expression only for a name that starts as it does)
	c = substr(l, 1, 1)
	return (l in DISCDIR) || (l in DISCFILE) || (c >= "0" && c <= "9" && l ~ /^[0-9]+(\.[a-z0-9]+)*$/) || (c == "v" && l ~ /^(video_ts|vts_[0-9]+_[0-9]+)\.(ifo|bup|vob)$/) || (c == "c" && l ~ /^(content|contenthash|contentcert)[0-9]+\.(cer|tbl)$/)
}
# Whether a name is a marker as pname() writes it (or redact.sed): an exact marker, then nothing, or an
# extension that pname() keeps
function pmarker(n,   i, t, x) {
	if (substr(n, 1, 1) != "[" || !(i = index(n, "]")) || !ismarker(substr(n, 1, i))) return 0
	t = substr(n, i + 1)
	if (t == "") return 1
	if (!match(t, /\.[A-Za-z0-9]+$/)) return 0
	x = tolower(substr(t, RSTART + 1)); t = substr(t, 1, RSTART - 1)
	return (x in EXT) && (t == "" || ((x in SUBX) && substr(t, 1, 1) == "." && (tolower(substr(t, 2)) in LANG)))
}
# A name of a path, n as it is written and v as it is meant: kept, or its marker of the class c with its
# extension after it (only one of a known kind; before that of a subtitle, also a language: "Film.eng.srt")
function pname(n, v, c,   x, e, b) {
	if (pkeep(n, v)) return n
	e = ""; b = v
	if (match(v, /\.[A-Za-z0-9]+$/) && RSTART > 1) {
		x = substr(v, RSTART + 1)
		if (tolower(x) in EXT) {
			e = "." x; b = substr(v, 1, RSTART - 1)
			if ((tolower(x) in SUBX) && match(b, /\.[A-Za-z]+$/) && RSTART > 1 && (tolower(substr(b, RSTART + 1)) in LANG)) {
				e = substr(b, RSTART) e; b = substr(b, 1, RSTART - 1)
			}
		}
	}
	return mark(c, b, 6) e
}
# What a name is meant to be: the octal escapes of the table of mounts (\040), and percent escapes ("%20",
# also "%2520" of an address inside an address)
function poct(v,   out) {
	if (!index(v, "\\")) return v
	out = ""
	while (match(v, /\\[0-3][0-7][0-7]/)) {
		out = out substr(v, 1, RSTART - 1) sprintf("%c", substr(v, RSTART + 1, 1) * 64 + substr(v, RSTART + 2, 1) * 8 + substr(v, RSTART + 3, 1))
		v = substr(v, RSTART + 4)
	}
	return out v
}
function pdec(v,   out) {
	if (!index(v, "%")) return v
	while (index(v, "%25")) gsub(/%25/, "%", v)
	out = ""
	while (match(v, /%[0-9A-Fa-f][0-9A-Fa-f]/)) {
		out = out substr(v, 1, RSTART - 1) sprintf("%c", HX[tolower(substr(v, RSTART + 1, 2))])
		v = substr(v, RSTART + 3)
	}
	return out v
}
function pxdec(v,   out) {
	if (!index(v, "\\x")) return v
	out = ""
	while (match(v, /\\x[0-9A-Fa-f][0-9A-Fa-f]/)) {
		out = out substr(v, 1, RSTART - 1) sprintf("%c", HX[tolower(substr(v, RSTART + 2, 2))])
		v = substr(v, RSTART + 4)
	}
	return out v
}
# The class of the name at depth i (1 the first after the root) of a path whose root is of kind k: "drive"
# (/var/media, /media, /mnt), "user" (/run/media: a user, then the drive), "net" (after the server) or
# "storage"; "" where the whole path stays (a folder of the system in /storage)
function pclass(k, i) {
	if (k == "drive") return i == 1 ? "volume" : "name"
	if (k == "user") return i == 2 ? "volume" : "name"
	if (k == "net") return i == 1 ? "share" : "name"
	return "name"
}
# Whether a path of /storage whose first name is v stays as it is: a folder of the system (hidden, or one of
# STOREKEEP)
function pstay(v,   l) {
	l = tolower(v)
	return substr(v, 1, 1) == "." || (l in STOREKEEP)
}
# The "'" that closes a path that starts after one, in t (the path and the rest of its part of the line): its
# position, or 0 where none could (see mpaths()). Of the "'" that are followed by a space, the end or one of
# ),;:] the first after which, up to the next such one, there is another path, or neither a "/" nor a name of a
# file, or a value between quotes of its own where the path before it ends in a name of a known kind (Kodi:
# "File '<path>' excluded. (Matches exclude rule RegExp: '<expression>')", "File matched:'<path>' (RE='...'"); where
# none is, the first of them. A name with "' " in it, "Rockin' 'Til Dawn.mkv", has no such end before its own. A
# value with a "/" that no "\" escapes (a path, as in an error of NFS: "error: 'NFS: Open of /export/<name>
# failed'") or that is the name of a file of a known kind stays with the path, whose names it may have: it is
# marked with them, and no name is let out.
function squote(t,   j, a, c, nc, CQ, seg, x, v) {
	nc = 0; j = 0
	while ((a = index(substr(t, j + 1), "'")) > 0) {
		j += a; c = substr(t, j + 1, 1)
		if (c == "" || index(" ),;:]", c)) CQ[++nc] = j
	}
	for (a = 1; a <= nc; a++) {
		seg = substr(t, CQ[a] + 1, (a < nc ? CQ[a + 1] : length(t) + 1) - CQ[a] - 1)
		if (match(seg, /[ (=:,]'[^ ]/)) {
			v = tolower(substr(seg, RSTART + 2))
			if (seg !~ /(^|[^\\])\// && !(match(v, /[^.]\.[a-z0-9]+$/) && (substr(v, RSTART + 2) in EXT)) && match(tolower(substr(t, 1, CQ[a] - 1)), /\.[a-z0-9]+$/) && (substr(tolower(t), RSTART + 1, RLENGTH - 1) in EXT)) return CQ[a]
		}
		if (seg ~ /\/(var\/media|media|mnt|run\/media|storage)\/|(smb|nfs|ftps?|sftp|davs?|webdavs?|afp):\/\//) return CQ[a]
		x = ""
		if (match(tolower(seg), /\.[a-z0-9]+$/)) x = substr(tolower(seg), RSTART + 1)
		if (!index(seg, "/") && !(x in EXT)) return CQ[a]
	}
	return nc ? CQ[1] : 0
}
# Paths with their slashes as they are
function plainp(s,   out, m, g, r, k, body, e, p, path, after, n, C, i, last, rem, x, q, ee, auth, res, l, o, sp) {
	out = ""
	while (more() && (index(s, "/media/") || index(s, "/mnt/") || index(s, "/storage/") || index(s, "://")) && match(s, /(^|[^A-Za-z0-9_.-])\/(var\/media|media|mnt|run\/media|storage)\/|(^|[^A-Za-z0-9+.-])(smb|nfs|ftps?|sftp|davs?|webdavs?|afp):\/\//)) {
		m = substr(s, RSTART, RLENGTH)
		body = substr(s, RSTART + RLENGTH)
		p = RSTART
		# (the character before it, where the match took one)
		g = match(m, /\/(var\/media|media|mnt|run\/media|storage)\/$|[A-Za-z]+:\/\/$/) - 1
		out = out substr(s, 1, p - 1 + g)
		r = substr(m, g + 1)
		l = tolower(r)
		k = (l ~ /:\/\/$/) ? "net" : (l == "/storage/") ? "storage" : (l == "/run/media/") ? "user" : "drive"
		# where it ends: a space ends it in a line of the table of mounts, and where it writes a space as \040
		o = index(body, "\\0") ? match(body, /\\0[0-7][0-7]/) : 0; sp = index(body, " ")
		e = length(body) + 1
		if (MNT || (o && (!sp || o < sp))) { if (match(body, /["<>\t| ]/)) e = RSTART }
		else if (match(body, /["<>\t|]/)) e = RSTART
		if ((index(body, "/media/") || index(body, "/mnt/") || index(body, "/storage/") || index(body, "://")) && match(body, /[ '"]\/(var\/media|media|mnt|run\/media|storage)\/|[ '"](smb|nfs|ftps?|sftp|davs?|webdavs?|afp):\/\//) && RSTART < e) e = RSTART
		if (g && substr(m, 1, 1) == "'" && (q = squote(substr(body, 1, e - 1)))) e = q
		path = substr(body, 1, e - 1); after = substr(body, e)
		res = ""
		# an address: its server stays (redact-ids.awk has marked it where it is of the home network), its user
		# and password go, and so does a query
		if (k == "net") {
			if ((q = index(path, "?")) > 0) { after = "?[redacted]" after; path = substr(path, 1, q - 1) }
			match(path, /^[^\/]*/)
			auth = substr(path, 1, RLENGTH); path = substr(path, RLENGTH + 1)
			sub(/^.*@/, "", auth)
			res = auth
			if (substr(path, 1, 1) == "/") { res = res "/"; path = substr(path, 2) }
			else { path = "" }
		}
		n = split(path, C, "/")
		if (k == "storage" && n >= 1 && pstay(C[1])) {
			out = out r path; s = after; continue
		}
		# (the last name: up to its last extension of a known kind that no letter or digit follows)
		rem = ""
		if (n >= 1) {
			last = C[n]; x = tolower(last); ee = 0; p = 0
			while (match(substr(x, p + 1), /\.[a-z0-9]+([^a-z0-9]|$)/)) {
				q = p + RSTART + RLENGTH - 1
				if (substr(x, q, 1) !~ /[a-z0-9]/) q--
				# (an end only where the name does not go on: "Film.Sub.Name" has none)
				if ((substr(x, p + RSTART + 1, q - p - RSTART) in EXT) && substr(x, q + 1, 1) !~ /[a-z0-9._-]/) ee = q
				p = q
			}
			if (ee && ee < length(last)) { rem = substr(last, ee + 1); C[n] = substr(last, 1, ee) }
			else if (match(C[n], / +$/)) { rem = substr(C[n], RSTART); C[n] = substr(C[n], 1, RSTART - 1) }
		}
		for (i = 1; i <= n && mname(); i++) {
			if (i > 1) res = res "/"
			# (a media folder of /storage: its name stays)
			if ((k == "storage" && i == 1 && (tolower(C[1]) in STOREDIR)) || (k == "user" && i == 1 && C[1] == "root")) res = res C[1]
			# (in an address, also its percent escapes: "Zorb%20Share" is the name "Zorb Share")
			else res = res pnamec(C[i], pclass(k, i), (k == "net") ? "net" : "")
		}
		# (written so that redact.sed does not take it again, in a log)
		if (k == "net") r = (MODE == "log") ? substr(r, 1, length(r) - 3) ":{//}" : r
		else if (MODE == "log") r = "{/}" substr(r, 2)
		out = out r res
		s = rem after
	}
	return out s
}
# Paths percent-encoded inside another address: bluray://%2fvar%2fmedia%2fA%20Drive%2fFilm%2f/menu. A path ends
# at the first character that an encoded path does not hold as it is (a "/", a space, a quote, "&", "?", "#").
function encp(s,   out, r, l, k, body, e, path, after, res, i, c, sep, v, stay) {
	out = ""
	while (more() && match(s, /%(25)*2[Ff](var%(25)*2[Ff]media|media|mnt|run%(25)*2[Ff]media|storage)%(25)*2[Ff]/)) {
		out = out substr(s, 1, RSTART - 1)
		r = substr(s, RSTART, RLENGTH)
		body = substr(s, RSTART + RLENGTH)
		l = tolower(r); gsub(/%(25)*2f/, "/", l)
		k = (l == "/storage/") ? "storage" : (l == "/run/media/") ? "user" : "drive"
		e = length(body) + 1
		if (match(body, /[\/ "'<>&?#\t|]/)) e = RSTART
		path = substr(body, 1, e - 1); after = substr(body, e)
		res = ""; i = 0; stay = 0
		while (path != "" && mname()) {
			if (match(path, /%(25)*2[Ff]/)) { c = substr(path, 1, RSTART - 1); sep = substr(path, RSTART, RLENGTH); path = substr(path, RSTART + RLENGTH) }
			else { c = path; sep = ""; path = "" }
			i++
			v = pdec(c)
			if (k == "storage" && i == 1) stay = pstay(v)
			if (stay || (k == "storage" && i == 1 && (tolower(v) in STOREDIR))) res = res c sep
			else res = res pnamec(c, pclass(k, i), "enc") sep
		}
		out = out r res
		s = after
	}
	return out s
}
# The names that systemd gives the mounts of drives: var-media-A\x20Drive.mount ("-" for "/", and "\x2d" for
# "-"), also as a second pass gets them, with markers in them
function unitp(s,   out, m, g, pre, name, suf, k, res, i, t, v) {
	out = ""
	while (more() && match(s, /(^|[^A-Za-z0-9_.:\\-])(var-media|media|mnt|run-media)-[][A-Za-z0-9_.:\\-]+\.(auto)?mount/)) {
		m = substr(s, RSTART, RLENGTH)
		g = (m ~ /^(var-media|media|mnt|run-media)-/) ? 0 : 1
		out = out substr(s, 1, RSTART - 1 + g)
		s = substr(s, RSTART + RLENGTH)
		m = substr(m, g + 1)
		match(m, /^(var-media|media|mnt|run-media)-/)
		pre = substr(m, 1, RLENGTH); name = substr(m, RLENGTH + 1)
		match(name, /\.(auto)?mount$/)
		suf = substr(name, RSTART); name = substr(name, 1, RSTART - 1)
		k = (pre == "run-media-") ? "user" : "drive"
		res = ""; i = 0
		while (name != "" && mname()) {
			# (a marker of a second pass has "-" in it: it is one name)
			if (match(name, /^\[[a-z0-9-]+(-[0-9a-f]+(\.[0-9a-f]+)?)?\](\.[A-Za-z0-9]+)*/) || match(name, /^[^-]*/)) { t = substr(name, 1, RLENGTH); name = substr(name, RLENGTH + 1) }
			i++
			v = pxdec(t)
			res = res (i > 1 ? "-" : "") pnamec(t, pclass(k, i), "unit")
			if (substr(name, 1, 1) == "-") name = substr(name, 2)
			else if (name != "") { res = res name; name = "" }
		}
		out = out pre res suf
	}
	return out s
}
# The labels of drives and partitions, as blkid prints them (LABEL="A Drive", PARTLABEL="...") and as udev
# keeps them (ID_FS_LABEL=A_Drive, ID_FS_LABEL_ENC=A\x20Drive): the marker of the drive, but for the labels of
# the partitions of LibreELEC and the ones that Windows and its tools give
function labels(s,   out, m, i, v, q) {
	out = ""
	while (more() && match(s, /(^|[^A-Za-z0-9_])((PART)?LABEL(_FATBOOT)?="[^"]*"|ID_FS_LABEL(_ENC)?=[^ \t]+|by-(part)?label\/[^ \t"'<>]+|by\\x2d(part)?label-[^ \t"'<>.]+)/)) {
		m = substr(s, RSTART, RLENGTH)
		out = out substr(s, 1, RSTART - 1)
		s = substr(s, RSTART + RLENGTH)
		# (the character before it, where the match took one)
		if (m !~ /^((PART)?LABEL|ID_FS_LABEL|by)/) { out = out substr(m, 1, 1); m = substr(m, 2) }
		# (/dev/disk/by-label/A\x20Drive, and its device unit dev-disk-by\x2dlabel-A\x5cx20Drive.device)
		if (m ~ /^by/) { i = (m ~ /^by-/) ? index(m, "/") : index(m, "label-") + 5 }
		else i = index(m, "=")
		v = substr(m, i + 1); q = ""
		if (substr(v, 1, 1) == "\"") { q = "\""; v = substr(v, 2, length(v) - 2) }
		if (m ~ /_ENC=|^by/) { v = pxdec(v); if (m ~ /^by\\/) v = pxdec(v) }
		if (v == "" || ismarker(v) || (tolower(v) in LABELKEEP)) out = out m
		else out = out substr(m, 1, i) q mark("volume", v, 6) q
	}
	return out s
}
# ---- the names of the Bluetooth devices that BlueZ keeps
# A byte of a word: a letter or a digit of ASCII, "_", or a byte of a character beyond ASCII (UTF-8).
function wordc(c) { return c != "" && (c ~ /[A-Za-z0-9_]/ || ORD[c] >= 128) }
function bttrim(v) { sub(/^[ \t]+/, "", v); sub(/[ \t\r]+$/, "", v); return v }
# Whether a name is looked for wherever it stands as a word: what is left of it without the signs at its two
# ends ("(null)" is "null") has 6 bytes or more, a letter (of ASCII, or beyond it), and is not one word of the
# letters A to Z alone. Shorter names ("TV", "K", "LG TV", "K380") and plain words ("Keyboard", "Remote",
# "Controller", "Television") stand in the logs for other things: such a name is taken where a line names an
# input device by it (btctx()), and nowhere else. So is a name that a marker could hold, whole or at one of its
# ends (only the characters of markers, "[" and those after it at its end, "]" and those before it at its start):
# a second pass of the filter, over its own markers, would find it in them ("bt-name", "Foo [lan").
function btfree(v,   i, j) {
	if (v ~ /^[][a-z0-9.-]+$/ || v ~ /\[[a-z0-9.-]*$/ || v ~ /^[a-z0-9.-]*\]/) return 0
	i = 1; j = length(v)
	while (i <= j && !alnum(substr(v, i, 1))) i++
	while (j >= i && !alnum(substr(v, j, 1))) j--
	v = substr(v, i, j - i + 1)
	if (length(v) < 6 || v ~ /^[A-Za-z]+$/) return 0
	if (v ~ /[A-Za-z]/) return 1
	for (i = 1; i <= length(v); i++) if (ORD[substr(v, i, 1)] >= 128) return 1
	return 0
}
function alnum(c) { return c ~ /[A-Za-z0-9]/ || ORD[c] >= 128 }
# A name of the list (NAMES): BTN[k] is the name, and its marker the one of that name. The kernel gives a
# Bluetooth input device the first 127 bytes of its name (BlueZ hands it over in a field of 128): those are
# looked for as well, with the marker of the whole name.
function btadd(v) {
	v = bttrim(v)
	if (v == "" || (v in BTK) || length(v) > 1024) return
	BTK[v] = ++NBC; BTN[NBC] = v
	btvar(v, NBC)
	if (length(v) > 127) btvar(substr(v, 1, 127), NBC)
}
# BS[1..NBS]: what is looked for in every line (64 at most; the rest goes to BW). BW[1..NBW]: what is taken
# where a line names an input device. BSK and BWK: the number of the name of each.
function btvar(x, k) {
	if (x in BTV) return
	BTV[x] = k
	if (btfree(x) && NBS < 64) { BS[++NBS] = x; BSK[NBS] = k }
	else { BW[++NBW] = x; BWK[NBW] = k }
}
# The longest first (a name that holds another one is taken whole), and at each end whether the name starts or
# ends with a byte of a word: there, the byte beside it in a line must be none, or the name is a part of a
# longer word ("Juan" in "Juana").
function btsort(   i, j, x, k) {
	for (i = 2; i <= NBS; i++) {
		x = BS[i]; k = BSK[i]
		for (j = i - 1; j >= 1 && length(BS[j]) < length(x); j--) { BS[j + 1] = BS[j]; BSK[j + 1] = BSK[j] }
		BS[j + 1] = x; BSK[j + 1] = k
	}
	for (i = 2; i <= NBW; i++) {
		x = BW[i]; k = BWK[i]
		for (j = i - 1; j >= 1 && length(BW[j]) < length(x); j--) { BW[j + 1] = BW[j]; BWK[j + 1] = BWK[j] }
		BW[j + 1] = x; BWK[j + 1] = k
	}
	for (i = 1; i <= NBS; i++) { BSL[i] = wordc(substr(BS[i], 1, 1)); BSR[i] = wordc(lastc(BS[i])) }
}
# The names in a line. Each one that is found becomes "\n<its number>\n" first, and its marker at the end: no
# line has a line end in it and no name has one, so that a shorter name is never found inside the marker of a
# longer one, nor across two of them. Each look goes on from where the last one ended, as in the other rules.
function btnames(s,   j, v, L, out, i, pre) {
	# (a line that the filter left out, and says so, as a second pass gets it)
	if (substr(s, 1, 10) == "[left out,") return s
	for (j = 1; j <= NBS; j++) {
		v = BS[j]
		if (!index(s, v)) continue
		L = length(v); out = ""
		while (more() && (i = index(s, v)) > 0) {
			pre = out substr(s, 1, i - 1)
			# (a part of a longer word: the look goes on from its next byte)
			if ((BSL[j] && wordc(lastc(pre))) || (BSR[j] && wordc(substr(s, i + L, 1)))) { out = pre substr(s, i, 1); s = substr(s, i + 1) }
			else { out = pre "\n" BSK[j] "\n"; s = substr(s, i + L) }
		}
		s = out s
	}
	if (NBW) s = btctx(s)
	if (index(s, "\n")) s = btmarks(s)
	return s
}
# The other names, where a line names an input device by them: the kernel's "input: NAME as /devices/..." and
# "... HID v1.11 Keyboard [NAME] on ...", systemd-logind's "Watching system buttons on /dev/input/event5
# (NAME)" and Kodi's "... device added: NAME (event5)" (and "removed"). An input device of the kernel may have
# a word after the name of its device ("NAME Consumer Control"): the name is taken there too.
function btctx(s,   i, j, a) {
	if ((i = index(s, "input: ")) > 0 && (j = index(substr(s, i + 7), " as /")) > 1) s = btput(s, i + 7, substr(s, i + 7, j - 1))
	if ((i = index(s, " HID v")) > 0 && (j = index(substr(s, i), " [")) > 0) {
		a = i + j + 1
		if ((j = index(substr(s, a), "] on ")) > 1) s = btput(s, a, substr(s, a, j - 1))
	}
	if ((i = index(s, "Watching system buttons on /dev/input/event")) > 0 && lastc(s) == ")" && (j = index(substr(s, i), " (")) > 0) {
		a = i + j + 1
		if (a < length(s)) s = btput(s, a, substr(s, a, length(s) - a))
	}
	a = 0
	if ((i = index(s, " device added: ")) > 0) a = i + 15
	else if ((i = index(s, " device removed: ")) > 0) a = i + 17
	if (a && (j = index(substr(s, a), " (event")) > 1) s = btput(s, a, substr(s, a, j - 1))
	return s
}
# x, the text at a of s: where it is a name of BW, or starts with one and a space, that name becomes its number
function btput(s, a, x,   m, v) {
	for (m = 1; m <= NBW; m++) {
		v = BW[m]
		if (x == v || substr(x, 1, length(v) + 1) == v " ") return substr(s, 1, a - 1) "\n" BWK[m] "\n" substr(s, a + length(v))
	}
	return s
}
function btmarks(s,   out, k) {
	out = ""
	while (match(s, /\n[0-9]+\n/)) {
		k = substr(s, RSTART + 1, RLENGTH - 2) + 0
		if (!(k in BTM)) BTM[k] = mark("bt-name", BTN[k], 6)
		out = out substr(s, 1, RSTART - 1) BTM[k]
		s = substr(s, RSTART + RLENGTH)
	}
	return out s
}
# GLib's key files, in which BlueZ keeps its devices: "\s" a space (at the start of a value), "\n", "\t", "\r",
# and "\\" a backslash
function gkey(v,   out, i, c, d) {
	out = ""
	for (i = 1; i <= length(v); i++) {
		c = substr(v, i, 1)
		if (c == "\\" && i < length(v)) {
			d = substr(v, ++i, 1)
			if (d == "s") c = " "; else if (d == "n") c = "\n"; else if (d == "t") c = "\t"; else if (d == "r") c = "\r"; else if (d == "\\") c = "\\"; else c = c d
		}
		out = out c
	}
	return out
}

# ---- one line
function ids(s) {
	# (the names first: a name may hold what another rule takes, a part of it then)
	if (NBC) s = btnames(s)
	NETSTAT = s ~ /^(tcp|udp|raw|udpl)6? +[0-9]+ +[0-9]+ /
	if (index(s, "SHA256:") || index(s, "MD5:") || index(s, " AAAA") || index(s, "|")) s = ssh(s)
	if (index(s, ":")) {
		s = macs(s)
		s = ip6(s)
	}
	if (index(s, "-")) s = uuids(s)
	if (index(s, ".")) s = ip4(s)
	if (s ~ /[Uu][Uu][Ii][Dd][=\/]/) s = uuidvals(s)
	if (index(s, "x2duuid-") || index(s, "x2dpartuuid-")) s = escuuids(s)
	if (s ~ / (vfat|exfat|ntfs3?|fuseblk) /) s = lsblk(s)
	if (s ~ /[Ss][Ee][Rr][Ii][Aa][Ll]|PDI/) s = serials(s)
	if (index(s, "dev_")) s = bluez(s)
	if (index(s, "_")) s = connman(s)
	if (index(s, "ssid: ") || index(s, "SSID ")) s = ssids(s)
	if (index(s, "://")) s = lannames(s)
	if (HOST != "" && index(s, HOST)) s = host(s)
	# (the paths last: the server of an address has its marker then, and is not taken for a name)
	if (index(s, "/media/") || index(s, "/mnt/") || index(s, "/storage/") || index(s, "://") || index(s, "%2") || index(s, "media-") || index(s, "mnt-") || index(s, "LABEL") || index(s, "label")) s = mpaths(s)
	return s
}

# ---- an EDID, as bytes E[0..n-1]: its serial number (bytes 12 to 15) zero, the text of a "Display Product
# Serial Number" descriptor "REDACTED", and the serial number of a DisplayID "Product Identification" block
# zero. Each checksum moves by what the bytes it covers moved, so that one that was right stays right and
# one that was wrong stays as wrong as it was. Returns 0, with nothing changed, where E is no EDID. With force
# (a file of edid/ that is no EDID, or is cut short: in it, nothing tells where its serial numbers are), the
# same without looking at its header, as far as it goes: bytes 12 to 15, and the text of a descriptor of the
# kind of a serial number at 54, 72, 90 or 108.
function edid(n, force,   i, o, b, d) {
	if (!force) {
		if (n < 128 || E[0] != 0 || E[7] != 0) return 0
		for (i = 1; i <= 6; i++) if (E[i] != 255) return 0
	}
	d = 0
	for (i = 12; i <= 15 && i < n; i++) { d += E[i]; E[i] = 0 }
	for (o = 54; o <= 108 && o + 17 < n; o += 18)
		if (E[o] == 0 && E[o + 1] == 0 && E[o + 2] == 0 && E[o + 3] == 255)
			for (i = 0; i < 13; i++) { d += E[o + 5 + i]; E[o + 5 + i] = (i < 8) ? RED[i + 1] : (i == 8 ? 10 : 32); d -= E[o + 5 + i] }
	if (n >= 128) E[127] = ((E[127] + d) % 256 + 256) % 256
	for (b = 128; b + 127 < n; b += 128) if (E[b] == 112) displayid(b)
	return 1
}
function displayid(b,   end, p, plen, tag, i, d) {
	end = b + 5 + E[b + 2]
	if (end > b + 126) return
	d = 0
	for (p = b + 5; p + 3 <= end; p += 3 + plen) {
		tag = E[p]; plen = E[p + 2]
		if (p + 3 + plen > end) break
		if ((tag == 0 || tag == 32) && plen >= 9) for (i = p + 8; i <= p + 11; i++) { d += E[i]; E[i] = 0 }
	}
	# (the checksum of the section moves by what the serial number moved; the sum of the block is then
	# what it was, and its own checksum stays)
	if (d) E[end] = ((E[end] + d) % 256 + 256) % 256
}
# Rows of hex that may be an EDID: as modetest prints a blob (32 digits, after tabs) or as hexdump prints
# one in pastekodi (16 pairs). They are held until the blob ends, and come out as they were unless they are
# an EDID.
function hold(s,   f) {
	f = (s ~ /^([0-9a-f][0-9a-f] )+/) ? "hd" : "mt"
	if (NROWS > 0 && f != HF) flush()
	HF = f
	ROW[++NROWS] = s
}
function flush(   i, j, n, r, ind, t) {
	if (NROWS == 0) return
	n = 0
	for (i = 1; i <= NROWS; i++) {
		r = ROW[i]
		gsub(/[ \t]/, "", r)
		for (j = 1; j < length(r); j += 2) E[n++] = HX[substr(r, j, 2)]
	}
	if (edid(n)) {
		match(ROW[1], /^[ \t]*/)
		ind = substr(ROW[1], 1, RLENGTH)
		t = (ROW[1] ~ / $/) ? " " : ""
		for (i = 0; i < n; i += 16) {
			r = ""
			for (j = i; j < i + 16 && j < n; j++) r = r (HF == "hd" ? (j > i ? " " : "") : "") sprintf("%02x", E[j])
			emit(HF == "hd" ? r t : ind r)
		}
	} else for (i = 1; i <= NROWS; i++) emit(ROW[i])
	NROWS = 0
}
# What comes out is held one line, so that a last line that had no line end gets none: redact.sh puts a line
# end and the line LAST (a random word of its run) after its input, and the line before LAST is then empty
# where the input ended with a line end, and is the last line of the input where it did not.
function out1(s) {
	if (HELD) print PREV
	PREV = s; HELD = 1
}
# A line that comes out: held with the others until their digests are made (flushq()), where they are made in
# batches
function emit(s) {
	# (at once where nothing waits: a line is held only behind one that waits for a digest, to keep their order.
	# A line printed at once can still hold the placeholder of a digest made before, from a marker kept in PNC or
	# BTM: resolved here. What is held goes out in 48 KB at most: redact.sed reads it through a pipe of 64 KB, and
	# what is more waits for it, so that the two did not work at the same time: 1.6 s more in 16 MiB of a log)
	if (!DEFER || (!NQ && !NBUF)) { out1(index(s, "\034") ? resolve(s) : s); return }
	BUF[++NBUF] = s; NHELD++; HBY += length(s) + 1
	if (HBY > HBMAX) HBMAX = HBY
	if (NQ >= 256 || NBUF >= 4096 || HBY >= 49152) flushq()
}

MODE == "edid" {
	for (i = 1; i <= NF; i++) E[NB++] = HX[tolower($i)]
	next
}
# Files of BlueZ (<adapter>/<device>/info, <adapter>/cache/<device>): the name and the alias in the part
# [General] of each (redact.sh gives them one after another, each after a line [hades-file]: a part of its own)
MODE == "btlist" || MODE == "btmark" {
	if (FNR == 1) SEC = ""
	if ($0 ~ /^[ \t]*\[/) { SEC = bttrim($0); next }
	if (SEC == "[General]" && match($0, /^[ \t]*(Name|Alias)[ \t]*=/)) {
		n = RLENGTH
		k = $0; sub(/^[ \t]+/, "", k); sub(/[ \t]*=.*$/, "", k)
		v = bttrim(substr($0, n + 1))
		if (length(v) <= 1024) v = bttrim(gkey(v))
		# (a name with a line end in it would be two names of the list: no log writes it as one line anyway)
		if (MODE == "btlist") { if (v != "" && !index(v, "\n") && length(v) <= 1024) print v }
		else if (v != "") print k ": " mark("bt-name", v, 6)
	}
	next
}
LAST != "" && $0 == LAST { ENDED = 1; next }
{
	# (the byte 034 is what the placeholders of digests are made with: none comes from the input)
	if (index($0, "\034")) gsub(/\034/, "?")
	if (length($0) >= 32767) { flush(); emit("[left out, a line of 32767 characters or more]"); next }
	if ($0 ~ /^[ \t]*[0-9a-f]{32}[ \t]*$/ || $0 ~ /^([0-9a-f][0-9a-f] ){15}[0-9a-f][0-9a-f] *$/) { hold($0); next }
	flush()
	NM = 0; NMN = 0; TOOMANY = 0
	r = (MODE == "log") ? paths(ids($0)) : ids($0)
	emit(TOOMANY == 2 ? "[left out, a line with more than 4096 names in its paths]" : TOOMANY ? "[left out, a line with more than 1024 addresses, names or paths]" : r)
}
END {
	if (MODE == "edid") {
		# (a copy that an earlier version damaged, putting a folder of a start through the filter for text:
		# each byte 0 a line end, so its header is 0a ff ff ff ff ff ff 0a. Its bytes 0 cannot be told from
		# its bytes 10, and its serial number is still in it: nothing of it is written)
		if (NB >= 8 && E[0] == 10 && E[7] == 10 && E[1] == 255 && E[2] == 255 && E[3] == 255 && E[4] == 255 && E[5] == 255 && E[6] == 255) exit
		# (a file that is no EDID, or not a whole one: its serial numbers go all the same, where an EDID has them)
		if (!edid(NB, 0)) edid(NB, 1)
		for (i = 0; i < NB; i++) printf "\\%03o", E[i]
		exit
	}
	flush()
	flushq()
	if (DGFILE != "" && KEY != "") dgsave()
	# (with STATS, what it did, for a measure: lines, names worked out and taken from the cache, digests made, lines
	# held back behind a digest still to make, digests that sha256sum did not give)
	if (STATS != "") printf "redact-ids: %d lines, %d names worked out, %d from the cache, %d digests made, %d lines held (%d bytes at most), %d digests failed\n", NR, NPN, NPC, NDG, NHELD, HBMAX, NFAIL > "/dev/stderr"
	if (!ENDED) { if (HELD) print PREV }
	# (a last line with no line end: where the input was cut while it was written (DROPCUT, from shutdown.sh), it is
	# left out: it may end in the middle of an address or a name, which no rule then sees whole)
	else if (HELD && PREV != "" && DROPCUT == "") printf "%s", PREV
}
