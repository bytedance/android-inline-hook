// Copyright (c) 2021-2022 ByteDance Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

// Created by Kelun Cai (caikelun@bytedance.com) on 2021-04-11.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#define _GNU_SOURCE
#pragma clang diagnostic push
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/in.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <signal.h>
#include <android/log.h>
#include "systest.h"
#include "systest_util.h"
#include "shadowhook.h"

extern __attribute((weak)) int pthread_mutex_timedlock(pthread_mutex_t* __mutex, const struct timespec* __timeout);
extern __attribute((weak)) int posix_memalign(void** __memptr, size_t __alignment, size_t __size);
extern __attribute((weak)) int dup3(int __old_fd, int __new_fd, int __flags);
extern __attribute((weak)) int sigaction64(int __signal, const struct sigaction64* __new_action, struct sigaction64* __old_action);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#define LOG(fmt, ...)  __android_log_print(ANDROID_LOG_INFO, "shadowhook_tag", fmt, ##__VA_ARGS__)
#pragma clang diagnostic pop

#define DELIMITER_TITLE "==================================================================="
#define DELIMITER_SUB   "-------------------------------------------------"
#define TO_STR_HELPER(x) #x
#define TO_STR(x) TO_STR_HELPER(x)

#define GLOBAL_VARIABLES(sym) \
static void *orig_##sym = NULL; \
static void *stub_##sym = NULL; \
static size_t count_##sym = 0; \
static int errno_##sym = 0

