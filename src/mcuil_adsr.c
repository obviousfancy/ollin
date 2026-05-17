/*
 * mcuil_adsr.c
 *
 *  Created on: May 2026
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Envolvente ADSR lineal optimizada para Cortex-M7 + FPU.
 *
 *  Análisis de ciclos de CPU en el hot-path (adsr_process_sample):
 *
 *  Estado ATTACK / DECAY / RELEASE:
 *      FADD   output += rate          → 1 ciclo (FPU pipeline M7)
 *      FCMP   output >= threshold     → 1 ciclo
 *      IT/B   branch si condición     → 1 ciclo (predecible)
 *      Total: ~3 ciclos / muestra
 *
 *  Estado SUSTAIN:
 *      MOV    output = sustain_level  → 1 ciclo (registro FPU)
 *      Total: ~1 ciclo / muestra
 *
 *  Estado IDLE:
 *      CMP    + return 0.0f           → 2 ciclos
 *
 *  La fase de transición (cuando output alcanza el umbral) añade
 *  ~3 ciclos extra para actualizar el estado y recompute puntual,
 *  pero ocurre exactamente UNA vez por etapa, no por muestra.
 */

#include "mcuil_adsr.h"

/* -----------------------------------------------------------------------
 * Helper interno: clamp float
 * Inline → el compilador lo sustituye en sitio, sin overhead de llamada.
 * --------------------------------------------------------------------- */

static inline float _clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* -----------------------------------------------------------------------
 * Recomputación de todos los rates
 * Llamada sólo en tiempo de configuración — no en hot-path.
 * --------------------------------------------------------------------- */

static void _adsr_recompute_all(adsr_t *env)
{
    float inv_sr = 1.0f / env->sample_rate; /* divisor constante */

    env->attack_rate = 1.0f / (env->attack_s * env->sample_rate);
    env->decay_rate  = (env->sustain_level - 1.0f) /
                       (env->decay_s * env->sample_rate);
    (void)inv_sr; /* guardado para uso futuro si se añaden más etapas */
    /* release_rate se calcula en adsr_note_off() porque depende de
     * env->output en ese instante */
}

/* -----------------------------------------------------------------------
 * Inicialización
 * --------------------------------------------------------------------- */

void adsr_init(adsr_t *env, float sample_rate)
{
    if (!env) return;

    env->sample_rate   = (sample_rate > 1000.0f) ? sample_rate : 44100.0f;
    env->attack_s      = 0.010f;   /* 10 ms  */
    env->decay_s       = 0.100f;   /* 100 ms */
    env->sustain_level = 0.700f;   /* 70%    */
    env->release_s     = 0.200f;   /* 200 ms */
    env->state         = ADSR_IDLE;
    env->output        = 0.0f;
    env->release_rate  = 0.0f;

    _adsr_recompute_all(env);
}

/* -----------------------------------------------------------------------
 * Setters
 * --------------------------------------------------------------------- */

void adsr_set_attack(adsr_t *env, float seconds)
{
    if (!env) return;
    env->attack_s    = _clampf(seconds, ADSR_MIN_TIME_S, ADSR_MAX_TIME_S);
    env->attack_rate = 1.0f / (env->attack_s * env->sample_rate);
}

void adsr_set_decay(adsr_t *env, float seconds)
{
    if (!env) return;
    env->decay_s    = _clampf(seconds, ADSR_MIN_TIME_S, ADSR_MAX_TIME_S);
    env->decay_rate = (env->sustain_level - 1.0f) /
                      (env->decay_s * env->sample_rate);
}

void adsr_set_sustain(adsr_t *env, float level)
{
    if (!env) return;
    env->sustain_level = _clampf(level, 0.0f, 1.0f);
    /* decay_rate depende de sustain_level: recalcular */
    env->decay_rate = (env->sustain_level - 1.0f) /
                      (env->decay_s * env->sample_rate);
}

void adsr_set_release(adsr_t *env, float seconds)
{
    if (!env) return;
    env->release_s = _clampf(seconds, ADSR_MIN_TIME_S, ADSR_MAX_TIME_S);
    /* release_rate se calcula en adsr_note_off(); no hay nada que precalcular aquí */
}

void adsr_set_all(adsr_t *env,
                  float attack_s, float decay_s,
                  float sustain_level, float release_s)
{
    if (!env) return;
    env->attack_s      = _clampf(attack_s,      ADSR_MIN_TIME_S, ADSR_MAX_TIME_S);
    env->decay_s       = _clampf(decay_s,        ADSR_MIN_TIME_S, ADSR_MAX_TIME_S);
    env->sustain_level = _clampf(sustain_level,  0.0f, 1.0f);
    env->release_s     = _clampf(release_s,      ADSR_MIN_TIME_S, ADSR_MAX_TIME_S);
    _adsr_recompute_all(env);
}

void adsr_set_sample_rate(adsr_t *env, float sample_rate)
{
    if (!env) return;
    env->sample_rate = (sample_rate > 1000.0f) ? sample_rate : 44100.0f;
    _adsr_recompute_all(env);
    /* release_rate es dependiente del estado actual; se recalculará en
     * el próximo adsr_note_off() — no hay inconsistencia crítica aquí */
}

