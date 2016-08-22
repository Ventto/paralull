Paralull
===================

*Paralull is a Wait-free Queue as lulling as Fetch-and-Add implementation
in C.*

## Requirements
Used for comparative tests:
- [liblfds v6.1.1](http://liblfds.org/) - A portable, license-free, lock-free data structure library written in C
- [Google/benchmark](https://github.com/google/benchmark) - A microbenchmark support library
- (submodule) Criterion - A KISS, non-intrusive cross-platform C unit testing framework

## Installation

Keep the submodule update:

`$ git submodule update`

Installation:

    $ mkdir build
    $ cd build
    $ cmake ..
    $ make

## Run

For unit tests:

    $ ./test/parallul_unit_tests

To run the Google microbenchmark:

    $ ./bench/bench

## How to map threads to cores

Generated bench: https://venthom.github.io/paralull/bench/

/* FIXME */

## How to add a new benchmark

Take a look at `~/bench/bench.cc`

For instance:
```
[...]

    BENCHMARK(libflds_enqueue)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

[...]

BENCHMARK(paralull_enqueue)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

[...]
```

## TODO
- Find out a way to install liblfds easily. Apparently there is something wrong with the release archives.

## Authors

Franklin Mathieu<br>
Thomas Venriès
