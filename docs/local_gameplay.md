# Local gameplay checkpoint

Run `gfx` from its executable directory. Use `gfx --teams 2`, `--teams 3`, or
`--teams 4` to choose the team count (four by default). Four local players are
assigned to those teams. Every member of a team uses that team's shape and colour.

## Controls

- 1-4: select Player 1 through Player 4.
- WASD: move. Mouse: aim. Hold left mouse: fire.
- R: reload the 30-round magazine. Reload takes 1.4 seconds. Reserve ammunition is unlimited.
- Shift while moving: boost at 1.75 times normal speed, for up to three seconds.
  Stamina starts recovering after 0.75 seconds without boosting, at one second
  of charge per second. Release Shift after exhaustion to boost again.

## What is drawn

The top cards show one score per configured team. A kill adds one point to the
attacker's team. Nameplates and small health bars identify living players. The
bottom panels show the selected player's health, kills, deaths, stamina and ammo.
White characters are spawn-protected. The open chevron shows the selected
player's aim. Short bright tracers travel quickly along the shot path.

Shots still use immediate hitscan damage. Tracer movement is visual feedback,
not projectile physics. The arena remains fixed when the window is resized.

## Player contact

Every shape uses radius 0.105 (75% of the previous 0.14). Visual scale was reduced
by the same factor so rotated shapes remain within that common hitbox. Living
players have equal mass: contact shares penetration correction equally. Opposing
movement cancels, moving players push idle players, and walls stop a pushing
chain. The fixed step and several contact passes keep the solver small and
readable. There is no general rigid-body physics engine or lingering momentum.
Respawning chooses a free position near the preferred spawn if it is occupied.

`Game::Update` accepts all players' intentions together. The local demo supplies
input for one player; the other overload is ready for independent inputs later.
Graphics and UI only read gameplay state.

## Validation

Build with CMake and run `ctest --test-dir build --output-on-failure` (add
`-C Release` for a multi-configuration build). Gameplay tests exercise contact,
walls, opposing movement, boost exhaustion/recovery, ammunition/reload, damage,
scoring, team assignments and respawn separation. Real window behaviour still
needs interactive checks, particularly at different display scales.

The HUD uses Dear ImGui's GLFW/OpenGL backends and bundled default font. Its MIT
license is copied into the packaged `assets/licenses` directory. Layout references:
[Day of Defeat HUD](https://www.dayofdefeat.com/Manual/hud.htm) and
[Teeworlds](https://www.teeworlds.com/?page=screenshots). No artwork was copied.
