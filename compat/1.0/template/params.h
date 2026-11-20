/* The settings (OpenGP5 parameters: opengp5/docs/platform/params.md). The platform sends them to the DSP, keeps
 * them across power-off, keeps them in presets, and lets a computer read and set them (Launchpad's Live page). */
#define GP5_PARAMS(P, C)                                                                                       \
    P(DRIVE, "Drive", "Drive", 0, 400, 200, GP5_U_DB10,   GP5_P_DSP | GP5_P_PRESET)   /* gain before the clip */ \
    P(TONE,  "Tone",  "Drive", 500, 8000, 3000, GP5_U_HZ, GP5_P_DSP | GP5_P_PRESET)   /* low-pass after it */    \
    P(LEVEL, "Level", "Drive", -200, 60, -60, GP5_U_DB10, GP5_P_DSP | GP5_P_PRESET)                           \
    C(ON,    "Effect", "Drive", 2, "Off|On", 1,           GP5_P_DSP)                  /* the foot switch */
