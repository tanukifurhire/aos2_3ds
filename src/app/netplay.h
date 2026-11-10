/* Local-wireless versus (UDS transport + rollback session). */
#ifndef AOS_APP_NETPLAY_H
#define AOS_APP_NETPLAY_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#include "../input/aos_input.h"

/* User button layout -> original input mask (main.c; shared so both modes use one mapping). */
uint16_t to_mask(const aos_input_out *o, u32 kh, const circlePosition *cp);

/* Host (P1) or join (P2) a match; my_id = own engine character id, delay = input
 * delay in frames (host's value is used by both). The host's BGM selection
 * (music.h) is sent in START and played by both. */
void netplay_run(C3D_RenderTarget *top, PrintConsole *con, bool host, int my_id, int delay, int stage);

#endif
