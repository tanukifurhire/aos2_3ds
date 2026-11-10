# AoS2 move lists (from the official "Characters & Techniques.pdf")

Original notation: **A** = Attack (Weapon A), **B** = Sub-weapon (Weapon B), **S** = Special,
**D** = Dash (held), **H** = Hyper. `(Hold)` = hold the button to charge.
15 playable characters; texture folders `charaXxxA/B/C` are colour variants of the same character.

## 3DS translation (user mapping, see aos_input.h, hyper_touch.h; revised 2026-11-10)

| Original | 3DS |
|---|---|
| A / B | A / B |
| S+A / S+B | X / Y |
| D+A, D+S+A … | hold L (Dash) + button |
| H / A+H | touch: left button |
| B+H | touch: right button |
| S+H | touch: top button (Iru: hold L for the dash version) |
| D+H | touch: bottom button |
| S+B+H (Accel Hyper) | touch: centre button |
| Shield | R (or L + R together) |
| Hyper Dash | L, L |

The bottom screen shows the five buttons with the move names (commandlist_names.txt), their cost and the
hyper gauge; the pause menu's Move list draws the same mapping as button glyphs (select_draw.c).

★/☆ = only available while a stated condition holds (noted per character).

## 1. Suguri (`charaSuguri`, prefix `sg_`)
| Input | Move |
|---|---|
| A | Beam Rifle |
| A (Hold) | Charged Rifle |
| D+A | Beam Rifle (Rapid-fire) |
| B | Missile (Homing) |
| B (Hold) | Missile (Sidewinder) |
| S+A | Sword (Close Range) / Sword Arrow (Long Range) |
| S+B | Sword Break |
| A+H | Hyper Rifle |
| B+H | Hyper Arrow |
| S+H | Hyper Sword |
| S+B+H | Accel Burst (Accel Hyper) |

## 2. Sora (`charaSora`)
| A | Beam Rifle | | A (Hold) | Charged Rifle |
|---|---|---|---|---|
| D+A | Rifle Spread | | B | Launcher (Homing) |
| B (Hold) | Launcher (Staggered Launch) | | S+A | Sword |
| S+B | Slash Through | | A+H | Hyper Spread |
| B+H | Hyper Launcher | | S+H | Boisterous Dance |
| S+B+H | Unlimited Charge (Accel) | | | |

## 3. Alte (`charaAlte`) — ★ only while the Searchlight Panel is deployed
| A (Hold) | Submachine Gun (D) | | B | Deploy Beam Panels |
|---|---|---|---|---|
| B (Hold) | Missile Shower | | ★ S (Hold) | Searchlight Shield |
| S+A | Shock Rod | | S+B | Claymore |
| H | Searchlight Panel | | ★ H | SLP Spark |
| D+H | Shock Ball | | B+H | Salvo Fire |
| S+H | Shock Rod Punisher | | S+B+H | Alte Desperation Attack (Accel) |

## 4. Tsih (`charaTsih`)
| A | Bendy Beam | | A (Hold) | Beam Ball Scattering |
|---|---|---|---|---|
| D+A | A Lot of Bendy Beams | | B | Throw a Rock |
| S+A | Wire Laser | | S+B | Swirl, Swirl and Release |
| H | A Whole Lot of Bendy Beams | | D+H | Rounding Wire Laser |
| B+H | A Lot of Rocks | | S+H | Stealth On |
| S+B+H | Throw Huge Rocks (Accel) | | | |

## 5. Nanako (`charaNanako`)
| A | Tentacle Tone | | A (Hold) | Tentacle Needle |
|---|---|---|---|---|
| D+A | Tentacle Fruit | | B | Ether Leaf |
| B (Hold) | Ether Bug | | S (Hold) | Deploy Bits |
| S+B | Shell Shot | | H | Tentacle Breath (One by One) |
| D+H | Tentacle Breath (All at Once) | | B+H | Flower Crown |
| S+H | Tentacle Sting | | S+B+H | Spring Storm (Accel) |

## 6. Kae (`charaKae`) — ★ only while Blazing mode is on
| A | \*Slash!\* | | D+A | \*Sla-slaaaash!\* |
|---|---|---|---|---|
| B (Hold) | Pow! / ★ Fooom! | | S+A | Zooooom! (★ variant listed, unnamed in PDF) |
| S+B | Baaang! | | H | ZOOOOOM! |
| D+H | POW! | | B+H | BA-BA-BAANG! |
| S+H | Blazing! / ★ Whaaam! | | S+B+H | WHAAAAM! (Accel) |

## 7. Mira (`charaMira`) — ★ only while chakrams are deployed
| A (Hold) | Scorching Beam Ninja Stars | | D+A | Gleaming Caltrops |
|---|---|---|---|---|
| B | Glittering Kunai | | B (Hold) | Counterattack Kiri |
| S+A | Quick Slash / ★ Swift Chakrams | | S+B | Binding Smoke / ★ Soaring Chakrams |
| H | Wrathful Tornado | | D+H | Purgatorial Smokescreen |
| B+H | Circling Chakrams / ★ Sky-Soaring Chakrams | | S+H | Divine Speed Slash / ★ Baiting Chakrams |
| S+B+H | Twin Dragon Tornado (Accel) | | | |
(PDF lists no plain-A move for Mira; A tap behaviour to be confirmed in code.)

