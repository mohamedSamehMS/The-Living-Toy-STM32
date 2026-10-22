/*
 * HAUDIO_interface.h - sampled 8-bit audio output, PUBLIC INTERFACE
 *
 *  The driver owns the three GPIO wires between the STM32 and its
 *  dedicated 74HC595. Every sample is shifted MSB-first and then
 *  latched once, so the R-2R ladder never displays an intermediate
 *  code. The sample timer is hardware paced; callers never busy-wait.
 */
#ifndef HAUDIO_INTERFACE_H_
#define HAUDIO_INTERFACE_H_

#include "STD_TYPES.h"

typedef struct
{
    u16 FreqHz;        /* pitch; 0 = rest */
    u16 DurationMs;    /* duration in milliseconds */
} SongNote_t;

typedef struct
{
    const SongNote_t *Notes;
    u16 NoteCount;
    u16 LoopForever;   /* 1 = repeat until stopped */
    u8 GainEighth;     /* 0 = full volume (legacy), 1..8 = ceiling */
} Song_t;

typedef enum
{
    HAUDIO_OK = 0,
    HAUDIO_NOK,
    HAUDIO_INVALID_PARAM
} HAUDIO_ReturnType_t;

typedef enum
{
    HAUDIO_STATE_IDLE = 0,
    HAUDIO_STATE_PLAYING,
    HAUDIO_STATE_STOPPING
} HAUDIO_State_t;

/* Configure the pins, build the sine table and configure TIM9.
 * The timer is deliberately left stopped and the output is centred. */
HAUDIO_ReturnType_t HAUDIO_enumInit(void);

/* Start at note zero. A second request replaces the current song after
 * a short fade-out, then begins the new note at the envelope midpoint. */
HAUDIO_ReturnType_t HAUDIO_enumPlaySong(const Song_t *Copy_pstSong);

/* Play an optional event sting only when the output is idle; active music
 * is never interrupted and the sting is dropped while it is busy. */
HAUDIO_ReturnType_t HAUDIO_enumPlaySting(const Song_t *Copy_pstSting);

/* Request a graceful approximately 10 ms fade, then stop TIM9. */
HAUDIO_ReturnType_t HAUDIO_enumStop(void);

/* Cooperative sequencer pump; call once per application super-loop pass. */
void HAUDIO_voidTask(void);

/* Read the driver state. */
HAUDIO_State_t HAUDIO_enumGetState(void);

#endif /* HAUDIO_INTERFACE_H_ */
