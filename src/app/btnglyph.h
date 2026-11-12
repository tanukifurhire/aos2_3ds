/* 3DS button glyphs (citro2d): face buttons as coloured discs, L/R as slanted pills, SELECT as a small pill, and
 * the touch hyper panel as a small map with one button lit. Used by the pause move list (select_draw.c) and the
 * Controls / Hyper layout screens (controls_draw.c), so every on-screen control hint follows the player's
 * bindings (aos_input.h aos_binds_current) and hyper layout (hyper_touch.h). Each returns the drawn width. */
#ifndef AOS_APP_BTNGLYPH_H
#define AOS_APP_BTNGLYPH_H

/* physical button AOS_BTN_* at x (left edge), vertical centre cy, opacity a, scale sc (1 = move list size) */
float glyph_btn(float x, float cy, int btn, float a, float sc);
float glyph_btn_width(int btn, float sc);
/* the button bound to action AOS_ACT_* now */
float glyph_act(float x, float cy, int act, float a, float sc);
float glyph_act_width(int act, float sc);
/* a map of the touch hyper panel (X + hexagon) with slot `slot` of character `chara` lit where the layout puts it */
float glyph_touch(float x, float cy, int chara, int slot, float a, float sc);

#endif
