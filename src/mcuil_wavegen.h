/*
 * mcuil_wavegen.h
 *
 *  Created on: Oct 7, 2025
 *      Author: Radof
 */

#ifndef ATLASLIBRARIES_HEADERS_MCUIL_WAVEGEN_H_
#define ATLASLIBRARIES_HEADERS_MCUIL_WAVEGEN_H_


#include <stdint.h>
#include <stddef.h>

#define WAVEGEN_USE_SINE_LUT 1

#define WAVEGEN_SINE_LUT_SIZE 1024

#define WAVEGEN_USE_POLYBLEP 1

#define WAVEGEN_MIN_AMPLITUDE 0.0f

#define WAVEGEN_MAX_AMPLITUDE 1.0f

#define WAVEGEN_MIN_DUTY 0.01f

#define WAVEGEN_MAX_DUTY 0.99f


typedef enum {
	WAVE_SINE,
	WAVE_SQUARE,
	WAVE_TRIANGLE,
	WAVE_SAWTOOTH
} wave_type_t;

typedef struct {
	wave_type_t type;
	float frequency; // Frecuencia en Hz
	float amplitude; // Amplitud (0.0 a 1.0)
	float phase;     // Fase en radianes
	float sample_rate; // Frecuencia de muestreo en Hz
	float duty; 		 // Ciclo de trabajo para onda cuadrada (0.0 a 1.0)
} wavegen_t;

void wavegen_global_init(void);
void wavegen_init(wavegen_t *osc,wave_type_t type, float sample_rate, float phase);

void wavegen_set_type(wavegen_t *osc, wave_type_t type);
void wavegen_set_sample_rate(wavegen_t *osc, float sample_rate);
void wavegen_set_frequency(wavegen_t *osc, float frequency);
void wavegen_set_amplitude(wavegen_t *osc, float amplitude);
void wavegen_set_phase(wavegen_t *osc, float phase);
void wavegen_set_duty(wavegen_t *osc, float duty);

float wavegen_next_sample(wavegen_t *osc);

void wavegen_render_block(wavegen_t *osc, float *buffer, size_t num_samples);

float wavegen_mix_next(wavegen_t *oscs, size_t count, const float *gains, int normalize);

void wavegen_render_mix_block(wavegen_t *oscs, size_t count, const float *gains, int normalize, float *buffer, size_t num_samples);

static inline uint16_t wavegen_float_to_u12(float x)
{
	if (x > 1.0f) x = 1.0f;
	if (x < -1.0f) return -1.0f;
	return (uint16_t)((x + 1.0f) * 2047.5f);

}

#endif /* ATLASLIBRARIES_HEADERS_MCUIL_WAVEGEN_H_ */
