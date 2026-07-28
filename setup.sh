#!/bin/bash
# God of Thunder 32X - toolchain setup.
# The sandbox only persists /home/user, so installed packages must be
# restored after a cold start. Safe to re-run.
set -e

need() { command -v "$1" >/dev/null 2>&1; }

if ! need sh-elf-gcc || ! need m68k-linux-gnu-as; then
    echo "Installing SH-2 and 68000 toolchains..."
    sudo apt-get install -y -q \
        gcc-sh-elf binutils-sh-elf libnewlib-sh-elf-dev \
        binutils-m68k-linux-gnu >/dev/null
fi

echo "sh-elf-gcc      : $(sh-elf-gcc -dumpversion)"
echo "m68k-linux-gnu-as: $(m68k-linux-gnu-as --version | head -1)"
echo "Toolchain ready."
