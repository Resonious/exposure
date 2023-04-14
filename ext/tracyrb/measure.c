#include "measure.h"
#if defined(__APPLE__)
#include <mach/mach_time.h>
#elif !defined(_WIN32)
#include <time.h>
#endif


/*
 * Like much of this project, this code is pulled directly from ruby-prof
 * https://github.com/ruby-prof/ruby-prof/blob/master/ext/ruby_prof/rp_measure_wall_time.c
 */

static double wall_time_multipler() {
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return 1.0 / frequency.QuadPart;
#elif defined(__APPLE__)
    mach_timebase_info_data_t mach_timebase;
    mach_timebase_info(&mach_timebase);
    return (uint64_t)mach_timebase.numer / (uint64_t)mach_timebase.denom / 1000000000.0;
#else
    return 1.0;
#endif
}

double measure_wall_time() {
    static double multiplier = 0.0;
    double raw;

    if (multiplier == 0.0)
       multiplier = wall_time_multipler();

#if defined(_WIN32)
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    raw = (double)time.QuadPart;
#elif defined(__APPLE__)
    raw = mach_absolute_time();// * (uint64_t)mach_timebase.numer / (uint64_t)mach_timebase.denom;
#elif defined(__linux__)
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    raw = tv.tv_sec + (tv.tv_nsec / 1000000000.0);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    raw = tv.tv_sec + (tv.tv_usec / 1000000.0);
#endif

    return raw * multiplier;
}
