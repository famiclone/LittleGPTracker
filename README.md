# Swine Tracker

![Swine Tracker](docs/st_banner.png)

**Swine Tracker** (f.k.a. *Little Piggy Tracker* / *LittleGPTracker*) is a music tracker optimised to run on portable game consoles. It is currently running on Windows, MacOS (intel/arm) & Linux, PSP, Miyoo Mini, and a collection of other retro gaming handhelds.

It implements the user interface of [littlesounddj](https://www.littlesounddj.com/lsd/index.php) and precedes [M8 tracker](https://www.dirtywave.com), two popular trackers greatly loved in the tracker community.

All versions are available for free under the [GPLv3 License](LICENSE). If you like the project and want to contribute, don't hesitate to make a pull request for this repo.

## About This Fork

This repository (`famiclone/SwineTracker`) is developed independently. It remains a derivative of the work of the original author `m-.-n` aka [Marc Nostromo](https://github.com/Mdashdotdashn/LittleGPTracker), and later community forks; their releases of the source code laid the foundation for everything here.

Active development happens on **`develop`**. **`main`** tracks stable merges. There is no ongoing sync with other public forks unless explicitly decided.

All implemented features have been tested not to break old
projects but make sure to backup your old cherished work
just to be safe &#9829;

## Releases

### Current Builds

Latest releases from this fork:

- [Releases](https://github.com/famiclone/SwineTracker/releases)


## Documentation

All the relevant documentation can be found in [Docs](docs) directory.

Recommended reading to get you started:

- [**Famiclone fork features**](docs/wiki/FORK_FEATURES.md) — Settings/Appearance, mixer FX, synth/SHAPES, Brick, commands
- [What is Swine Tracker](docs/wiki/What-is-SwineTracker.md)
- [Quick-Start Guide](docs/wiki/quick_start_guide.md)
- [Swine Tracker Configuration](docs/SwineTrackerConf.md)
- [Tips and Tricks](docs/wiki/tips_and_tricks.md)
- [CHANGELOG](CHANGELOG)

## Features per platform

| Platform    | MIDI_Possible | MIDI_enabled | Soundfonts | Note                                 |
|-------------|---------------|--------------|------------|--------------------------------------|
| PSP         | NO            | NO           | YES        | [See notes](projects/resources/PSP/INSTALL_HOW_TO.txt) |
| DEB         | YES           | YES          | YES        |                                      |
| X64         | YES           | YES          | MAYBE      |                                      |
| X86         | YES           | YES          | YES        |                                      |
| STEAM       | YES           | YES          | MAYBE      |                                      |
| MIYOO       | NO            | NO           | YES        | Port by [Nine-H](https://ninethehacker.xyz) |
| W32         | YES           | YES          | YES        | Built in VS2008 with love            |
| RASPI       | YES           | YES          | YES        | Versatile platform                   |
| CHIP        | YES           | YES          | YES        | [See notes](projects/resources/CHIP/INSTALL_HOW_TO.txt) |
| BITTBOY     | MAYBE         | NO           | YES        |                                      |
| PORTMASTER  | MAYBE         | NO           | NO         | Not currently maintained in this repo, reach out to the [maintainers](https://discord.gg/JxYBp9HTAY) with questions related to Portmaster |
| GARLIC      | NO            | NO           | NO         | No longer maintained, use Portmaster|
| GARLICPLUS  | MAYBE         | NO           | YES        | Port by [Simotek](http://simotek.net)|
| RG35XXPLUS  | MAYBE         | NO           | YES        | Port by [Simotek](http://simotek.net)|
| MACOS       | YES           | YES          | MAYBE      | Port by [clsource](https://genserver.social/clsource); `make PLATFORM=MACOS` |
| TG5040      | MAYBE         | NO           | MAYBE      | TrimUI Brick: sleep, mic Record, `make brick` |

* **MIDI functionality __greatly__ depends on kernel support, please feature request your favourite OS maintainer =)**
* **Install ffmpeg by following install instructions for your platform [here](https://www.ffmpeg.org/download.html)**
* **PrintFX requires full ffmpeg. If marked as TBA, it requires a redesign using [libav](https://trac.ffmpeg.org/wiki/Using%20libav*)**
