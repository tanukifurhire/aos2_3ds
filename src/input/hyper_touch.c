/* Touch-screen hyper buttons -> per-frame input mask (hyper_touch.h). */
#include "hyper_touch.h"

#include <stddef.h>
#include <string.h>

#define K(c, n) "COMMANDLIST_" c "_" n
#define M1(k) {k, NULL, 1.0f}
#define M1A(k, a) {k, a, 1.0f}
#define M3(k) {k, NULL, 3.0f}
#define M3A(k, a) {k, a, 3.0f}
#define NONE {NULL, NULL, 0.0f}

/* Hyper rows of each character's standard dispatch, by slot (UP S+H, LEFT H, RIGHT B+H, DOWN D+H, CENTER
 * S+B+H), names from the PC's pause command list (FUN_00756600 & co., docs/re/<char>.md "Hypers"). Sora's
 * D+H (0x17, the random-fan Hyper Spread, sora.md §2) shares the name of H, so its slot is left empty (user
 * choice; the H button clears a held L so it always gives the plain one). Suguri has no D+H row (D held + H = Hyper Rifle); Sumika's second form has no S+B+H. */
static const ht_move slots[AOS_NUM_CHARS][HT_NSLOT] = {
	[AOS_CH_SORA] = {M1(K("SORA", "BOISTEROUSDANCE")), M1(K("SORA", "HYPERSPREAD")), M1(K("SORA", "HYPERLAUNCHER")),
	                 NONE, M3(K("SORA", "UNLIMITEDCHARGE"))},
	[AOS_CH_ALTE] = {M1(K("ALTE", "SHOCKRODPUNISHER")), M1A(K("ALTE", "SEARCHLIGHTPANEL"), K("ALTE", "SLPSPARK")),
	                 M1(K("ALTE", "SALVOFIRE")), M1(K("ALTE", "SHOCKBALL")), M3(K("ALTE", "DESPERATION"))},
	[AOS_CH_TSIH] = {M1(K("TSIH", "STEALTHON")), M1(K("TSIH", "AWHOLELOTOFBENDYBEAMS")), M1(K("TSIH", "ALOTOFROCKS")),
	                 M1(K("TSIH", "ROUNDINGWIRELASER")), M3(K("TSIH", "THROWHUGEROCKS"))},
	[AOS_CH_MIRA] = {M1A(K("MIRA", "DIVINESPEEDSLASH"), K("MIRA", "BAITINGCHAKRAMS")), M1(K("MIRA", "WRATHFULTORNADO")),
	                 M1A(K("MIRA", "CIRCLINGCHAKRAMS"), K("MIRA", "SKYSOARINGCHAKRAMS")),
	                 M1(K("MIRA", "PURGATORIALSMOKESCREEN")), M3(K("MIRA", "TWINDRAGONTORNADO"))},
	[AOS_CH_SHAM] = {M1(K("SHAM", "DWARFRUSH")), M1(K("SHAM", "POLYSPRITE")), M1(K("SHAM", "BOUNDINGCUBE")),
	                 M1(K("SHAM", "DELTASPRITE")), M3(K("SHAM", "ARACHNECALL"))},
	[AOS_CH_NATH] = {M1(K("NATH", "EXTENSION")), M1(K("NATH", "METALPOUNDING")), M1(K("NATH", "CARPETBOMBING")),
	                 M1(K("NATH", "LASERBITDEPLOYMENT")), M3(K("NATH", "BIRDFORM"))},
	[AOS_CH_STARBREAKER] = {M1(K("STARBREAKER", "ACCEPTYOURDOOM")), M1(K("STARBREAKER", "LETTHESEAOFFLAMEEMBRACEYOU")),
	                        M1(K("STARBREAKER", "TAKEYOURDYINGBREATH")),
	                        M1(K("STARBREAKER", "SUFFERFROMTHEINEVITABLEDEATHCRISIS")),
	                        M3(K("STARBREAKER", "WITNESSTHEFINALMOMENTOFTHISPLANET"))},
	[AOS_CH_SUGURI] = {M1(K("SUGURI", "HYPERSWORD")), M1(K("SUGURI", "HYPERRIFLE")), M1(K("SUGURI", "HYPERARROW")), NONE,
	                   M3(K("SUGURI", "ACCELBURST"))},
	[AOS_CH_SAKI] = {M1(K("SAKI", "ROLLINGTAMBOURINE")), M1(K("SAKI", "CYCLONECYMBAL")), M1(K("SAKI", "SUNSHINEBELL")),
	                 M1(K("SAKI", "GALAXYBELL")), M3(K("SAKI", "BIGBANGBELL"))},
	[AOS_CH_IRU] = {M1(K("IRU", "WIDEAREAELECTRICFIELD")), M1(K("IRU", "HIGHOUTPUTLINEARRAY")), M1(K("IRU", "GENOCIDEBOMB")),
	                M1(K("IRU", "FULLFIREBULLETCURTAIN")), M3(K("IRU", "DESTRUCTIONRAY"))},
	[AOS_CH_NANAKO] = {M1(K("NANAKO", "TENTACLESTING")), M1(K("NANAKO", "TENTACLEBREATHONEBYONE")),
	                   M1(K("NANAKO", "FLOWERCROWN")), M1(K("NANAKO", "TENTACLEBREATHALLATONCE")),
	                   M3(K("NANAKO", "SPRINGSTORM"))},
	[AOS_CH_KAE] = {M1A(K("KAE", "BLAZING"), K("KAE", "HYPERWHAM")), M1(K("KAE", "HYPERZOOM")), M1(K("KAE", "HYPERBANG")),
	                M1(K("KAE", "HYPERPOW")), M3(K("KAE", "WHAM"))},
	[AOS_CH_KYOKO] = {M1A(K("KYOKO", "TREEOFICE"), K("KYOKO", "WATERMIRROR")), M1(K("KYOKO", "EARLYSUMMERRAIN")),
	                  M1(K("KYOKO", "ICEOVER")), M1(K("KYOKO", "FREEZING")), M3(K("KYOKO", "AVALANCHE"))},
	[AOS_CH_HIME] = {M1(K("HIME", "UNTOUCHABLE")), M1(K("HIME", "AMBER")), M1(K("HIME", "LIFELONGBURNING")),
	                 M1(K("HIME", "LEASH")), M3(K("HIME", "GODKNOWS"))},
	[AOS_CH_SUMIKA] = {M1(K("SUMIKA", "ANGELWING")), M1(K("SUMIKA", "MYSTICBLAST")),
	                   M1A(K("SUMIKA", "TROPICALBALL"), K("SUMIKA", "COQUETTISHSWORD")),
	                   M1A(K("SUMIKA", "OVERLIMIT"), K("SUMIKA", "HEAVENSFEATHER")),
	                   M3A(K("SUMIKA", "PRETTYJUSTICE"), K("SUMIKA", "ETERNALFEATHER"))},
	[AOS_CH_SUMIKA2] = {M1(K("SUMIKA", "ANGELWING")), M1(K("SUMIKA", "MYSTICBLAST")),
	                    M1A(K("SUMIKA", "TROPICALBALL"), K("SUMIKA", "COQUETTISHSWORD")),
	                    M1A(K("SUMIKA", "OVERLIMIT"), K("SUMIKA", "HEAVENSFEATHER")), NONE},
};

