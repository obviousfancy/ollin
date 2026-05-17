/*
 * mcuil_note_freq.h
 *
 *  Created on: Sep 22, 2025
 *      Author: Radof
 *
 *  Biblioteca de mapeo MIDI <-> Hz <-> nombre de nota.
 *  Diseñada para mínimo uso de ciclos de CPU en STM32H753ZI (Cortex-M7 + FPU).
 *
 *  Estrategia de eficiencia:
 *  - La tabla de 128 frecuencias se computa UNA sola vez en notes_build_table()
 *    usando powf(); después, todas las consultas son O(1) por indexación.
 *  - notes_hz_to_midi() es la única función con complejidad O(n), usarla
 *    sólo en inicialización o fuera del hot-path de audio.
 *  - notes_hz_to_name() llama internamente notes_hz_to_midi(); mismo criterio.
 */

#ifndef INC_MCUIL_NOTE_FREQ_H_
#define INC_MCUIL_NOTE_FREQ_H_

#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Constantes
 * --------------------------------------------------------------------- */

/** Número total de notas MIDI estándar (0 – 127). */
#define NOTES_MIDI_COUNT  128

/**
 * Tamaño mínimo recomendado para los buffers de nombre de nota.
 * Formato máximo: "C#-1\0" → 5 bytes; se reserva margen cómodo.
 */
#define NOTES_NAME_BUF_MIN  8

/* -----------------------------------------------------------------------
 * Tipos
 * --------------------------------------------------------------------- */

/** Configuración global de afinación. */
typedef struct {
    float a4_hz;   /**< Frecuencia de A4 en Hz. Valor por defecto: 440.0 */
} notes_config_t;

/* -----------------------------------------------------------------------
 * API pública
 * --------------------------------------------------------------------- */

/**
 * @brief  Inicializa la configuración global de afinación.
 *         Invalida la tabla interna; la próxima consulta la reconstruirá.
 * @param  config  Puntero a la configuración.  Si es NULL o el valor de
 *                 a4_hz está fuera del rango [10, 2000] Hz, se usa 440 Hz.
 */
void notes_init(notes_config_t *config);

/**
 * @brief  Construye (o reconstruye) la tabla interna de 128 frecuencias.
 *         Se llama automáticamente si la tabla no está lista, pero puede
 *         invocarse explícitamente durante la inicialización del sistema.
 * @param  out_table  Array de 128 floats donde se escriben las frecuencias.
 *                    También se copia a la caché interna estática.
 */
void notes_build_table(float out_table[NOTES_MIDI_COUNT]);

/**
 * @brief  Convierte un número de nota MIDI a frecuencia en Hz.
 *         Complejidad: O(1) (índice de tabla).
 * @param  midi_note  Nota MIDI [0, 127].
 * @return Frecuencia en Hz.
 */
float notes_midi_to_hz(uint8_t midi_note);

/**
 * @brief  Devuelve la nota MIDI más cercana a la frecuencia dada.
 *         Complejidad: O(n) — usar fuera del hot-path de audio.
 * @param  hz  Frecuencia en Hz [20, 20000].
 * @return Número MIDI [0, 127], o -1 si hz está fuera de rango.
 */
int notes_hz_to_midi(float hz);

/**
 * @brief  Convierte un número MIDI al nombre en notación americana.
 *         Ejemplo: 69 → "A4", 60 → "C4", 61 → "C#4" (sharps=true)
 *                                               "Db4" (sharps=false)
 * @param  midi_note     Nota MIDI [0, 127].
 * @param  sharps        true = usar sostenidos (#), false = usar bemoles (b).
 * @param  out_name      Buffer de salida (mínimo NOTES_NAME_BUF_MIN bytes).
 * @param  out_name_size Tamaño del buffer de salida.
 * @return true si se escribió con éxito, false en caso de error.
 */
bool notes_midi_to_name(uint8_t midi_note, bool sharps,
                        char *out_name, size_t out_name_size);

/**
 * @brief  Parsea un nombre de nota en notación americana a número MIDI.
 *         Formatos soportados: "C4", "C#4", "Db4", "A-1", "B0", "G#8".
 *         Sostenidos: '#'. Bemoles: 'b' (minúscula únicamente).
 * @param  name           Cadena de la nota (ej. "A4", "Bb3").
 * @param  out_midi_note  Resultado MIDI [0, 127].
 * @return true si el parseo fue exitoso, false si el formato es inválido.
 */
bool notes_name_to_midi(const char *name, uint8_t *out_midi_note);

/**
 * @brief  Convierte una frecuencia Hz al nombre de la nota más cercana,
 *         e indica la desviación en cents respecto a la afinación exacta.
 * @param  hz             Frecuencia en Hz [20, 20000].
 * @param  sharps         true = sostenidos, false = bemoles.
 * @param  out_name       Buffer de salida (mínimo NOTES_NAME_BUF_MIN bytes).
 * @param  out_name_size  Tamaño del buffer.
 * @param  cents_out      Si no es NULL, se escribe la desviación en cents
 *                        [-50, +50]. Positivo = por encima de la nota.
 * @return true si hz está dentro del rango MIDI y el nombre se escribió.
 */
bool notes_hz_to_name(float hz, bool sharps,
                      char *out_name, size_t out_name_size,
                      float *cents_out);

/**
 * @brief  Retorna el puntero a la cadena del nombre de la clase de nota
 *         (sin octava). Ejemplo: 69 → "A", 61 → "C#".
 *         El puntero apunta a memoria estática de sólo lectura.
 * @param  midi_note  Nota MIDI [0, 127].
 * @return Puntero a cadena constante.
 */
const char* notes_get_name(uint8_t midi_note);

/**
 * @brief  Retorna el número de octava de una nota MIDI.
 *         Convención estándar MIDI: C4 = 60, C-1 = 0.
 * @param  midi_note  Nota MIDI [0, 127].
 * @return Número de octava [-1, 9].
 */
int notes_get_octave(uint8_t midi_note);

/**
 * @brief  Imprime la tabla completa de notas MIDI por stdout.
 *         Útil sólo para depuración; no llamar en código de producción.
 */
void notes_print_table(void);

#endif /* INC_MCUIL_NOTE_FREQ_H_ */
