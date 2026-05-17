/*
 * mcuil_biquad.c
 *
 *  Created on: May 2026
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Implementación del filtro Biquad IIR — Direct Form II Transpuesta.
 *
 *  Fórmulas de coeficientes:
 *  Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter
 *  coefficients", versión 1.51 (dominio público, referencia de la industria).
 *
 *  Todas las funciones biquad_set_*() siguen el mismo patrón:
 *   1. Clamp de parámetros (evita NaN/Inf en los coeficientes)
 *   2. Cálculo de omega0 = 2π·fc/fs
 *   3. Cálculo de sin(omega0), cos(omega0), alpha
 *   4. Cálculo de {b0,b1,b2,a0,a1,a2}
 *   5. Normalización dividiendo todo por a0
 *   6. Guardado de parámetros para recálculo futuro
 *
 *  sinf/cosf se llaman a tasa de CONTROL, no de AUDIO (ver mcuil_biquad.h).
 */

#include "mcuil_biquad.h"
#include <math.h>

/* -----------------------------------------------------------------------
 * Constantes internas
 * --------------------------------------------------------------------- */

#ifndef M_PI_F
#define M_PI_F  3.14159265358979323846f
#endif

#define TWO_PI_F  (2.0f * M_PI_F)

/* Límites de seguridad para parámetros */
#define BIQUAD_FC_MIN     1.0f      /* 1 Hz mínimo de frecuencia de corte */
#define BIQUAD_Q_MIN      0.1f      /* Q mínimo (evita divisiones near-zero) */
#define BIQUAD_Q_MAX    100.0f
#define BIQUAD_GAIN_MIN -60.0f     /* dB */
#define BIQUAD_GAIN_MAX  60.0f     /* dB */

/* -----------------------------------------------------------------------
 * Helpers internos
 * --------------------------------------------------------------------- */

static inline float _clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/**
 * Núcleo de normalización y almacenamiento de coeficientes.
 * Divide {b0,b1,b2,a1,a2} por a0 y escribe en la estructura.
 * Llamado por TODAS las funciones biquad_set_*() al final.
 */
static inline void _biquad_store(biquad_t *bq,
                                  float b0, float b1, float b2,
                                  float a0, float a1, float a2)
{
    float inv_a0 = 1.0f / a0;
    bq->b0 = b0 * inv_a0;
    bq->b1 = b1 * inv_a0;
    bq->b2 = b2 * inv_a0;
    bq->a1 = a1 * inv_a0;  /* nota: a1 ya dividido queda listo para el hot-path */
    bq->a2 = a2 * inv_a0;
}

/* -----------------------------------------------------------------------
 * Inicialización
 * --------------------------------------------------------------------- */

void biquad_init(biquad_t *bq, float sample_rate)
{
    if (!bq) return;
    bq->sample_rate = (sample_rate > 1000.0f) ? sample_rate : 44100.0f;
    bq->frequency   = 1000.0f;
    bq->Q           = 0.707f;
    bq->gain_db     = 0.0f;
    bq->type        = BIQUAD_LOWPASS;
    bq->s1 = bq->s2 = 0.0f;

    /* Inicializar como bypass — coeficientes de paso directo */
    bq->b0 = 1.0f; bq->b1 = 0.0f; bq->b2 = 0.0f;
    bq->a1 = 0.0f; bq->a2 = 0.0f;

    /* Calcular coeficientes reales del LP inicial */
    biquad_set_lowpass(bq, bq->frequency, bq->Q);
}

void biquad_flush(biquad_t *bq)
{
    if (!bq) return;
    bq->s1 = bq->s2 = 0.0f;
}

/* -----------------------------------------------------------------------
 * Low-pass
 *   b0 = (1 - cos0)/2
 *   b1 =  1 - cos0
 *   b2 = (1 - cos0)/2
 *   a0 =  1 + alpha
 *   a1 = -2·cos0
 *   a2 =  1 - alpha
 * --------------------------------------------------------------------- */

