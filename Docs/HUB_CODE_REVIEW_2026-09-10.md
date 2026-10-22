# Hub code review — architecture audit and status verification

Scope: source and checked-in generated build manifests only. No source was modified.

## Findings

### CRITICAL

1. `src/main.c:721` — `(void)MRCC_EnablePeripheralClock(MRCC_USART1);` enables the prohibited ESP/USART1 clock. `src/main.c:757` through `src/main.c:765` also configure `MGPIO_PIN_9`/`MGPIO_PIN_10` and initialize `MUSART_USART1`. The configured hub build must leave PA9/PA10 and USART1 untouched. **Fix:** replace this legacy boot path with the hub boot path; do not enable/configure/use USART1.

2. `src/main.c:787` — `Game_voidPaintBootScreen();` followed by the legacy `while (1)` at `src/main.c:790` launches the online Tic-Tac-Toe application, not the hub. There is no `APP_STORE_enumInit()` or `HUB_enumRun()` call in `main.c`; both only occur as definitions in `src/APP/APP_STORE.c:14` and `src/APP/HUB_program.c:186`. This makes the built hub, Snake, and Math Quiz code unreachable from reset. **Fix:** initialize the RAM store before calling `HUB_enumRun()` and remove the retired TTT application loop from the build entry point.

3. `Debug/src/APP/subdir.mk:12`, `:18`, `:20`, and `:21` compile `APP_TTT.c`, `NET_program.c`, `TTT_renderer.c`, and `TTT_session.c`; `Debug/src/subdir.mk:20` compiles the legacy root `main.c`. TTT is specified as deferred, but both its stub/renderer/session/network path and its old application entry point are built. **Fix:** remove deferred TTT/network sources and the retired application entry-point contents from the hub build composition.

### HIGH

1. `include/APP/SNAKE_config.h:19` contains only `#include "STD_TYPES.h"`, while `include/APP/SNAKE_config.h:60` through `:68` use `HTFT_RGB565` and `HTFT_COLOR_*`. The required `#include <HAL/TFT/HTFT_interface.h>` is absent, so the configuration header is not self-contained. **Fix:** add that exact HTFT interface include to `include/APP/SNAKE_config.h`.

### MEDIUM

1. `src/APP/HUB_menu.c:180` — `Local_u8Row = (u8)((Local_u8Row + 1u) % Copy_pstMenu->RowCount);` wraps the menu cursor. The architecture requires a cursor that clamps and never wraps. **Fix:** saturate at the last row (or disable the cycling behavior) rather than using `% Copy_pstMenu->RowCount`.

### LOW — dead-code inventory

The following configured/declarative items have no use outside their definition and should remain inventory-only for this review:

- `include/APP/MATHQ_config.h:61` — `MATHQ_CFG_FEEDBACK_BAD_MS`.
- `src/APP/APP_MATHQ.c:77` — `MQ_KEYS`.
- `include/APP/MATHQ_config.h:37` and `:78`–`:81` — `MATHQ_CFG_DEMO_ACCURACY_PCT`, `MATHQ_CFG_DEMO_THINK_MS`, `MATHQ_CFG_DEMO_WALK_MS`, `MATHQ_CFG_DEMO_PRESS_MS`, `MATHQ_CFG_DEMO_RESTART_MS`.
- `include/APP/SNAKE_config.h:78`–`:79` — `SNAKE_CFG_DEMO_CRASH_LEN`, `SNAKE_CFG_DEMO_FOOD_AHEAD`.

## Verified checks

