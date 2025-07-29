// Copyright (c) 2021-2025 ByteDance Inc.
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

#include "sh_switch.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"
#include "sh_config.h"
#include "sh_errno.h"
#include "sh_hub.h"
#include "sh_inst.h"
#include "sh_island.h"
#include "sh_linker.h"
#include "sh_log.h"
#include "sh_safe.h"
#include "sh_sig.h"
#include "sh_trampo.h"
#include "sh_util.h"
#include "shadowhook.h"
#include "tree.h"

#define SH_SWITCH_DELAY_SEC                    10
#define SH_SWITCH_GLUE_LAUNCHER_ANON_PAGE_NAME "shadowhook-interceptor-glue-launcher"
#if defined(__arm__)
#define SH_SWITCH_GLUE_LAUNCHER_SZ 20
#elif defined(__aarch64__)
#define SH_SWITCH_GLUE_LAUNCHER_SZ 28
#endif

#define SH_SWITCH_HOOK_MODE_NONE   0  // only interceptors
#define SH_SWITCH_HOOK_MODE_UNIQUE 1  // with hook in unique mode
#define SH_SWITCH_HOOK_MODE_QUEUE  2  // with hooks in multi and shared mode

// interceptor for each target_addr
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct sh_switch_interceptor {
  shadowhook_interceptor_t pre;
  void *data;
  size_t flags;
  bool enabled;
  SLIST_ENTRY(sh_switch_interceptor, ) link;
} sh_switch_interceptor_t;
#pragma clang diagnostic pop

// interceptor list
typedef SLIST_HEAD(sh_switch_interceptor_list, sh_switch_interceptor, ) sh_switch_interceptor_list_t;

// proxy-info for each hook-task in multi-mode
typedef struct sh_switch_proxy {
  uintptr_t new_addr;
  uintptr_t *orig_addr;
  TAILQ_ENTRY(sh_switch_proxy, ) link;
} sh_switch_proxy_t;

// proxy-info queue
typedef TAILQ_HEAD(sh_switch_proxy_queue, sh_switch_proxy, ) sh_switch_proxy_queue_t;

// switch
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct sh_switch {
  size_t intercept_flags_union;
  size_t hook_mode;
  uintptr_t target_addr;  // key
  sh_addr_info_t addr_info;
  sh_inst_t inst;
  uintptr_t start_addr;  // = proxy_addr(without interceptor) / glue_launcher_addr(with interceptor)
  uintptr_t proxy_addr;  // = 0 (none mode) / = new_addr(unique mode) / first new_addr(queue mode)
  uintptr_t resume_addr;
  sh_hub_t *hub;                    // for shared mode
  sh_switch_proxy_queue_t proxies;  // for multi mode
  uintptr_t glue_launcher_addr;     // trampoline for shadowhook_interceptor_glue()
  sh_switch_interceptor_list_t interceptors;
  size_t interceptors_size;
  time_t destroy_ts;
  RB_ENTRY(sh_switch) link_rbtree;
  TAILQ_ENTRY(sh_switch, ) link_tailq;
} sh_switch_t;
#pragma clang diagnostic pop

// switch tree
static __inline__ int sh_switch_cmp(sh_switch_t *a, sh_switch_t *b) {
  if (a->target_addr == b->target_addr)
    return 0;
  else
    return a->target_addr > b->target_addr ? 1 : -1;
}
typedef RB_HEAD(sh_switch_tree, sh_switch) sh_switch_tree_t;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
RB_GENERATE_STATIC(sh_switch_tree, sh_switch, link_rbtree, sh_switch_cmp)
#pragma clang diagnostic pop

// switch tree object
static sh_switch_tree_t sh_switches = RB_INITIALIZER(&sh_switches);
static pthread_mutex_t sh_switches_lock = PTHREAD_MUTEX_INITIALIZER;

// switch queue
typedef TAILQ_HEAD(sh_switch_queue, sh_switch, ) sh_switch_queue_t;

// switch queue object
static sh_switch_queue_t sh_switches_delayed_destroy = TAILQ_HEAD_INITIALIZER(sh_switches_delayed_destroy);
static pthread_mutex_t sh_switches_delayed_destroy_lock = PTHREAD_MUTEX_INITIALIZER;

