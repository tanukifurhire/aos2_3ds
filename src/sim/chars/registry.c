/* Character registration. Each character module (src/sim/chars/<name>.c)
 * defines a strong aos_register_<name>() that fills its entries in aos_tab /
 * aos_otab (the equivalent of its part of FUN_0049ae40). The weak defaults
 * below let the engine link while characters are still being ported. */
#include "../sim.h"

#define WEAK __attribute__((weak))

WEAK void aos_register_sora(void) {}
WEAK void aos_register_alte(void) {}
WEAK void aos_register_tsih(void) {}
WEAK void aos_register_mira(void) {}
WEAK void aos_register_sham(void) {}
WEAK void aos_register_nath(void) {}
WEAK void aos_register_starbreaker(void) {}
WEAK void aos_register_suguri(void) {}
WEAK void aos_register_saki(void) {}
WEAK void aos_register_iru(void) {}
WEAK void aos_register_nanako(void) {}
WEAK void aos_register_kae(void) {}
WEAK void aos_register_kyoko(void) {}
WEAK void aos_register_hime(void) {}
WEAK void aos_register_sumika(void) {}

/* CPU AI registration (per character, src/sim/chars/ai_<name>.c): each fills
 * aos_tab.cpu[id][0..2] (table 0x881400 + id*0x10 + diff*4). Weak defaults. */
WEAK void aos_register_ai_sora(void) {}
WEAK void aos_register_ai_alte(void) {}
WEAK void aos_register_ai_tsih(void) {}
WEAK void aos_register_ai_mira(void) {}
WEAK void aos_register_ai_sham(void) {}
WEAK void aos_register_ai_nath(void) {}
WEAK void aos_register_ai_starbreaker(void) {}
WEAK void aos_register_ai_suguri(void) {}
WEAK void aos_register_ai_saki(void) {}
WEAK void aos_register_ai_iru(void) {}
WEAK void aos_register_ai_nanako(void) {}
WEAK void aos_register_ai_kae(void) {}
WEAK void aos_register_ai_kyoko(void) {}
WEAK void aos_register_ai_hime(void) {}
WEAK void aos_register_ai_sumika(void) {}

void aos_register_chars(void)
{
	aos_register_sora();
	aos_register_alte();
	aos_register_tsih();
	aos_register_mira();
	aos_register_sham();
	aos_register_nath();
	aos_register_starbreaker();
	aos_register_suguri();
	aos_register_saki();
	aos_register_iru();
	aos_register_nanako();
	aos_register_kae();
	aos_register_kyoko();
	aos_register_hime();
	aos_register_sumika(); /* also registers the second form (id 18) */
	/* CPU AI (after the character tables are filled) */
	aos_register_ai_sora();
	aos_register_ai_alte();
	aos_register_ai_tsih();
	aos_register_ai_mira();
	aos_register_ai_sham();
	aos_register_ai_nath();
	aos_register_ai_starbreaker();
	aos_register_ai_suguri();
	aos_register_ai_saki();
	aos_register_ai_iru();
	aos_register_ai_nanako();
	aos_register_ai_kae();
	aos_register_ai_kyoko();
	aos_register_ai_hime();
	aos_register_ai_sumika(); /* ids 17 and 18 */
}
