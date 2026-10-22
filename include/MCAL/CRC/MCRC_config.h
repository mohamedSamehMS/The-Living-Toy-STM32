/*
 * MCRC_config.h
 *
 *  Configuration of the MCRC driver.
 *
 *  Section 1: the KAT constant - the STM32 hardware CRC unit returned
 *  0xAE24E09D for "123456789" on the bench; the textbook byte-wise
 *  CRC-32/MPEG-2 value for the same input is 0x0376E6E7.
 *
 *  Reference: RM0368 Rev 6, Ch 4 (CRC), -4.4
 *
 *  Revision history:
 *    Rev 1 - first release (FOTA-03 spec)
 *
 *  Created on: Sep 15, 2026
 *      Author: dell
 */

#ifndef MCRC_CONFIG_H_
#define MCRC_CONFIG_H_

/* =====================================================================
 *  1. Known-answer test constant
 *
 *  Check string "123456789" (9 bytes, no terminator, no padding -
 *  9 -> padded to 12 with 0x00 per the canonical padding rule).
 *  STM32 hardware CRC unit bench result: 0xAE24E09D for the 9-byte input.
 *  Textbook byte-wise CRC-32/MPEG-2 value for the same input: 0x0376E6E7.
 * ===================================================================== */
#define MCRC_KAT_EXPECTED  		0xAE24E09DUL
#define MCRC_KAT_STRING         "123456789"
#define MCRC_KAT_LENGTH         9u

#endif /* MCRC_CONFIG_H_ */
