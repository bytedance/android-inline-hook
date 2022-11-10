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

#include <fcntl.h>
#include <jni.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "shadowhook.h"
#include "unittest.h"

#define HACKER_JNI_VERSION    JNI_VERSION_1_6
#define HACKER_JNI_CLASS_NAME "com/bytedance/shadowhook/sample/NativeHandler"

static int unittest_jni_hook_sym_addr(JNIEnv *env, jobject thiz, jint api_level) {
  (void)env;
  (void)thiz;

  return unittest_hook_sym_addr(api_level);
}

static int unittest_jni_hook_sym_name(JNIEnv *env, jobject thiz, jint api_level) {
  (void)env;
  (void)thiz;

  return unittest_hook_sym_name(api_level);
}

static int unittest_jni_unhook(JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;

  return unittest_unhook();
}

static int unittest_jni_run(JNIEnv *env, jobject thiz, jboolean hookee2_loaded) {
  (void)env;
  (void)thiz;

  return unittest_run(hookee2_loaded);
}

static void unittest_jni_dump_records(JNIEnv *env, jobject thiz, jstring pathname) {
  (void)thiz;

  const char *c_pathname = (*env)->GetStringUTFChars(env, pathname, 0);
  if (NULL == c_pathname) return;

  int fd = open(c_pathname, O_CREAT | O_WRONLY | O_CLOEXEC | O_TRUNC | O_APPEND, S_IRUSR | S_IWUSR);
  if (fd >= 0) {
    shadowhook_dump_records(fd, SHADOWHOOK_RECORD_ITEM_ALL);
    //    shadowhook_dump_records(fd, SHADOWHOOK_RECORD_ITEM_CALLER_LIB_NAME | SHADOWHOOK_RECORD_ITEM_OP |
    //                                    SHADOWHOOK_RECORD_ITEM_LIB_NAME | SHADOWHOOK_RECORD_ITEM_SYM_NAME |
    //                                    SHADOWHOOK_RECORD_ITEM_ERRNO | SHADOWHOOK_RECORD_ITEM_STUB);
    close(fd);
  }

  (*env)->ReleaseStringUTFChars(env, pathname, c_pathname);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved;

  if (NULL == vm) return JNI_ERR;

  JNIEnv *env;
  if (JNI_OK != (*vm)->GetEnv(vm, (void **)&env, HACKER_JNI_VERSION)) return JNI_ERR;
  if (NULL == env || NULL == *env) return JNI_ERR;

  jclass cls;
  if (NULL == (cls = (*env)->FindClass(env, HACKER_JNI_CLASS_NAME))) return JNI_ERR;

  JNINativeMethod m[] = {{"nativeHookSymAddr", "(I)I", (void *)unittest_jni_hook_sym_addr},
                         {"nativeHookSymName", "(I)I", (void *)unittest_jni_hook_sym_name},
                         {"nativeUnhook", "()I", (void *)unittest_jni_unhook},
                         {"nativeRun", "(Z)I", (void *)unittest_jni_run},
                         {"nativeDumpRecords", "(Ljava/lang/String;)V", (void *)unittest_jni_dump_records}};
  if (0 != (*env)->RegisterNatives(env, cls, m, sizeof(m) / sizeof(m[0]))) return JNI_ERR;

  return HACKER_JNI_VERSION;
}