/* -----------------------------------------------------------------------
 * Control de nota
 * --------------------------------------------------------------------- */

void adsr_note_on(adsr_t *env)
{
    if (!env) return;

    /* Si el nivel ya alcanzó el pico, saltar directo a DECAY (sin
     * permanecer en ATTACK con rate = 0, lo que bloquearía la FSM). */
    if (env->output >= 1.0f) {
        env->output = 1.0f;
        env->state  = ADSR_DECAY;
        /* Decay completo desde 1.0 */
        env->decay_rate = (env->sustain_level - 1.0f) /
                          (env->decay_s * env->sample_rate);
        return;
    }

    /* Retrigger: partir del nivel actual hacia 1.0.
     * Esto preserva la continuidad de la señal y evita clicks.
     * El tiempo de ataque es proporcional a la distancia restante. */
    float remaining = 1.0f - env->output;
    env->attack_rate = remaining / (env->attack_s * env->sample_rate);
    env->state = ADSR_ATTACK;
}

void adsr_note_off(adsr_t *env)
{
    if (!env) return;
    if (env->state == ADSR_IDLE) return; /* ya silenciado */

    float level = env->output;
    if (level <= 0.0f) {
        /* Nivel ya en cero: transición directa a IDLE */
        env->output = 0.0f;
        env->state  = ADSR_IDLE;
        return;
    }

    /* Release desde el nivel actual: tiempo constante independientemente
     * del estado previo (sosteniendo en ATTACK, DECAY o SUSTAIN). */
    env->release_rate = -level / (env->release_s * env->sample_rate);
    env->state = ADSR_RELEASE;
}

void adsr_reset(adsr_t *env)
{
    if (!env) return;
    env->state  = ADSR_IDLE;
    env->output = 0.0f;
}

/* -----------------------------------------------------------------------
 * Hot-path: adsr_process_sample
 *
 * Diagrama de transiciones:
 *
 *   IDLE ──note_on──► ATTACK ──output≥1.0──► DECAY ──output≤sustain──► SUSTAIN
 *     ▲                 │                      │                          │
 *     │                 │                      │                          │
 *     └──output≤0.0──── RELEASE ◄──note_off────┴──────────────────────────┘
 *
 * --------------------------------------------------------------------- */

float adsr_process_sample(adsr_t *env)
{
    switch (env->state) {

        /* ---- ATTACK ------------------------------------------------- */
        case ADSR_ATTACK:
            env->output += env->attack_rate;
            if (env->output >= 1.0f) {
                env->output = 1.0f;
                env->state  = ADSR_DECAY;
                /* Recompute decay_rate completo (desde 1.0 al sustain_level)
                 * para mantener el tiempo de decay correcto tras un retrigger
                 * que pudo haber modificado el attack_rate parcialmente. */
                env->decay_rate = (env->sustain_level - 1.0f) /
                                  (env->decay_s * env->sample_rate);
            }
            return env->output;

        /* ---- DECAY -------------------------------------------------- */
        case ADSR_DECAY:
            env->output += env->decay_rate;
            if (env->output <= env->sustain_level) {
                env->output = env->sustain_level;
                env->state  = ADSR_SUSTAIN;
            }
            return env->output;

        /* ---- SUSTAIN ------------------------------------------------ */
        case ADSR_SUSTAIN:
            /* Sin aritmética: el nivel es fijo. */
            env->output = env->sustain_level;
            return env->output;

        /* ---- RELEASE ------------------------------------------------ */
        case ADSR_RELEASE:
            env->output += env->release_rate;
            if (env->output <= 0.0f) {
                env->output = 0.0f;
                env->state  = ADSR_IDLE;
            }
            return env->output;

        /* ---- IDLE --------------------------------------------------- */
        case ADSR_IDLE:
        default:
            return 0.0f;
    }
}

/* -----------------------------------------------------------------------
 * Procesamiento en bloque
 * --------------------------------------------------------------------- */

void adsr_process_block(adsr_t *env, float *buffer, size_t num_samples)
{
    if (!env || !buffer) return;
    for (size_t i = 0; i < num_samples; ++i) {
        buffer[i] = adsr_process_sample(env);
    }
}

/*
 * adsr_apply_block — combina generación y multiplicación en un solo bucle.
 *
 * Esto es más eficiente que:
 *     adsr_process_block(env, env_buf, n);
 *     for (i=0; i<n; i++) audio[i] *= env_buf[i];
 *
 * porque elimina el buffer intermedio, reduciendo accesos a memoria
 * y aprovechando mejor el pipeline load-use del Cortex-M7.
 */
void adsr_apply_block(adsr_t *env, float *audio_buffer, size_t num_samples)
{
    if (!env || !audio_buffer) return;
    for (size_t i = 0; i < num_samples; ++i) {
        audio_buffer[i] *= adsr_process_sample(env);
    }
}
