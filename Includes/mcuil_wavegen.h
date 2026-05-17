/*
 * mcuil_wavegen.h
 *
 *  Created on: Oct 7, 2025
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Generador de formas de onda para audio digital.
 *  Optimizado para STM32H753ZI (Cortex-M7 @ 480 MHz, FPU de doble precisión).
 *
 *  Estrategia de eficiencia:
 *  - Onda senoidal: LUT de 1024 puntos con interpolación lineal (WAVEGEN_USE_SINE_LUT).
 *    Elimina sinf() en el hot-path; la FPU del M7 completa la interpolación en ~3 ciclos.
 *  - PolyBLEP (WAVEGEN_USE_POLYBLEP): suprime aliasing en cuadrada/sierra sin FFT.
 *  - El avance de fase se almacena normalizado [0, 1) para evitar el módulo trigonométrico.
 *  - wavegen_render_block() y wavegen_render_mix_block() procesan en bloques,
 *    aprovechando la caché de instrucciones del M7.
 */

#ifndef ATLASLIBRARIES_HEADERS_MCUIL_WAVEGEN_H_
#define ATLASLIBRARIES_HEADERS_MCUIL_WAVEGEN_H_

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Opciones de compilación
 * --------------------------------------------------------------------- */

/** Habilita la LUT de seno (recomendado). Elimina sinf() en hot-path. */
#ifndef WAVEGEN_USE_SINE_LUT
#define WAVEGEN_USE_SINE_LUT  1
#endif

/** Tamaño de la LUT de seno. Debe ser potencia de 2 para el enmascaramiento. */
#ifndef WAVEGEN_SINE_LUT_SIZE
#define WAVEGEN_SINE_LUT_SIZE 1024
#endif

/** Habilita corrección PolyBLEP para cuadrada y diente de sierra. */
#ifndef WAVEGEN_USE_POLYBLEP
#define WAVEGEN_USE_POLYBLEP  1
#endif

/* Límites de amplitud */
#define WAVEGEN_MIN_AMPLITUDE  0.0f
#define WAVEGEN_MAX_AMPLITUDE  1.0f

/* Límites de ciclo de trabajo (duty) para la onda cuadrada */
#define WAVEGEN_MIN_DUTY  0.01f
#define WAVEGEN_MAX_DUTY  0.99f

/* -----------------------------------------------------------------------
 * Tipos
 * --------------------------------------------------------------------- */

/** Formas de onda soportadas. */
typedef enum {
    WAVE_SINE     = 0,
    WAVE_SQUARE   = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAWTOOTH = 3
} wave_type_t;

/**
 * Descriptor de un oscilador.
 * Todos los campos deben inicializarse con wavegen_init() antes de usar.
 */
typedef struct {
    wave_type_t type;
    float frequency;    /**< Frecuencia en Hz */
    float amplitude;    /**< Amplitud [0.0, 1.0] */
    float phase;        /**< Fase normalizada [0.0, 1.0) */
    float sample_rate;  /**< Frecuencia de muestreo en Hz (≥ 1000) */
    float duty;         /**< Ciclo de trabajo para onda cuadrada [0.01, 0.99] */
} wavegen_t;

/* -----------------------------------------------------------------------
 * API pública
 * --------------------------------------------------------------------- */

/** Inicializa la LUT global de seno. Llamar una sola vez al arrancar. */
void wavegen_global_init(void);

/**
 * @brief  Inicializa un oscilador con parámetros base.
 * @param  osc          Puntero al oscilador.
 * @param  type         Forma de onda.
 * @param  sample_rate  Frecuencia de muestreo en Hz (< 1000 Hz → usa 44100).
 * @param  phase        Fase inicial normalizada [0.0, 1.0).
 */
void wavegen_init(wavegen_t *osc, wave_type_t type,
                  float sample_rate, float phase);

/* --- Setters individuales -------------------------------------------- */
void wavegen_set_type       (wavegen_t *osc, wave_type_t type);
void wavegen_set_sample_rate(wavegen_t *osc, float sample_rate);
void wavegen_set_frequency  (wavegen_t *osc, float frequency);
void wavegen_set_amplitude  (wavegen_t *osc, float amplitude);
void wavegen_set_phase      (wavegen_t *osc, float phase);
void wavegen_set_duty       (wavegen_t *osc, float duty);

/* --- Generación de muestras ----------------------------------------- */

/**
 * @brief  Genera y retorna la siguiente muestra, avanzando la fase.
 *         Hot-path de audio: mínimo de ramas y operaciones FPU.
 */
float wavegen_next_sample(wavegen_t *osc);

/**
 * @brief  Renderiza un bloque de `num_samples` muestras en `out`.
 *         Útil para procesamiento por bloques (mejor uso de caché).
 */
void wavegen_render_block(wavegen_t *osc, float *out, size_t num_samples);

/**
 * @brief  Genera la siguiente muestra mezclando `count` osciladores.
 * @param  oscs       Array de osciladores.
 * @param  count      Número de osciladores.
 * @param  gains      Array de ganancias por oscilador (NULL = todos 1.0).
 * @param  normalize  Si 1, normaliza la suma por la suma de |gains|.
 */
float wavegen_mix_next(wavegen_t *oscs, size_t count,
                       const float *gains, int normalize);

/**
 * @brief  Renderiza un bloque mezclando `count` osciladores.
 */
void wavegen_render_mix_block(wavegen_t *oscs, size_t count,
                               const float *gains, int normalize,
                               float *buffer, size_t num_samples);

/* -----------------------------------------------------------------------
 * Conversión de salida para DAC de 12 bits
 *
 *  Mapea el rango [-1.0, +1.0] al rango [0, 4095] para uso directo
 *  con el DAC del STM32H753ZI.
 *
 *  Corrección v1.1:
 *  - El retorno anterior de `return -1.0f` para x < -1.0f era incorrecto:
 *    retornaba un float desde una función uint16_t (conversión implícita a 0,
 *    perdiendo la intención del clamp negativo). Ahora retorna 0.
 * --------------------------------------------------------------------- */
static inline uint16_t wavegen_float_to_u12(float x)
{
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;   /* FIX: clamp correcto antes de la conversión */
    return (uint16_t)((x + 1.0f) * 2047.5f);
}

#endif /* ATLASLIBRARIES_HEADERS_MCUIL_WAVEGEN_H_ */
