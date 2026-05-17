/*
 * mcuil_wavegen.c
 *
 *  Created on: Oct 7, 2025
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Correcciones v1.1:
 *  - wavegen_set_sample_rate(): la asignación osc->sample_rate ahora se
 *    aplica tanto para valores válidos como para valores por debajo del
 *    mínimo (antes sólo se asignaba dentro del bloque de validación).
 */

#include "mcuil_wavegen.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -----------------------------------------------------------------------
 * LUT global de seno
 * --------------------------------------------------------------------- */

#if WAVEGEN_USE_SINE_LUT
static float sine_lut[WAVEGEN_SINE_LUT_SIZE];
static int   s_lut_ready = 0;
#endif

/* -----------------------------------------------------------------------
 * Helpers internos
 * --------------------------------------------------------------------- */

static inline float clampf(float value, float lo, float hi)
{
    return (value < lo) ? lo : (value > hi) ? hi : value;
}

/* -----------------------------------------------------------------------
 * Inicialización global
 * --------------------------------------------------------------------- */

void wavegen_global_init(void)
{
#if WAVEGEN_USE_SINE_LUT
    if (!s_lut_ready) {
        for (int i = 0; i < WAVEGEN_SINE_LUT_SIZE; ++i) {
            sine_lut[i] = sinf(2.0f * (float)M_PI *
                               ((float)i / (float)WAVEGEN_SINE_LUT_SIZE));
        }
        s_lut_ready = 1;
    }
#endif
}

/* -----------------------------------------------------------------------
 * Inicialización de oscilador
 * --------------------------------------------------------------------- */

void wavegen_init(wavegen_t *osc, wave_type_t type,
                  float sample_rate, float phase)
{
    if (!osc) return;

#if WAVEGEN_USE_SINE_LUT
    if (!s_lut_ready) wavegen_global_init();
#endif

    osc->type        = type;
    osc->frequency   = 440.0f;
    osc->amplitude   = 0.5f;
    osc->sample_rate = (sample_rate > 1000.0f) ? sample_rate : 44100.0f;
    osc->duty        = 0.5f;

    /* Fase normalizada a [0, 1) — funciona para valores positivos y negativos */
    float p = phase - (float)(int)phase;
    osc->phase = (p < 0.0f) ? p + 1.0f : p;
}

/* -----------------------------------------------------------------------
 * Setters individuales
 * --------------------------------------------------------------------- */

void wavegen_set_type(wavegen_t *osc, wave_type_t type)
{
    if (!osc) return;
    osc->type = type;
}

/*
 * FIX v1.1: la versión anterior sólo asignaba osc->sample_rate dentro del
 * bloque if(sample_rate < 1000), por lo que un valor VÁLIDO nunca se
 * guardaba. Ahora se guarda siempre, clampando el mínimo a 1000 Hz.
 */
void wavegen_set_sample_rate(wavegen_t *osc, float sample_rate)
{
    if (!osc) return;
    if (sample_rate < 1000.0f) sample_rate = 1000.0f;
    osc->sample_rate = sample_rate;
}

void wavegen_set_frequency(wavegen_t *osc, float frequency)
{
    if (!osc) return;
    if (frequency < 0.0f) frequency = 0.0f;
    float nyquist = 0.5f * osc->sample_rate;
    osc->frequency = (frequency > nyquist) ? nyquist : frequency;
}

void wavegen_set_amplitude(wavegen_t *osc, float amplitude)
{
    if (!osc) return;
    osc->amplitude = clampf(amplitude, WAVEGEN_MIN_AMPLITUDE, WAVEGEN_MAX_AMPLITUDE);
}

void wavegen_set_phase(wavegen_t *osc, float phase)
{
    if (!osc) return;
    float p = phase - (float)(int)phase;
    osc->phase = (p < 0.0f) ? p + 1.0f : p;
}

void wavegen_set_duty(wavegen_t *osc, float duty)
{
    if (!osc) return;
    osc->duty = clampf(duty, WAVEGEN_MIN_DUTY, WAVEGEN_MAX_DUTY);
}

/* -----------------------------------------------------------------------
 * LUT de seno con interpolación lineal
 * --------------------------------------------------------------------- */

