/*
 * mcuil_adsr.h
 *
 *  Created on: May 2026
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Envolvente ADSR (Attack – Decay – Sustain – Release) lineal.
 *
 *  Diseño orientado a eficiencia en STM32H753ZI (Cortex-M7 + FPU):
 *
 *  1. PRECOMPUTACIÓN DE RATES:
 *     Los incrementos por muestra (attack_rate, decay_rate) se calculan
 *     una sola vez en cada llamada a adsr_set_*() o adsr_set_all().
 *     El hot-path de audio (adsr_process_sample) ejecuta únicamente:
 *         output += rate;          → 1 FADD
 *         comparación de estado;   → 1 FCMP + 1 branch
 *     En el Cortex-M7, ambas operaciones tienen latencia de 1 ciclo.
 *
 *  2. RELEASE DESDE NIVEL ACTUAL:
 *     adsr_note_off() calcula release_rate en el momento de soltar la nota,
 *     partiendo del nivel actual. Esto asegura que el tiempo de release sea
 *     constante sin importar en qué estado se encontraba la envolvente.
 *
 *  3. RETRIGGER SIN CLICK:
 *     adsr_note_on() parte del nivel de salida actual hacia 1.0, evitando
 *     la discontinuidad que produciría reiniciar desde 0.
 *
 *  4. PROCESAMIENTO EN BLOQUE:
 *     adsr_process_block()  → escribe la envolvente en un buffer de floats.
 *     adsr_apply_block()    → multiplica la envolvente directamente sobre
 *                             un buffer de audio (patrón muy común en DSP).
 *     Ambas funciones son más eficientes que llamar adsr_process_sample()
 *     individualmente por el mejor aprovechamiento del pipeline del M7.
 *
 *  Uso básico:
 *  @code
 *      adsr_t env;
 *      adsr_init(&env, 44100.0f);
 *      adsr_set_all(&env, 0.01f, 0.1f, 0.7f, 0.2f);  // A D S R
 *
 *      adsr_note_on(&env);
 *      // ... generar muestras de audio ...
 *      adsr_note_off(&env);
 *      // ... continuar hasta que adsr_is_active() retorne false ...
 *  @endcode
 */

#ifndef MCUIL_ADSR_H_
#define MCUIL_ADSR_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Constantes configurables en tiempo de compilación
 * --------------------------------------------------------------------- */

/** Tiempo mínimo por etapa (segundos). Evita división por cero. */
#ifndef ADSR_MIN_TIME_S
#define ADSR_MIN_TIME_S   0.001f    /* 1 ms */
#endif

/** Tiempo máximo por etapa (segundos). */
#ifndef ADSR_MAX_TIME_S
#define ADSR_MAX_TIME_S  60.0f
#endif

/* -----------------------------------------------------------------------
 * Tipos
 * --------------------------------------------------------------------- */

/** Estados internos de la máquina de estados ADSR. */
typedef enum {
    ADSR_IDLE    = 0,   /**< Silencio — no produce salida */
    ADSR_ATTACK  = 1,   /**< Rampa ascendente hacia 1.0 */
    ADSR_DECAY   = 2,   /**< Rampa descendente hacia sustain_level */
    ADSR_SUSTAIN = 3,   /**< Nivel fijo mientras la nota está activa */
    ADSR_RELEASE = 4    /**< Rampa descendente hacia 0.0 tras soltar la nota */
} adsr_state_t;

/**
 * Descriptor de la envolvente ADSR.
 * Inicializar siempre con adsr_init() antes de cualquier uso.
 */
typedef struct {
    /* -- Parámetros de usuario ---------------------------------------- */
    float attack_s;       /**< Duración del ataque en segundos */
    float decay_s;        /**< Duración del decay en segundos */
    float sustain_level;  /**< Nivel de sustain [0.0, 1.0] */
    float release_s;      /**< Duración del release en segundos */
    float sample_rate;    /**< Frecuencia de muestreo en Hz */

    /* -- Rates precomputados (NO modificar directamente) -------------- */
    float attack_rate;    /**< Incremento por muestra en ATTACK */
    float decay_rate;     /**< Incremento por muestra en DECAY (negativo) */
    float release_rate;   /**< Incremento por muestra en RELEASE (negativo,
                               calculado en adsr_note_off()) */

    /* -- Estado en tiempo de ejecución -------------------------------- */
    adsr_state_t state;   /**< Estado actual de la máquina */
    float        output;  /**< Nivel actual de la envolvente [0.0, 1.0] */
} adsr_t;

/* -----------------------------------------------------------------------
 * Inicialización
 * --------------------------------------------------------------------- */