## 8. Iru (`charaIru`)
| A | High Speed Photon Rifle | | A (Hold) | Scatter-shot Photon Rifle |
|---|---|---|---|---|
| B (Hold) | Straight Rockets | | (D)+S+A | Electromagnetic Field |
| S+B | Homing Missile Pod | | H | High Output Linear Ray |
| D+H | Full-Fire Bullet Curtain | | B+H | Genocide Bomb |
| (D)+S+H | Wide-Area Electric Field | | S+B+H | Destruction Ray (Accel) |

## 9. Saki (`charaSaki`)
| A | Tornado Cymbal | | D+A | Stardust Bell |
|---|---|---|---|---|
| B | Tambourine Boomerang | | B (Hold) | Maracas Grenade |
| S+A | Maracas Hammer | | D+S+A | Maracas Swing |
| S+B | Tambourine Blade | | H | Cyclone Cymbal |
| D+H | Galaxy Bell | | B+H (Hold) | Sunshine Bell |
| S+H | Rolling Tambourine | | S+B+H | Big Bang Bell (Accel) |

## 10. Sham (`charaSham`) — ☆ while dwarfs deployed, ★ while sprites deployed
| A (Hold) | Sprite Call | | D+A | Beam Ribbon / ★ Sprite Release |
|---|---|---|---|---|
| B | Mysterious Cube | | S+A (Hold) | Resistant Sphere |
| S+B | Dwarf Call / ☆ Dwarf Tackle | | H | Poly Sprite |
| D+H | Delta Sprite | | B+H | Bounding Cube |
| S+H | Dwarf Rush | | S+B+H | Arachne Call (Accel) |

## 11. Kyoko (`charaKyoko`)
| A | Drizzling Rain | | A (Hold) | Hard Rain |
|---|---|---|---|---|
| D+A | Clean Stream | | B (Hold) | Icy Crystal |
| S+A | Icicle (Close) / Ripple (Long) | | S+B | Crystal |
| H | Early Summer Rain | | D+H | Freezing |
| B+H | Ice Over | | S+H | Tree of Ice (Close) / Water Mirror (Long) |
| S+B+H | Avalanche (Accel) | | | |

## 12. Nath (`charaNath`) — two phases; PDF lists Phase 1
| A | Spread Beam | | D+A | Reflecting Beam |
|---|---|---|---|---|
| B | Homing Bombing | | S+A | Metal Fist |
| S+B | Beam Reflector / Extension | | H | Metal Pounding |
| D+H | Laser Bit Deployment | | B+H | Carpet Bombing |
| S+B+H | Bird Form (Accel) | | | |
(Phase 2 moves are not in the PDF — recover from code.)

## 13. Hime (`charaHime`) — two phases; PDF lists Phase 1
| A | Faint Breeze | | A (Hold) | Needle Fruit |
|---|---|---|---|---|
| D+A | Faint Breeze (Plenty) | | B | Paradise of Faint Pain |
| B (Hold) | Fever | | S+A | Delicate Binding |
| S+B | Sadism / Untouchable | | H | Amber |
| D+H | Leash | | B+H | Life-long Burning |
| S+B+H | God Knows (Accel) | | | |

## 14. Star Breaker (`charaStar`)
| A | Burn in Crimson Flames | | D+A | Fear the Approaching Sparks |
|---|---|---|---|---|
| B | Count Your Last Moments | | B (Hold) | Shiver in the Invisible Murderous Intent |
| S+A | Melt in the Azure Flame | | S+B | Be Charred in the Burning Death Crisis |
| H | Let the Sea of Flame Embrace You | | D+H | Suffer from the Inevitable Death Crisis |
| B+H | Take Your Dying Breath | | S+H | Accept Your Doom |
| S+B+H | Witness the Final Moment of This Planet (Accel) | | | |

## 15. Sumika (`charaSumika`) — ☆ while extra feathers in field, ★ while enemy is in a barrel
| A (Hold) | Sumika Pretty Sword | | D+A | Sumika Fantastic Feather |
|---|---|---|---|---|
| B | Sumika Heartful Arrow | | B (Hold) | Sumika Marble Ball |
| S+A | Sumika Shiny Feather | | S+B | Sumika Happy Barrel |
| H | Sumika Mystic Blast | | D+H | Sumika Over Limit / ☆ Heaven's Feather |
| B+H | Sumika Tropical Ball / ★ Coquettish Sword | | S+H | Sumika Angel Wing |
| S+B+H | Sumika Pretty Justice / ☆ Eternal Feather (Accel) | | | |

## Notes
- The PDF text was extracted programmatically; a few entries on dense pages (Kae, Mira, Nath,
  Hime) lost their pairing — the code is the source of truth and these tables get corrected
  as each character is reverse engineered.
- Missing from the PDF: system mechanics (HP, heat, hyper gauge, shield, dash), all numbers.