#if WAVEGEN_USE_SINE_LUT
static inline float sine_lut_linear(float phase)
{
    float pos = phase * (float)WAVEGEN_SINE_LUT_SIZE;
    int   i0  = (int)pos;
    float frac = pos - (float)i0;
    int   i1  = i0 + 1;
    i0 &= (WAVEGEN_SINE_LUT_SIZE - 1);
    i1 &= (WAVEGEN_SINE_LUT_SIZE - 1);
    return sine_lut[i0] + frac * (sine_lut[i1] - sine_lut[i0]);
}
#endif

/* -----------------------------------------------------------------------
 * PolyBLEP — corrección de discontinuidades para cuadrada y sierra
 * --------------------------------------------------------------------- */

#if WAVEGEN_USE_POLYBLEP
static inline float poly_blep(float t, float dt)
{
    if (t < dt) {
        float x = t / dt;
        return  x + x - x * x - 1.0f;         /* 2x - x² - 1 */
    }
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        return  x * x + x + x + 1.0f;         /* x² + 2x + 1 */
    }
    return 0.0f;
}
#endif

/* -----------------------------------------------------------------------
 * Generación de muestra — hot-path de audio
 * --------------------------------------------------------------------- */

float wavegen_next_sample(wavegen_t *osc)
{
    if (!osc) return 0.0f;

    float t  = osc->phase;
    float dt = (osc->frequency > 0.0f) ?
               (osc->frequency / osc->sample_rate) : 0.0f;
    float y  = 0.0f;

    switch (osc->type) {

        case WAVE_SINE:
#if WAVEGEN_USE_SINE_LUT
            y = sine_lut_linear(t);
#else
            y = sinf(2.0f * (float)M_PI * t);
#endif
            break;

        case WAVE_SQUARE:
            y = (t < osc->duty) ? 1.0f : -1.0f;
#if WAVEGEN_USE_POLYBLEP
            y += poly_blep(t, dt);
            {
                float td = t - osc->duty;
                if (td < 0.0f) td += 1.0f;
                y -= poly_blep(td, dt);
            }
#endif
            break;

        case WAVE_TRIANGLE:
            y = 4.0f * fabsf(t - 0.5f) - 1.0f;
            break;

        case WAVE_SAWTOOTH:
            y = 2.0f * t - 1.0f;
#if WAVEGEN_USE_POLYBLEP
            y -= poly_blep(t, dt);
#endif
            break;

        default:
            break;
    }

    /* Avance de fase normalizado — evita módulo trigonométrico */
    osc->phase += dt;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;

    /* Aplicar amplitud y clamp de seguridad numérica */
    y *= osc->amplitude;
    if      (y >  1.0f) y =  1.0f;
    else if (y < -1.0f) y = -1.0f;

    return y;
}

/* -----------------------------------------------------------------------
 * Procesamiento por bloques
 * --------------------------------------------------------------------- */

void wavegen_render_block(wavegen_t *osc, float *out, size_t num_samples)
{
    if (!osc || !out) return;
    for (size_t i = 0; i < num_samples; ++i) {
        out[i] = wavegen_next_sample(osc);
    }
}

/* -----------------------------------------------------------------------
 * Mezcla de osciladores
 * --------------------------------------------------------------------- */

float wavegen_mix_next(wavegen_t *oscs, size_t count,
                       const float *gains, int normalize)
{
    if (!oscs || count == 0) return 0.0f;

    float acc = 0.0f;
    float den = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        float g = gains ? gains[i] : 1.0f;
        if (g != 0.0f) {
            acc += g * wavegen_next_sample(&oscs[i]);
            if (normalize) den += (g < 0.0f ? -g : g); /* fabsf inline */
        }
    }

    if (normalize && den > 0.0f) acc /= den;

    if      (acc >  1.0f) acc =  1.0f;
    else if (acc < -1.0f) acc = -1.0f;

    return acc;
}

void wavegen_render_mix_block(wavegen_t *oscs, size_t count,
                               const float *gains, int normalize,
                               float *buffer, size_t num_samples)
{
    if (!oscs || !buffer || count == 0) return;

    for (size_t i = 0; i < num_samples; ++i) {
        float acc = 0.0f;
        float den = 0.0f;
        for (size_t k = 0; k < count; ++k) {
            float g = gains ? gains[k] : 1.0f;
            if (g != 0.0f) {
                acc += g * wavegen_next_sample(&oscs[k]);
                if (normalize) den += (g < 0.0f ? -g : g);
            }
        }
        if (normalize && den > 0.0f) acc /= den;
        if      (acc >  1.0f) acc =  1.0f;
        else if (acc < -1.0f) acc = -1.0f;
        buffer[i] = acc;
    }
}