static sh_trampo_mgr_t sh_switch_interceptor_trampo_mgr;

static size_t sh_switch_get_hook_mode(size_t flags) {
  if (flags & SHADOWHOOK_HOOK_WITH_SHARED_MODE) {
    return SHADOWHOOK_HOOK_WITH_SHARED_MODE;
  } else if (flags & SHADOWHOOK_HOOK_WITH_UNIQUE_MODE) {
    return SHADOWHOOK_HOOK_WITH_UNIQUE_MODE;
  } else if (flags & SHADOWHOOK_HOOK_WITH_MULTI_MODE) {
    return SHADOWHOOK_HOOK_WITH_MULTI_MODE;
  } else {
    // use the default mode
    if (SHADOWHOOK_IS_SHARED_MODE) {
      return SHADOWHOOK_HOOK_WITH_SHARED_MODE;
    } else if (SHADOWHOOK_IS_UNIQUE_MODE) {
      return SHADOWHOOK_HOOK_WITH_UNIQUE_MODE;
    } else if (SHADOWHOOK_IS_MULTI_MODE) {
      return SHADOWHOOK_HOOK_WITH_MULTI_MODE;
    } else {
      abort();
    }
  }
}

void sh_switch_init(void) {
  sh_trampo_init_mgr(&sh_switch_interceptor_trampo_mgr, SH_SWITCH_GLUE_LAUNCHER_ANON_PAGE_NAME,
                     SH_SWITCH_GLUE_LAUNCHER_SZ, 0);
}

static void sh_switch_inst_set_orig_addr(uintptr_t addr, void *arg) {
  uintptr_t *pkg = (uintptr_t *)arg;
  sh_switch_t *self = (sh_switch_t *)*pkg++;
  uintptr_t *orig_addr = (uintptr_t *)*pkg++;
  uintptr_t *orig_addr2 = (uintptr_t *)*pkg;

  if (NULL != orig_addr) __atomic_store_n(orig_addr, addr, __ATOMIC_SEQ_CST);
  if (NULL != orig_addr2) __atomic_store_n(orig_addr2, addr, __ATOMIC_SEQ_CST);
  if (self->addr_info.is_proc_start) sh_safe_set_orig_addr(self->target_addr, addr);
  self->resume_addr = addr;
}

static int sh_switch_inst_hook(sh_switch_t *self, uintptr_t new_addr, uintptr_t *orig_addr,
                               uintptr_t *orig_addr2) {
  uintptr_t pkg[3] = {(uintptr_t)self, (uintptr_t)orig_addr, (uintptr_t)orig_addr2};
  int r = sh_inst_hook(&self->inst, self->target_addr, &self->addr_info, new_addr,
                       new_addr == self->glue_launcher_addr, sh_switch_inst_set_orig_addr, pkg);
  if (0 == r) self->start_addr = new_addr;
  return r;
}

static int sh_switch_inst_rehook(sh_switch_t *self, uintptr_t new_addr) {
  int r = sh_inst_rehook(&self->inst, self->target_addr, &self->addr_info, new_addr,
                         new_addr == self->glue_launcher_addr);
  if (0 == r) self->start_addr = new_addr;
  return r;
}

static int sh_switch_inst_unhook(sh_switch_t *self) {
  int r = sh_inst_unhook(&self->inst, self->target_addr);
  if (0 != r) return r;

  if (self->addr_info.is_proc_start) sh_safe_set_orig_addr(self->target_addr, 0);
  return 0;
}