#define HOOK_SYM_ADDR(sym) \
do { \
    if(NULL == sym) break; \
    if(NULL != stub_##sym) break; \
    errno_##sym = 0; \
    if(NULL == (stub_##sym = shadowhook_hook_sym_addr((void *)sym, SHADOWHOOK_IS_UNIQUE_MODE ? (void *)unique_proxy_##sym : (void *)shared_proxy_##sym, (void **)(&orig_##sym)))) \
        LOG("hook sym addr FAILED: "TO_STR(sym)". errno: %d", errno_##sym = shadowhook_get_errno()); \
} while (0)

#define HOOK_SYM_NAME(lib, sym) \
do { \
    if(NULL != stub_##sym) break; \
    errno_##sym = 0; \
    if(NULL == (stub_##sym = shadowhook_hook_sym_name(TO_STR(lib), TO_STR(sym), SHADOWHOOK_IS_UNIQUE_MODE ? (void *)unique_proxy_##sym : (void *)shared_proxy_##sym, (void **)&orig_##sym))) \
        LOG("hook sym name FAILED: "TO_STR(lib)", "TO_STR(sym)". errno: %d", errno_##sym = shadowhook_get_errno()); \
    else \
        errno_##sym = shadowhook_get_errno(); \
} while (0)

#define UNHOOK_SYM_ADDR(sym) \
do { \
    if(NULL == sym) break; \
    if(NULL == stub_##sym) break; \
    if(0 != shadowhook_unhook(stub_##sym)) \
        LOG("unhook sym addr FAILED: "TO_STR(sym)". errno: %d", shadowhook_get_errno()); \
    else \
        errno_##sym = shadowhook_get_errno(); \
    stub_##sym = NULL; \
} while (0)

#define UNHOOK_SYM_NAME(sym) \
do { \
    if(NULL == stub_##sym) break; \
    if(0 != shadowhook_unhook(stub_##sym)) \
        LOG("unhook sym addr FAILED: "TO_STR(sym)". errno: %d", shadowhook_get_errno()); \
    else \
        errno_##sym = shadowhook_get_errno(); \
    stub_##sym = NULL; \
} while (0)

#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    #define COUNT(sym) __atomic_add_fetch(&count_##sym, 1, __ATOMIC_RELAXED)
#else
    #define COUNT(sym)
#endif

#define SHOW2(sym, readable_sym) LOG("%s %-3d  %-32s  %zu", 0 == errno_##sym ? "  " : "->", errno_##sym, TO_STR(readable_sym), count_##sym)
#define SHOW(sym) SHOW2(sym, sym)


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

// pthread_create
GLOBAL_VARIABLES(pthread_create);
typedef int (*type_pthread_create)(pthread_t *, pthread_attr_t const *, void* (*)(void*), void*);
static int unique_proxy_pthread_create(pthread_t *pthread_ptr, pthread_attr_t const *attr, void* (*start_routine)(void*), void *arg)
{
    COUNT(pthread_create);
    return ((type_pthread_create)orig_pthread_create)(pthread_ptr, attr, start_routine, arg);
}
static int shared_proxy_pthread_create(pthread_t *pthread_ptr, pthread_attr_t const *attr, void* (*start_routine)(void*), void *arg)
{
    COUNT(pthread_create);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_create, type_pthread_create, pthread_ptr, attr, start_routine, arg);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_exit
GLOBAL_VARIABLES(pthread_exit);
typedef void (*type_pthread_exit)(void *);
static void unique_proxy_pthread_exit(void *return_value)
{
    COUNT(pthread_exit);
    ((type_pthread_exit)orig_pthread_exit)(return_value);
}
static void shared_proxy_pthread_exit(void *return_value)
{
    COUNT(pthread_exit);
    SHADOWHOOK_CALL_PREV(shared_proxy_pthread_exit, type_pthread_exit, return_value);
    SHADOWHOOK_POP_STACK();
}

// pthread_getspecific
GLOBAL_VARIABLES(pthread_getspecific);
typedef void* (*type_pthread_getspecific)(pthread_key_t);
static void* unique_proxy_pthread_getspecific(pthread_key_t key)
{
    COUNT(pthread_getspecific);
    return ((type_pthread_getspecific)orig_pthread_getspecific)(key);
}
static void* shared_proxy_pthread_getspecific(pthread_key_t key)
{
    COUNT(pthread_getspecific);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_getspecific, type_pthread_getspecific, key);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_setspecific
GLOBAL_VARIABLES(pthread_setspecific);
typedef int (*type_pthread_setspecific)(pthread_key_t, const void *);
static int unique_proxy_pthread_setspecific(pthread_key_t key, const void *value)
{
    COUNT(pthread_setspecific);
    return ((type_pthread_setspecific)orig_pthread_setspecific)(key, value);
}
static int shared_proxy_pthread_setspecific(pthread_key_t key, const void *value)
{
    COUNT(pthread_setspecific);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_setspecific, type_pthread_setspecific, key, value);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_mutex_lock
GLOBAL_VARIABLES(pthread_mutex_lock);
typedef int (*type_pthread_mutex_lock)(pthread_mutex_t *);
static int unique_proxy_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    COUNT(pthread_mutex_lock);
    return ((type_pthread_mutex_lock)orig_pthread_mutex_lock)(mutex);
}
static int shared_proxy_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    COUNT(pthread_mutex_lock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_mutex_lock, type_pthread_mutex_lock, mutex);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_mutex_timedlock
GLOBAL_VARIABLES(pthread_mutex_timedlock);
typedef int (*type_pthread_mutex_timedlock)(pthread_mutex_t *, const struct timespec *);
static int unique_proxy_pthread_mutex_timedlock(pthread_mutex_t* mutex, const struct timespec* timeout)
{
    COUNT(pthread_mutex_timedlock);
    return ((type_pthread_mutex_timedlock)orig_pthread_mutex_timedlock)(mutex, timeout);
}
static int shared_proxy_pthread_mutex_timedlock(pthread_mutex_t* mutex, const struct timespec* timeout)
{
    COUNT(pthread_mutex_timedlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_mutex_timedlock, type_pthread_mutex_timedlock, mutex, timeout);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_mutex_unlock
GLOBAL_VARIABLES(pthread_mutex_unlock);
typedef int (*type_pthread_mutex_unlock)(pthread_mutex_t *);
static int unique_proxy_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    COUNT(pthread_mutex_unlock);
    return ((type_pthread_mutex_unlock)orig_pthread_mutex_unlock)(mutex);
}
static int shared_proxy_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    COUNT(pthread_mutex_unlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_mutex_unlock, type_pthread_mutex_unlock, mutex);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_rwlock_rdlock
GLOBAL_VARIABLES(pthread_rwlock_rdlock);
typedef int (*type_pthread_rwlock_rdlock)(pthread_rwlock_t *);
static int unique_proxy_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    COUNT(pthread_rwlock_rdlock);
    return ((type_pthread_rwlock_rdlock)orig_pthread_rwlock_rdlock)(rwlock);
}
static int shared_proxy_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    COUNT(pthread_rwlock_rdlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_rwlock_rdlock, type_pthread_rwlock_rdlock, rwlock);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_rwlock_timedrdlock
GLOBAL_VARIABLES(pthread_rwlock_timedrdlock);
typedef int (*type_pthread_rwlock_timedrdlock)(pthread_rwlock_t *, const struct timespec *);
static int unique_proxy_pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock, const struct timespec *timeout)
{
    COUNT(pthread_rwlock_timedrdlock);
    return ((type_pthread_rwlock_timedrdlock)orig_pthread_rwlock_timedrdlock)(rwlock, timeout);
}
static int shared_proxy_pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock, const struct timespec *timeout)
{
    COUNT(pthread_rwlock_timedrdlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_rwlock_timedrdlock, type_pthread_rwlock_timedrdlock, rwlock, timeout);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_rwlock_wrlock
GLOBAL_VARIABLES(pthread_rwlock_wrlock);
typedef int (*type_pthread_rwlock_wrlock)(pthread_rwlock_t *);
static int unique_proxy_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    COUNT(pthread_rwlock_wrlock);
    return ((type_pthread_rwlock_wrlock)orig_pthread_rwlock_wrlock)(rwlock);
}
static int shared_proxy_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    COUNT(pthread_rwlock_wrlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_rwlock_wrlock, type_pthread_rwlock_wrlock, rwlock);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_rwlock_timedwrlock
GLOBAL_VARIABLES(pthread_rwlock_timedwrlock);
typedef int (*type_pthread_rwlock_timedwrlock)(pthread_rwlock_t *, const struct timespec *);
static int unique_proxy_pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock, const struct timespec *timeout)
{
    COUNT(pthread_rwlock_timedwrlock);
    return ((type_pthread_rwlock_timedwrlock)orig_pthread_rwlock_timedwrlock)(rwlock, timeout);
}
static int shared_proxy_pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock, const struct timespec *timeout)
{
    COUNT(pthread_rwlock_timedwrlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_rwlock_timedwrlock, type_pthread_rwlock_timedwrlock, rwlock, timeout);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pthread_rwlock_unlock
GLOBAL_VARIABLES(pthread_rwlock_unlock);
typedef int (*type_pthread_rwlock_unlock)(pthread_rwlock_t *);
static int unique_proxy_pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    COUNT(pthread_rwlock_unlock);
    return ((type_pthread_rwlock_unlock)orig_pthread_rwlock_unlock)(rwlock);
}
static int shared_proxy_pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    COUNT(pthread_rwlock_unlock);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pthread_rwlock_unlock, type_pthread_rwlock_unlock, rwlock);
    SHADOWHOOK_POP_STACK();
    return r;
}

// malloc
GLOBAL_VARIABLES(malloc);
typedef void *(*type_malloc)(size_t);
static void *unique_proxy_malloc(size_t sz)
{
    COUNT(malloc);
    return ((type_malloc)orig_malloc)(sz);
}
static void *shared_proxy_malloc(size_t sz)
{
    COUNT(malloc);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_malloc, type_malloc, sz);
    SHADOWHOOK_POP_STACK();
    return r;
}

// calloc
GLOBAL_VARIABLES(calloc);
typedef void *(*type_calloc)(size_t, size_t);
static void *unique_proxy_calloc(size_t cnt, size_t sz)
{
    COUNT(calloc);
    return ((type_calloc)orig_calloc)(cnt, sz);
}
static void *shared_proxy_calloc(size_t cnt, size_t sz)
{
    COUNT(calloc);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_calloc, type_calloc, cnt, sz);
    SHADOWHOOK_POP_STACK();
    return r;
}

// realloc
GLOBAL_VARIABLES(realloc);
typedef void *(*type_realloc)(void *, size_t);
static void *unique_proxy_realloc(void *ptr, size_t sz)
{
    COUNT(realloc);
    return ((type_realloc)orig_realloc)(ptr, sz);
}
static void *shared_proxy_realloc(void *ptr, size_t sz)
{
    COUNT(realloc);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_realloc, type_realloc, ptr, sz);
    SHADOWHOOK_POP_STACK();
    return r;
}

// memalign
GLOBAL_VARIABLES(memalign);
typedef void *(*type_memalign)(size_t, size_t);
static void *unique_proxy_memalign(size_t align, size_t sz)
{
    COUNT(memalign);
    return ((type_memalign)orig_memalign)(align, sz);
}
static void *shared_proxy_memalign(size_t align, size_t sz)
{
    COUNT(memalign);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_memalign, type_memalign, align, sz);
    SHADOWHOOK_POP_STACK();
    return r;
}

// posix_memalign
GLOBAL_VARIABLES(posix_memalign);
typedef int (*type_posix_memalign)(void **, size_t, size_t);
static int unique_proxy_posix_memalign(void **ptr, size_t align, size_t sz)
{
    COUNT(posix_memalign);
    return ((type_posix_memalign)orig_posix_memalign)(ptr, align, sz);
}
static int shared_proxy_posix_memalign(void **ptr, size_t align, size_t sz)
{
    COUNT(posix_memalign);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_posix_memalign, type_posix_memalign, ptr, align, sz);
    SHADOWHOOK_POP_STACK();
    return r;
}

// free
GLOBAL_VARIABLES(free);
typedef void (*type_free)(void *);
static void unique_proxy_free(void *ptr)
{
    COUNT(free);
    ((type_free)orig_free)(ptr);
}
static void shared_proxy_free(void *ptr)
{
    COUNT(free);
    SHADOWHOOK_CALL_PREV(shared_proxy_free, type_free, ptr);
    SHADOWHOOK_POP_STACK();
}

#ifdef __LP64__

// mmap64
GLOBAL_VARIABLES(mmap64);
typedef void* (*type_mmap64)(void*, size_t, int, int, int, off64_t);
static void* unique_proxy_mmap64(void* addr, size_t sz, int prot, int flags, int fd, off64_t offset)
{
    COUNT(mmap64);
    return ((type_mmap64)orig_mmap64)(addr, sz, prot, flags, fd, offset);
}
static void* shared_proxy_mmap64(void* addr, size_t sz, int prot, int flags, int fd, off64_t offset)
{
    COUNT(mmap64);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_mmap64, type_mmap64, addr, sz, prot, flags, fd, offset);
    SHADOWHOOK_POP_STACK();
    return r;
}

