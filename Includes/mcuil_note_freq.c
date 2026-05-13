/*
 * mcuil_note_freq.c
 *
 *  Created on: Sep 22, 2025
 *      Author: Obviousfancy
 */

#include "mcuil_note_freq.h"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <string.h>


static float at_a4 = 440.0f;
static float at_table[NOTES_MIDI_COUNT];
static int   at_table_ready = 0;

#define TOTAL_OCTAVES 10 // De la octava 0 a la 7
//FRECUENCIA A LA QUE SE AFINARA
#define A4_INDEX 9   // La nota A4 es la décima nota (índice 9) en la secuencia de 12 notas
#define OCTAVE_OFFSET 4
#define NUM_NOTES 12
#define SAMPLES 256  // Número de muestras por ciclo de onda
#define MAX_DAC_VALUE 4095  // Máximo valor para 12-bit DAC

extern uint32_t audio_dac[SAMPLES];  // Arreglo para almacenar los datos de la onda

float noteFrequencies[NUM_NOTES * TOTAL_OCTAVES];

const char* noteNames[NUM_NOTES] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/*FUNCIONALES*/
void fillTableOfFrequencies() {
    for (int octave = 0; octave < TOTAL_OCTAVES; ++octave) {
        for (int note = 0; note < NUM_NOTES; ++note) {
            //int index = octave * NUM_NOTES + note;
            //noteFrequencies[index] = A4 * pow(2, (note - A4_INDEX + (octave - OCTAVE_OFFSET) * NUM_NOTES) / 12.0);
        }
    }
}

void notes_init(notes_config_t *config) {
	//Si la configuración es nula, se usa la afinación por defecto.
	if(config->a4_hz > 10.0f && config->a4_hz < 2000.0f )
	{
		at_a4 = config->a4_hz;
	}else
	{
		at_a4 = 440.0f;

	}
	at_table_ready = 0;
}

void notes_build_table(float out_table[NOTES_MIDI_COUNT]) {
	if(!at_table_ready)
	{
		for (int i = 0; i < NOTES_MIDI_COUNT; i++) {
			out_table[i] = at_a4 * powf(2.0f, (i - 69) / 12.0f);
			at_table[i] = out_table[i];
		}

		at_table_ready = 1;
	}else
	{

		//memcpy(out_table, at_table, sizeof(at_table));
		at_table_ready = 1;
	}
}


float notes_midi_to_hz(uint8_t midi_note) {
	//if(midi_note < NOTES_MIDI_COUNT)
	//{
		if(!at_table_ready)
		{
			notes_build_table(at_table);


		}
		return at_table[midi_note];
		//return at_table[midi_note];
	//}
	//return -1.0f;
}

int notes_hz_to_midi(float hz) {
	if(hz < 20.0f || hz > 20000.0f)
	{
		return -1;
	}
	if(!at_table_ready)
	{
		notes_build_table(at_table);
	}
	int closest_note = -1;
	float closest_diff = 1e6; // Un valor grande inicial

	for (int i = 0; i < NOTES_MIDI_COUNT; i++) {
		float diff = fabsf(at_table[i] - hz);
		if (diff < closest_diff) {
			closest_diff = diff;
			closest_note = i;
		}
	}
	return closest_note;
}




const char* notes_get_name(uint8_t midi_note){
	static const char* names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};

	return names[midi_note % 12];

}

int notes_get_octave(uint8_t midi_note) {
	return (midi_note / 12) - 1; // estándar MIDI: C4 = 60
}

void notes_print_table(void) {
    for (int i = 0; i < NOTES_MIDI_COUNT; i++) {
        printf("%3d | %-3s%-2d | %8.2f Hz\n",
               i, notes_get_name(i), notes_get_octave(i), at_table[i]);
    }
}
