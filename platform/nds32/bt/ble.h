/* BLE on the control processor (ble.c): connectable advertising and one connection as a peripheral. */
#ifndef GP5_BLE_H
#define GP5_BLE_H
#include <stdint.h>

enum { BLE_OFF = 0, BLE_ADVERTISING = 1, BLE_CONNECTED = 2 };

struct ble_state {
    uint32_t state;      /* BLE_OFF, BLE_ADVERTISING, BLE_CONNECTED */
    uint32_t version;    /* the BLE core's version register (0x08000A00 expected) */
    int32_t  radio;      /* radio start-up: 0, or the step that failed (negative) */
    uint32_t events;     /* events that ended (advertising and connection) */
    uint32_t missed;     /* connection events in which nothing was found */
    uint32_t late;       /* events that couldn't be armed in time */
    uint32_t errors;     /* the core's error interrupts */
    uint32_t clock;      /* the native clock, in slots (625 us) */
    uint32_t rx_ok, rx_err;          /* packets received: fine / with an error */
    uint32_t conn_req, connects, disconnects, updates;
    uint32_t interval;   /* the connection interval, slots */
    uint32_t ll_rx, att_rx, tx_ok, tx_drop;
    uint32_t bad_req, not_ours;      /* requests we couldn't follow / addressed to someone else */
    uint32_t rx_dup;                 /* repeats of packets already handled */
    int32_t  first_delta;            /* us: the first synced packet minus the anchor we predicted (first window) */
    uint32_t first_synced, first_evt;
    uint32_t midi_rx, midi_tx, midi_notify;
    int32_t  delta_sum; uint32_t delta_n;   /* synced packets' offset from the predicted anchor: sum, count */
    uint32_t synced, unsynced, rx_none, missed_evt;
    uint32_t sysex_rx, sysex_tx, port_drop;   /* SysEx messages in / out over the platform's second port */       /* connection events with a re-sync / with packets but a sync error */                /* connection requests we couldn't follow */
    uint8_t  last_reason;            /* why the last connection ended (0x08: supervision time-out) */
    uint32_t stalls;                 /* connections ended by the main loop's watchdog: their events had stopped */
    uint32_t adv_restarts;           /* advertising started again by the watchdog: its events had stopped */
};
extern struct ble_state ble;

struct ble_cfg { uint32_t win; int32_t offset; };   /* tuning, live: window after a sync (us), window start shift (us) */
extern struct ble_cfg ble_cfg;

/* addr: our static random address, least significant byte first (top two bits of addr[5] set);
 * adv: the advertising data (at most 31 bytes). Starts the radio and advertises (connectable); 0, or the radio's
 * failed step (negative). Everything after this runs in the Bluetooth interrupts (platform hooks gp5_bt_irq,
 * gp5_ble_irq). */
int  ble_start(const uint8_t addr[6], const uint8_t *adv, unsigned len, const uint8_t *scan, unsigned slen);
/* BLE-MIDI: MIDI bytes the central wrote (timestamps removed) go to this function, if set; ble_midi_send()
 * notifies one message (at most 18 bytes) with a timestamp from ms. -1 if not connected / notifications off. */
extern void (*ble_midi_rx)(const uint8_t *bytes, unsigned n);
int  ble_midi_send(const uint8_t *msg, unsigned n, uint32_t ms);
void ble_poll(void);   /* updates ble.clock (for showing it) */
#endif
