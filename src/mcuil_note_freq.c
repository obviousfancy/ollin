/*
 * mcuil_note_freq.c
 *
 *  Created on: Sep 22, 2025
 *      Author: Radof / Jonathan Mejorado Lopez
 *
 *  Notas de implementación:
 *  - La tabla at_table[128] se construye una sola vez con powf(); el resto
 *    de operaciones son indexaciones O(1), sin aritmética en punto flotante
 *    durante el hot-path de audio.
 *  - notes_name_to_midi() y notes_hz_to_midi() son las únicas funciones con
 *    bucles; deben usarse sólo en tiempo de configuración, no por muestra.
 *  - notes_hz_to_name() usa logf() (1 llamada) para calcular los cents;
 *    también reservada para fuera del hot-path.
 */

#include "mcuil_note_freq.h"
#include <stdio.h>

/* -----------------------------------------------------------------------
 * Estado estático (tabla de frecuencias cacheada)
 * --------------------------------------------------------------------- */

static float at_a4          = 440.0f;
static float at_table[NOTES_MIDI_COUNT];
static int   at_table_ready = 0;

/* -----------------------------------------------------------------------
 * Tablas constantes (ROM — sin coste de RAM en tiempo de ejecución)
 * --------------------------------------------------------------------- */

/** Nombres de clase de nota con sostenidos (índice 0 = C). */
static const char* const k_names_sharp[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/** Nombres de clase de nota con bemoles (índice 0 = C). */
static const char* const k_names_flat[12] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
};

/**
 * Semitono base de cada letra de nota (A=0, B=1, ..., G=6 → índice 'X'-'A').
 * Convención: C = 0.
 */
static const int8_t k_letter_semitone[7] = {
    /*A*/9, /*B*/11, /*C*/0, /*D*/2, /*E*/4, /*F*/5, /*G*/7
};

/* Constante ln(2) para el cálculo de cents — evita división por muestra */
#define LN2_F  0.693147180559945f

/* -----------------------------------------------------------------------
 * Funciones internas
 * --------------------------------------------------------------------- */

/** Asegura que la tabla esté construida antes de cualquier consulta. */
static inline void _ensure_table(void)
{
    if (!at_table_ready) {
        notes_build_table(at_table);
    }
}

/* -----------------------------------------------------------------------
 * API pública — inicialización y tabla
 * --------------------------------------------------------------------- */

void notes_init(notes_config_t *config)
{
    if (config && config->a4_hz > 10.0f && config->a4_hz < 2000.0f) {
        at_a4 = config->a4_hz;
    } else {
        at_a4 = 440.0f;
    }
    at_table_ready = 0; /* fuerza reconstrucción en la próxima consulta */
}

void notes_build_table(float out_table[NOTES_MIDI_COUNT])
{
    if (at_table_ready) {
        /* Tabla ya lista: sólo copia al buffer del llamador si se proporcionó
         * uno diferente al interno. */
        if (out_table && out_table != at_table) {
            for (int i = 0; i < NOTES_MIDI_COUNT; ++i) {
                out_table[i] = at_table[i];
            }
        }
        return;
    }

    for (int i = 0; i < NOTES_MIDI_COUNT; ++i) {
        float hz = at_a4 * powf(2.0f, (float)(i - 69) / 12.0f);
        at_table[i] = hz;
        if (out_table && out_table != at_table) {
            out_table[i] = hz;
        }
    }
    at_table_ready = 1;
}

/* -----------------------------------------------------------------------
 * API pública — conversiones
 * --------------------------------------------------------------------- */

float notes_midi_to_hz(uint8_t midi_note)
{
    _ensure_table();
    return at_table[midi_note]; /* acceso O(1) */
}

int notes_hz_to_midi(float hz)
{
    if (hz < 20.0f || hz > 20000.0f) return -1;

    _ensure_table();

    int   closest_note = 0;
    float closest_diff = fabsf(at_table[0] - hz);

    for (int i = 1; i < NOTES_MIDI_COUNT; ++i) {
        float diff = fabsf(at_table[i] - hz);
        if (diff < closest_diff) {
            closest_diff = diff;
            closest_note = i;
        }
        /* Optimización: la tabla es monótonamente creciente; si la diferencia
         * empieza a aumentar habiendo ya encontrado un mínimo, podemos parar. */
        if (at_table[i] > hz && diff > closest_diff) break;
    }
    return closest_note;
}

/* -----------------------------------------------------------------------
 * notes_midi_to_name
 * --------------------------------------------------------------------- */

