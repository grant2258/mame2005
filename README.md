This is just a core for my personal use no real plans for it, there is no affiliation with libretro.


# todo
* audiobuffer frameskip needs added
* update all system16
* hook up propper tgp for vr
* add relative and abs mouse definitions problem is RA doesnt really provide this information look into it.

# cpu cores
* updated v60 cpu core to mame0114
* added mb86233 cpu core from mame0114

# audio cores
* update rf5c68 soundcore to mame096u4
* update okim6295.c to 111u3 for pin support also added stream_set_sample_rate and updated all drivers using it also updated qsound core.

# porting notes
* removed the need cpuintrf_temp_str() in cpu interface
* added BITMAP_ADDR8, BITMAP_ADDR16, BITMAP_ADDR32 macros
* added ROM_IGNORE a while back fixed it so its working properly
* added STEP2 and STEP32 macros
* BITNEW macro
* hooked up cpu interface divider no idea why it wasnt hooked up.
* added pal1bit(1-7) macros

# games now working (added or updated to working status)

# added
* top roller

# updated all pengo sets

# sega system18
* hammer away prototype

# sega system32
* update to 097u5 for testing against plus issues

# model1
* Virtua Fighter
* Virtua Racing
* Virtua Formula


# cps1 and cps2
* updated all cps2 sets
* most of the updates are done. Still need to add a few hacks and some inputs need fixed some sf2 hacks are running fast because of this.

# ssv driver ( backports from mame0114)
* Change Air Blade (Japan)
* Dramatic Adventure Quiz Keith & Lucy (Japan)
* Drift Out '94 - The Hard Order (Japan)
* Dyna Gear
* Eagle Shot Golf"
* Gourmet Battle Quiz Ryohrioh CooKing (Japan)
* Koi Koi Shimasho 2 - Super Real Hanafuda (Japan)
* Lovely Pop Mahjong JangJang Shimasho 2 (Japan)
* Lovely Pop Mahjong JangJang Shimasho (Japan)
* Mahjong Hyper Reaction 2 (Japan)
* Mahjong Hyper Reaction (Japan)
* Meosis Magic (Japan)
* Mobil Suit Gundam Final Shooting (Japan)
* Pachinko Sexy Reaction 2 (Japan)
* Pachinko Sexy Reaction (Japan)
* Storm Blade (US)
* Super Real Mahjong P7 (Japan)
* Super Real Mahjong PIV (Japan)
* Super Real Mahjong PIV (Japan, older set)
* Survival Arts (USA)
* Survival Arts (World)
* Twin Eagle II - The Rescue Mission
* Ultra X Weapons / Ultra Keibitai
* Vasara
* Vasara 2 (set 1)
* Vasara 2 (set 2)
* Visco / Datt Japan", "Monster Slider (Japan)


# other fixes
* Fixed 64street attract mode
* Fixed Vaportra game over screen