static int sh_switch_proxy_add(sh_switch_t *self, uintptr_t new_addr, uintptr_t *orig_addr, bool add_to_hub) {
  int r;
  uintptr_t hub_trampo_addr = (NULL == self->hub ? 0 : sh_hub_get_trampo_addr(self->hub));
  bool is_hub_in_queue = false;

  // check duplicated proxy function
  sh_switch_proxy_t *proxy;
  TAILQ_FOREACH(proxy, &self->proxies, link) {
    if (0 != hub_trampo_addr && proxy->new_addr == hub_trampo_addr) {
      is_hub_in_queue = true;
      if (sh_hub_is_proxy_duplicated(self->hub, new_addr)) {
        return SHADOWHOOK_ERRNO_HOOK_HUB_DUP;
      }
    } else if (proxy->new_addr == new_addr) {
      return SHADOWHOOK_ERRNO_HOOK_MULTI_DUP;
    }
  }

  if (add_to_hub) {
    if (NULL == self->hub) {
      if (0 != (r = sh_hub_create(&self->hub))) return r;
    }
    if (0 != (r = sh_hub_add_proxy(self->hub, new_addr))) return r;
    if (NULL != orig_addr) *orig_addr = self->resume_addr;
  }

  if (!add_to_hub || !is_hub_in_queue) {
    // add at the end of the proxy-info queue
    if (NULL == (proxy = malloc(sizeof(sh_switch_proxy_t)))) {
      if (add_to_hub) sh_hub_del_proxy(self->hub, new_addr);
      return SHADOWHOOK_ERRNO_OOM;
    }
    proxy->new_addr = (add_to_hub ? sh_hub_get_trampo_addr(self->hub) : new_addr);
    proxy->orig_addr = (add_to_hub ? sh_hub_get_orig_addr(self->hub) : orig_addr);
    TAILQ_INSERT_TAIL(&self->proxies, proxy, link);

    // add at the end of the runtime proxy queue
    *(proxy->orig_addr) = self->resume_addr;
    sh_switch_proxy_t *prev = TAILQ_PREV(proxy, sh_switch_proxy_queue, link);
    if (NULL != prev) {
      __atomic_store_n(prev->orig_addr, proxy->new_addr, __ATOMIC_RELEASE);
    } else {
      self->hook_mode = SH_SWITCH_HOOK_MODE_QUEUE;
      __atomic_store_n(&self->proxy_addr, proxy->new_addr, __ATOMIC_RELEASE);
    }
  }

  return 0;
}

static int sh_switch_proxy_add_shared(sh_switch_t *self, uintptr_t new_addr, uintptr_t *orig_addr) {
  return sh_switch_proxy_add(self, new_addr, orig_addr, true);
}

static int sh_switch_proxy_add_multi(sh_switch_t *self, uintptr_t new_addr, uintptr_t *orig_addr) {
  return sh_switch_proxy_add(self, new_addr, orig_addr, false);
}

static int sh_switch_proxy_del(sh_switch_t *self, uintptr_t new_addr, bool del_from_hub) {
  int r;
  uintptr_t hub_trampo_addr = (NULL == self->hub ? 0 : sh_hub_get_trampo_addr(self->hub));
  sh_switch_proxy_t *proxy = NULL;

  if (del_from_hub) {
    // remove from hub
    if (0 != (r = sh_hub_del_proxy(self->hub, new_addr))) return r;
    if (0 == sh_hub_get_proxy_count(self->hub)) {
      TAILQ_FOREACH(proxy, &self->proxies, link) {
        if (proxy->new_addr == hub_trampo_addr) break;
      }
      if (NULL == proxy) abort();
    } else {
      return 0;  // finished
    }
  } else {
    // find in proxy-info queue
    TAILQ_FOREACH(proxy, &self->proxies, link) {
      if (proxy->new_addr != hub_trampo_addr && proxy->new_addr == new_addr) break;
    }
    if (NULL == proxy) return SHADOWHOOK_ERRNO_UNHOOK_NOTFOUND;
  }

  // remove node from runtime proxy queue
  sh_switch_proxy_t *prev = TAILQ_PREV(proxy, sh_switch_proxy_queue, link);
  sh_switch_proxy_t *next = TAILQ_NEXT(proxy, link);
  if (NULL != prev) {
    if (NULL != next) {
      __atomic_store_n(prev->orig_addr, next->new_addr, __ATOMIC_RELEASE);
    } else {
      __atomic_store_n(prev->orig_addr, self->resume_addr, __ATOMIC_RELEASE);
    }
  } else {
    if (NULL != next) {
      if (self->proxy_addr == self->start_addr) {
        // prev == NULL && next != NULL && proxy_addr == start_addr
        if (0 != (r = sh_switch_inst_rehook(self, next->new_addr))) return r;
      }
      __atomic_store_n(&self->proxy_addr, next->new_addr, __ATOMIC_RELEASE);
    } else {
      self->hook_mode = SH_SWITCH_HOOK_MODE_NONE;
      __atomic_store_n(&self->proxy_addr, 0, __ATOMIC_RELEASE);
    }
  }

  // remove from proxy-info queue
  TAILQ_REMOVE(&self->proxies, proxy, link);
  free(proxy);
  return 0;
}

