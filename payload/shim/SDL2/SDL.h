/*
 * SwanStationPS5 disc daemon - the few SDL calls the shared disc code (src/discimport.c, src/cdscsi.c) uses, on
 * plain C11 atomics and pthreads, so the payload does not need SDL. Only for the payload build (payload/Makefile).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_PAYLOAD_SDL_SHIM_H
#define SwanStationPS5_PAYLOAD_SDL_SHIM_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef enum
{
    SDL_FALSE = 0,
    SDL_TRUE = 1
} SDL_bool;

typedef struct
{
    _Atomic int value;
} SDL_atomic_t;

static inline int SDL_AtomicGet(SDL_atomic_t *a)
{
    return atomic_load(&a->value);
}

static inline int SDL_AtomicSet(SDL_atomic_t *a, int v)
{
    return atomic_exchange(&a->value, v);
}

static inline int SDL_AtomicAdd(SDL_atomic_t *a, int v)
{
    return atomic_fetch_add(&a->value, v);
}

static inline SDL_bool SDL_AtomicCAS(SDL_atomic_t *a, int oldval, int newval)
{
    return atomic_compare_exchange_strong(&a->value, &oldval, newval) ? SDL_TRUE : SDL_FALSE;
}

static inline uint64_t SDL_GetTicks64(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static inline uint32_t SDL_GetTicks(void)
{
    return (uint32_t)SDL_GetTicks64();
}

static inline void SDL_Delay(uint32_t ms)
{
    usleep(ms * 1000u);
}

/* threads */
typedef int (*SDL_ThreadFunction)(void *);

typedef struct SDL_Thread
{
    pthread_t id;
    SDL_ThreadFunction fn;
    void *data;
} SDL_Thread;

static inline void *sdl_shim_trampoline(void *arg)
{
    SDL_Thread *t = arg;
    t->fn(t->data);
    return NULL;
}

static inline SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
    (void)name;
    SDL_Thread *t = malloc(sizeof(*t));
    if (!t)
        return NULL;
    t->fn = fn;
    t->data = data;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    int rc = pthread_create(&t->id, &attr, sdl_shim_trampoline, t);
    pthread_attr_destroy(&attr);
    if (rc != 0)
    {
        free(t);
        return NULL;
    }
    return t;
}

/* the SDL_Thread is left allocated: the trampoline still reads it while the thread starts */
static inline void SDL_DetachThread(SDL_Thread *t)
{
    pthread_detach(t->id);
}

/* mutexes */
typedef struct
{
    pthread_mutex_t m;
} SDL_mutex;

static inline SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *m = malloc(sizeof(*m));
    if (m)
        pthread_mutex_init(&m->m, NULL);
    return m;
}

static inline int SDL_LockMutex(SDL_mutex *m)
{
    return pthread_mutex_lock(&m->m);
}

static inline int SDL_TryLockMutex(SDL_mutex *m)
{
    return pthread_mutex_trylock(&m->m) == 0 ? 0 : 1;
}

static inline int SDL_UnlockMutex(SDL_mutex *m)
{
    return pthread_mutex_unlock(&m->m);
}

#endif