#else

// __mmap2
GLOBAL_VARIABLES(__mmap2);
typedef void* (*type___mmap2)(void*, size_t, int, int, int, size_t);
static void* unique_proxy___mmap2(void* addr, size_t sz, int prot, int flags, int fd, size_t pages)
{
    COUNT(__mmap2);
    return ((type___mmap2)orig___mmap2)(addr, sz, prot, flags, fd, pages);
}
static void* shared_proxy___mmap2(void* addr, size_t sz, int prot, int flags, int fd, size_t pages)
{
    COUNT(__mmap2);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy___mmap2, type___mmap2, addr, sz, prot, flags, fd, pages);
    SHADOWHOOK_POP_STACK();
    return r;
}

#endif

// mremap
GLOBAL_VARIABLES(mremap);
typedef void* (*type_mremap)(void*, size_t, size_t, int, void*);
static void* unique_proxy_mremap(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr)
{
    COUNT(mremap);
    return ((type_mremap)orig_mremap)(old_addr, old_size, new_size, flags, new_addr);
}
static void* shared_proxy_mremap(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr)
{
    COUNT(mremap);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy_mremap, type_mremap, old_addr, old_size, new_size, flags, new_addr);
    SHADOWHOOK_POP_STACK();
    return r;
}

// munmap
GLOBAL_VARIABLES(munmap);
typedef int (*type_munmap)(void*, size_t);
static int unique_proxy_munmap(void *addr, size_t sz)
{
    COUNT(munmap);
    return ((type_munmap)orig_munmap)(addr, sz);
}
static int shared_proxy_munmap(void *addr, size_t sz)
{
    COUNT(munmap);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_munmap, type_munmap, addr, sz);
    SHADOWHOOK_POP_STACK();
    return r;
}

