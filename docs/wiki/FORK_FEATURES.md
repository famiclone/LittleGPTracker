# Features

Features added in `[famiclone/LittleGPTracker](https://github.com/famiclone/LittleGPTracker)` on top of the community Little Piggy Tracker line. Development targets `develop`; `master` holds stable merges.

Samples must be **8- or 16-bit PCM WAV** (stereo OK). 24-bit files will not load.

---

## Platforms

### TrimUI Brick (TG5040)

- Dedicated `PLATFORM=TG5040` / `make brick` build (Colima-friendly cross-compile path).
- Deep sleep on **KEY_POWER** with wake recovery (joystick re-init, power settle / cooldown so buttons stay alive after wake).
- Optional `LGPT_NO_NATIVE_SLEEP` / `minui-power-control` integration via launch scripts.
- Mic sample capture from the Import dialog (**Record**) on Brick.



### macOS

- `make PLATFORM=MACOS` convenience build.
- Keyboard and audio device routing fixes for SDL2 / RTAudio.

---



## Project, Settings, Appearance

Gearbox map stays `P G / S|L C P I / M T T`. Nested screens do **not** add a new letter; they keep highlighting **P**.

### Project screen

Project-only controls:

- **Name** — edit or randomize (same keyboard/Random flow as new project); renames the project folder on disk.
- Tempo, Master, Drive / soft-clip Type, Transpose, Scale.
- Compact Sequencer / Compact Instruments.
- Load / Save / Save Song As.
- **Settings** entry (also **R+Right**).
- Exit.



### Settings (under Project)

- **MIDI** — MIDI out device.
- **Render** — Off / Stereo / Stems (real-time WAV capture while playing; Start arms/finishes render).
- **Appearance** (also **R+Right**).
- Back / **R+Left** / **R+Up** → Project.



### Appearance

- Edit each palette slot (BACKGROUND, FOREGROUND, HICOLOR1/2, …) with live RGB + swatch.
- **Save Scheme** → `schemes/<name>.xml` (under the app root / `root:schemes`).
- **Load Scheme** from that directory.
- Last loaded/saved scheme is remembered in `bin:last_scheme` and reapplied on startup (after `config.xml` defaults).

Bundled schemes: `monokai`, `everforest`, `catppuccin-macchiato`, `msx`.

You can still set colours in `config.xml`; Appearance schemes override them when a last scheme is present.

---



## Song / UI chrome

- Song title strip (**Song/Live: name tempo**) removed; play timer stays visible.
- **BPM** shown under the play timer on Song.
- Gearbox Song cell shows **L** in Live mode and **S** in Song mode.

---



## Mixer

Mixer view: eight track columns plus a **Master** column.

Per channel (tracks + Master), rows typically include:

- Volume
- HPF mode
- LPF
- **DLY** — tempo-synced delay (step length + wet/feedback); persisted in project MIXER data
- **REV** — Schroeder-style reverb (size + wet); persisted with MIXER
- **Tape** — tape-style saturation / head-bump / HF roll-off send

Instrument-level **DEL / REV / TAPE** sends add into the channel FX path when set.

MIXER node is registered before project load so vol / filters / delay / reverb / tape restore correctly from `lgptsav.dat`.

---



## Instruments



### Shared

- Optional **user name** (persisted in the song; also used in `.ins` files).
- **Load / Save** instrument presets as `instruments/<name>.ins`.
- **Play** audition from the Instrument screen (retrigger / stop).
- Waveform / synth preview strip; param bar overlays.
- Boolean/list labels use **ON/OFF** (and similar short labels); older project strings still load.



### Sample instruments

- Loop modes: **NONE**, **LOOP**, **PINGPONG**, **OSC**, **SYNC**, **GRAIN**.
- **Slices** independent of loop mode; **slice to end** oneshot through sample end.
- **Reverse** instrument flag and phrase **RVRS** command.
- Filter **mode**: OFF / LOWPASS / HIGHPASS / BANDPASS / LP+HP (legacy original/bassy/scream labels still map on load).
- Sends: **DEL**, **REV**, **TAPE**.
- Instrument **timestretch** parameter; phrase **TSTR** command for grain/time-stretch control.
- Improved GRAIN playback (multi-grain windows, stretch behaviour).



### Synth instruments

Two engine modes:

- **SYNTH** — classic wavetable-style engine (pulse widths, saw, triangle, sine, noise, supersaw, overflow, …) with SIZE / MULT / WARP / SCAN style controls.
- **SHAPES** — macro shapes (CSAW, MORPH, FM, vowel, kick/snare/cymbal, chords, pads, …) with TIMBRE / COLOR / DEGRADE / REDUX.

Shared: filter mode/cut/res, amp, limiter (clip/sin/fold/wrap/post), chorus, DEL/REV/TAPE sends, envelopes, LFO, table.

### MIDI instruments

Unchanged in role; MIDI device selection lives under **Settings**.

### Phrase view

- Sample / synth waveform overlay in the phrase strip.
- Preview updates when synth engine params change (including SHAPES / size / stretch-related fields).

---



## Commands (additions)


| Cmd      | Meaning                                                                                                 |
| -------- | ------------------------------------------------------------------------------------------------------- |
| **RVRS** | Reverse playback (`00` toggle, `01` rev, `02` fwd from offset).                                         |
| **TSTR** | Time stretch: `aabb` — stretch toward `bb` (`7F` ≈ 1×) at speed `aa` (works with GRAIN / stretch path). |


Existing commands (ARPG, FLTR, LPOF, SLCE, …) remain as in upstream docs.

---



## Builds / tooling

- `make PLATFORM=MACOS`
- `make brick` / TG5040 Cross build path
- Sample rate guards and audio-path hardening around preview and Brick targets

---



## See also

- [What is Little Piggy Tracker](What-is-LittlePiggyTracker.md) — base UI reference (updated where fork behaviour differs)
- [config.xml](config_xml.md) — colours and global flags
- [tips and tricks](tips_and_tricks.md) — slices, oscillator, looper sync
- Root [CHANGELOG](../../CHANGELOG) — versioned notes for this fork

