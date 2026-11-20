/* ui_test's parameters: one of every unit, a choice, a read-only and a hidden one */
#define GP5_PARAMS(P, C)                                                                  \
    P(MIX,   "Mix",    "Main", 0, 100, 50, GP5_U_PERCENT, GP5_P_DSP | GP5_P_PRESET)         \
    P(TIME,  "Time",   "Main", 10, 2000, 350, GP5_U_MS,   GP5_P_DSP | GP5_P_PRESET)         \
    C(SHAPE, "Shape",  "Mod",  3, "Sine|Square|Ramp", 0,  GP5_P_DSP | GP5_P_PRESET)         \
    P(GAIN,  "Gain",   "Main", -600, 60, 0, GP5_U_DB10,   GP5_P_DSP)                        \
    P(PITCH, "Pitch",  "Mod",  -12, 12, 0, GP5_U_SEMITONES, GP5_P_DSP)                      \
    P(FINE,  "Fine",   "Mod",  -500, 500, 0, GP5_U_CENTS10, GP5_P_DSP)                      \
    P(FREQ,  "Freq",   "Mod",  20, 20000, 440, GP5_U_HZ,   GP5_P_DSP)                       \
    P(RATE,  "Rate",   "Mod",  50, 1500, 503, GP5_U_CUSTOM, GP5_P_DSP)                      \
    P(STATE, "State",  "Info", 0, 3, 0, GP5_U_NONE,       GP5_P_READONLY)                   \
    P(SECRET,"Secret", "Info", 0, 9, 0, GP5_U_NONE,       GP5_P_HIDDEN)
