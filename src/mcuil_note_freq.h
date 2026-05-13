/*
 * mcuil_note_freq.h
 *
 *  Created on: Sep 22, 2025
 *      Author: Radof
 */

#ifndef INC_MCUIL_NOTE_FREQ_H_
#define INC_MCUIL_NOTE_FREQ_H_

#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*Funcionales*/

void fillTableOfFrequencies();



// Config global de afinación.
typedef struct {
    float a4_hz;   // A4 = 440.0 por defecto
} notes_config_t;

// Tamaño estándar MIDI.
#define NOTES_MIDI_COUNT 128


void notes_init(notes_config_t *config);

void notes_build_table(float out_table[NOTES_MIDI_COUNT]);

float notes_midi_to_hz(uint8_t midi_note);

int notes_hz_to_midi(float hz);

bool notes_midi_to_name(uint8_t midi_note, bool sharps, char *out_name, size_t out_name_size);

bool notes_name_to_midi(const char *name, uint8_t *out_midi_note);

bool notes_hz_to_name(float hz, bool sharps, char *out_name, size_t out_name_size,float *cents_out);

const char* notes_get_name(uint8_t midi_note);

int notes_get_octave(uint8_t midi_note);

#endif /* INC_MCUIL_NOTE_FREQ_H_ */