static int sh_switch_proxy_del_shared(sh_switch_t *self, uintptr_t new_addr) {
  return sh_switch_proxy_del(self, new_addr, true);
}

static int sh_switch_proxy_del_multi(sh_switch_t *self, uintptr_t new_addr) {
  return sh_switch_proxy_del(self, new_addr, false);
}

void shadowhook_interceptor_caller(void *ctx, shadowhook_cpu_context_t *cpu_context, void **next_hop) {
  sh_switch_t *self = (sh_switch_t *)ctx;

#if defined(__aarch64__)
  cpu_context->pc = self->target_addr;
#elif defined(__arm__)
  cpu_context->regs[15] = SH_UTIL_CLEAR_BIT0(self->target_addr);
#endif

  sh_switch_interceptor_t *interceptor;
  SLIST_FOREACH(interceptor, &self->interceptors, link) {
    if (interceptor->enabled) interceptor->pre(cpu_context, interceptor->data);
  }

  uintptr_t proxy_addr = __atomic_load_n(&self->proxy_addr, __ATOMIC_ACQUIRE);
  *next_hop = (void *)(0 != proxy_addr ? proxy_addr : self->resume_addr);
}

static int sh_switch_interceptor_add(sh_switch_t *self, shadowhook_interceptor_t pre, void *data,
                                     size_t flags) {
  // check repeated interceptor
  sh_switch_interceptor_t *interceptor;
  SLIST_FOREACH(interceptor, &self->interceptors, link) {
    if (interceptor->pre == pre && interceptor->data == data && interceptor->enabled) {
      return SHADOWHOOK_ERRNO_INTERCEPT_DUP;
    }
  }

  // try to re-enable an exists item
  SLIST_FOREACH(interceptor, &self->interceptors, link) {
    if (interceptor->pre == pre && interceptor->data == data && !interceptor->enabled) {
      interceptor->flags = flags;
      self->intercept_flags_union |= flags;
      self->interceptors_size++;
      __atomic_store_n((bool *)&interceptor->enabled, true, __ATOMIC_RELEASE);
      SH_LOG_INFO("switch-interceptor: size %zu: add(re-enable) pre %" PRIxPTR ", data %" PRIxPTR,
                  self->interceptors_size, (uintptr_t)pre, (uintptr_t)data);
      return 0;
    }
  }

  // create new item
  if (NULL == (interceptor = malloc(sizeof(sh_switch_interceptor_t)))) return SHADOWHOOK_ERRNO_OOM;
  interceptor->pre = pre;
  interceptor->data = data;
  interceptor->flags = flags;
  interceptor->enabled = true;

  self->intercept_flags_union |= flags;
  self->interceptors_size++;

  // insert to the head of the interceptor-list
  // equivalent to: SLIST_INSERT_HEAD(&self->interceptors, interceptor, link);
  // but: __ATOMIC_RELEASE ensures readers see only fully-constructed item
  SLIST_NEXT(interceptor, link) = SLIST_FIRST(&self->interceptors);
  __atomic_store_n((uintptr_t *)(&SLIST_FIRST(&self->interceptors)), (uintptr_t)interceptor,
                   __ATOMIC_RELEASE);

  SH_LOG_INFO("switch-interceptor: size %zu: add(new) pre %" PRIxPTR ", data %" PRIxPTR,
              self->interceptors_size, (uintptr_t)pre, (uintptr_t)data);
  return 0;
}

