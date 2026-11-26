#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: which network access is in effect (Settings > LibreELEC > Services > Network access, and the
# wizard of the first start), read from the settings themselves as the settings add-on reads them (hades_netaccess.py):
#   network access: <Recommended | As in LibreELEC | own settings> (samba=<off|password|open> ssh=<refuses|accepts>
#   firewall=<none|home|public|custom> airplay=<on|off|unknown>)
# Recommended: Samba off or with a password, SSH refusing root's default password, a firewall, AirPlay off. As in
# LibreELEC: Samba on without a password, SSH taking the default password, no firewall, AirPlay on. Only reads.
# HADES_ROOT stands for / in a test.
S="${HADES_ROOT:-}/storage/.cache/services"
one() { head -n 1 "$1" 2>/dev/null; }
# a value of the file of a service that is on, the last one where it is there twice
opt() { sed -n "s/^$2=//p" "$S/$1.conf" 2>/dev/null | tail -n 1 | tr -d '"\r\t '; }
on() { [ -f "$S/$1.conf" ] && [ ! -f "$S/$1.disabled" ]; }

samba=off
if on samba; then
  case "$(opt samba SAMBA_SECURE)" in false|0) samba=open ;; *) samba=password ;; esac
fi
ssh=refuses
[ "$(one "${HADES_ROOT:-}/storage/.cache/hades-network")" = libreelec ] && ssh=accepts
firewall=none
if on iptables; then
  firewall=$(opt iptables RULES)
  case "${firewall}" in none|home|public|custom) ;; *) firewall=home ;; esac
fi
airplay=unknown
g="${HADES_ROOT:-}/storage/.kodi/userdata/guisettings.xml"
if [ -f "$g" ]; then
  v=$(sed -n 's|.*<setting id="services.airplay"[^>]*>\([a-z]*\)</setting>.*|\1|p' "$g" | head -n 1)
  case "$v" in true) airplay=on ;; false) airplay=off ;; esac
fi
case "${samba}:${ssh}:${firewall}:${airplay}" in
  open:accepts:none:on) state="As in LibreELEC" ;;
  off:refuses:home:off|off:refuses:public:off|off:refuses:custom:off|password:refuses:home:off|password:refuses:public:off|password:refuses:custom:off) state=Recommended ;;
  *) state="own settings" ;;
esac
echo "network access: ${state} (samba=${samba} ssh=${ssh} firewall=${firewall} airplay=${airplay})"
