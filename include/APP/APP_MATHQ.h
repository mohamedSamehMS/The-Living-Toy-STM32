/*
 * APP_MATHQ.h — Math Quiz module contract. PENDING MODULE (stub).
 *  LOCATION: APP/ (flat). FILENAME IS APP_MATHQ.h — prefix included.
 *
 *  STATUS: the game's renderer + generator design are complete and
 *  hardware-proven (MATHQ_renderer.c / MATHQ_config.h, in the build,
 *  untouched since validation). The module body is gated by THE
 *  FORK — the one design ruling only the owner can make:
 *    (a) NUMERIC answer entry — the spec's wording; requires the
 *        zero-digit workaround ("0" is globally BACK, so 100+/200+
 *        get repurposed as digit-0 keys in answer contexts) plus a
 *        clear key
 *    (b) MULTIPLE CHOICE — dissolves the zero problem entirely,
 *        fits younger children, at the cost of the "numeric answer
 *  entry" wording
 *  Either way the module also carries the §7 pacing fix (three-beat
 *  wrong-answer feedback: wrong -> answer reveal -> gap) — a ~10-line
 *  state-timeline change + three config knobs, independent of the
 *  fork. The keypad-cursor input seam is already proven by the demo.
 */
#ifndef APP_MATHQ_H_
#define APP_MATHQ_H_
#include "STD_TYPES.h"
#include "APP/HUB_interface.h"

GameResult_t APP_MATHQ_enumRun(void);
u16          APP_MATHQ_u16GetScore(void);

#endif /* APP_MATHQ_H_ */
