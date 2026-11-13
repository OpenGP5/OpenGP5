/* Passthrough: the smallest DSP firmware. Copy this folder to start your own (interface: gp5_dsp.h).
 * The guitar goes to both outputs unchanged; the platform applies the volume knob afterwards. */
static void fw_init(void) {}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    for (uint32_t i = 0; i < GP5_BLOCK; i++) { out_left[i] = in[i]; out_right[i] = in[i]; }
}

static int fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n)
{
    (void)cmd; (void)payload; (void)n;
    return 0;                       /* no commands of its own */
}
