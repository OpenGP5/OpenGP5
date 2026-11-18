/* params_test, version A of a firmware */
#define GP5_PARAMS(P, C)                                                                  \
    P(MIX,   "Mix",    "Main", 0, 100, 50, GP5_U_PERCENT, GP5_P_DSP | GP5_P_PRESET)         \
    P(TIME,  "Time",   "Main", 10, 2000, 350, GP5_U_MS,   GP5_P_DSP | GP5_P_PRESET)         \
    C(SHAPE, "Shape",  "Mod",  3, "Sine|Square|Ramp", 0,  GP5_P_DSP | GP5_P_PRESET)         \
    P(BRIGHT,"Bright", "Setup", 0, 10, 7, GP5_U_NONE,     GP5_P_SAVE)                       \
    P(STATE, "State",  "Info", 0, 3, 0, GP5_U_NONE,       GP5_P_READONLY)
