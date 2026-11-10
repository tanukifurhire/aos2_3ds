package main

// Asset-pass settings. This file is an input of the asset pass (assetSources): editing it re-packs.

// Sprite atlas scales and edge padding: see build.sh and docs/PERF.md ("Texture plan", "Atlas bleed").
var (
	packScales = "effectMdl=0.25,beamMdl=0.25,particleMdl=0.25,chara*=0.25,systemMdl/noise*=0.25,systemMdl/speedline=0.25,systemMdl/loading=0.25" +
		",*/hm_laser=0.5,*/ir_laser=0.5,*/ky_lsr=0.5,*/nk_lsr=0.5,*/nt_laser0*=0.5,*/sr_laser=0.5,*/sb_laser=0.5,*/sg_lsr0*=0.5,*/sg_slash=0.5,*/sa_laser*=0.5,*/sab_laser*=0.5"
	packEdge = "effectMdl,beamMdl,particleMdl,msgMdl,chara*"
)
