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
#   - MAC and Bluetooth addresses (also in the names connman gives its services), but all-zero, broadcast
#     and group addresses.
#   - UUIDs (partitions, the graphics card), and the serial numbers of FAT and NTFS volumes where a UUID
#     goes; the Bluetooth base UUIDs stay (they name profiles, not devices).
#   - Serial numbers that hardware reports (displays, graphics cards, USB devices), the GPU PDI of NVIDIA,
#     and in an EDID that is printed as rows of hex, its serial number and serial string (see edid()).
#   - SSH fingerprints, public keys and the "randomart" of a key.
#   - Network names (SSID) as iwd, connman and wpa_supplicant write them.
#   - Names of computers of the home network in addresses (no dot, or .local, .lan, .home and the like),
#     and the name of this system (HOST) wherever it stands as a word.
# What stays, because it says what the machine is and not whose: makes and models, the names that devices
# and drivers give themselves, public server names, ports, prefix lengths, and the time zone.
# Variables: KEYFILE (its first line is the secret; without it a marker has no second part), TMP (a file
# for the hashing, that nobody else writes), HOST (this system's network name), MODE ("edid": the input is
# an EDID as `od -An -v -tx1` prints it, and what comes out is that EDID without its serial numbers, as
# octal escapes for printf).
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
}

# ---- markers
# The first n hex digits of SHA-256 of the secret, the class and the value; "" without the secret.
function digest(cls, v,   k, h, c) {
	k = cls SUBSEP v
	if (k in DG) return DG[k]
	if (KEY == "" || ND >= 4000) return ""
	printf "%s\n%s\n%s\n", KEY, cls, v > TMP
	close(TMP)
	c = "sha256sum " TMP
	h = ""
	c | getline h
	close(c)
	h = substr(h, 1, 8)
	if (h !~ /^[0-9a-f]+$/ || length(h) != 8) h = ""
	DG[k] = h
	ND++
	return h
}
function mark(cls, v, n,   h) {
	h = digest(cls, v)
	return "[" cls (h == "" ? "" : "-" substr(h, 1, n)) "]"
}
# An address: [class-NNNN.HHHH], NNNN the same for its whole network, HHHH its own.
function pair(cls, net, a,   h1, h2) {
	h1 = digest(cls "/net", net)
	h2 = digest(cls, a)
	if (h1 == "" || h2 == "") return "[" cls "]"
	return "[" cls "-" substr(h1, 1, 4) "." substr(h2, 1, 4) "]"
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
# Whether v is a marker of this filter (or a redaction of redact.sed): it is not marked again.
function ismarker(v) { return v ~ /^\[[a-z0-9-]+(-[0-9a-f]+(\.[0-9a-f]+)?)?\]$/ }
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
	# (a group address: of a multicast, not of a device)
	if ((("0x" substr(l, 1, 2)) + 0) % 2 == 1) return m
	return mark("mac", l, 6)
}
function colons(h,   i, r) {
	r = ""
	for (i = 1; i <= 11; i += 2) r = r (i > 1 ? ":" : "") substr(h, i, 2)
	return r
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

# ---- one line
function ids(s) {
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
	if (index(s, "_")) s = connman(s)
	if (index(s, "ssid: ") || index(s, "SSID ")) s = ssids(s)
	if (index(s, "://")) s = lannames(s)
	if (HOST != "" && index(s, HOST)) s = host(s)
	return s
}

# ---- an EDID, as bytes E[0..n-1]: its serial number (bytes 12 to 15) zero, the text of a "Display Product
# Serial Number" descriptor "REDACTED", and the serial number of a DisplayID "Product Identification" block
# zero. Each checksum moves by what the bytes it covers moved, so that one that was right stays right and
# one that was wrong stays as wrong as it was. Returns 0, with nothing changed, where E is no EDID.
function edid(n,   i, o, b, d) {
	if (n < 128 || E[0] != 0 || E[7] != 0) return 0
	for (i = 1; i <= 6; i++) if (E[i] != 255) return 0
	d = 0
	for (i = 12; i <= 15; i++) { d += E[i]; E[i] = 0 }
	for (o = 54; o <= 108; o += 18)
		if (E[o] == 0 && E[o + 1] == 0 && E[o + 2] == 0 && E[o + 3] == 255)
			for (i = 0; i < 13; i++) { d += E[o + 5 + i]; E[o + 5 + i] = (i < 8) ? RED[i + 1] : (i == 8 ? 10 : 32); d -= E[o + 5 + i] }
	E[127] = ((E[127] + d) % 256 + 256) % 256
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
function emit(s) {
	if (HELD) print PREV
	PREV = s; HELD = 1
}

MODE == "edid" {
	for (i = 1; i <= NF; i++) E[NB++] = HX[tolower($i)]
	next
}
LAST != "" && $0 == LAST { ENDED = 1; next }
{
	if (length($0) >= 32767) { flush(); emit("[left out, a line of 32767 characters or more]"); next }
	if ($0 ~ /^[ \t]*[0-9a-f]{32}[ \t]*$/ || $0 ~ /^([0-9a-f][0-9a-f] ){15}[0-9a-f][0-9a-f] *$/) { hold($0); next }
	flush()
	NM = 0; TOOMANY = 0
	r = (MODE == "log") ? paths(ids($0)) : ids($0)
	emit(TOOMANY ? "[left out, a line with more than 1024 addresses, names or paths]" : r)
}
END {
	if (MODE == "edid") {
		edid(NB)
		for (i = 0; i < NB; i++) printf "\\%03o", E[i]
		exit
	}
	flush()
	if (!ENDED) { if (HELD) print PREV }
	else if (HELD && PREV != "") printf "%s", PREV
}
