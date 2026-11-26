/* Bluetooth for a firmware that asks for it (firmware.json: "bluetooth": true; OpenGP5 1.0): the pedal advertises as
 * "OpenGP5" (1.0; was "GP-5") with the BLE-MIDI service, accepts one connection, and carries MIDI both ways: the platform's command
 * protocol (SysEx: Launchpad, gp5_link.py) on the second MIDI port, and notes / controllers / program changes to the
 * firmware's fw_midi(). Our own code (bt/radio.c, bt/ble.c; work/bluetooth/notes.md has how it was worked out).
 *
 * The address: a static random one made from the chip's efuse bytes, so each pedal keeps its own. */
#include <stdint.h>
#include "bt/ble.h"
#include "gp5_firmware.h"
#ifdef GP5_BT_TRACE
extern volatile uint32_t gp5_crash[4];
#endif

#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))

/* one byte of the chip's efuse (0x40023000: 0x08 control, 0x0C data, 0x10 status), n < 0x3B */
static uint8_t efuse(unsigned n)
{
    uint32_t k;
    R32(0x40023018u) = 1;
    R16(0x40023008u) |= 0x2000u;
    for (k = 0; k < 300u; k++) __asm__ volatile ("nop");
    R16(0x40023008u) = (uint16_t)((R16(0x40023008u) & ~0x1FFu) | n);
    R16(0x40023008u) |= 0x4000u;
    R16(0x40023008u) &= (uint16_t)~0x4000u;
    for (k = 0; k < 300u; k++) __asm__ volatile ("nop");
    for (k = 0; k < 100000u && !(R32(0x40023010u) & 2u); k++) { }
    return (uint8_t)R32(0x4002300Cu);
}

/* six bytes from efuse bytes 2..9 (where the chip's ID is), least significant first; the top two bits set: a
 * static random address (Bluetooth spec, Vol 6 Part B 1.3.2.1) */
static void address(uint8_t a[6])
{
    uint32_t h = 2166136261u, i;
    uint8_t e[8];
    for (i = 0; i < 8u; i++) e[i] = efuse(2u + i);
    for (i = 0; i < 6u; i++) {
        unsigned j;
        for (j = 0; j < 8u; j++) { h ^= e[j]; h *= 16777619u; }
        h ^= i;
        a[i] = (uint8_t)(h >> 8);
    }
    a[5] |= 0xC0u;
}

void gp5_bt_start(void)
{
    /* advertising: flags, the BLE-MIDI service (so MIDI apps find it); scan response: the name */
    static const uint8_t adv[] = { 0x02, 0x01, 0x06, 0x11, 0x07, 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33,
                                   0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03 };
    static const uint8_t scan[] = { 0x08, 0x09, 'O', 'p', 'e', 'n', 'G', 'P', '5' };   /* the name ("GP-5" before 1.0) */
    uint8_t a[6];
    int r;
    address(a);
    r = ble_start(a, adv, sizeof adv, scan, sizeof scan);
    if (r) gp5_log("bluetooth: the radio didn't start (step %d)", r);
    else gp5_log("bluetooth: advertising as OpenGP5 (%02X:%02X:%02X:%02X:%02X:%02X)", a[5], a[4], a[3], a[2], a[1], a[0]);
#ifdef GP5_BT_TRACE
    {   extern volatile uint8_t bt_debug[];                     /* ble.c (debug builds): the tools find it by this line */
        gp5_log("bluetooth: debug trace at %08X, crash record at %08X", (unsigned)bt_debug, (unsigned)gp5_crash); }
#endif
}
