# Twini-Golf (3DS homebrew edition)

Twini-Golf is originally a game created in 48 hours for the [2021 GMTK Game Jam](https://itch.io/jam/gmtk-2021) using C++ and [SDL2](https://www.libsdl.org/). It can be played on [itch.io](https://polymars.itch.io/twini-golf).
The 3DS edition uses C++, libctru, libcitro3d and libcitro2d for graphics, along with mpg123 for loading the sound effects.

## Background
Twini-Golf is an experimental mini golf game where you play on multiple golf courses at once, simultaneously controlling each ball. More information on how to play is available on the game's [itch.io page](https://polymars.itch.io/twini-golf).
The 3DS edition is played by using the Circle Pad as a direction selector, then holding the A button to charge strength, releasing at the right time to have the hit you want. Alternatively, the touchscreen can be used by holding the stylus down, then turning around it to select a direction, and releasing it at the right time.

## Compiling

Install devkitARM by following https://devkitpro.org/wiki/Getting_Started then install the package `3ds-mpg123`. Once that's done, run `make` in the folder of this cloned repository.