bool notes_midi_to_name(uint8_t midi_note, bool sharps,
                        char *out_name, size_t out_name_size)
{
    if (!out_name || out_name_size == 0) return false;

    const char* const *names = sharps ? k_names_sharp : k_names_flat;
    const char *note_str = names[midi_note % 12];
    int octave = notes_get_octave(midi_note);

    int written = snprintf(out_name, out_name_size, "%s%d", note_str, octave);
    return (written > 0 && (size_t)written < out_name_size);
}

/* -----------------------------------------------------------------------
 * notes_name_to_midi
 *
 *  Parsea cadenas con el formato:  LETRA [# | b] OCTAVA
 *  Ejemplos válidos:  "C4", "C#4", "Db4", "A-1", "B0", "G#8", "Bb3"
 *  Convención MIDI:   C4 = 60, C-1 = 0, G9 = 127.
 * --------------------------------------------------------------------- */

bool notes_name_to_midi(const char *name, uint8_t *out_midi_note)
{
    if (!name || !out_midi_note) return false;

    /* --- 1. Parsear letra de nota ------------------------------------ */
    char letter = (char)toupper((unsigned char)name[0]);
    if (letter < 'A' || letter > 'G') return false;

    int semitone = (int)k_letter_semitone[letter - 'A'];
    int pos = 1;

    /* --- 2. Sostenido o bemol (opcional) ----------------------------- */
    if (name[pos] == '#') {
        semitone += 1;
        pos++;
    } else if (name[pos] == 'b') {   /* sólo 'b' minúscula = bemol */
        semitone -= 1;
        pos++;
    }

    /* --- 3. Número de octava (puede ser negativo: "-1") -------------- */
    if (name[pos] == '\0') return false;

    int sign   = 1;
    int octave = 0;

    if (name[pos] == '-') {
        sign = -1;
        pos++;
    }
    if (!isdigit((unsigned char)name[pos])) return false;

    while (isdigit((unsigned char)name[pos])) {
        octave = octave * 10 + (name[pos] - '0');
        pos++;
    }
    octave *= sign;

    /* --- 4. Normalizar semitono por accidentales extremos (Cb, B#) --- */
    if (semitone < 0)  { semitone += 12; octave--; }
    if (semitone >= 12){ semitone -= 12; octave++; }

    /* --- 5. Calcular y validar MIDI ---------------------------------- */
    int midi = (octave + 1) * 12 + semitone;
    if (midi < 0 || midi > 127) return false;

    *out_midi_note = (uint8_t)midi;
    return true;
}

/* -----------------------------------------------------------------------
 * notes_hz_to_name
 *
 *  Encuentra la nota MIDI más cercana, luego calcula la desviación en
 *  cents:  cents = 1200 * log2(hz / ref_hz)
 *                = 1200 * ln(hz / ref_hz) / ln(2)
 *  Se usa logf() una sola vez; apto fuera del hot-path.
 * --------------------------------------------------------------------- */

bool notes_hz_to_name(float hz, bool sharps,
                      char *out_name, size_t out_name_size,
                      float *cents_out)
{
    if (!out_name || out_name_size == 0) return false;

    int midi = notes_hz_to_midi(hz);
    if (midi < 0) return false;

    if (!notes_midi_to_name((uint8_t)midi, sharps, out_name, out_name_size)) {
        return false;
    }

    if (cents_out) {
        float ref_hz = at_table[midi];
        if (ref_hz > 0.0f && hz > 0.0f) {
            *cents_out = 1200.0f * logf(hz / ref_hz) / LN2_F;
        } else {
            *cents_out = 0.0f;
        }
    }
    return true;
}

/* -----------------------------------------------------------------------
 * Utilidades de nombre/octava
 * --------------------------------------------------------------------- */

const char* notes_get_name(uint8_t midi_note)
{
    return k_names_sharp[midi_note % 12];
}

int notes_get_octave(uint8_t midi_note)
{
    return (int)(midi_note / 12) - 1; /* C4 = 60 → octave 4 */
}

/* -----------------------------------------------------------------------
 * Depuración
 * --------------------------------------------------------------------- */

void notes_print_table(void)
{
    _ensure_table();
    for (int i = 0; i < NOTES_MIDI_COUNT; ++i) {
        printf("%3d | %-3s%-3d | %8.3f Hz\n",
               i,
               notes_get_name((uint8_t)i),
               notes_get_octave((uint8_t)i),
               at_table[i]);
    }
}
