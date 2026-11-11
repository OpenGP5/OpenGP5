/* Host test of fx_ui.c:  cc -Wall -Wextra -fsanitize=address,undefined fx_ui_test.c ../fx_ui.c && ./a.out */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../fx_ui.h"

static int drain(uint32_t *ids, uint32_t *vals) { int n = 0; while (fx_ui_take(&ids[n], &vals[n])) n++; return n; }
static uint32_t val_of(int n, uint32_t *id, uint32_t *v, uint32_t want) { for (int i = 0; i < n; i++) if (id[i] == want) return v[i]; assert(0); return 0; }

int main(void)
{
    uint32_t id[16], v[16]; char t[FX_COLS + 1], u[FX_COLS + 1]; int n, i;

    fx_ui_init();
    assert(drain(id, v) == 0 && !fx_ui_on() && !fx_ui_frozen() && fx_ui_level() == 255 && !fx_ui_live());
    fx_ui_text(t); assert(!strcmp(t, "MIX       50%"));
    fx_ui_status(t); assert(!strcmp(t, "MIX 50 OFF   "));

    /* a knob that sits still (wherever it is, with a little jitter) changes nothing */
    fx_ui_knob(255); fx_ui_knob(253); fx_ui_knob(255); fx_ui_knob(251);
    assert(drain(id, v) == 0 && !fx_ui_live());
    fx_ui_text(t); assert(!strcmp(t, "MIX       50%"));
    /* turned: the setting follows the knob over its whole range */
    fx_ui_knob(240); assert(fx_ui_live());
    n = drain(id, v); assert(n == 1 && id[0] == 6 && v[0] == 30 * 8);
    fx_ui_knob(0); n = drain(id, v); assert(n == 1 && v[0] == 0);
    fx_ui_knob(128); n = drain(id, v); assert(n == 1 && v[0] == 128);
    fx_ui_knob(255); n = drain(id, v); assert(n == 1 && v[0] == 256);
    fx_ui_knob(255); assert(drain(id, v) == 0);

    /* choosing another setting sends nothing and leaves both settings alone, although the knob is at full */
    fx_ui_turn(1); assert(!fx_ui_live() && drain(id, v) == 0);
    fx_ui_text(t); assert(!strcmp(t, "DELAY   300MS"));
    fx_ui_knob(255); fx_ui_knob(254); assert(drain(id, v) == 0);
    fx_ui_text(t); assert(!strcmp(t, "DELAY   300MS"));
    fx_ui_knob(128); n = drain(id, v); assert(n == 1 && id[0] == 1 && v[0] == 26 * 441 && fx_ui_live());
    fx_ui_turn(-1); fx_ui_knob(128); fx_ui_text(t); assert(!strcmp(t, "MIX      100%"));   /* mix kept its value */

    /* every setting: text fits the 13 columns at both ends of the knob; values are within the DSP's limits */
    fx_ui_init();
    for (i = 0; i < 12; i++) {
        fx_ui_knob(100); fx_ui_knob(0); fx_ui_text(t); assert(strlen(t) == FX_COLS);
        fx_ui_knob(255); fx_ui_text(u); assert(strlen(u) == FX_COLS); printf("%s | %s\n", t, u);
        fx_ui_turn(1);
    }
    fx_ui_text(t); assert(!strncmp(t, "MIX", 3));          /* wrapped round */
    assert(fx_ui_level() == 255);
    fx_ui_resend_all(); n = drain(id, v);
    assert(n == 13 && id[11] == 9 && id[12] == 0);         /* everything once; freeze, then on/off, last */
    assert(val_of(n, id, v, 1) == 22491 && val_of(n, id, v, 2) == 8177 && (int32_t)val_of(n, id, v, 3) == 1024);
    assert(val_of(n, id, v, 4) == 11025 && val_of(n, id, v, 5) == 250 && val_of(n, id, v, 7) == 8);
    assert(val_of(n, id, v, 8) == 35721 && val_of(n, id, v, 10) == 256 && val_of(n, id, v, 11) == 256);
    assert((int32_t)val_of(n, id, v, 12) == 1024 && val_of(n, id, v, 6) == 256);

    fx_ui_init(); fx_ui_resend_all(); n = drain(id, v);    /* defaults */
    assert(val_of(n, id, v, 1) == 13230 && val_of(n, id, v, 10) == 64 && val_of(n, id, v, 11) == 176
           && val_of(n, id, v, 0) == 0 && val_of(n, id, v, 6) == 128);

    /* LEVEL goes to dsp_ctl, not to the DSP as a 0x7F10 parameter */
    fx_ui_turn(-1); fx_ui_text(t); assert(!strncmp(t, "LEVEL", 5));
    fx_ui_knob(200); fx_ui_knob(128); assert(drain(id, v) == 0 && fx_ui_level() == 128);

    /* PARA push: on / off; Foot press: freeze / unfreeze */
    fx_ui_para_push(); n = drain(id, v); assert(n == 1 && id[0] == 0 && v[0] == 1);
    fx_ui_foot_press(); n = drain(id, v); assert(n == 1 && id[0] == 9 && v[0] == 1);
    fx_ui_status(t); assert(!strcmp(t, "MIX 50 ON  FR"));
    fx_ui_foot_press(); n = drain(id, v); assert(n == 1 && id[0] == 9 && v[0] == 0);
    /* off lets go of the loop; a press while off switches on and freezes */
    fx_ui_foot_press(); drain(id, v);
    fx_ui_para_push(); n = drain(id, v); assert(n == 2 && !fx_ui_on() && !fx_ui_frozen());
    fx_ui_foot_press(); n = drain(id, v);
    assert(n == 2 && id[0] == 9 && v[0] == 1 && id[1] == 0 && v[1] == 1 && fx_ui_on() && fx_ui_frozen());
    puts("ALL OK");
    return 0;
}
