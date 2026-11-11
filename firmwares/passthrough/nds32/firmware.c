/* Passthrough: the smallest control-processor firmware. Copy this folder to start your own
 * (interface: platform/nds32/gp5_firmware.h). The big knob is the volume; nothing else does anything. */
#include "gp5_firmware.h"

static unsigned level, shown = 1000;

void fw_init(void) { level = 0; shown = 1000; }
void fw_foot(void) {}
void fw_para_push(void) {}
void fw_para_turn(int detents) { (void)detents; }
void fw_knob(unsigned knob) { level = knob; }
unsigned fw_level(void) { return level; }
void fw_dsp_ready(void) {}
int fw_take(uint32_t *id, uint32_t *value) { (void)id; (void)value; return 0; }   /* no settings */
unsigned fw_setting_count(void) { return 0; }
unsigned fw_active(void) { return 1; }

int fw_draw(unsigned knob, int force)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[14] = "VOLUME     00";
    (void)knob;
    if (force) { gp5_text(0, "PASSTHROUGH  "); gp5_text(1, "             "); gp5_text(3, "             "); gp5_text(4, "             "); shown = 1000; }
    if (level != shown) {
        line[11] = hex[(level >> 4) & 15]; line[12] = hex[level & 15];
        gp5_text(2, line);
        shown = level;
    }
    return 0;
}