void biquad_set_lowpass(biquad_t *bq, float fc, float Q)
{
    if (!bq) return;
    fc = _clampf(fc, BIQUAD_FC_MIN, bq->sample_rate * 0.499f);
    Q  = _clampf(Q,  BIQUAD_Q_MIN,  BIQUAD_Q_MAX);

    float omega0 = TWO_PI_F * fc / bq->sample_rate;
    float sin0   = sinf(omega0);
    float cos0   = cosf(omega0);
    float alpha  = sin0 / (2.0f * Q);

    float b1 = 1.0f - cos0;
    _biquad_store(bq,
        b1 * 0.5f,          /* b0 */
        b1,                 /* b1 */
        b1 * 0.5f,          /* b2 */
        1.0f + alpha,       /* a0 */
        -2.0f * cos0,       /* a1 */
        1.0f - alpha        /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->type      = BIQUAD_LOWPASS;
}

/* -----------------------------------------------------------------------
 * High-pass
 *   b0 = (1 + cos0)/2
 *   b1 = -(1 + cos0)
 *   b2 = (1 + cos0)/2
 *   a0 =  1 + alpha
 *   a1 = -2·cos0
 *   a2 =  1 - alpha
 * --------------------------------------------------------------------- */

void biquad_set_highpass(biquad_t *bq, float fc, float Q)
{
    if (!bq) return;
    fc = _clampf(fc, BIQUAD_FC_MIN, bq->sample_rate * 0.499f);
    Q  = _clampf(Q,  BIQUAD_Q_MIN,  BIQUAD_Q_MAX);

    float omega0 = TWO_PI_F * fc / bq->sample_rate;
    float sin0   = sinf(omega0);
    float cos0   = cosf(omega0);
    float alpha  = sin0 / (2.0f * Q);

    float b1 = 1.0f + cos0;
    _biquad_store(bq,
         b1 *  0.5f,        /* b0 */
        -b1,                /* b1 */
         b1 *  0.5f,        /* b2 */
         1.0f + alpha,      /* a0 */
        -2.0f * cos0,       /* a1 */
         1.0f - alpha       /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->type      = BIQUAD_HIGHPASS;
}

/* -----------------------------------------------------------------------
 * Band-pass (ganancia unitaria en fc)
 *   b0 =  sin0/2
 *   b1 =  0
 *   b2 = -sin0/2
 *   a0 =  1 + alpha
 *   a1 = -2·cos0
 *   a2 =  1 - alpha
 * --------------------------------------------------------------------- */

void biquad_set_bandpass(biquad_t *bq, float fc, float Q)
{
    if (!bq) return;
    fc = _clampf(fc, BIQUAD_FC_MIN, bq->sample_rate * 0.499f);
    Q  = _clampf(Q,  BIQUAD_Q_MIN,  BIQUAD_Q_MAX);

    float omega0 = TWO_PI_F * fc / bq->sample_rate;
    float sin0   = sinf(omega0);
    float cos0   = cosf(omega0);
    float alpha  = sin0 / (2.0f * Q);
    float hs     = sin0 * 0.5f;

    _biquad_store(bq,
         hs,                /* b0 */
         0.0f,              /* b1 */
        -hs,                /* b2 */
         1.0f + alpha,      /* a0 */
        -2.0f * cos0,       /* a1 */
         1.0f - alpha       /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->type      = BIQUAD_BANDPASS;
}

/* -----------------------------------------------------------------------
 * Notch (band-reject)
 *   b0 =  1
 *   b1 = -2·cos0
 *   b2 =  1
 *   a0 =  1 + alpha
 *   a1 = -2·cos0
 *   a2 =  1 - alpha
 * --------------------------------------------------------------------- */

void biquad_set_notch(biquad_t *bq, float fc, float Q)
{
    if (!bq) return;
    fc = _clampf(fc, BIQUAD_FC_MIN, bq->sample_rate * 0.499f);
    Q  = _clampf(Q,  BIQUAD_Q_MIN,  BIQUAD_Q_MAX);

    float omega0  = TWO_PI_F * fc / bq->sample_rate;
    float sin0    = sinf(omega0);
    float cos0    = cosf(omega0);
    float alpha   = sin0 / (2.0f * Q);
    float neg2cos = -2.0f * cos0;

    _biquad_store(bq,
         1.0f,             /* b0 */
         neg2cos,          /* b1 */
         1.0f,             /* b2 */
         1.0f + alpha,     /* a0 */
         neg2cos,          /* a1 */
         1.0f - alpha      /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->type      = BIQUAD_NOTCH;
}

/* -----------------------------------------------------------------------
 * All-pass (todo-paso)
 *   b0 =  1 - alpha
 *   b1 = -2·cos0
 *   b2 =  1 + alpha
 *   a0 =  1 + alpha
 *   a1 = -2·cos0
 *   a2 =  1 - alpha
 * --------------------------------------------------------------------- */

void biquad_set_allpass(biquad_t *bq, float fc, float Q)
{
    if (!bq) return;
    fc = _clampf(fc, BIQUAD_FC_MIN, bq->sample_rate * 0.499f);
    Q  = _clampf(Q,  BIQUAD_Q_MIN,  BIQUAD_Q_MAX);

    float omega0  = TWO_PI_F * fc / bq->sample_rate;
    float sin0    = sinf(omega0);
    float cos0    = cosf(omega0);
    float alpha   = sin0 / (2.0f * Q);
    float neg2cos = -2.0f * cos0;

    _biquad_store(bq,
         1.0f - alpha,     /* b0 */
         neg2cos,          /* b1 */
         1.0f + alpha,     /* b2 */
         1.0f + alpha,     /* a0 */
         neg2cos,          /* a1 */
         1.0f - alpha      /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->type      = BIQUAD_ALLPASS;
}

/* -----------------------------------------------------------------------
 * Peak EQ (ecualización paramétrica)
 *   A  = sqrt(10^(dBgain/20))
 *   b0 =  1 + alpha·A
 *   b1 = -2·cos0
 *   b2 =  1 - alpha·A
 *   a0 =  1 + alpha/A
 *   a1 = -2·cos0
 *   a2 =  1 - alpha/A
 * --------------------------------------------------------------------- */

void biquad_set_peak(biquad_t *bq, float fc, float Q, float gain_db)
{
    if (!bq) return;
    fc      = _clampf(fc,      BIQUAD_FC_MIN,   bq->sample_rate * 0.499f);
    Q       = _clampf(Q,       BIQUAD_Q_MIN,    BIQUAD_Q_MAX);
    gain_db = _clampf(gain_db, BIQUAD_GAIN_MIN, BIQUAD_GAIN_MAX);

    float omega0  = TWO_PI_F * fc / bq->sample_rate;
    float sin0    = sinf(omega0);
    float cos0    = cosf(omega0);
    float alpha   = sin0 / (2.0f * Q);
    float A       = sqrtf(powf(10.0f, gain_db / 20.0f));
    float neg2cos = -2.0f * cos0;
    float alphaA  = alpha * A;
    float alphaOA = alpha / A;  /* alpha over A */

    _biquad_store(bq,
         1.0f + alphaA,    /* b0 */
         neg2cos,          /* b1 */
         1.0f - alphaA,    /* b2 */
         1.0f + alphaOA,   /* a0 */
         neg2cos,          /* a1 */
         1.0f - alphaOA    /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->gain_db   = gain_db;
    bq->type      = BIQUAD_PEAK;
}

/* -----------------------------------------------------------------------
 * Low shelf (estante de graves)
 *   A  = sqrt(10^(dBgain/20))
 *   b0 =    A·[(A+1)-(A-1)·cos0 + 2·sqrt(A)·alpha]
 *   b1 =  2·A·[(A-1)-(A+1)·cos0                  ]
 *   b2 =    A·[(A+1)-(A-1)·cos0 - 2·sqrt(A)·alpha]
 *   a0 =      [(A+1)+(A-1)·cos0 + 2·sqrt(A)·alpha]
 *   a1 =  -2·[(A-1)+(A+1)·cos0                   ]
 *   a2 =      [(A+1)+(A-1)·cos0 - 2·sqrt(A)·alpha]
 * --------------------------------------------------------------------- */

void biquad_set_lowshelf(biquad_t *bq, float fc, float Q, float gain_db)
{
    if (!bq) return;
    fc      = _clampf(fc,      BIQUAD_FC_MIN,   bq->sample_rate * 0.499f);
    Q       = _clampf(Q,       BIQUAD_Q_MIN,    BIQUAD_Q_MAX);
    gain_db = _clampf(gain_db, BIQUAD_GAIN_MIN, BIQUAD_GAIN_MAX);

    float omega0 = TWO_PI_F * fc / bq->sample_rate;
    float sin0   = sinf(omega0);
    float cos0   = cosf(omega0);
    float A      = sqrtf(powf(10.0f, gain_db / 20.0f));
    float sqrtA  = sqrtf(A);
    float alpha  = sin0 / (2.0f * Q);
    float twoSqrtAalpha = 2.0f * sqrtA * alpha;

    float Ap1    = A + 1.0f;
    float Am1    = A - 1.0f;
    float Ap1c   = Ap1 * cos0;
    float Am1c   = Am1 * cos0;

    _biquad_store(bq,
           A * (Ap1 - Am1c + twoSqrtAalpha),   /* b0 */
        2.0f*A * (Am1 - Ap1c),                  /* b1 */
           A * (Ap1 - Am1c - twoSqrtAalpha),   /* b2 */
               (Ap1 + Am1c + twoSqrtAalpha),   /* a0 */
       -2.0f * (Am1 + Ap1c),                   /* a1 */
               (Ap1 + Am1c - twoSqrtAalpha)    /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->gain_db   = gain_db;
    bq->type      = BIQUAD_LOWSHELF;
}

/* -----------------------------------------------------------------------
 * High shelf (estante de agudos)
 *   A  = sqrt(10^(dBgain/20))
 *   b0 =    A·[(A+1)+(A-1)·cos0 + 2·sqrt(A)·alpha]
 *   b1 = -2·A·[(A-1)+(A+1)·cos0                  ]
 *   b2 =    A·[(A+1)+(A-1)·cos0 - 2·sqrt(A)·alpha]
 *   a0 =      [(A+1)-(A-1)·cos0 + 2·sqrt(A)·alpha]
 *   a1 =   2·[(A-1)-(A+1)·cos0                   ]
 *   a2 =      [(A+1)-(A-1)·cos0 - 2·sqrt(A)·alpha]
 * --------------------------------------------------------------------- */

void biquad_set_highshelf(biquad_t *bq, float fc, float Q, float gain_db)
{
    if (!bq) return;
    fc      = _clampf(fc,      BIQUAD_FC_MIN,   bq->sample_rate * 0.499f);
    Q       = _clampf(Q,       BIQUAD_Q_MIN,    BIQUAD_Q_MAX);
    gain_db = _clampf(gain_db, BIQUAD_GAIN_MIN, BIQUAD_GAIN_MAX);

    float omega0 = TWO_PI_F * fc / bq->sample_rate;
    float sin0   = sinf(omega0);
    float cos0   = cosf(omega0);
    float A      = sqrtf(powf(10.0f, gain_db / 20.0f));
    float sqrtA  = sqrtf(A);
    float alpha  = sin0 / (2.0f * Q);
    float twoSqrtAalpha = 2.0f * sqrtA * alpha;

    float Ap1  = A + 1.0f;
    float Am1  = A - 1.0f;
    float Ap1c = Ap1 * cos0;
    float Am1c = Am1 * cos0;

    _biquad_store(bq,
           A * (Ap1 + Am1c + twoSqrtAalpha),   /* b0 */
       -2.0f*A * (Am1 + Ap1c),                  /* b1 */
           A * (Ap1 + Am1c - twoSqrtAalpha),   /* b2 */
               (Ap1 - Am1c + twoSqrtAalpha),   /* a0 */
        2.0f * (Am1 - Ap1c),                   /* a1 */
               (Ap1 - Am1c - twoSqrtAalpha)    /* a2 */
    );

    bq->frequency = fc;
    bq->Q         = Q;
    bq->gain_db   = gain_db;
    bq->type      = BIQUAD_HIGHSHELF;
}

/* -----------------------------------------------------------------------
 * Coeficientes directos y recálculo
 * --------------------------------------------------------------------- */

void biquad_set_coefficients(biquad_t *bq,
                              float b0, float b1, float b2,
                              float a1, float a2)
{
    if (!bq) return;
    bq->b0 = b0; bq->b1 = b1; bq->b2 = b2;
    bq->a1 = a1; bq->a2 = a2;
    /* Nota: a1/a2 ya normalizados (a0=1) */
}

void biquad_update_coefficients(biquad_t *bq)
{
    if (!bq) return;
    switch (bq->type) {
        case BIQUAD_LOWPASS:   biquad_set_lowpass  (bq, bq->frequency, bq->Q); break;
        case BIQUAD_HIGHPASS:  biquad_set_highpass  (bq, bq->frequency, bq->Q); break;
        case BIQUAD_BANDPASS:  biquad_set_bandpass  (bq, bq->frequency, bq->Q); break;
        case BIQUAD_NOTCH:     biquad_set_notch     (bq, bq->frequency, bq->Q); break;
        case BIQUAD_ALLPASS:   biquad_set_allpass   (bq, bq->frequency, bq->Q); break;
        case BIQUAD_PEAK:      biquad_set_peak      (bq, bq->frequency, bq->Q, bq->gain_db); break;
        case BIQUAD_LOWSHELF:  biquad_set_lowshelf  (bq, bq->frequency, bq->Q, bq->gain_db); break;
        case BIQUAD_HIGHSHELF: biquad_set_highshelf (bq, bq->frequency, bq->Q, bq->gain_db); break;
        default: break;
    }
}

void biquad_set_sample_rate(biquad_t *bq, float sample_rate)
{
    if (!bq) return;
    bq->sample_rate = (sample_rate > 1000.0f) ? sample_rate : 44100.0f;
    biquad_update_coefficients(bq);
}

/* -----------------------------------------------------------------------
 * Procesamiento por bloque
 * --------------------------------------------------------------------- */

void biquad_process_block(biquad_t *bq,
                          const float *in, float *out,
                          size_t num_samples)
{
    if (!bq || !in || !out) return;
    for (size_t i = 0; i < num_samples; ++i) {
        out[i] = biquad_process_sample(bq, in[i]);
    }
}

/* -----------------------------------------------------------------------
 * Cascada
 * --------------------------------------------------------------------- */

void biquad_cascade_init(biquad_cascade_t *casc, float sample_rate)
{
    if (!casc) return;
    casc->count = 0;
    for (uint8_t i = 0; i < BIQUAD_CASCADE_MAX_STAGES; ++i) {
        biquad_init(&casc->stages[i], sample_rate);
    }
}

biquad_t* biquad_cascade_add_stage(biquad_cascade_t *casc)
{
    if (!casc || casc->count >= BIQUAD_CASCADE_MAX_STAGES) return NULL;
    return &casc->stages[casc->count++];
}

biquad_t* biquad_cascade_get_stage(biquad_cascade_t *casc, uint8_t i)
{
    if (!casc || i >= casc->count) return NULL;
    return &casc->stages[i];
}

void biquad_cascade_flush(biquad_cascade_t *casc)
{
    if (!casc) return;
    for (uint8_t i = 0; i < casc->count; ++i) {
        biquad_flush(&casc->stages[i]);
    }
}

void biquad_cascade_process_block(biquad_cascade_t *casc,
                                   const float *in, float *out,
                                   size_t num_samples)
{
    if (!casc || !in || !out || casc->count == 0) return;

    /* Primera etapa: leer de 'in' y escribir en 'out' */
    biquad_process_block(&casc->stages[0], in, out, num_samples);

    /* Etapas siguientes: in-place sobre 'out' */
    for (uint8_t i = 1; i < casc->count; ++i) {
        biquad_process_block(&casc->stages[i], out, out, num_samples);
    }
}
