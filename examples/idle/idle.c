#include "deadlock/dl.h"
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h> /* clock_gettime */
#endif

typedef unsigned long long time_ns;

static time_ns now_ns(void);

static void spin_run(DL_TASK_ARGS);

time_ns start;
dltask spinner;

int
main(void)
{
	start = now_ns();
	spinner = dlcreate(spin_run, NULL);
	return dlmain(&spinner, NULL, NULL);
}

static void
spin_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY_VOID;

	putc('.', stdout); /* do anything */

	/* Recurse for 5 seconds */
	if (now_ns() - start >= 5000000000) {
		dlterminate();
		return;
	}

	dlrecapture(&spinner, spin_run);
	dldetach(&spinner);
}

static time_ns
now_ns(void)
{
        struct timespec t;
#if _POSIX_C_SOURCE >= 199309L
        clock_gettime(CLOCK_MONOTONIC, &t);
#else
        timespec_get(&t, TIME_UTC);
#endif
        return t.tv_sec * 1000000000ull + t.tv_nsec;
}
