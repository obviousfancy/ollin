/*
 * mcuil_smooth.h
 *
 *  Created on: May 2026
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Suavizador de parámetros de primer orden (1-pole IIR lowpass).
 *
 *  PROPÓSITO:
 *  Cuando el usuario controla un parámetro en tiempo real (frecuencia de corte
 *  de un filtro, amplitud, velocidad del LFO) con un ADC, potenciómetro o
 *  encoder, los cambios abruptos generan discontinuidades en los coeficientes
 *  del DSP que producen "zipper noise" (clicks audibles).
 *
 *  Este módulo aplica un filtro paso-bajas de primer orden sobre la señal
 *  de control, suavizando el cambio con una constante de tiempo configurable.
 *
 *  FUNCIONAMIENTO:
 *  La ecuación es la de un filtro IIR de un polo:
 *
 *      output[n] = output[n-1] + coef * (target - output[n-1])
 *                = (1 - coef) * output[n-1] + coef * target
 *
 *  Donde coef = 1 - exp(-1 / (tau * sample_rate))
 *
 *  Con tau = 0.005s (5ms): el parámetro alcanza ~63% del valor destino
 *  en 5ms y ~99% en ~25ms. Suficientemente rápido para no percibirse como
 *  "lento", pero suficiente para eliminar el zipper noise.
 *
 *  COSTE EN CPU:
 *  Hot-path: 1 FSUB + 1 FMUL + 1 FADD = 3 operaciones FPU por muestra de
 *  control. Si se llama a tasa de bloque (cada 128 muestras), el coste
 *  es prácticamente cero.
 *
 *  USO:
 *  @code
 *      smooth_t fc_smooth;
 *      smooth_init(&fc_smooth, 44100.0f, 0.005f);  // tau = 5ms
 *
 *      // En el bucle de control (tasa de bloque o ADC):
 *      float fc_raw = adc_to_hz(adc_read());
 *      float fc     = smooth_process(&fc_smooth, fc_raw);
 *      biquad_set_lowpass(&filter, fc, 0.707f);
 *  @endcode
 *
 *  Este archivo es header-only: todas las funciones son static inline.
 *  No requiere archivo .c asociado.
 */

#ifndef MCUIL_SMOOTH_H_
#define MCUIL_SMOOTH_H_

#include <math.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Tipo
 * --------------------------------------------------------------------- */

/**
 * Suavizador de parámetros de primer orden.
 * Inicializar con smooth_init() antes de usar.
 */
typedef struct {
    float output;  /**< Valor de salida actual */
    float coef;    /**< Coeficiente de seguimiento [0, 1) */
} smooth_t;

/* -----------------------------------------------------------------------
 * Funciones inline — sin coste de llamada
 * --------------------------------------------------------------------- */

/**
 * @brief  Inicializa el suavizador.
 * @param  s            Puntero al suavizador.
 * @param  sample_rate  Frecuencia a la que se llamará smooth_process() en Hz.
 *                      Puede ser la tasa de audio (44100) o la de control
 *                      (tasa de bloque = sample_rate / block_size).
 * @param  tau_s        Constante de tiempo en segundos.
 *                      0.001–0.010 s es un rango útil para audio.
 *                      Tau más grande = suavizado más lento.
 */
static inline void smooth_init(smooth_t *s, float sample_rate, float tau_s)
{
    if (!s) return;
    if (tau_s  <= 0.0f) tau_s  = 0.001f;
    if (sample_rate <= 0.0f) sample_rate = 44100.0f;
    s->output = 0.0f;
    /* coef = 1 - exp(-1/(tau*sr)): cuánto se acerca el output al target
     * en cada llamada. Calculado una sola vez con expf() — no en hot-path. */
    s->coef = 1.0f - expf(-1.0f / (tau_s * sample_rate));
}

/**
 * @brief  Procesa un valor objetivo y retorna el valor suavizado.
 *         Hot-path: 3 operaciones FPU.
 * @param  s       Puntero al suavizador.
 * @param  target  Valor destino (ej. frecuencia leída del ADC).
 * @return Valor suavizado actual.
 */
static inline float smooth_process(smooth_t *s, float target)
{
    s->output += s->coef * (target - s->output);
    return s->output;
}

/**
 * @brief  Fuerza el output al valor dado sin suavizado (útil en init/reset).
 */
static inline void smooth_reset(smooth_t *s, float value)
{
    if (s) s->output = value;
}

/**
 * @brief  Retorna el valor de salida actual sin avanzar.
 */
static inline float smooth_get(const smooth_t *s)
{
    return s ? s->output : 0.0f;
}

#endif /* MCUIL_SMOOTH_H_ */
