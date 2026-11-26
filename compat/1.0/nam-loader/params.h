/* NAM Loader's parameters (OpenGP5 1.0: platform/dsp/gp5_params.h). The ids are the ones Launchpad has used since
 * 1.1 (live settings 0..5). GATE..TREBLE are the tone playing's: each tone keeps its own (nds32/firmware.c, store
 * record "AMPT"); the firmware sets these when the tone changes, and a change from the computer goes to that tone.
 * IR (1.3): which of the 12 cab IRs the tone playing plays, kept per tone the same way. SLOT (1.3.1): the capture
 * playing; a computer sets it to switch (the slot must hold a capture). */
#define GP5_PARAMS(P, C)                                                                                         \
    C(CAB,    "Cab IR",    "Cab",  2, "Off|On", 1,          GP5_P_HIDDEN)                    /* the IR on / off */ \
    P(TAPS,   "IR length", "Cab",  0, 1024, 0, GP5_U_CUSTOM, GP5_P_HIDDEN)                    /* 0 = the whole file */ \
    P(GATE,   "Gate",      "Tone", 0, 61, 0, GP5_U_CUSTOM,   0)                               /* 0 off, -90..-30 dBFS */ \
    P(BASS,   "Bass",      "Tone", 0, 24, 12, GP5_U_CUSTOM,  0)                               /* -12..+12 dB */     \
    P(MID,    "Mid",       "Tone", 0, 24, 12, GP5_U_CUSTOM,  0)                                                     \
    P(TREBLE, "Treble",    "Tone", 0, 24, 12, GP5_U_CUSTOM,  0)                                                     \
    P(IR,     "IR",        "Cab",  0, 12, 0, GP5_U_CUSTOM,   0)                               /* 0 none, 1..12: cab-NN (1.3) */ \
    P(SLOT,   "Capture",   "Bank", 0, 30, 0, GP5_U_CUSTOM,   0)                               /* 0 built-in, 1..30: tone-NN (1.3.1) */