/**
 * @brief  Inicializa la envolvente con parámetros por defecto.
 *         Attack = 10 ms, Decay = 100 ms, Sustain = 0.7, Release = 200 ms.
 * @param  env          Puntero a la estructura adsr_t.
 * @param  sample_rate  Frecuencia de muestreo en Hz (< 1000 Hz → usa 44100).
 */
void adsr_init(adsr_t *env, float sample_rate);

/* -----------------------------------------------------------------------
 * Setters de parámetros
 * Cada setter recalcula sólo el rate afectado → O(1), sin ramificaciones.
 * --------------------------------------------------------------------- */

/** Configura el tiempo de ataque (segundos). */
void adsr_set_attack (adsr_t *env, float seconds);

/** Configura el tiempo de decay (segundos). */
void adsr_set_decay  (adsr_t *env, float seconds);

/**
 * Configura el nivel de sustain [0.0, 1.0].
 * Recalcula también decay_rate porque depende de sustain_level.
 */
void adsr_set_sustain(adsr_t *env, float level);

/** Configura el tiempo de release (segundos). */
void adsr_set_release(adsr_t *env, float seconds);

/**
 * @brief  Configura los cuatro parámetros en una sola llamada.
 *         Recalcula todos los rates de una vez (más eficiente que cuatro
 *         llamadas individuales si se configuran todos a la vez).
 */
void adsr_set_all(adsr_t *env,
                  float attack_s, float decay_s,
                  float sustain_level, float release_s);

/**
 * @brief  Actualiza la frecuencia de muestreo y recalcula todos los rates.
 *         Útil si el sistema cambia de sample rate en tiempo de ejecución.
 */
void adsr_set_sample_rate(adsr_t *env, float sample_rate);

/* -----------------------------------------------------------------------
 * Control de nota
 * --------------------------------------------------------------------- */

/**
 * @brief  Activa el ataque (note-on).
 *         Si la nota se activa mientras otra envolvente está activa
 *         (retrigger), el ataque parte del nivel actual hacia 1.0,
 *         evitando el click de reiniciar desde cero.
 */
void adsr_note_on (adsr_t *env);

/**
 * @brief  Inicia el release (note-off).
 *         El rate de release se calcula desde el nivel actual, por lo que
 *         el tiempo de release es constante independientemente del estado.
 */
void adsr_note_off(adsr_t *env);

/**
 * @brief  Silencia la envolvente inmediatamente (hard reset).
 *         Útil para panic o cambio abrupto de parche.
 */
void adsr_reset(adsr_t *env);

/* -----------------------------------------------------------------------
 * Procesamiento — hot-path de audio
 * --------------------------------------------------------------------- */

/**
 * @brief  Genera y retorna el siguiente valor de envolvente [0.0, 1.0],
 *         avanzando la máquina de estados.
 *         Hot-path: ~2 operaciones FPU + 1 branch por muestra.
 * @return Nivel actual de la envolvente.
 */
float adsr_process_sample(adsr_t *env);

/**
 * @brief  Escribe `num_samples` valores de envolvente en `buffer`.
 *         El buffer debe estar pre-asignado.
 */
void adsr_process_block(adsr_t *env, float *buffer, size_t num_samples);

/**
 * @brief  Multiplica la envolvente directamente sobre un buffer de audio.
 *         Equivale a: audio_buffer[i] *= adsr_process_sample(env);
 *         Patrón más común en síntesis: aplica la forma de amplitud
 *         sin necesitar un buffer intermedio.
 */
void adsr_apply_block(adsr_t *env, float *audio_buffer, size_t num_samples);

/* -----------------------------------------------------------------------
 * Utilidades inline (coste cero en tiempo de ejecución)
 * --------------------------------------------------------------------- */

/** Retorna el estado actual de la envolvente. */
static inline adsr_state_t adsr_get_state(const adsr_t *env)
{
    return env->state;
}

/** Retorna el nivel de salida actual sin avanzar la envolvente. */
static inline float adsr_get_output(const adsr_t *env)
{
    return env->output;
}

/**
 * @brief  Retorna true si la envolvente está produciendo señal.
 *         Usar para saber cuándo un canal de voz puede liberarse.
 */
static inline bool adsr_is_active(const adsr_t *env)
{
    return env->state != ADSR_IDLE;
}

/**
 * @brief  Retorna true si la nota sigue activa (antes del note-off).
 *         Útil para lógica de legato.
 */
static inline bool adsr_is_held(const adsr_t *env)
{
    return (env->state == ADSR_ATTACK  ||
            env->state == ADSR_DECAY   ||
            env->state == ADSR_SUSTAIN);
}

#endif /* MCUIL_ADSR_H_ */
