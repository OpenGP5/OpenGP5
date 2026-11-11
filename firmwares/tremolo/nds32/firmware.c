/* Tremolo, control half (interface: platform/nds32/gp5_firmware.h).
 *   big knob   depth
 *   PARA turn  rate, 0.25 Hz per click (0.5 .. 15 Hz)
 *   foot       tremolo on / off (the LEDs show it)
 * Each change is queued and handed to the platform in fw_take(), which sends it to the DSP half. */
#include "gp5_firmware.h"

enum { ID_RATE, ID_DEPTH, ID_ON, N_IDS };

static uint32_t value[N_IDS];
static uint32_t dirty;                  /* bit per id: not sent yet */
static unsigned knob_seen = 1000, shown_rate, shown_depth, shown_on = 2;

static void set(unsigned id, uint32_t v) { if (value[id] != v) { value[id] = v; dirty |= 1u << id; } }

void fw_init(void)
{
    value[ID_RATE] = 500; value[ID_DEPTH] = 128; value[ID_ON] = 1;
    dirty = 0; knob_seen = 1000; shown_on = 2;
}

void fw_dsp_ready(void) { dirty = (1u << N_IDS) - 1; }        /* the DSP starts with its defaults: send all */
unsigned fw_setting_count(void) { return N_IDS; }
unsigned fw_level(void) { return 255; }                       /* full volume: the knob is the depth */
unsigned fw_active(void) { return value[ID_ON]; }

void fw_foot(void) { set(ID_ON, !value[ID_ON]); }
void fw_para_push(void) {}

void fw_para_turn(int detents)
{
    int r = (int)value[ID_RATE] + 25 * detents;
    set(ID_RATE, (uint32_t)(r < 50 ? 50 : r > 1500 ? 1500 : r));
}

void fw_knob(unsigned knob)
{
    /* the knob reading wobbles by a step or two: only follow real moves */
    if (knob_seen != 1000 && (knob + 2 >= knob_seen && knob <= knob_seen + 2)) return;
    knob_seen = knob;
    set(ID_DEPTH, knob);
}

int fw_take(uint32_t *id, uint32_t *v)
{
    for (unsigned i = 0; i < N_IDS; i++)
        if (dirty & (1u << i)) { dirty &= ~(1u << i); *id = i; *v = value[i]; return 1; }
    return 0;
}

static void number(char *p, unsigned v, unsigned digits)   /* right-aligned decimal */
{
    for (unsigned i = digits; i-- > 0; v /= 10) p[i] = (char)('0' + v % 10);
}

int fw_draw(unsigned knob, int force)
{
    (void)knob;
    if (force) { gp5_text(0, "TREMOLO      "); gp5_text(4, "             "); shown_on = 2; shown_rate = shown_depth = 100000; }
    if (value[ID_RATE] != shown_rate) {
        char s[14] = "RATE  00.00HZ";
        number(s + 6, value[ID_RATE] / 100, 2); number(s + 9, value[ID_RATE] % 100, 2);
        gp5_text(1, s); shown_rate = value[ID_RATE];
    }
    if (value[ID_DEPTH] != shown_depth) {
        char s[14] = "DEPTH   000% ";
        number(s + 8, value[ID_DEPTH] * 100 / 255, 3);
        gp5_text(2, s); shown_depth = value[ID_DEPTH];
    }
    if (value[ID_ON] != shown_on) {
        gp5_text(3, value[ID_ON] ? "ON           " : "OFF          "); shown_on = value[ID_ON];
    }
    return 0;
}
