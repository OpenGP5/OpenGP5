/* Passthrough: the smallest control-processor firmware. Copy this folder to start your own
 * (interface: platform/nds32/gp5_firmware.h, OpenGP5 1.0). The volume knob is the output level (the platform's
 * default); this file only shows it. Every other callback is optional: the platform's defaults do nothing. */
#include "gp5_firmware.h"

static unsigned shown = 1000;

int fw_draw(int force)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[14] = "VOLUME     00";
    unsigned level = gp5_knob();
    if (force) { gp5_text(0, "PASSTHROUGH  "); gp5_text(1, "             "); gp5_text(3, "             "); gp5_text(4, "             "); shown = 1000; }
    if (level != shown) {
        line[11] = hex[(level >> 4) & 15]; line[12] = hex[level & 15];
        gp5_text(2, line);
        shown = level;
    }
    return 0;
}
