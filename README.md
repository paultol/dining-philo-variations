# Dining Philosophers Problem: Variations in C++

A little study of classic Dinining Philosophers problem (see Wikipedia for
details).

## Main Thread Algorithm

The main thread sends a finite number of hunger signals to five philosopher
threads in a tight loop in random order, then waits till every philosopher has
one meal per each signal he receives then joins the philospher threads and
prints some statistics.

## Philosopher Thread Algorithm

Originally own the left chopstick. Send a message asking to yield a chopstick to
a peer when non-owned chopstick is needed. Yield a chopstick (by sending a
"yielded" message to asking peer) when asked unless this thread is hungry and
the thread asking for a chopstick is "greater" in some global order -- in which
case, remember to yield the chopstick later after satisfying own hunger.

Some variations may be added later (e.g. anti-starvation protocol or other
improvements in fairness or performance).

## Trying It Out

### Build Prerequisites:

Gnu Make (any version still around should be good enough), gcc with solid C++11
support (I used 5.4).

### Run Prerequisites

Some OS supporting C++ threads.

### Steps

* Clone the repository
* In root directory, run `make dp` or simply `make` to build non-optimized
  program or `make dp.fast` for optimized build.
* Run the built binary:
```
dp [<tot-num-hungers>]

where <tot-num-hungers> is the total number of hunger signals
    (default is 1,000,000).
```

