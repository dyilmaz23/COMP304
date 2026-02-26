# COMP304 Project 1 - Shell-ish

Repo: https://github.com/dyilmaz23/COMP304

## Build
gcc -Wall -Wextra -g -o shellish shellish-skeleton.c

## Run (recommended)
rlwrap --always-readline ./shellish

## Custom Command: Tail
tail <file> [N]
prints the last N lines of a file, if no N is specified the default number of lines that will be printed is 5

usage examples:
tail data.txt -> prints the last 5 lines of the 'data.txt' file

taild data.txt 10 -> prints the last 10 lines of the 'data.txt' file