- Math Quiz stale placeholder is absent: the beat-2 block proceeds from `MATHQ_voidPaintHeart(Mq_u8Hearts, 0u);` at `src/APP/APP_MATHQ.c:448` to the locally constructed answer digits at `:450`–`:456`; no `MATHQ_voidPaintAnswer("0", 1u, ...)` call exists.
- `Mq_voidPaintResults` uses `Mq_voidAppendStr(Local_ach, "/10");` at `src/APP/APP_MATHQ.c:344` and `Mq_voidAppendStr(Local_ach, "%");` at `:349`; no fixed-offset append is present.
- Snake uses the corrected tail-slot expression at `src/APP/APP_SNAKE.c:356`: `(Snk_u16Head + SNAKE_BODY_MAX - Snk_u16Len) % SNAKE_BODY_MAX`.
- Hub labels are `"SCORES"` and `"GAME END"` at `src/APP/HUB_program.c:74` and `:76`. The score banner uses distinct hundreds/tens/ones divisions at `:161`–`:163` (and likewise for high score at `:165`–`:167`).
- `HUB_RENDERER_privateCenter2x` uses signed `s32` at `src/APP/HUB_renderer.c:15`–`:20` and clamps negative coordinates to zero.
- Keypad list navigation joins `BTN_NUM_2` with up and `BTN_NUM_8` with down at `src/APP/HUB_menu.c:122`–`:133`; 4/6 fall through intentionally at `:135`–`:138`.
- `APP_INPUT` has an empty HButtons slot and only forwards `HNEC_REMOTE` events at `src/APP/APP_INPUT.c:10`–`:26`.
- The RAM persistence seam is implemented in `src/APP/APP_STORE.c:11`–`:33`, but is not initialized from the actual entry point (critical finding 2).
- `include/MCAL/EXTI/MEXTI_config.h:67` configures line 0 as PA/falling/enabled; lines 1–15 at `:68`–`:82` are all disabled.
- The four requested `APP_` module/header pairs exist: `APP_SNAKE`, `APP_MATHQ`, `APP_MM`, and `APP_TTT`. The generated manifest builds the corresponding module sources at `Debug/src/APP/subdir.mk:7`–`:12`.
- Both playable implementations own their loops, consume `APP_INPUT`, have a BACK exit-confirm state, and return `GAME_RESULT_EXITED` only there: Snake `src/APP/APP_SNAKE.c:416`–`:477`; Math Quiz `src/APP/APP_MATHQ.c:358`–`:514`.
- Renderer sweep: `HUB_renderer.c`, `SNAKE_renderer.c`, `MATHQ_renderer.c`, `MM_renderer.c`, and `TTT_renderer.c` include no MCAL/input headers. Their examined code contains drawing/geometry only; no FSM or input polling was found.
- HUD packing is fixed: `src/APP/MM_renderer.c:135` is `"PAIR:0/8"`; `TRIES:00` occupies the other eight cells at `:142`–`:146`.

## 2x title law

No literal supplied to `HTFT_voidDrawString2x` exceeds eight characters. The literal calls are: `"ONLINE"` (6) at `src/main.c:363`; `"RESULTS"` (7) at `src/APP/APP_MATHQ.c:336`; `"MEMORY"` (6) at `src/APP/APP_MM.c:12`; and `"TIC TAC"` (7) / `"TOE"` (3) at `src/APP/APP_TTT.c:19`–`:20`.

The remaining 2x calls take bounded/generated content rather than literals: hub descriptor titles through `src/APP/HUB_renderer.c:29`, Math Quiz equation/answer buffers through `src/APP/MATHQ_renderer.c:151`, `:158`, and `:171`, and legacy-TTT buffers at `src/main.c:374` and `:404`. Hub titles are `"GAME HUB"`, `"SCORES"`, `"SETTINGS"`, and `"GAME END"` at `src/APP/HUB_program.c:73`–`:76`, all within eight characters. This is a code-only length verification; the legacy path itself remains a critical integration violation.

## Module status (code only)

| Module | Status | Basis |
|---|---|---|
| `HUB_program.c` | BUILT+SUSPECT | Built, but reset never enters `HUB_enumRun`. |
| `HUB_menu.c` | BUILT+SUSPECT | Built; keypad behavior is present, but kiosk cursor wraps. |
| `HUB_renderer.c` | BUILT+SUSPECT | Painter contract verified; unreachable from current reset path. |
| `APP_SNAKE.c` | BUILT+SUSPECT | Loop/exit/tail fix verified; unreachable from current reset path. |
| `SNAKE_renderer.c` | BUILT+SUSPECT | Dumb renderer verified; dependent config header lacks required HTFT include and game is unreachable. |
| `APP_MATHQ.c` | BUILT+SUSPECT | Loop/exit and requested fixes verified; unreachable from current reset path. |
| `MATHQ_renderer.c` | BUILT+SUSPECT | Dumb renderer verified; game is unreachable. |
| `APP_MM.c` | STUB | Built placeholder screen, returns only EXITED. |
| `MM_renderer.c` | BUILT+SUSPECT | Renderer and HUD packing verify, but paired APP module is a stub and hub is unreachable. |
| `APP_TTT.c` | STUB | Built placeholder, although TTT should be deferred. |
| `TTT_renderer.c` | BUILT+SUSPECT | Painter code is built despite the deferred-TTT contract. |

## Could not confirm from code alone

- Remote wiring, NEC timing/repeat behavior, TFT electrical wiring, panel orientation, and on-glass rendering.
- Whether the flashed binary corresponds to this source/build-manifest state.
- Actual USART2 console output and the absence/presence of PA9/PA10 activity on hardware.
- Flash persistence behavior (the source currently implements only the RAM seam).
