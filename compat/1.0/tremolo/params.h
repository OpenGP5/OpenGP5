/* The tremolo's parameters (OpenGP5 1.0: platform/dsp/gp5_params.h). The platform sends them to the DSP, keeps
 * them across power-off, puts RATE, DEPTH and SHAPE in presets, and lets a computer read and set all of them. */
#define GP5_PARAMS(P, C)                                                                                     \
    P(RATE,  "Rate",  "Tremolo", 50, 1500, 500, GP5_U_CUSTOM,  GP5_P_DSP | GP5_P_PRESET)  /* 1/100 Hz */      \
    P(DEPTH, "Depth", "Tremolo", 0, 100, 50, GP5_U_PERCENT,     GP5_P_DSP | GP5_P_PRESET)                    \
    C(SHAPE, "Shape", "Tremolo", 3, "Sine|Triangle|Square", 0,  GP5_P_DSP | GP5_P_PRESET)                    \
    C(ON,    "Effect", "Tremolo", 2, "Off|On", 1,               GP5_P_DSP)                    /* foot switch */
