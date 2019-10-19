# ka9q-unix
Port of the KA9Q Network Operating System (NOS) to modern BSD Unix as a user process.

## Supported platforms

This has been built and tested on:

 * Linux
 * FreeBSD
 * MacOS X

## Building

$ mkdir build
$ cd build
$ cmake ..
$ make

## Building with debug symbols

$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make