const ht_move *ht_slot(int chara, int slot)
{
	static const ht_move none = NONE;
	if (chara < 0 || chara >= AOS_NUM_CHARS || slot < 0 || slot >= HT_NSLOT)
		return &none;
	return &slots[chara][slot];
}

int ht_pos_slot(int chara, int pos)
{
	if (chara < 0 || chara >= AOS_NUM_CHARS || pos < 0 || pos >= HT_NSLOT)
		return pos;
	const ht_move *s = slots[chara];
	/* left/right are under the thumbs: an empty one takes a top/bottom move (bottom first), so the gap ends up at
	 * the top or bottom */
	const int side[2] = {HT_LEFT, HT_RIGHT};
	int donor[2] = {-1, -1};
	bool used_d = false, used_u = false;
	for (int i = 0; i < 2; i++)
		if (!s[side[i]].key) {
			if (s[HT_DOWN].key && !used_d)
				donor[i] = HT_DOWN, used_d = true;
			else if (s[HT_UP].key && !used_u)
				donor[i] = HT_UP, used_u = true;
		}
	for (int i = 0; i < 2; i++)
		if (donor[i] >= 0 && (pos == side[i] || pos == donor[i]))
			return pos == side[i] ? donor[i] : side[i];
	return pos;
}

bool ht_ready(const aos_world *w, int player, int slot)
{
	const aos_player *p = &w->p[player & 1];
	const ht_move *m = ht_slot(p->id, slot);
	/* aos_may_hyper (FUN_00780950) only reads the two players */
	return m->key && p->hyper >= m->cost && aos_may_hyper((aos_world *)w, (aos_player *)p);
}

