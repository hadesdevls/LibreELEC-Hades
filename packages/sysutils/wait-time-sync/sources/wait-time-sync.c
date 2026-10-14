/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020-present Team LibreELEC (https://libreelec.tv) */

#include <sys/timex.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define POLL_FREQ 10

/* Hades: flag files created by the NTP clients once they have synchronized the
 * clock. When the RTC runs in local time (/etc/adjtime says LOCAL) they keep the
 * kernel's STA_UNSYNC set, so that it never writes the RTC, and adjtimex() keeps
 * returning TIME_ERROR. */
static const char *const sync_flags[] = {
  "/run/connman/ntp-synchronized",
  "/run/systemd/timesync/synchronized",
};

static int sync_flag_present(void)
{
  for (size_t i = 0; i < sizeof(sync_flags) / sizeof(sync_flags[0]); i++)
    if (access(sync_flags[i], F_OK) == 0)
      return 1;
  return 0;
}

void usage(char *name)
{
  if (!name)
    name = "wait-time-sync";

  printf("Usage: %s [-t seconds | --timeout seconds]\n", name);
  exit(2);
}

int main(int argc, char** argv)
{
  unsigned timeout = UINT_MAX;
  int rc = 0;

  if (argc == 3)
  {
    unsigned long val;
    char *p;

    if (strcmp(argv[1], "-t") && strcmp(argv[1], "--timeout"))
      usage(argv[0]);
    val = strtoul(argv[2], &p, 0);
    if (*p || val == 0 || val >= UINT_MAX / POLL_FREQ)
      usage(argv[0]);
    timeout = (unsigned)val * POLL_FREQ;
  }
  else if (argc != 1)
    usage(argv[0]);

  for ( ; timeout; --timeout)
  {
    struct timex tx = {};

    rc = adjtimex(&tx);
    if (rc != TIME_ERROR)
      break;
    if (sync_flag_present())
    {
      rc = TIME_OK;
      break;
    }
    usleep(1000000U / POLL_FREQ);
  }

  return rc == -1 ? errno : !timeout;
}
