# CS2 Rock The Vote

![Downloads](https://img.shields.io/github/downloads/FemboyKZ/mm-cs2rockthevote/total?style=flat-square) ![Last commit](https://img.shields.io/github/last-commit/FemboyKZ/mm-cs2rockthevote?style=flat-square) ![Open issues](https://img.shields.io/github/issues/FemboyKZ/mm-cs2rockthevote?style=flat-square) ![Closed issues](https://img.shields.io/github/issues-closed/FemboyKZ/mm-cs2rockthevote?style=flat-square) ![Size](https://img.shields.io/github/repo-size/FemboyKZ/mm-cs2rockthevote?style=flat-square) ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/FemboyKZ/mm-cs2rockthevote/build.yml?style=flat-square)

CS2 RTV Plugin using Metamod: Source

## Usage

### Requirements

* CS2 Dedicated Server
* [Metamod: Source 2.0](https://www.metamodsource.net/downloads.php?branch=dev)
* (Optional\*) [mm-cs2admin](https://github.com/FemboyKZ/mm-cs2admin)
* (Optional\*\*) [mm-cs2whitelist](https://github.com/FemboyKZ/mm-cs2whitelist)
* (Optional\*\*\*) [mm-cs2menus](https://github.com/FemboyKZ/mm-cs2menus)

\*Admin commands like `reloadrtv` and `mapmenu` will not work without it.

\*\*When loaded alongside cs2rtv, only players that are whitelisted can use !rtv.
This stops players from spamming it on join before getting kicked to trigger a vote maliciously.

\*\*\*When loaded alongside cs2rtv, the plugin will use cs2menus' menus instead of built in ones.

### Install

1. Download the [latest release](https://github.com/FemboyKZ/mm-cs2rockthevote/releases/latest) and extract it in your server's root folder (`/game/csgo/`).
2. Configure the plugin in `core.cfg` and the maplist in `maplist.txt`

### Configuration

* `/cfg/cs2rtv/core.cfg` - Main config file
* `/cfg/maplist.txt` - Maplist file

## Build

### Prerequisites

* This repository is cloned recursively (ie. has submodules)
* [python3](https://www.python.org/)
* [ambuild](https://github.com/alliedmodders/ambuild), make sure ``ambuild`` command is available via the ``PATH`` environment variable;
* MSVC (VS build tools)/Clang installed for Windows/Linux.

### AMBuild

```bash
mkdir -p build && cd build
python3 ../configure.py --enable-optimize
ambuild
```

## Credits

* [SourceMod](https://github.com/alliedmodders/sourcemod)
* [zer0.k's MetaMod Sample plugin fork](https://github.com/zer0k-z/mm_misc_plugins)
* [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod)