static int sh_switch_interceptor_del(sh_switch_t *self, shadowhook_interceptor_t pre, void *data) {
  int r = SHADOWHOOK_ERRNO_UNHOOK_NOTFOUND;
  size_t intercept_flags_union = 0;

  sh_switch_interceptor_t *interceptor;
  SLIST_FOREACH(interceptor, &self->interceptors, link) {
    if (interceptor->enabled) {
      if (interceptor->pre == pre && interceptor->data == data) {
        self->interceptors_size--;
        __atomic_store_n((bool *)&interceptor->enabled, false, __ATOMIC_RELEASE);
        SH_LOG_INFO("switch-interceptor: size %zu: del pre %" PRIxPTR ", data %" PRIxPTR,
                    self->interceptors_size, (uintptr_t)pre, (uintptr_t)data);
        r = 0;
      } else {
        intercept_flags_union |= interceptor->flags;
      }
    }
  }

  if (0 == r) self->intercept_flags_union = intercept_flags_union;

  return r;
}

static int sh_switch_create_glue_launcher(sh_switch_t *self) {
  self->glue_launcher_addr = sh_trampo_alloc(&sh_switch_interceptor_trampo_mgr);
  if (0 == self->glue_launcher_addr) return SHADOWHOOK_ERRNO_OOM;

  sh_inst_build_glue_launcher((void *)(self->glue_launcher_addr), self);
  sh_util_clear_cache(self->glue_launcher_addr, SH_SWITCH_GLUE_LAUNCHER_SZ);

  SH_LOG_INFO("switch: create glue_launcher, target_addr %" PRIxPTR, self->target_addr);
  return 0;
}

static void sh_switch_destroy_inner(sh_switch_t *self) {
  if (NULL != self->hub) sh_hub_destroy(self->hub);

  if (0 != self->glue_launcher_addr)
    sh_trampo_free(&sh_switch_interceptor_trampo_mgr, self->glue_launcher_addr);

  while (!TAILQ_EMPTY(&self->proxies)) {
    sh_switch_proxy_t *proxy = TAILQ_FIRST(&self->proxies);
    TAILQ_REMOVE(&self->proxies, proxy, link);
    free(proxy);
  }

  while (!SLIST_EMPTY(&self->interceptors)) {
    sh_switch_interceptor_t *interceptor = SLIST_FIRST(&self->interceptors);
    SLIST_REMOVE_HEAD(&self->interceptors, link);
    free(interceptor);
  }

  free(self);
}

static void sh_switch_destroy(sh_switch_t *self, bool with_delay) {
  SH_LOG_INFO("switch: destroy, target_addr %" PRIxPTR, self->target_addr);

  if (!TAILQ_EMPTY(&sh_switches_delayed_destroy) || with_delay) {
    pthread_mutex_lock(&sh_switches_delayed_destroy_lock);
    time_t now = 0;
    sh_switch_t *sw, *tmp;
    TAILQ_FOREACH_SAFE(sw, &sh_switches_delayed_destroy, link_tailq, tmp) {
      if (0 == now) now = sh_util_get_stable_timestamp();
      if (now - sw->destroy_ts > SH_SWITCH_DELAY_SEC) {
        TAILQ_REMOVE(&sh_switches_delayed_destroy, sw, link_tailq);
        SH_LOG_INFO("switch: delayed destroy, target_addr %" PRIxPTR, sw->target_addr);
        sh_switch_destroy_inner(sw);
      } else {
        break;
      }
    }
    if (with_delay) {
      if (0 == now) now = sh_util_get_stable_timestamp();
      self->destroy_ts = now;
      TAILQ_INSERT_TAIL(&sh_switches_delayed_destroy, self, link_tailq);
    }
    pthread_mutex_unlock(&sh_switches_delayed_destroy_lock);
  }

  if (!with_delay) {
    sh_switch_destroy_inner(self);
  }
}

