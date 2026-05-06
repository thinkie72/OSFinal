//
// thread_compat.h
// Tiny Win32-style threading wrapper.
// On Windows: pulls in <windows.h> and uses the native API.
// On macOS / Linux: provides the same names backed by pthreads.
//
// Reference docs (use these — they describe the canonical behavior):
//   CreateThread:          https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
//   WaitForSingleObject:   https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
//   WaitForMultipleObjects:https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects
//   CRITICAL_SECTION:      https://learn.microsoft.com/windows/win32/sync/critical-section-objects
//   InterlockedIncrement:  https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-interlockedincrement
//
#ifndef OSFINAL_THREAD_COMPAT_H
#define OSFINAL_THREAD_COMPAT_H

#ifdef _WIN32

#include <windows.h>

#else  // ===== macOS / Linux pthread shim =====

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

typedef void          *LPVOID;
typedef unsigned long  DWORD;
typedef DWORD         *LPDWORD;
typedef int            BOOL;
typedef size_t         SIZE_T;
typedef void          *LPSECURITY_ATTRIBUTES;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);

#define WINAPI
#define INFINITE      0xFFFFFFFFUL
#define WAIT_OBJECT_0 0
#define TRUE  1
#define FALSE 0

typedef struct _HANDLE_struct {
    pthread_t th;
    int       joined;
} *HANDLE;

typedef struct {
    LPTHREAD_START_ROUTINE func;
    LPVOID                 arg;
} _tc_adapter_args;

static void *_tc_adapter_fn(void *p) {
    _tc_adapter_args *a = (_tc_adapter_args *)p;
    LPTHREAD_START_ROUTINE f = a->func;
    LPVOID                 x = a->arg;
    free(a);
    DWORD r = f(x);
    return (void *)(uintptr_t)r;
}

static inline HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
                                  LPTHREAD_START_ROUTINE start, LPVOID param,
                                  DWORD flags, LPDWORD tid) {
    (void)sa; (void)stack; (void)flags; (void)tid;
    HANDLE h = (HANDLE)malloc(sizeof(*h));
    if (!h) return NULL;
    h->joined = 0;
    _tc_adapter_args *a = (_tc_adapter_args *)malloc(sizeof(*a));
    if (!a) { free(h); return NULL; }
    a->func = start;
    a->arg  = param;
    if (pthread_create(&h->th, NULL, _tc_adapter_fn, a) != 0) {
        free(a); free(h); return NULL;
    }
    return h;
}

static inline DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    (void)ms;
    if (h && !h->joined) {
        pthread_join(h->th, NULL);
        h->joined = 1;
    }
    return WAIT_OBJECT_0;
}

static inline DWORD WaitForMultipleObjects(DWORD n, HANDLE *handles,
                                           BOOL waitAll, DWORD ms) {
    (void)waitAll; (void)ms;
    for (DWORD i = 0; i < n; i++) WaitForSingleObject(handles[i], INFINITE);
    return WAIT_OBJECT_0;
}

static inline BOOL CloseHandle(HANDLE h) {
    if (!h) return TRUE;
    if (!h->joined) pthread_detach(h->th);
    free(h);
    return TRUE;
}

// ----- CRITICAL_SECTION -----
typedef pthread_mutex_t CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_init(cs, NULL); }
static inline void EnterCriticalSection   (CRITICAL_SECTION *cs)  { pthread_mutex_lock(cs); }
static inline void LeaveCriticalSection   (CRITICAL_SECTION *cs)  { pthread_mutex_unlock(cs); }
static inline void DeleteCriticalSection  (CRITICAL_SECTION *cs)  { pthread_mutex_destroy(cs); }

// ----- CONDITION_VARIABLE -----
typedef pthread_cond_t CONDITION_VARIABLE;
static inline void InitializeConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_init(cv, NULL); }
static inline BOOL SleepConditionVariableCS(CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD ms) {
    (void)ms;
    pthread_cond_wait(cv, cs);
    return TRUE;
}
static inline void WakeConditionVariable    (CONDITION_VARIABLE *cv) { pthread_cond_signal(cv); }
static inline void WakeAllConditionVariable (CONDITION_VARIABLE *cv) { pthread_cond_broadcast(cv); }

// ----- Atomic counter -----
static inline long InterlockedIncrement(volatile long *p) {
    return __sync_add_and_fetch(p, 1);
}
static inline long InterlockedExchange(volatile long *p, long v) {
    return __sync_lock_test_and_set(p, v);
}

#endif // _WIN32

#endif // OSFINAL_THREAD_COMPAT_H
