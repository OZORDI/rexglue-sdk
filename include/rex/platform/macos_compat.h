/**
 * @file        rex/platform/macos_compat.h
 * @brief       macOS compatibility shims for Linux-specific APIs
 *
 * Provides pipe-based eventfd, gettid, cpu_set_t stubs, etc.
 * Only included on Apple platforms.
 */

#pragma once

#ifdef __APPLE__

#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <cstdint>

// ============================================================================
// eventfd shim using pipes
// ============================================================================

#define EFD_CLOEXEC 0
#define EFD_NONBLOCK 0
#define EFD_SEMAPHORE 0

// eventfd compatibility: returns a pipe read fd, stores write fd in a global map
// For simplicity, we use a pipe pair. The "eventfd" fd is the read end.
// We store the write end by convention at fd+1 (not ideal, but works for our case).
// Better approach: use a side table.

#include <unordered_map>
#include <mutex>

namespace rex::platform::detail {
inline std::unordered_map<int, int>& eventfd_write_map() {
    static std::unordered_map<int, int> map;
    return map;
}
inline std::mutex& eventfd_mutex() {
    static std::mutex m;
    return m;
}
}  // namespace rex::platform::detail

inline int eventfd(unsigned int initval, int flags) {
    (void)flags;
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    // Set non-blocking on both ends
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    // Store the write end
    {
        std::lock_guard<std::mutex> lock(rex::platform::detail::eventfd_mutex());
        rex::platform::detail::eventfd_write_map()[pipefd[0]] = pipefd[1];
    }

    // Write initial value if non-zero
    if (initval > 0) {
        uint64_t val = initval;
        write(pipefd[1], &val, sizeof(val));
    }

    return pipefd[0];
}

inline int eventfd_write(int fd, uint64_t val) {
    int write_fd;
    {
        std::lock_guard<std::mutex> lock(rex::platform::detail::eventfd_mutex());
        auto it = rex::platform::detail::eventfd_write_map().find(fd);
        if (it == rex::platform::detail::eventfd_write_map().end()) return -1;
        write_fd = it->second;
    }
    return (write(write_fd, &val, sizeof(val)) == sizeof(val)) ? 0 : -1;
}

inline int eventfd_read(int fd, uint64_t* val) {
    return (read(fd, val, sizeof(*val)) == sizeof(*val)) ? 0 : -1;
}

inline void eventfd_close(int fd) {
    int write_fd = -1;
    {
        std::lock_guard<std::mutex> lock(rex::platform::detail::eventfd_mutex());
        auto it = rex::platform::detail::eventfd_write_map().find(fd);
        if (it != rex::platform::detail::eventfd_write_map().end()) {
            write_fd = it->second;
            rex::platform::detail::eventfd_write_map().erase(it);
        }
    }
    close(fd);
    if (write_fd >= 0) close(write_fd);
}

// ============================================================================
// gettid shim
// ============================================================================

inline uint64_t rex_gettid() {
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
}

#ifndef gettid
#define gettid() ((pid_t)rex_gettid())
#endif

// ============================================================================
// cpu_set_t stubs (macOS doesn't support CPU affinity)
// ============================================================================

#ifndef CPU_SETSIZE
#define CPU_SETSIZE 1024

typedef struct {
    uint64_t __bits[CPU_SETSIZE / 64];
} cpu_set_t;

#define CPU_ZERO(set) memset((set), 0, sizeof(cpu_set_t))
#define CPU_SET(cpu, set) ((set)->__bits[(cpu) / 64] |= (1ULL << ((cpu) % 64)))
#define CPU_CLR(cpu, set) ((set)->__bits[(cpu) / 64] &= ~(1ULL << ((cpu) % 64)))
#define CPU_ISSET(cpu, set) (((set)->__bits[(cpu) / 64] & (1ULL << ((cpu) % 64))) != 0)
#define CPU_COUNT(set) ({                         \
    int _count = 0;                               \
    for (int _i = 0; _i < CPU_SETSIZE / 64; _i++) \
        _count += __builtin_popcountll((set)->__bits[_i]); \
    _count;                                       \
})
#endif

// sched_getaffinity/sched_setaffinity stubs (no-op on macOS)
inline int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t* mask) {
    (void)pid; (void)cpusetsize;
    CPU_ZERO(mask);
    // Report all CPUs as available
    int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 0; i < ncpu; i++) CPU_SET(i, mask);
    return 0;
}

inline int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t* mask) {
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;  // No-op on macOS
}

// ============================================================================
// pthread_gettid_np shim (Linux version returns pid_t tid)
// ============================================================================

// On Linux, pthread_gettid_np(pthread_t) returns pid_t
// On macOS, pthread_threadid_np(pthread_t, uint64_t*) fills a uint64_t
// We redefine to match the Linux API
inline pid_t pthread_gettid_np_compat(pthread_t thread) {
    uint64_t tid;
    pthread_threadid_np(thread, &tid);
    return (pid_t)tid;
}
#define pthread_gettid_np(t) pthread_gettid_np_compat(t)

// ============================================================================
// syscall shim
// ============================================================================

#include <sys/syscall.h>

#ifndef SYS_gettid
#define SYS_gettid 0  // Unused, gettid() macro handles it
#endif

// ============================================================================
// SIGRTMIN/SIGRTMAX shim (macOS doesn't have real-time signals)
// Use high signal numbers that won't conflict with standard signals
// ============================================================================

#ifndef SIGRTMIN
#define SIGRTMIN SIGUSR1
#endif

#ifndef SIGRTMAX
#define SIGRTMAX SIGUSR2
#endif

// ============================================================================
// pthread_setname_np shim
// On Linux: pthread_setname_np(pthread_t, const char*)
// On macOS: pthread_setname_np(const char*) - only for current thread
// Use rex_pthread_setname_np to avoid conflicts with system declaration
// ============================================================================

inline int rex_pthread_setname_np(pthread_t thread, const char* name) {
    if (pthread_equal(thread, pthread_self())) {
        return pthread_setname_np(name);
    }
    return 0;  // Can't set name of other threads on macOS
}

// ============================================================================
// pthread_getaffinity_np / pthread_setaffinity_np shims
// macOS doesn't support these - use no-op stubs
// ============================================================================

inline int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize, cpu_set_t* cpuset) {
    (void)thread;
    return sched_getaffinity(0, cpusetsize, cpuset);
}

inline int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const cpu_set_t* cpuset) {
    (void)thread;
    return sched_setaffinity(0, cpusetsize, cpuset);
}

// ============================================================================
// pthread_sigqueue shim (macOS doesn't have it)
// ============================================================================

inline int pthread_sigqueue(pthread_t thread, int sig, const union sigval value) {
    (void)value;
    return pthread_kill(thread, sig);
}

// ============================================================================
// sigqueue shim (macOS doesn't have sigqueue, use kill)
// ============================================================================

#ifndef sigqueue
inline int sigqueue(pid_t pid, int sig, const union sigval value) {
    (void)value;
    return kill(pid, sig);
}
#endif

#endif  // __APPLE__
