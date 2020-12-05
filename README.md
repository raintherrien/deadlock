# Deadlock

A very simple task-based multithreading scheduler. Tasks are fine-grained units of work scheduled concurrently across available cores on the machine.

Deadlock tasks are function pointers with no stack-swapping trickery, with a dead simple work-stealing execution strategy.

The Deadlock framework (scheduler, work queue, workers) is designed to not fail, like an operating system. :) Once initialized, only operating system concurrency primitives may fail, which is hopefully unlikely. This is primarily achieved using fixed length queues and atomic synchronization where possible. The resulting design philosophy of user code should be: handle errors by queuing error handling tasks and pretend the framework doesn't even exist. For example, one might use a flexible array member to pass error codes in a generic fork-join situation:
```c
TODO
```