// __openat
GLOBAL_VARIABLES(__openat);
typedef void* (*type___openat)(int, const char *, int, int);
static void* unique_proxy___openat(int fd, const char *pathname, int flags, int mode)
{
    COUNT(__openat);
    return ((type___openat)orig___openat)(fd, pathname, flags, mode);
}
static void* shared_proxy___openat(int fd, const char *pathname, int flags, int mode)
{
    COUNT(__openat);
    void *r = SHADOWHOOK_CALL_PREV(shared_proxy___openat, type___openat, fd, pathname, flags, mode);
    SHADOWHOOK_POP_STACK();
    return r;
}

// close
GLOBAL_VARIABLES(close);
typedef int (*type_close)(int);
static int unique_proxy_close(int fd)
{
    COUNT(close);
    return ((type_close)orig_close)(fd);
}
static int shared_proxy_close(int fd)
{
    COUNT(close);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_close, type_close, fd);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pipe
GLOBAL_VARIABLES(pipe);
typedef int (*type_pipe)(int *);
static int unique_proxy_pipe(int *fds)
{
    COUNT(pipe);
    return ((type_pipe)orig_pipe)(fds);
}
static int shared_proxy_pipe(int *fds)
{
    COUNT(pipe);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pipe, type_pipe, fds);
    SHADOWHOOK_POP_STACK();
    return r;
}

// pipe2
GLOBAL_VARIABLES(pipe2);
typedef int (*type_pipe2)(int *, int);
static int unique_proxy_pipe2(int *fds, int flags)
{
    COUNT(pipe2);
    return ((type_pipe2)orig_pipe2)(fds, flags);
}
static int shared_proxy_pipe2(int *fds, int flags)
{
    COUNT(pipe2);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_pipe2, type_pipe2, fds, flags);
    SHADOWHOOK_POP_STACK();
    return r;
}

// dup
GLOBAL_VARIABLES(dup);
typedef int (*type_dup)(int);
static int unique_proxy_dup(int oldfd)
{
    COUNT(dup);
    return ((type_dup)orig_dup)(oldfd);
}
static int shared_proxy_dup(int oldfd)
{
    COUNT(dup);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_dup, type_dup, oldfd);
    SHADOWHOOK_POP_STACK();
    return r;
}

// dup2
GLOBAL_VARIABLES(dup2);
typedef int (*type_dup2)(int, int);
static int unique_proxy_dup2(int oldfd, int newfd)
{
    COUNT(dup2);
    return ((type_dup2)orig_dup2)(oldfd, newfd);
}
static int shared_proxy_dup2(int oldfd, int newfd)
{
    COUNT(dup2);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_dup2, type_dup2, oldfd, newfd);
    SHADOWHOOK_POP_STACK();
    return r;
}

// dup3
GLOBAL_VARIABLES(dup3);
typedef int (*type_dup3)(int, int, int);
static int unique_proxy_dup3(int oldfd, int newfd, int flags)
{
    COUNT(dup3);
    return ((type_dup3)orig_dup3)(oldfd, newfd, flags);
}
static int shared_proxy_dup3(int oldfd, int newfd, int flags)
{
    COUNT(dup3);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_dup3, type_dup3, oldfd, newfd, flags);
    SHADOWHOOK_POP_STACK();
    return r;
}

// socket
GLOBAL_VARIABLES(socket);
typedef int (*type_socket)(int, int, int);
static int unique_proxy_socket(int af, int type, int protocol)
{
    COUNT(socket);
    return ((type_socket)orig_socket)(af, type, protocol);
}
static int shared_proxy_socket(int af, int type, int protocol)
{
    COUNT(socket);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_socket, type_socket, af, type, protocol);
    SHADOWHOOK_POP_STACK();
    return r;
}

// socketpair
GLOBAL_VARIABLES(socketpair);
typedef int (*type_socketpair)(int, int, int, int *);
static int unique_proxy_socketpair(int af, int type, int protocol, int *fds)
{
    COUNT(socketpair);
    return ((type_socketpair)orig_socketpair)(af, type, protocol, fds);
}
static int shared_proxy_socketpair(int af, int type, int protocol, int *fds)
{
    COUNT(socketpair);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_socketpair, type_socketpair, af, type, protocol, fds);
    SHADOWHOOK_POP_STACK();
    return r;
}

// eventfd
GLOBAL_VARIABLES(eventfd);
typedef int (*type_eventfd)(unsigned int, int);
static int unique_proxy_eventfd(unsigned int initial_value, int flags)
{
    COUNT(eventfd);
    return ((type_eventfd)orig_eventfd)(initial_value, flags);
}
static int shared_proxy_eventfd(unsigned int initial_value, int flags)
{
    COUNT(eventfd);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_eventfd, type_eventfd, initial_value, flags);
    SHADOWHOOK_POP_STACK();
    return r;
}

// read
GLOBAL_VARIABLES(read);
typedef ssize_t (*type_read)(int, void* const, size_t);
static ssize_t unique_proxy_read(int fd, void* const buf, size_t count)
{
    COUNT(read);
    return ((type_read)orig_read)(fd, buf, count);
}
static ssize_t shared_proxy_read(int fd, void* const buf, size_t count)
{
    COUNT(read);
    ssize_t r = SHADOWHOOK_CALL_PREV(shared_proxy_read, type_read, fd, buf, count);
    SHADOWHOOK_POP_STACK();
    return r;
}

