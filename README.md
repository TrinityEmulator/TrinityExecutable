# Trinity Launch Executable

This is the code for our executable that launches Trinity without using commands and terminals. It allows simple clicks to launch our emulator, and is capable of detecting the available CPU acceleration methods, including HAXM and Hyper-V.

## Build

To build the code, you'll need an MSYS environment that resembles the one for building Trinity. You can find the detailed instructions [here](https://github.com/TrinityEmulator/TrinityEmulator#windows). 

Having installed the environment, open the **MSYS2 MinGW x64** terminal and simply run `make` to build. 

## Usage

The executable is meant to replace commands and terminals. You simply need to put the built executable in the root directory of Trinity and click it to launch Trinity.