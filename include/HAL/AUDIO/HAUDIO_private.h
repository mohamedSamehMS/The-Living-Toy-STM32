/*
 * HAUDIO_private.h - sampled audio output, PRIVATE header
 *
 *  Only HAUDIO_program.c includes this file. Shared ISR state remains
 *  file-static; the declarations below are implementation helpers.
 */
#ifndef HAUDIO_PRIVATE_H_
#define HAUDIO_PRIVATE_H_

#include "STD_TYPES.h"

static void HAUDIO_privateSampleISR(void);
static void HAUDIO_privateShiftAndLatch(u8 Copy_u8Sample);
static u32 HAUDIO_privatePhaseIncrement(u16 Copy_u16FrequencyHz);
static void HAUDIO_privateLoadNote(u16 Copy_u16Index);
static void HAUDIO_privateBeginStop(void);

#endif /* HAUDIO_PRIVATE_H_ */
