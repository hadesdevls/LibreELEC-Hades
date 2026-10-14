#!/bin/sh

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2014 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2018-present Team LibreELEC (https://libreelec.tv)

if [ -f /sys/class/rtc/rtc0/wakealarm ]; then
  logger -t setwakeup.sh "### Setting system wakeup time ###"
  WAKEUP="$1"
  # Hades: the kernel reads the RTC as UTC. When the RTC runs in local time (/etc/adjtime
  # says LOCAL, as on a PC that also runs Windows), program the local time of the wakeup.
  if [ "$(sed -n 3p /etc/adjtime 2>/dev/null)" = "LOCAL" ]; then
    WAKEUP="$(TZ=UTC date -d "$(date -d "@$1" '+%Y-%m-%d %H:%M:%S')" +%s)"
    [ -n "$WAKEUP" ] || WAKEUP="$1"
    logger -t setwakeup.sh "### RTC in local time: wakeup $1 programmed as $WAKEUP ###"
  fi
  echo 0 >/sys/class/rtc/rtc0/wakealarm
  echo $WAKEUP >/sys/class/rtc/rtc0/wakealarm
  logger -t setwakeup.sh "### $(cat /proc/driver/rtc) ###"
fi