static int sh_switch_create(sh_switch_t **self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                            uintptr_t new_addr, size_t hook_mode) {
  *self = calloc(1, sizeof(sh_switch_t));
  if (NULL == *self) return SHADOWHOOK_ERRNO_OOM;

  (*self)->intercept_flags_union = 0;  // default flags
  (*self)->hook_mode = hook_mode;
  (*self)->target_addr = target_addr;
  memcpy(&(*self)->addr_info, addr_info, sizeof(sh_addr_info_t));
  (*self)->proxy_addr = new_addr;
  TAILQ_INIT(&((*self)->proxies));
  SLIST_INIT(&(*self)->interceptors);
  SH_LOG_INFO("switch: create, target_addr %" PRIxPTR, target_addr);
  return 0;
}

static sh_switch_t *sh_switch_find(uintptr_t target_addr) {
  sh_switch_t key = {.target_addr = target_addr};
  return RB_FIND(sh_switch_tree, &sh_switches, &key);
}

static int sh_switch_hook_unique(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                                 uintptr_t *orig_addr, size_t *backup_len) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL != self) {
    if (0 != self->proxy_addr) {
      if (SH_SWITCH_HOOK_MODE_UNIQUE != self->hook_mode) {
        r = SHADOWHOOK_ERRNO_MODE_CONFLICT;
      } else {
        r = SHADOWHOOK_ERRNO_HOOK_DUP;
      }
      goto end;
    } else {
      self->hook_mode = SH_SWITCH_HOOK_MODE_UNIQUE;
      if (NULL != orig_addr) *orig_addr = self->resume_addr;
      __atomic_store_n(&self->proxy_addr, new_addr, __ATOMIC_RELEASE);
    }
  } else {
    if (0 != (r = sh_switch_create(&self, target_addr, addr_info, new_addr, SH_SWITCH_HOOK_MODE_UNIQUE)))
      goto end;
    if (0 != (r = sh_switch_inst_hook(self, self->proxy_addr, orig_addr, NULL))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    RB_INSERT(sh_switch_tree, &sh_switches, self);
  }
  *backup_len = self->inst.backup_len;
  r = 0;  // OK

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

static int sh_switch_hook_multi(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                                uintptr_t *orig_addr, size_t *backup_len) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL != self) {
    if (SH_SWITCH_HOOK_MODE_UNIQUE == self->hook_mode) {
      r = SHADOWHOOK_ERRNO_MODE_CONFLICT;
      goto end;
    }
    if (0 != (r = sh_switch_proxy_add_multi(self, new_addr, orig_addr))) goto end;
  } else {
    if (0 != (r = sh_switch_create(&self, target_addr, addr_info, new_addr, SH_SWITCH_HOOK_MODE_QUEUE)))
      goto end;
    if (0 != (r = sh_switch_proxy_add_multi(self, new_addr, orig_addr))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    if (0 != (r = sh_switch_inst_hook(self, self->proxy_addr, orig_addr, NULL))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    RB_INSERT(sh_switch_tree, &sh_switches, self);
  }

  *backup_len = self->inst.backup_len;
  r = 0;  // OK

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

static int sh_switch_hook_shared(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                                 uintptr_t *orig_addr, size_t *backup_len) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL != self) {
    if (SH_SWITCH_HOOK_MODE_UNIQUE == self->hook_mode) {
      r = SHADOWHOOK_ERRNO_MODE_CONFLICT;
      goto end;
    }
    if (0 != (r = sh_switch_proxy_add_shared(self, new_addr, orig_addr))) goto end;
  } else {
    if (0 != (r = sh_switch_create(&self, target_addr, addr_info, new_addr, SH_SWITCH_HOOK_MODE_QUEUE)))
      goto end;
    if (0 != (r = sh_switch_proxy_add_shared(self, new_addr, orig_addr))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    if (0 != (r = sh_switch_inst_hook(self, self->proxy_addr, orig_addr, sh_hub_get_orig_addr(self->hub)))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    RB_INSERT(sh_switch_tree, &sh_switches, self);
  }

  *backup_len = self->inst.backup_len;
  r = 0;  // OK

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

int sh_switch_hook(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr, uintptr_t *orig_addr,
                   size_t flags, size_t *backup_len) {
  int r;
  size_t hook_mode = sh_switch_get_hook_mode(flags);
  char *hook_mode_str;

  if (SHADOWHOOK_HOOK_WITH_UNIQUE_MODE == hook_mode) {
    r = sh_switch_hook_unique(target_addr, addr_info, new_addr, orig_addr, backup_len);
    hook_mode_str = "UNIQUE";
  } else if (SHADOWHOOK_HOOK_WITH_SHARED_MODE == hook_mode) {
    r = sh_switch_hook_shared(target_addr, addr_info, new_addr, orig_addr, backup_len);
    hook_mode_str = "SHARED";
  } else {
    r = sh_switch_hook_multi(target_addr, addr_info, new_addr, orig_addr, backup_len);
    hook_mode_str = "MULTI";
  }

  if (0 == r)
    SH_LOG_INFO("switch: hook in %s mode OK: target_addr %" PRIxPTR ", new_addr %" PRIxPTR, hook_mode_str,
                target_addr, new_addr);

  return r;
}

static void sh_switch_inst_set_orig_addr2(uintptr_t addr, void *arg) {
  uintptr_t *orig_addr = (uintptr_t *)arg;
  if (NULL != orig_addr) __atomic_store_n(orig_addr, addr, __ATOMIC_SEQ_CST);
}

int sh_switch_hook_invisible(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                             uintptr_t *orig_addr, size_t *backup_len) {
  int r;
  sh_inst_t inst;
  memset(&inst, 0, sizeof(sh_inst_t));

  pthread_mutex_lock(&sh_switches_lock);
  r = sh_inst_hook(&inst, target_addr, addr_info, new_addr, false, sh_switch_inst_set_orig_addr2, orig_addr);
  pthread_mutex_unlock(&sh_switches_lock);

  if (0 == r)
    SH_LOG_INFO("switch: hook invisible OK: target_addr %" PRIxPTR ", new_addr %" PRIxPTR, target_addr,
                new_addr);

  *backup_len = inst.backup_len;
  return r;
}

static int sh_switch_unhook_unique(uintptr_t target_addr) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL == self) {
    r = SHADOWHOOK_ERRNO_UNHOOK_NOTFOUND;
    goto end;
  } else {
    if (SH_SWITCH_HOOK_MODE_UNIQUE != self->hook_mode) {
      r = SHADOWHOOK_ERRNO_MODE_CONFLICT;
      goto end;
    }
    self->hook_mode = SH_SWITCH_HOOK_MODE_NONE;
    __atomic_store_n(&self->proxy_addr, 0, __ATOMIC_RELEASE);
    r = 0;
    if (0 == self->interceptors_size) {
      r = sh_switch_inst_unhook(self);
      RB_REMOVE(sh_switch_tree, &sh_switches, self);
      sh_switch_destroy(self, true);
    }
    r = 0;  // OK
  }

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

static int sh_switch_unhook_multi(uintptr_t target_addr, uintptr_t new_addr) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL == self) {
    r = SHADOWHOOK_ERRNO_UNHOOK_NOTFOUND;
    goto end;
  } else {
    if (SH_SWITCH_HOOK_MODE_QUEUE != self->hook_mode) {
      r = SHADOWHOOK_ERRNO_MODE_CONFLICT;
      goto end;
    }
    if (0 != (r = sh_switch_proxy_del_multi(self, new_addr))) goto end;
    if (TAILQ_EMPTY(&self->proxies) && 0 == self->interceptors_size) {
      r = sh_switch_inst_unhook(self);
      RB_REMOVE(sh_switch_tree, &sh_switches, self);
      sh_switch_destroy(self, true);
    }
  }

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

static int sh_switch_unhook_shared(uintptr_t target_addr, uintptr_t new_addr) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL == self) {
    r = SHADOWHOOK_ERRNO_UNHOOK_NOTFOUND;
    goto end;
  } else {
    if (SH_SWITCH_HOOK_MODE_QUEUE != self->hook_mode) {
      r = SHADOWHOOK_ERRNO_MODE_CONFLICT;
      goto end;
    }
    if (0 != (r = sh_switch_proxy_del_shared(self, new_addr))) goto end;
    if (TAILQ_EMPTY(&self->proxies) && 0 == self->interceptors_size) {
      r = sh_switch_inst_unhook(self);
      RB_REMOVE(sh_switch_tree, &sh_switches, self);
      sh_switch_destroy(self, true);
    }
  }

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

