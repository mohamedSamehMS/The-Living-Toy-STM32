/*
 * APP_MM.h — Memory Match module contract.
 *  LOCATION: APP/ (flat). FILENAME IS APP_MM.h — prefix included.
 *
 *  THE MODULE (built over the hardware-proven MM_renderer): the
 *  7-state FSM — PICK1 / PICK2 / MATCH_BEAT / REVEAL / FLIPBACK /
 *  WIN / EXIT_ASK. Three pedagogical beats (§7 applied from day
 *  one): the match celebration, the mismatch memorization hold (the
 *  documented REVEAL delayed-action — fires once after its
 *  deadline, input-free by construction), and the flip-back settle.
 *
 *  INPUT: the system-wide dual map over the 4x4 grid — 2/8/4/6
 *  single-step (wrap in the grid, the MathQ convention), D-pad
 *  glides, CH flips the card under the cursor, 0 = the exit seam.
 *
 *  SCORE MODEL: 80 - (tries - 8), floor 0 — a perfect game (every
 *  second flip a match) is 8 tries = 80 points; each extra try
 *  costs one. Valid after GAMEOVER only.
 *
 *  ZERO RTOS KNOWLEDGE (the negative contract): deadline checks
 *  via GetElapsed_ms only — the class the Stage-1 shim ports as-is.
 *  Consumers are stall-tolerant by design (the historical ~270 ms
 *  envelope; the current tree is faster still — invisible either way).
 */
#ifndef APP_MM_H_
#define APP_MM_H_
#include "STD_TYPES.h"
#include "APP/HUB_interface.h"

GameResult_t APP_MM_enumRun(void);
u16          APP_MM_u16GetScore(void);

#endif /* APP_MM_H_ */