// write
GLOBAL_VARIABLES(write);
typedef ssize_t (*type_write)(int, const void* const, size_t);
static ssize_t unique_proxy_write(int fd, const void* const buf, size_t count)
{
    COUNT(write);
    return ((type_write)orig_write)(fd, buf, count);
}
static ssize_t shared_proxy_write(int fd, const void* const buf, size_t count)
{
    COUNT(write);
    ssize_t r = SHADOWHOOK_CALL_PREV(shared_proxy_write, type_write, fd, buf, count);
    SHADOWHOOK_POP_STACK();
    return r;
}

// readv
GLOBAL_VARIABLES(readv);
typedef ssize_t (*type_readv)(int, const struct iovec*, int);
static ssize_t unique_proxy_readv(int fd, const struct iovec* iov, int count)
{
    COUNT(readv);
    return ((type_readv)orig_readv)(fd, iov, count);
}
static ssize_t shared_proxy_readv(int fd, const struct iovec* iov, int count)
{
    COUNT(readv);
    ssize_t r = SHADOWHOOK_CALL_PREV(shared_proxy_readv, type_readv, fd, iov, count);
    SHADOWHOOK_POP_STACK();
    return r;
}

// writev
GLOBAL_VARIABLES(writev);
typedef ssize_t (*type_writev)(int, const struct iovec*, int);
static ssize_t unique_proxy_writev(int fd, const struct iovec* iov, int count)
{
    COUNT(writev);
    return ((type_writev)orig_writev)(fd, iov, count);
}
static ssize_t shared_proxy_writev(int fd, const struct iovec* iov, int count)
{
    COUNT(writev);
    ssize_t r = SHADOWHOOK_CALL_PREV(shared_proxy_writev, type_writev, fd, iov, count);
    SHADOWHOOK_POP_STACK();
    return r;
}

// signal
GLOBAL_VARIABLES(signal);
typedef sighandler_t (*type_signal)(int, sighandler_t);
static sighandler_t unique_proxy_signal(int signum, sighandler_t handler)
{
    COUNT(signal);
    return ((type_signal)orig_signal)(signum, handler);
}
static sighandler_t shared_proxy_signal(int signum, sighandler_t handler)
{
    COUNT(signal);
    sighandler_t r = SHADOWHOOK_CALL_PREV(shared_proxy_signal, type_signal, signum, handler);
    SHADOWHOOK_POP_STACK();
    return r;
}

// bsd_signal
GLOBAL_VARIABLES(bsd_signal);
typedef sighandler_t (*type_bsd_signal)(int, sighandler_t);
static sighandler_t unique_proxy_bsd_signal(int signum, sighandler_t handler)
{
    COUNT(bsd_signal);
    return ((type_bsd_signal)orig_bsd_signal)(signum, handler);
}
static sighandler_t shared_proxy_bsd_signal(int signum, sighandler_t handler)
{
    COUNT(bsd_signal);
    sighandler_t r = SHADOWHOOK_CALL_PREV(shared_proxy_bsd_signal, type_bsd_signal, signum, handler);
    SHADOWHOOK_POP_STACK();
    return r;
}

// sigaction
GLOBAL_VARIABLES(sigaction);
typedef int (*type_sigaction)(int, const struct sigaction*, struct sigaction*);
static int unique_proxy_sigaction(int signal, const struct sigaction* act, struct sigaction* oldact)
{
    COUNT(sigaction);
    return ((type_sigaction)orig_sigaction)(signal, act, oldact);
}
static int shared_proxy_sigaction(int signal, const struct sigaction* act, struct sigaction* oldact)
{
    COUNT(sigaction);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_sigaction, type_sigaction, signal, act, oldact);
    SHADOWHOOK_POP_STACK();
    return r;
}

// sigaction64
GLOBAL_VARIABLES(sigaction64);
typedef int (*type_sigaction64)(int, const struct sigaction64*, struct sigaction64*);
static int unique_proxy_sigaction64(int signal, const struct sigaction64* act, struct sigaction64* oldact)
{
    COUNT(sigaction64);
    return ((type_sigaction64)orig_sigaction64)(signal, act, oldact);
}
static int shared_proxy_sigaction64(int signal, const struct sigaction64* act, struct sigaction64* oldact)
{
    COUNT(sigaction64);
    int r = SHADOWHOOK_CALL_PREV(shared_proxy_sigaction64, type_sigaction64, signal, act, oldact);
    SHADOWHOOK_POP_STACK();
    return r;
}

