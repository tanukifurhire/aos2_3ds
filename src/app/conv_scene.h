/* Story conversation scene (scene 5, docs/re/modes.md §C) on the 3DS: the
 * renderer + scene glue around src/modes/conv.c, and a port-only test picker
 * that opens any of the 56 scripts directly (main menu: Y). */
#ifndef AOS_APP_CONV_SCENE_H
#define AOS_APP_CONV_SCENE_H

#include <stdbool.h>

/* Port-only scene id of the conversation test picker (scene.h ids 20..23 taken). */
#define SCENE_CONV_TEST 27

/* Registers SCENE_CONVERSATION (5) and SCENE_CONV_TEST. In story mode the
 * scene reads its script id / backdrop stage from modes_flow_conv_script() /
 * modes_flow_conv_backdrop() and leaves through modes_flow_conv_exit(). */
void conv_scenes_register(void);

/* Language of the dialogue font/texts: DAT_00881ec0 value (LANG_*). True if
 * its dialogue font is in romfs (en/ptbr/sp share "dialogue"). */
bool conv_lang_has_font(int lang);

#endif
