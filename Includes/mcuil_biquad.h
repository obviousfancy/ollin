/*
 * mcuil_biquad.h
 *
 *  Created on: May 2026
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Filtro IIR Biquad de segundo orden — bloque fundamental del DSP de audio.
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  ¿QUÉ ES UN BIQUAD?
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  Un filtro Biquad ("bi-cuadrático") implementa la siguiente ecuación
 *  en diferencias sobre cada muestra de audio:
 *
 *      y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2]
 *                      - a1·y[n-1] - a2·y[n-2]
 *
 *  donde x[n] es la entrada e y[n] la salida.
 *  Los cinco coeficientes {b0, b1, b2, a1, a2} determinan el
 *  comportamiento del filtro. Cambiando sólo esos coeficientes —
 *  con exactamente la misma estructura de código — se obtienen:
 *      • Low-pass (LP)        • High-pass (HP)
 *      • Band-pass (BP)       • Notch (rechaza banda)
 *      • All-pass (fase)      • Peak EQ (banda paramétrica)
 *      • Low shelf            • High shelf
 *
 *  Un ecualizador paramétrico de N bandas son simplemente N biquads
 *  en cascada — ver biquad_cascade_t al final de este archivo.
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  IMPLEMENTACIÓN: Direct Form II Transpuesta
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  De las cuatro formas canónicas, la forma II transpuesta es la más
 *  eficiente y la más estable numéricamente en aritmética de punto
 *  flotante de precisión simple (la FPU del Cortex-M7 es SP):
 *
 *      y    = b0·x + s1        ← muestra de salida
 *      s1   = b1·x - a1·y + s2 ← estado 1 (delay de 1 muestra)
 *      s2   = b2·x - a2·y      ← estado 2 (delay de 2 muestras)
 *
 *  Costo en CPU: 5 FMUL + 4 FADD = 9 operaciones FPU por muestra.
 *  En Cortex-M7 @ 480 MHz con FPU single-precision: ~5-7 ciclos/muestra.
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  CÁLCULO DE COEFICIENTES: ¿por qué sinf/cosf y NO una LUT?
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  Las funciones biquad_set_*() llaman a sinf() y cosf() para calcular
 *  los coeficientes. Estas funciones son costosas (~20-60 ciclos cada una),
 *  pero se llaman a "tasa de control", NO a tasa de audio:
 *
 *      Tasa de audio:   44 100 muestras/s  → wavegen usa LUT (correcto)
 *      Tasa de control: ~100–344 veces/s   → sinf/cosf es OK
 *
 *  A 344 recálculos/s × 60 ciclos = 20 640 ciclos/s sobre 480 000 000
 *  disponibles = 0.004% del CPU.  Una LUT ahorraría ese 0.004% a cambio
 *  de perder precisión en los coeficientes y degradar la calidad del
 *  filtro.  El trade-off no vale la pena para la capa de parámetros.
 *
 *  Para controles en tiempo real (potenciómetro, encoder, ADC) que pueden
 *  generar "zipper noise" (clicks por cambios abruptos), usar mcuil_smooth.h
 *  para suavizar el parámetro ANTES de pasarlo a biquad_set_*().
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  USO BÁSICO
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  @code
 *  biquad_t lpf;
 *  biquad_init(&lpf, 44100.0f);
 *  biquad_set_lowpass(&lpf, 1000.0f, 0.707f);   // Butterworth 1kHz
 *
 *  // En el callback de audio (por bloque):
 *  biquad_process_block(&lpf, input, output, 128);
 *
 *  // Control en tiempo real (desde ADC/encoder, fuera del callback):
 *  smooth_t fc_smooth;
 *  smooth_init(&fc_smooth, 44100.0f / 128, 0.005f);  // tau 5ms @ tasa bloque
 *  float fc = smooth_process(&fc_smooth, adc_to_hz(adc_read()));
 *  biquad_set_lowpass(&lpf, fc, 0.707f);
 *  @endcode
 */

#ifndef MCUIL_BIQUAD_H_
#define MCUIL_BIQUAD_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Constante de configuración
 * --------------------------------------------------------------------- */

/** Número máximo de etapas en una cascada de biquads (EQ N bandas). */
#ifndef BIQUAD_CASCADE_MAX_STAGES
#define BIQUAD_CASCADE_MAX_STAGES  8
#endif

/* -----------------------------------------------------------------------
 * Tipos de filtro
 * --------------------------------------------------------------------- */

typedef enum {
    BIQUAD_LOWPASS   = 0,  /**< Paso bajas    — parámetros: fc, Q              */
    BIQUAD_HIGHPASS  = 1,  /**< Paso altas    — parámetros: fc, Q              */
    BIQUAD_BANDPASS  = 2,  /**< Paso banda    — parámetros: fc, Q              */
    BIQUAD_NOTCH     = 3,  /**< Rechaza banda — parámetros: fc, Q              */
    BIQUAD_ALLPASS   = 4,  /**< Todo-paso     — parámetros: fc, Q (solo fase)  */
    BIQUAD_PEAK      = 5,  /**< Pico EQ       — parámetros: fc, Q, gain_db     */
    BIQUAD_LOWSHELF  = 6,  /**< Shelf bajas   — parámetros: fc, Q, gain_db     */
    BIQUAD_HIGHSHELF = 7   /**< Shelf altas   — parámetros: fc, Q, gain_db     */
} biquad_type_t;