int sh_switch_unhook(uintptr_t target_addr, uintptr_t new_addr, size_t flags) {
  int r;
  size_t hook_mode = sh_switch_get_hook_mode(flags);
  char *hook_mode_str;

  if (SHADOWHOOK_HOOK_WITH_UNIQUE_MODE == hook_mode) {
    r = sh_switch_unhook_unique(target_addr);
    hook_mode_str = "UNIQUE";
  } else if (SHADOWHOOK_HOOK_WITH_SHARED_MODE == hook_mode) {
    r = sh_switch_unhook_shared(target_addr, new_addr);
    hook_mode_str = "SHARED";
  } else {
    r = sh_switch_unhook_multi(target_addr, new_addr);
    hook_mode_str = "MULTI";
  }

  if (0 == r)
    SH_LOG_INFO("switch: unhook in %s mode OK: target_addr %" PRIxPTR ", new_addr %" PRIxPTR, hook_mode_str,
                target_addr, new_addr);

  return r;
}

int sh_switch_intercept(uintptr_t target_addr, sh_addr_info_t *addr_info, shadowhook_interceptor_t pre,
                        void *data, size_t flags, size_t *backup_len) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL != self) {
    if (0 == self->glue_launcher_addr) {
      if (0 != (r = sh_switch_create_glue_launcher(self))) goto end;
    }
    if (self->start_addr != self->glue_launcher_addr) {
      if (0 != (r = sh_switch_inst_rehook(self, self->glue_launcher_addr))) goto end;
    }
  } else {
    if (0 != (r = sh_switch_create(&self, target_addr, addr_info, 0, SH_SWITCH_HOOK_MODE_NONE))) goto end;
    if (0 != (r = sh_switch_create_glue_launcher(self))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    if (0 != (r = sh_switch_inst_hook(self, self->glue_launcher_addr, NULL, NULL))) {
      sh_switch_destroy(self, false);
      goto end;
    }
    RB_INSERT(sh_switch_tree, &sh_switches, self);
  }
  if (0 != (r = sh_switch_interceptor_add(self, pre, data, flags))) goto end;
  *backup_len = self->inst.backup_len;
  r = 0;  // OK

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

int sh_switch_unintercept(uintptr_t target_addr, shadowhook_interceptor_t pre, void *data) {
  int r;
  pthread_mutex_lock(&sh_switches_lock);

  sh_switch_t *self = sh_switch_find(target_addr);
  if (NULL == self) {
    r = SHADOWHOOK_ERRNO_UNHOOK_NOTFOUND;
    goto end;
  } else {
    if (0 != (r = sh_switch_interceptor_del(self, pre, data))) goto end;
    if (0 == self->interceptors_size) {
      if (0 == self->proxy_addr) {
        r = sh_switch_inst_unhook(self);
        RB_REMOVE(sh_switch_tree, &sh_switches, self);
        sh_switch_destroy(self, true);
      } else {
        if (0 != (r = sh_switch_inst_rehook(self, self->proxy_addr))) goto end;
      }
    }
  }

end:
  pthread_mutex_unlock(&sh_switches_lock);
  return r;
}

void sh_switch_free_after_dlclose(struct dl_phdr_info *info) {
  pthread_mutex_lock(&sh_switches_lock);
  sh_switch_t *sw, *tmp;
  RB_FOREACH_SAFE(sw, sh_switch_tree, &sh_switches, tmp) {
    if (sh_linker_is_addr_in_elf_pt_load(sw->target_addr, (void *)info->dlpi_addr, info->dlpi_phdr,
                                         info->dlpi_phnum)) {
      RB_REMOVE(sh_switch_tree, &sh_switches, sw);
      sh_inst_free_after_dlclose(&sw->inst, sw->target_addr);
      SH_LOG_INFO("switch: free_after_dlclose OK. target_addr %" PRIxPTR, sw->target_addr);
      sh_switch_destroy(sw, false);
    }
  }
  pthread_mutex_unlock(&sh_switches_lock);

  sh_island_cleanup_after_dlclose((uintptr_t)info->dlpi_addr);
}