void ht_init(ht_state *s)
{
	memset(s, 0, sizeof *s);
	s->slot = -1;
}

uint16_t ht_apply(ht_state *s, int chara, int scheme, int slot, uint16_t mask)
{
	const uint16_t phys = mask;
	if (slot < 0 || slot >= HT_NSLOT || !ht_slot(chara, slot)->key) {
		mask &= (uint16_t)~s->suppress;
		s->suppress &= phys; /* a held-back button comes back only after it was let go */
		s->slot = -1;
		s->age = 0;
		s->last = mask;
		return mask;
	}
	if (slot != s->slot)
		s->age = 0;
	if (chara == AOS_CH_SUMIKA2)
		scheme = 0; /* FUN_00771d10 serves both schemes */
	uint16_t set, clear = 0, sticky = 0;
	if (scheme == 0) {
		/* FUN_0075a030 & co.: first match of S+B+H, S+H, B+H, D+H, H (Iru: D+S+H before S+H — a held L
		 * picks the dash version, iru.md §2) */
		bool has_d = ht_slot(chara, HT_DOWN)->key != NULL || chara == AOS_CH_SORA; /* Sora: D+H row kept off the panel */
		switch (slot) {
		case HT_CENTER: set = AOS_IN_S | AOS_IN_B | AOS_IN_H; break;
		case HT_UP: set = AOS_IN_S | AOS_IN_H; clear = AOS_IN_B; break;
		case HT_RIGHT: set = AOS_IN_B | AOS_IN_H; clear = AOS_IN_S; break;
		case HT_DOWN: set = AOS_IN_D | AOS_IN_H; clear = AOS_IN_S | AOS_IN_B; break;
		default: set = AOS_IN_H; clear = (uint16_t)(AOS_IN_S | AOS_IN_B | (has_d ? AOS_IN_D : 0)); break;
		}
	} else {
		/* FUN_0075a4b0 & co.: H held (>1, the press frame counts) and the trigger pressed (==3) */
		uint16_t trig = slot == HT_CENTER ? AOS_IN_SHIELD
		              : slot == HT_UP     ? AOS_IN_S
		              : slot == HT_RIGHT  ? AOS_IN_B
		              : slot == HT_DOWN   ? AOS_IN_D
		                                  : AOS_IN_A;
		set = AOS_IN_H | trig;
		if (s->age == 0 && (s->last & trig)) { /* already down: let it up for one frame so it is pressed next */
			set = AOS_IN_H;
			clear = trig;
		} else {
			/* the rows test the triggers in this order, first press wins: hold back a physical press of another
			 * trigger on the frame ours is pressed (it comes through one frame later, while the hyper runs) */
			const uint16_t trigs = AOS_IN_SHIELD | AOS_IN_S | AOS_IN_B | AOS_IN_D | AOS_IN_A;
			if (!(s->last & trig))
				clear = (uint16_t)(trigs & ~trig & ~s->last);
		}
		/* Iru's advanced row 3 is "S held && H held" without an edge (iru.md §2): a held X would turn every
		 * other button into Wide-Area Electric Field */
		if (chara == AOS_CH_IRU && slot != HT_UP && slot != HT_CENTER)
			sticky = AOS_IN_S;
	}
	if (scheme == 0)
		sticky = clear;
	s->suppress |= (uint16_t)(sticky & phys); /* comes back only after a release */
	s->suppress &= phys;
	mask = (uint16_t)((mask & ~s->suppress & ~clear) | set);
	s->slot = (int8_t)slot;
	if (s->age < 0xff)
		s->age++;
	s->last = mask;
	return mask;
}
