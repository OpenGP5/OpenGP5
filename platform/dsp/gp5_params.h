/* OpenGP5 1.0 parameters: a firmware's settings, declared once, used on both processors (docs/platform/params.md).
 *
 * A firmware lists them in params.h at the top of its folder (the build finds it):
 *
 *     #define GP5_PARAMS(P, C)                                                                         \
 *         P(MIX,   "Mix",   "Main",  0, 100, 50, GP5_U_PERCENT, GP5_P_DSP | GP5_P_PRESET)              \
 *         P(TIME,  "Time",  "Delay", 10, 2000, 350, GP5_U_MS,   GP5_P_DSP | GP5_P_PRESET)              \
 *         C(SHAPE, "Shape", "Mod",   3, "Sine|Square|Ramp", 0,  GP5_P_DSP | GP5_P_PRESET)
 *
 *   P(id, name, group, min, max, default, unit, flags)       a number, min..max (-32768..32767)
 *   C(id, name, group, count, "A|B|C", default, flags)       a choice: 0..count-1, the names separated by |
 *
 * That gives GP5_P_MIX, GP5_P_TIME, ... (0, 1, 2 ...), GP5_NPARAMS, and the table the platform and the computer
 * read. Names and groups at most 15 characters, the choices string at most 47 (longer ones are cut when sent).
 * Flags:
 *   GP5_P_DSP       the DSP gets it: fw_param(id, value) there, before the next fw_process, and gp5_param(id)
 *   GP5_P_PRESET    part of a preset (manifest "presets": N); also kept across power-off as the current sound
 *   GP5_P_SAVE      kept across power-off on its own (a global setting, not in presets)
 *   GP5_P_READONLY  the computer may read it, not set it (a state: "looping", the note being played)
 *   GP5_P_HIDDEN    not shown in Launchpad's editor
 * Units (how the computer shows a value): GP5_U_NONE, _PERCENT, _DB10 (tenths of a dB), _MS, _HZ, _SEMITONES,
 * _CENTS10 (tenths of a cent), _CUSTOM (the control processor's fw_param_text() writes the text). */
#ifndef GP5_PARAMS_H
#define GP5_PARAMS_H
#include <stdint.h>

enum { GP5_U_NONE, GP5_U_PERCENT, GP5_U_DB10, GP5_U_MS, GP5_U_HZ, GP5_U_SEMITONES, GP5_U_CENTS10, GP5_U_CUSTOM };
#define GP5_P_DSP      0x01u
#define GP5_P_PRESET   0x02u
#define GP5_P_SAVE     0x04u
#define GP5_P_READONLY 0x08u
#define GP5_P_HIDDEN   0x10u
#define GP5_P_CHOICE   0x80u                          /* (set by C(...)) */

typedef struct {
    const char *name, *group, *choices;               /* choices: "A|B|C" for a choice, else 0 */
    int16_t min, max, def;
    uint8_t unit, flags;
} gp5_param_desc_t;

#ifdef GP5_HAS_PARAMS
#include "params.h"                                   /* the firmware's: GP5_PARAMS(P, C) */
#define GP5__ID_P(id, ...) GP5_P_##id,
#define GP5__ID_C(id, ...) GP5_P_##id,
enum { GP5_PARAMS(GP5__ID_P, GP5__ID_C) GP5_NPARAMS };
#undef GP5__ID_P
#undef GP5__ID_C
#define GP5__D_P(id, nm, gr, mn, mx, df, un, fl) { nm, gr, 0, (int16_t)(mn), (int16_t)(mx), (int16_t)(df), (uint8_t)(un), (uint8_t)(fl) },
#define GP5__D_C(id, nm, gr, n, ch, df, fl)      { nm, gr, ch, 0, (int16_t)((n) - 1), (int16_t)(df), GP5_U_NONE, (uint8_t)((fl) | GP5_P_CHOICE) },
static const gp5_param_desc_t gp5_param_table[GP5_NPARAMS] __attribute__((unused)) = { GP5_PARAMS(GP5__D_P, GP5__D_C) };
#undef GP5__D_P
#undef GP5__D_C
#else
#define GP5_NPARAMS 0
#endif

/* ---- for firmwares ---- */
#if defined(__NDS32__) && !defined(GP5_CTL_SIDE)
#define GP5_CTL_SIDE                                  /* the control processor (or a host test of its code) */
#endif
#if defined(GP5_HAS_PARAMS) && !defined(GP5_CTL_SIDE)
/* DSP: the current values (the platform keeps them in step with the control processor) */
extern int32_t gp5_param_values[GP5_NPARAMS];
#define gp5_param(id) (gp5_param_values[(id)])
#endif
#ifdef GP5_CTL_SIDE
int32_t gp5_param(unsigned id);                       /* the current value (the default before anything is set) */
int gp5_param_set(unsigned id, int32_t value);        /* from the pedal's own controls: clamped, sent to the DSP if
                                                         it is a DSP parameter, saved 2 s after the last change; 1 if
                                                         it changed */
void fw_param_changed(unsigned id);                   /* optional: a value changed from outside (the computer, a
                                                         preset load: then once per changed parameter) */
int fw_param_text(unsigned id, int32_t value, char *text);   /* optional, for GP5_U_CUSTOM: up to 15 characters into
                                                         text; return 1 (0: the computer shows the number) */
/* Presets (manifest "presets": N): every GP5_P_PRESET parameter, and a name of up to 12 characters each. */
unsigned gp5_preset_count(void);                      /* N */
int gp5_preset_current(void);                         /* the preset loaded last, -1 if none */
int gp5_preset_used(unsigned n);                      /* 1 if preset n holds a sound */
int gp5_preset_dirty(void);                           /* 1 if the sound differs from the preset loaded */
int gp5_preset_load(unsigned n);                      /* 0 if it is empty (nothing changes) */
int gp5_preset_save(unsigned n);                      /* the current sound into n (keeps its name); 1 when saved */
const char *gp5_preset_name(unsigned n);              /* "" when it has none */
int gp5_preset_rename(unsigned n, const char *name);  /* printable ASCII, 12 characters at most */
#endif
#endif