/* -----------------------------------------------------------------------
 * Estructura principal
 * --------------------------------------------------------------------- */

/**
 * Biquad de segundo orden — Direct Form II Transpuesta.
 * Inicializar con biquad_init() antes de cualquier uso.
 */
typedef struct {
    /* -- Coeficientes (a0 ya dividido — listos para el hot-path) ----- */
    float b0, b1, b2;  /**< Numerador   */
    float a1, a2;      /**< Denominador (a0 normalizado a 1) */

    /* -- Estado interno (Direct Form II Transpuesta) ----------------- */
    float s1;          /**< Registro de estado 1 */
    float s2;          /**< Registro de estado 2 */

    /* -- Parámetros guardados (para recálculo y lectura) ------------- */
    float frequency;   /**< Frecuencia de corte/central en Hz */
    float Q;           /**< Factor de calidad / resonancia    */
    float gain_db;     /**< Ganancia en dB (sólo peak/shelf)  */
    float sample_rate; /**< Frecuencia de muestreo en Hz      */
    biquad_type_t type;
} biquad_t;

/* -----------------------------------------------------------------------
 * Estructura de cascada (EQ paramétrico de N bandas)
 * --------------------------------------------------------------------- */

/**
 * Cascada de hasta BIQUAD_CASCADE_MAX_STAGES biquads en serie.
 * Cada etapa es un biquad independiente con sus propios parámetros.
 * Uso: ecualizador paramétrico, crossover multibanda, filtro de orden N.
 */
typedef struct {
    biquad_t stages[BIQUAD_CASCADE_MAX_STAGES];
    uint8_t  count;   /**< Número de etapas activas [0, BIQUAD_CASCADE_MAX_STAGES] */
} biquad_cascade_t;

/* ═══════════════════════════════════════════════════════════════════════
 * API PÚBLICA
 * ═══════════════════════════════════════════════════════════════════════ */

/* -----------------------------------------------------------------------
 * Inicialización
 * --------------------------------------------------------------------- */

/**
 * @brief  Inicializa el filtro como bypass (pass-through sin filtrado).
 *         Defaults: LP a 1 kHz, Q = 0.707, sample_rate según parámetro.
 * @param  bq           Puntero al biquad.
 * @param  sample_rate  Frecuencia de muestreo en Hz.
 */
void biquad_init(biquad_t *bq, float sample_rate);

/**
 * @brief  Reinicia los registros de estado sin cambiar los coeficientes.
 *         Útil para evitar artefactos al procesar señales discontinuas.
 */
void biquad_flush(biquad_t *bq);

/* -----------------------------------------------------------------------
 * Configuración por tipo de filtro
 * Cada función calcula y almacena los coeficientes.
 * Coeficientes basados en: R. Bristow-Johnson, "Audio EQ Cookbook".
 * --------------------------------------------------------------------- */

/**
 * @brief  Low-pass (paso bajas).
 * @param  bq   Puntero al biquad.
 * @param  fc   Frecuencia de corte en Hz. Rango recomendado: [20, fs/2).
 * @param  Q    Factor Q. Q = 0.707 = Butterworth (sin resonancia).
 *              Q > 1 añade resonancia en fc.
 */
void biquad_set_lowpass(biquad_t *bq, float fc, float Q);

/**
 * @brief  High-pass (paso altas).
 * @param  fc  Frecuencia de corte en Hz.
 * @param  Q   Factor Q. Q = 0.707 = Butterworth.
 */
void biquad_set_highpass(biquad_t *bq, float fc, float Q);

/**
 * @brief  Band-pass (paso banda), ganancia unitaria en fc.
 * @param  fc  Frecuencia central en Hz.
 * @param  Q   Ancho de banda: BW = fc / Q en Hz.
 */
void biquad_set_bandpass(biquad_t *bq, float fc, float Q);

/**
 * @brief  Notch (rechaza banda / Band-reject).
 * @param  fc  Frecuencia de rechazo en Hz.
 * @param  Q   Selectividad: Q alto = banda de rechazo estrecha.
 */
void biquad_set_notch(biquad_t *bq, float fc, float Q);

/**
 * @brief  All-pass (todo-paso): pasa toda la energía, sólo altera la fase.
 *         Util para alinear fases entre vías de un crossover.
 * @param  fc  Frecuencia de 90° de desfase en Hz.
 * @param  Q   Factor Q.
 */
void biquad_set_allpass(biquad_t *bq, float fc, float Q);

/**
 * @brief  Peak / Peaking EQ (banda de ecualización paramétrica).
 *         Amplifica o atenúa una banda centrada en fc.
 * @param  fc       Frecuencia central en Hz.
 * @param  Q        Selectividad de la banda.
 * @param  gain_db  Ganancia en dB. Positivo = boost, negativo = cut.
 *                  0 dB = bypass.
 */
void biquad_set_peak(biquad_t *bq, float fc, float Q, float gain_db);

