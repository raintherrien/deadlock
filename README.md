# Deadlock

A very simple task-based multithreading scheduler. Tasks are fine-grained units of work scheduled concurrently across available cores on the machine.

Deadlock tasks are function pointers with no stack-swapping trickery, with a dead simple work-stealing execution strategy.
