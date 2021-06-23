Deadlock
========

A very simple task-based multithreading scheduler. Tasks are fine-grained units of work scheduled concurrently across available cores on the machine.

Usage
-----

Deadlock tasks are function pointers with no stack-swapping trickery, with a dead simple work-stealing execution strategy.

The Deadlock framework (scheduler, work queue, workers) is designed to not fail, like an operating system. :) Once initialized, only operating system concurrency primitives may fail, which is hopefully unlikely. This is primarily achieved using fixed length queues and atomic synchronization where possible. The resulting design philosophy of user code should be: handle errors by queuing error handling tasks and pretend the framework doesn't even exist.

Task invocation is a memory barrier. This allows a program to be structured, using simple `dlnext()` atomic operations, in a thread safe way without any traditional locking or atomic primitives!

API
---

The API is evolving! And very pooly documented. Refer to `dl.h` and maybe `graph.h`, and read the `examples/`.
