/* Weak placeholders: each src/stage/st_<code>.c defines the real stage_def and
 * overrides these at link time. A stub stage draws nothing (gameplay clear colour). */
#include "stage.h"

#define STUB(sym, code, name) __attribute__((weak)) const stage_def sym = {code, name, 0, 0, 0, 0, 0, 0, 0};
STUB(stage_sl, "sl", "Menu")
STUB(stage_tr, "tr", "?")
STUB(stage_bf, "bf", "?")
STUB(stage_gr, "gr", "?")
STUB(stage_dr, "dr", "?")
STUB(stage_ri, "ri", "?")
STUB(stage_se_def, "se", "?")
STUB(stage_br, "br", "?")
STUB(stage_ct, "ct", "?")
STUB(stage_iw, "iw", "?")
STUB(stage_ct2, "ct", "?")
STUB(stage_ft, "ft", "?")
STUB(stage_gz, "gz", "?")
STUB(stage_hl, "hl", "?")
STUB(stage_ml, "ml", "?")
STUB(stage_sh, "sh", "?")