/**
 * @brief  Low shelf (estante de graves).
 *         Amplifica o atenúa todas las frecuencias por debajo de fc.
 * @param  fc       Frecuencia de corte del shelf en Hz.
 * @param  Q        Pendiente de transición (~0.707 = pendiente suave).
 * @param  gain_db  Ganancia aplicada a la región de graves.
 */
void biquad_set_lowshelf(biquad_t *bq, float fc, float Q, float gain_db);

/**
 * @brief  High shelf (estante de agudos).
 *         Amplifica o atenúa todas las frecuencias por encima de fc.
 * @param  fc       Frecuencia de corte del shelf en Hz.
 * @param  Q        Pendiente de transición.
 * @param  gain_db  Ganancia aplicada a la región de agudos.
 */
void biquad_set_highshelf(biquad_t *bq, float fc, float Q, float gain_db);

/**
 * @brief  Configura coeficientes directamente (para diseño externo).
 *         Permite usar coeficientes calculados con MATLAB, Python/scipy,
 *         o cualquier herramienta de diseño de filtros.
 *         Los coeficientes deben estar normalizados (a0 = 1 ya dividido).
 */
void biquad_set_coefficients(biquad_t *bq,
                              float b0, float b1, float b2,
                              float a1, float a2);

/**
 * @brief  Recalcula los coeficientes con los parámetros almacenados.
 *         Útil si se cambió sample_rate con biquad_set_sample_rate().
 */
void biquad_update_coefficients(biquad_t *bq);

/**
 * @brief  Actualiza la frecuencia de muestreo y recalcula coeficientes.
 */
void biquad_set_sample_rate(biquad_t *bq, float sample_rate);

/* -----------------------------------------------------------------------
 * Procesamiento — hot-path de audio
 * Implementado como inline para máximo rendimiento.
 * --------------------------------------------------------------------- */

/**
 * @brief  Procesa una muestra. Hot-path: 5 FMUL + 4 FADD = 9 ops FPU.
 *         Direct Form II Transpuesta.
 */
static inline float biquad_process_sample(biquad_t *bq, float x)
{
    float y = bq->b0 * x + bq->s1;
    bq->s1  = bq->b1 * x - bq->a1 * y + bq->s2;
    bq->s2  = bq->b2 * x - bq->a2 * y;
    return y;
}

/**
 * @brief  Procesa un bloque de muestras in-place.
 *         Si in == out, el procesamiento es in-place (común en DSP).
 * @param  bq           Puntero al biquad.
 * @param  in           Buffer de entrada.
 * @param  out          Buffer de salida (puede ser igual a in).
 * @param  num_samples  Número de muestras.
 */
void biquad_process_block(biquad_t *bq,
                          const float *in, float *out,
                          size_t num_samples);

/* -----------------------------------------------------------------------
 * Cascada de Biquads — EQ paramétrico de N bandas
 * --------------------------------------------------------------------- */

/** Inicializa la cascada (0 etapas activas). */
void biquad_cascade_init(biquad_cascade_t *casc, float sample_rate);

/**
 * @brief  Añade una etapa a la cascada.
 * @return Puntero a la etapa añadida, o NULL si la cascada está llena.
 *         El llamador puede configurar la etapa con biquad_set_*().
 */
biquad_t* biquad_cascade_add_stage(biquad_cascade_t *casc);

/**
 * @brief  Retorna el puntero a la etapa i (para reconfigurar en tiempo real).
 * @return NULL si i >= count.
 */
biquad_t* biquad_cascade_get_stage(biquad_cascade_t *casc, uint8_t i);

/** Reinicia los estados de todas las etapas sin cambiar coeficientes. */
void biquad_cascade_flush(biquad_cascade_t *casc);

/**
 * @brief  Procesa una muestra a través de todas las etapas en serie.
 *         Hot-path: 9 ops FPU × count etapas.
 */
static inline float biquad_cascade_process_sample(biquad_cascade_t *casc,
                                                   float x)
{
    float y = x;
    for (uint8_t i = 0; i < casc->count; ++i) {
        y = biquad_process_sample(&casc->stages[i], y);
    }
    return y;
}

/**
 * @brief  Procesa un bloque a través de la cascada.
 *         Si in == out, el procesamiento es in-place.
 */
void biquad_cascade_process_block(biquad_cascade_t *casc,
                                   const float *in, float *out,
                                   size_t num_samples);

/* -----------------------------------------------------------------------
 * Utilidades de consulta
 * --------------------------------------------------------------------- */

/** Retorna la frecuencia de corte/central almacenada. */
static inline float biquad_get_frequency  (const biquad_t *bq) { return bq->frequency; }
/** Retorna el Q almacenado. */
static inline float biquad_get_Q          (const biquad_t *bq) { return bq->Q; }
/** Retorna la ganancia en dB almacenada. */
static inline float biquad_get_gain_db    (const biquad_t *bq) { return bq->gain_db; }
/** Retorna el tipo de filtro activo. */
static inline biquad_type_t biquad_get_type(const biquad_t *bq) { return bq->type; }

#endif /* MCUIL_BIQUAD_H_ */