// art::ArtMethod::Invoke
GLOBAL_VARIABLES(_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
typedef void (*type__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc)(void *, void *, uint32_t *, uint32_t, void *, const char *);
static void unique_proxy__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc(void *thiz, void *thread, uint32_t *args, uint32_t args_size, void *result, const char *shorty)
{
    COUNT(_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
    ((type__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc)orig__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc)(thiz, thread, args, args_size, result, shorty);
}
static void shared_proxy__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc(void *thiz, void *thread, uint32_t *args, uint32_t args_size, void *result, const char *shorty)
{
    COUNT(_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
    SHADOWHOOK_CALL_PREV(shared_proxy__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc, type__ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc, thiz, thread, args, args_size, result, shorty);
    SHADOWHOOK_POP_STACK();
}

// art::ArtMethod::Invoke
GLOBAL_VARIABLES(_ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
typedef void (*type__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc)(void *, void *, uint32_t *, uint32_t, void *, const char *);
static void unique_proxy__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc(void *thiz, void *thread, uint32_t *args, uint32_t args_size, void *result, const char *shorty)
{
    COUNT(_ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
    ((type__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc)orig__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc)(thiz, thread, args, args_size, result, shorty);
}
static void shared_proxy__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc(void *thiz, void *thread, uint32_t *args, uint32_t args_size, void *result, const char *shorty)
{
    COUNT(_ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
    SHADOWHOOK_CALL_PREV(shared_proxy__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc, type__ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc, thiz, thread, args, args_size, result, shorty);
    SHADOWHOOK_POP_STACK();
}

#ifdef __LP64__

// art::Heap::ThrowOutOfMemoryError
GLOBAL_VARIABLES(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE);
typedef void (*type__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE)(void *, void *, size_t, int);
static void unique_proxy__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE(void *heap, void* self, size_t byte_count, int allocator_type)
{
    COUNT(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE);
    ((type__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE)orig__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE)(heap, self, byte_count, allocator_type);
}
static void shared_proxy__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE(void *heap, void* self, size_t byte_count, int allocator_type)
{
    COUNT(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE);
    SHADOWHOOK_CALL_PREV(shared_proxy__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE, type__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE, heap, self, byte_count, allocator_type);
    SHADOWHOOK_POP_STACK();
}

#else

// art::Heap::ThrowOutOfMemoryError
GLOBAL_VARIABLES(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE);
typedef void (*type__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE)(void *, void *, size_t, int);
static void unique_proxy__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE(void *heap, void* self, size_t byte_count, int allocator_type)
{
    COUNT(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE);
    ((type__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE)orig__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE)(heap, self, byte_count, allocator_type);
}
static void shared_proxy__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE(void *heap, void* self, size_t byte_count, int allocator_type)
{
    COUNT(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE);
    SHADOWHOOK_CALL_PREV(shared_proxy__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE, type__ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE, heap, self, byte_count, allocator_type);
    SHADOWHOOK_POP_STACK();
}

#endif

// Non-existent symbol
GLOBAL_VARIABLES(fake_sym);
typedef void (*type_fake_sym)(void);
static void unique_proxy_fake_sym(void)
{
    COUNT(fake_sym);
    ((type_fake_sym)orig_fake_sym)();
}
static void shared_proxy_fake_sym(void)
{
    COUNT(fake_sym);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
    SHADOWHOOK_CALL_PREV(shared_proxy_fake_sym, type_fake_sym);
#pragma clang diagnostic pop
    SHADOWHOOK_POP_STACK();
}

#pragma clang diagnostic pop

int systest_hook(void)
{
    HOOK_SYM_ADDR(pthread_create);
    HOOK_SYM_ADDR(pthread_exit);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    HOOK_SYM_ADDR(pthread_getspecific);
    HOOK_SYM_ADDR(pthread_setspecific);
    HOOK_SYM_ADDR(pthread_mutex_lock);
    HOOK_SYM_ADDR(pthread_mutex_timedlock);
    HOOK_SYM_ADDR(pthread_mutex_unlock);
#endif
    HOOK_SYM_ADDR(pthread_rwlock_rdlock);
    HOOK_SYM_ADDR(pthread_rwlock_timedrdlock);
#ifndef __LP64__
    if(systest_util_get_api_level() < __ANDROID_API_S__)
#endif
        HOOK_SYM_ADDR(pthread_rwlock_wrlock);
    HOOK_SYM_ADDR(pthread_rwlock_timedwrlock);
    HOOK_SYM_ADDR(pthread_rwlock_unlock);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    HOOK_SYM_ADDR(malloc);
    HOOK_SYM_ADDR(calloc);
    HOOK_SYM_ADDR(realloc);
    HOOK_SYM_ADDR(memalign);
    HOOK_SYM_ADDR(posix_memalign);
    HOOK_SYM_ADDR(free);
#endif
#ifdef __LP64__
    HOOK_SYM_ADDR(mmap64);
#else
    HOOK_SYM_NAME(libc.so, __mmap2);
#endif
    HOOK_SYM_ADDR(mremap);
    HOOK_SYM_ADDR(munmap);
    HOOK_SYM_NAME(libc.so, __openat);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    HOOK_SYM_ADDR(close);
#endif
    HOOK_SYM_ADDR(pipe);
    HOOK_SYM_ADDR(pipe2);
    HOOK_SYM_ADDR(dup);
    HOOK_SYM_ADDR(dup2);
    HOOK_SYM_ADDR(dup3);
    HOOK_SYM_ADDR(socket);
    HOOK_SYM_ADDR(socketpair);
    HOOK_SYM_ADDR(eventfd);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    HOOK_SYM_NAME(libc.so, read);
    HOOK_SYM_NAME(libc.so, write);
    HOOK_SYM_ADDR(readv);
    HOOK_SYM_ADDR(writev);
#endif
#if defined(__LP64__) || __ANDROID_API__ >= __ANDROID_API_L__
    HOOK_SYM_ADDR(signal);
#else
    HOOK_SYM_ADDR(bsd_signal);
#endif
    HOOK_SYM_ADDR(sigaction);
    HOOK_SYM_ADDR(sigaction64);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    if(systest_util_get_api_level() >= __ANDROID_API_M__)
        HOOK_SYM_NAME(libart.so, _ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
    else
        HOOK_SYM_NAME(libart.so, _ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
#endif
#ifdef __LP64__
    HOOK_SYM_NAME(libart.so, _ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE);
#else
    HOOK_SYM_NAME(libart.so, _ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE);
#endif
    HOOK_SYM_NAME(libshadowhook_non_existent_library.so, fake_sym);
    return 0;
}

int systest_unhook(void)
{
    UNHOOK_SYM_ADDR(pthread_create);
    UNHOOK_SYM_ADDR(pthread_exit);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    UNHOOK_SYM_ADDR(pthread_getspecific);
    UNHOOK_SYM_ADDR(pthread_setspecific);
    UNHOOK_SYM_ADDR(pthread_mutex_lock);
    UNHOOK_SYM_ADDR(pthread_mutex_timedlock);
    UNHOOK_SYM_ADDR(pthread_mutex_unlock);
#endif
    UNHOOK_SYM_ADDR(pthread_rwlock_rdlock);
    UNHOOK_SYM_ADDR(pthread_rwlock_timedrdlock);
#ifndef __LP64__
    if(systest_util_get_api_level() < __ANDROID_API_S__)
#endif
        UNHOOK_SYM_ADDR(pthread_rwlock_wrlock);
    UNHOOK_SYM_ADDR(pthread_rwlock_timedwrlock);
    UNHOOK_SYM_ADDR(pthread_rwlock_unlock);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    UNHOOK_SYM_ADDR(malloc);
    UNHOOK_SYM_ADDR(calloc);
    UNHOOK_SYM_ADDR(realloc);
    UNHOOK_SYM_ADDR(memalign);
    UNHOOK_SYM_ADDR(posix_memalign);
    UNHOOK_SYM_ADDR(free);
#endif
#ifdef __LP64__
    UNHOOK_SYM_ADDR(mmap64);
#else
    UNHOOK_SYM_NAME(__mmap2);
#endif
    UNHOOK_SYM_ADDR(mremap);
    UNHOOK_SYM_ADDR(munmap);
    UNHOOK_SYM_NAME(__openat);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    UNHOOK_SYM_ADDR(close);
#endif
    UNHOOK_SYM_ADDR(pipe);
    UNHOOK_SYM_ADDR(pipe2);
    UNHOOK_SYM_ADDR(dup);
    UNHOOK_SYM_ADDR(dup2);
    UNHOOK_SYM_ADDR(dup3);
    UNHOOK_SYM_ADDR(socket);
    UNHOOK_SYM_ADDR(socketpair);
    UNHOOK_SYM_ADDR(eventfd);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    UNHOOK_SYM_NAME(read);
    UNHOOK_SYM_NAME(write);
    UNHOOK_SYM_ADDR(readv);
    UNHOOK_SYM_ADDR(writev);
#endif
#if defined(__LP64__) || __ANDROID_API__ >= __ANDROID_API_L__
    UNHOOK_SYM_ADDR(signal);
#else
    UNHOOK_SYM_ADDR(bsd_signal);
#endif
    UNHOOK_SYM_ADDR(sigaction);
    UNHOOK_SYM_ADDR(sigaction64);
#ifdef DEPENDENCY_ON_LOCAL_LIBRARY
    if(systest_util_get_api_level() >= __ANDROID_API_M__)
        UNHOOK_SYM_NAME(_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
    else
        UNHOOK_SYM_NAME(_ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc);
#endif
#ifdef __LP64__
    UNHOOK_SYM_NAME(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE);
#else
    UNHOOK_SYM_NAME(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE);
#endif
    UNHOOK_SYM_NAME(fake_sym);
    return 0;
}

static void *systest_simulate_thd(void *arg)
{
    (void)arg;
    return NULL;
}

static void systest_simulate(void)
{
    // pthread_create, pthread_exit
    pthread_t thd;
    pthread_create(&thd, NULL, &systest_simulate_thd, NULL);
    pthread_join(thd, NULL);

    // pthread_getspecific, pthread_setspecific
    static pthread_key_t systest_tls_key;
    static bool systest_tls_key_inited = false;
    if(!systest_tls_key_inited)
    {
        pthread_key_create(&systest_tls_key, NULL);
        systest_tls_key_inited = true;
    }
    void *tls_value = pthread_getspecific(systest_tls_key);
    pthread_setspecific(systest_tls_key, (void *)0 == tls_value ? (void *)1 : (void *)0);

    // pthread_mutex_lock, pthread_mutex_timedlock, pthread_mutex_unlock
    struct timespec timeout;
    timeout.tv_sec = time(NULL) + 1;
    timeout.tv_nsec = 0;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    if(NULL != pthread_mutex_timedlock)
    {
        timeout.tv_sec = time(NULL) + 1;
        if(0 == pthread_mutex_timedlock(&mutex, &timeout))
            pthread_mutex_unlock(&mutex);
    }

    // pthread_rwlock_rdlock, pthread_rwlock_wrlock, pthread_rwlock_unlock
    // pthread_rwlock_timedrdlock, pthread_rwlock_timedwrlock
    static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
    pthread_rwlock_rdlock(&rwlock);
    pthread_rwlock_unlock(&rwlock);
    pthread_rwlock_wrlock(&rwlock);
    pthread_rwlock_unlock(&rwlock);
    timeout.tv_sec = time(NULL) + 1;
    if(0 == pthread_rwlock_timedrdlock(&rwlock, &timeout))
        pthread_rwlock_unlock(&rwlock);
    timeout.tv_sec = time(NULL) + 1;
    if(0 == pthread_rwlock_timedwrlock(&rwlock, &timeout))
        pthread_rwlock_unlock(&rwlock);

    // malloc, calloc, realloc, memalign, posix_memalign, free ...
    void *p = malloc(16);
    p = realloc(p, 24);
    free(p);
    p = calloc(1, 16);
    free(p);
    p = memalign(16, 32);
    free(p);
    if(NULL != posix_memalign)
    {
        posix_memalign(&p, 16, 32);
        free(p);
    }

    // mmap64, mmap, mremap, munmap
    p = mmap64(NULL, 512, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    p = mremap(p, 512, 1024, 0);
    munmap(p, 1024);

    // open, close
    int fd = open("/dev/null", O_RDWR);
    close(fd);

    // pipe, pipe2
    int fds[2];
    if(0 == pipe(fds))
    {
        close(fds[0]);
        close(fds[1]);
    }
    if(0 == pipe2(fds, O_CLOEXEC))
    {
        close(fds[0]);
        close(fds[1]);
    }

    // dup, dup2, dup3
    fd = open("/dev/null", O_RDWR);
    int fd2 = dup(fd);
    close(fd);
    close(fd2);

    // dup2
    fd = open("/dev/null", O_RDWR);
    fd2 = open("/dev/null", O_RDWR);
    dup2(fd, fd2);
    close(fd);
    close(fd2);

    // dup3
    if(NULL != dup3)
    {
        fd  = open("/dev/null", O_RDWR);
        fd2 = open("/dev/null", O_RDWR);
        dup3(fd, fd2, O_CLOEXEC);
        close(fd);
        close(fd2);
    }

    // socket, socketpair
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    close(fd);
    if(0 == socketpair(AF_UNIX, SOCK_STREAM, 0, fds))
    {
        close(fds[0]);
        close(fds[1]);
    }

    // eventfd
    fd = eventfd(0, EFD_CLOEXEC);
    close(fd);

    // read, write
    pipe(fds);
    char a = 'a';
    write(fds[1], &a, sizeof(a));
    read(fds[0], &a, sizeof(a));
    close(fds[0]);
    close(fds[1]);

    // readv, writev
    pipe(fds);
    a = 'a';
    char b = 'b';
    struct iovec iov[2] = {
        {&a, sizeof(a)},
        {&b, sizeof(b)},
    };
    writev(fds[1], iov, 2);
    readv(fds[0], iov, 2);
    close(fds[0]);
    close(fds[1]);

    // signal, sigaction, sigaction64
    sighandler_t old_signal_handler = signal(SIGURG, NULL);
    memset(&old_signal_handler, 0, sizeof(sighandler_t));
    struct sigaction old_sigaction_handler;
    sigaction(SIGURG, NULL, &old_sigaction_handler);
    if(NULL != sigaction64)
    {
        struct sigaction64 old_sigaction64_handler;
        sigaction64(SIGURG, NULL, &old_sigaction64_handler);
    }
}

static void systest_dump(void)
{
    LOG(DELIMITER_TITLE);
    SHOW(pthread_create);
    SHOW(pthread_exit);

    LOG(DELIMITER_SUB);
    SHOW(pthread_getspecific);
    SHOW(pthread_setspecific);

    LOG(DELIMITER_SUB);
    SHOW(pthread_mutex_lock);
    SHOW(pthread_mutex_timedlock);
    SHOW(pthread_mutex_unlock);

    LOG(DELIMITER_SUB);
    SHOW(pthread_rwlock_rdlock);
    SHOW(pthread_rwlock_timedrdlock);
    SHOW(pthread_rwlock_wrlock);
    SHOW(pthread_rwlock_timedwrlock);
    SHOW(pthread_rwlock_unlock);

    LOG(DELIMITER_SUB);
    SHOW(malloc);
    SHOW(calloc);
    SHOW(realloc);
    SHOW(memalign);
    SHOW(posix_memalign);
    SHOW(free);

    LOG(DELIMITER_SUB);
#ifdef __LP64__
    SHOW(mmap64);
#else
    SHOW(__mmap2);
#endif
    SHOW(mremap);
    SHOW(munmap);

    LOG(DELIMITER_SUB);
    SHOW(__openat);
    SHOW(close);
    SHOW(pipe);
    SHOW(pipe2);
    SHOW(dup);
    SHOW(dup2);
    SHOW(dup3);
    SHOW(socket);
    SHOW(socketpair);
    SHOW(eventfd);

    LOG(DELIMITER_SUB);
    SHOW(read);
    SHOW(readv);
    SHOW(write);
    SHOW(writev);

    LOG(DELIMITER_SUB);
#if defined(__LP64__) || __ANDROID_API__ >= __ANDROID_API_L__
    SHOW(signal);
#else
    SHOW(bsd_signal);
#endif
    SHOW(sigaction);
    SHOW(sigaction64);

    LOG(DELIMITER_SUB);
    if(systest_util_get_api_level() >= __ANDROID_API_M__)
        SHOW2(_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc, art::ArtMethod::Invoke);
    else
        SHOW2(_ZN3art6mirror9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc, art::mirror::ArtMethod::Invoke);
#ifdef __LP64__
    SHOW2(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEmNS0_13AllocatorTypeE, art::Heap::ThrowOutOfMemoryError);
#else
    SHOW2(_ZN3art2gc4Heap21ThrowOutOfMemoryErrorEPNS_6ThreadEjNS0_13AllocatorTypeE, art::Heap::ThrowOutOfMemoryError);
#endif

    LOG(DELIMITER_SUB);
    SHOW(fake_sym);
}

int systest_run(void)
{
    systest_simulate();
    systest_dump();
    return 0;
}
