// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: whether a CEC line answers at all (xbmc/peripherals/devices/PeripheralCecLine.h,
// CPeripheralCecAdapter::AskLine()), by the tx_status of a poll; the line of issue 24 (every message "timed out",
// tx_status 0xa0) is given up on after two polls, a line with or without a TV is opened by libCEC as before. One look
// at the line (AskInRow()) makes its polls in a row: libCEC is asked to open the line only after it, never between
// two of them (an open on such a line did not return in issue 24).
#include "PeripheralCecLine.h"

#include <cstdio>
#include <vector>

using namespace PERIPHERALS::CEC_LINE;

namespace
{
int g_checks = 0, g_failures = 0;
void Check(bool ok, const char* what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}
//! One look at a line whose polls give \p answers in turn; \p stopAt: the pause after that many polls says the
//! thread stops (0: never)
struct Look
{
  Line line;
  int asked;
  int pauses;
};
Look LookAt(const std::vector<Line>& answers, int stopAt = 0)
{
  size_t next = 0;
  int pauses = 0;
  int asked = 0;
  const Line line = AskInRow([&] { return next < answers.size() ? answers[next++] : Line::ALIVE; },
                             [&]
                             {
                               ++pauses;
                               return stopAt == 0 || pauses < stopAt;
                             },
                             asked);
  return {line, asked, pauses};
}
} // namespace

int main()
{
  Check(Of(true, TX_OK) == Line::ALIVE, "the TV acknowledged the poll: the line works");
  Check(Of(true, TX_NACK | TX_MAX_RETRIES) == Line::ALIVE, "no TV, a negative acknowledgement: the line works");
  Check(Of(true, TX_TIMEOUT | TX_MAX_RETRIES) == Line::DEAD, "issue 24, tx_status 0xa0: nothing answers");
  Check(Of(true, TX_ERROR | TX_MAX_RETRIES) == Line::DEAD, "an error after the retries: nothing answers");
  Check(Of(true, TX_ARB_LOST | TX_MAX_RETRIES) == Line::DEAD, "arbitration lost after the retries: nothing answers");
  Check(Of(true, TX_LOW_DRIVE) == Line::DEAD, "a line held low: nothing answers");
  Check(Of(true, TX_ABORTED) == Line::UNKNOWN, "aborted (the adapter went or was set up again): nothing said");
  Check(Of(false, 0) == Line::UNKNOWN, "the kernel refused the poll: nothing said, libCEC opens as before");
  Check(Of(true, 0) == Line::UNKNOWN, "no status: nothing said");
  Check(Of(true, TX_OK | TX_TIMEOUT) == Line::ALIVE, "a result of the line wins over a timeout of a retry");
  Check(!GivenUp(0) && !GivenUp(1), "one poll without a result is not enough (a busy line)");
  Check(GivenUp(2), "two in a row give the line up");
  Check(AGAIN_SECONDS >= 10 && AGAIN_SECONDS <= 60, "it is asked again within a minute");

  // one look at the line, before libCEC is asked to open it
  const Look dead = LookAt({Line::DEAD, Line::DEAD});
  Check(dead.line == Line::DEAD && dead.asked == 2 && dead.pauses == 1,
        "issue 24: two polls in a row without a result, one pause between them, and no open of libCEC between them");
  const Look busy = LookAt({Line::DEAD, Line::ALIVE});
  Check(busy.line == Line::ALIVE && busy.asked == 2, "a busy line: the second poll gets a result, libCEC opens it");
  const Look tv = LookAt({Line::ALIVE});
  Check(tv.line == Line::ALIVE && tv.asked == 1 && tv.pauses == 0, "a line that answers: one poll, libCEC opens it");
  const Look unknown = LookAt({Line::UNKNOWN});
  Check(unknown.line == Line::UNKNOWN && unknown.asked == 1, "a poll the kernel refused: libCEC opens it, as before");
  const Look deadThenUnknown = LookAt({Line::DEAD, Line::UNKNOWN});
  Check(deadThenUnknown.line == Line::UNKNOWN && deadThenUnknown.asked == 2,
        "no result, then nothing said: libCEC opens it, as before");
  const Look never = LookAt({Line::DEAD, Line::DEAD, Line::ALIVE});
  Check(never.line == Line::DEAD && never.asked == DEAD_IN_ROW, "no more polls in one look than DEAD_IN_ROW");
  const Look stopping = LookAt({Line::DEAD, Line::ALIVE}, 1);
  Check(stopping.line == Line::DEAD && stopping.asked == 1,
        "the thread stops between two polls: the look ends without a result, libCEC is not asked on the way out");
  Check(BETWEEN_SECONDS >= 1 && BETWEEN_SECONDS < AGAIN_SECONDS, "a pause between two polls, shorter than the wait after a look");
  std::printf("CEC-LINE %d checks, %d failed\n", g_checks, g_failures);
  if (!g_failures)
    std::printf("ALL PASSED\n");
  return g_failures ? 1 : 0;
}
