// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: what a bridge test does with the CCM 001 reader (ccm001_oracle.h): every
// transport that the test reads back goes through it; the test's own Check() says at the end whether all were
// read as the clause says. Included by a test after its Check().
#ifndef HADES_CCM001_ORACLE_CHECK_H
#define HADES_CCM001_ORACLE_CHECK_H

#include "ccm001_oracle.h"

static long g_oracleRead, g_oracleFailed, g_oracleMulti, g_oracleBars, g_oracleCm40;
static int g_oracleLines;

static void OracleTransport(const uint8_t* bytes, const char* label, const char* way)
{
  struct ccm_oracle_opts o;
  ccm_oracle_defaults(&o);
  o.depth = 12; // the 12-bit YCbCr 4:2:2 signal of the tunnel (and hdr_bit_depth 12, which the bridge requires)
  static struct ccm_oracle_result r;
  const int ok = ccm_oracle_read(bytes, &o, &r);
  ++g_oracleRead;
  g_oracleFailed += !ok;
  g_oracleMulti += ok && r.packets > 1;
  g_oracleBars += ok && r.bar_pixels > 0;
  g_oracleCm40 += ok && r.cm40 > 0;
  if (!ok || g_oracleLines < 4)
  {
    char text[512];
    ccm_oracle_describe(&r, text, sizeof(text));
    printf("%s: %s: %s\n", label, way, text);
    g_oracleLines += ok;
  }
}

static void OracleSummary(void)
{
  printf("CCM 001 reader: %ld transports read, %ld not as the clause says; %ld of several packets, %ld with bars "
         "checked black, %ld with CM v4.0 blocks\n",
         g_oracleRead, g_oracleFailed, g_oracleMulti, g_oracleBars, g_oracleCm40);
  Check(g_oracleRead > 0 && g_oracleFailed == 0,
        "every transport reads as ETSI GS CCM 001 says it is sent (an independent reader: three copies with their "
        "CRC, headers, lengths, table 3, blocks and their order, black outside the active area)");
}

#endif
