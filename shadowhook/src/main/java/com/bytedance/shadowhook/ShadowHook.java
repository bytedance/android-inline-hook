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

package com.bytedance.shadowhook;

public final class ShadowHook {
    private static final int ERRNO_OK = 0;
    private static final int ERRNO_PENDING = 1;
    private static final int ERRNO_UNINIT = 2;
    private static final int ERRNO_LOAD_LIBRARY_EXCEPTION = 100;
    private static final int ERRNO_INIT_EXCEPTION = 101;

    private static boolean inited = false;
    private static int initErrno = ERRNO_UNINIT;
    private static long initCostMs = -1;

    private static final String libName = "shadowhook";

    private static final ILibLoader defaultLibLoader = null;
    private static final int defaultMode = Mode.SHARED.getValue();
    private static final boolean defaultDebuggable = false;
    private static final boolean defaultRecordable = false;

    public static int init() {
        return init(null);
    }

    public static synchronized int init(Config config) {
        if (inited) {
            return initErrno;
        }
        inited = true;

        long start = System.currentTimeMillis();

        if (config == null) {
            //use default parameters
            config = new ConfigBuilder().build();
        }

        // load libshadowhook.so
        if (!loadLibrary(config)) {
            initErrno = ERRNO_LOAD_LIBRARY_EXCEPTION;
            initCostMs = System.currentTimeMillis() - start;
            return initErrno;
        }

        // call native shadowhook_init()
        try {
            initErrno = nativeInit(config.getMode(), config.getDebuggable());
        } catch (Throwable ignored) {
            initErrno = ERRNO_INIT_EXCEPTION;
        }

        // call native shadowhook_set_recordable()
        if (config.getRecordable()) {
            try {
                nativeSetRecordable(config.getRecordable());
            } catch (Throwable ignored) {
                initErrno = ERRNO_INIT_EXCEPTION;
            }
        }

        initCostMs = System.currentTimeMillis() - start;
        return initErrno;
    }

    public static int getInitErrno() {
        return initErrno;
    }

    public static long getInitCostMs() {
        return initCostMs;
    }

    public static Mode getMode() {
        if (isInitedOk()) {
            return Mode.SHARED.getValue() == nativeGetMode() ? Mode.SHARED : Mode.UNIQUE;
        }
        return Mode.SHARED;
    }

    public static boolean getDebuggable() {
        if (isInitedOk()) {
            return nativeGetDebuggable();
        }
        return false;
    }

    public static void setDebuggable(boolean debuggable) {
        if (isInitedOk()) {
            nativeSetDebuggable(debuggable);
        }
    }

    public static boolean getRecordable() {
        if (isInitedOk()) {
            return nativeGetRecordable();
        }
        return false;
    }

    public static void setRecordable(boolean recordable) {
        if (isInitedOk()) {
            nativeSetRecordable(recordable);
        }
    }

    public static String toErrmsg(int errno) {
        if (errno == ERRNO_OK) {
            return "OK";
        } else if (errno == ERRNO_PENDING) {
            return "Pending task";
        } else if (errno == ERRNO_UNINIT) {
            return "Not initialized";
        } else if (errno == ERRNO_LOAD_LIBRARY_EXCEPTION) {
            return "Load libshadowhook.so failed";
        } else if (errno == ERRNO_INIT_EXCEPTION) {
            return "Init exception";
        } else {
            if (isInitedOk()) {
                return nativeToErrmsg(errno);
            }
            return "Unknown";
        }
    }

    public static String getRecords(RecordItem... recordItems) {
        if (isInitedOk()) {
            int itemFlags = 0;
            for (RecordItem recordItem : recordItems) {
                switch (recordItem) {
                    case TIMESTAMP:
                        itemFlags |= recordItemTimestamp;
                        break;
                    case CALLER_LIB_NAME:
                        itemFlags |= recordItemCallerLibName;
                        break;
                    case OP:
                        itemFlags |= recordItemOp;
                        break;
                    case LIB_NAME:
                        itemFlags |= recordItemLibName;
                        break;
                    case SYM_NAME:
                        itemFlags |= recordItemSymName;
                        break;
                    case SYM_ADDR:
                        itemFlags |= recordItemSymAddr;
                        break;
                    case NEW_ADDR:
                        itemFlags |= recordItemNewAddr;
                        break;
                    case BACKUP_LEN:
                        itemFlags |= recordItemBackupLen;
                        break;
                    case ERRNO:
                        itemFlags |= recordItemErrno;
                        break;
                    case STUB:
                        itemFlags |= recordItemStub;
                        break;
                    default:
                        break;
                }
            }

            if (itemFlags == 0) {
                itemFlags = recordItemAll;
            }

            return nativeGetRecords(itemFlags);
        }
        return null;
    }

    public static String getArch() {
        if (isInitedOk()) {
            return nativeGetArch();
        }
        return "unknown";
    }

    private static boolean loadLibrary(Config config) {
        try {
            if (config == null || config.getLibLoader() == null) {
                System.loadLibrary(libName);
            } else {
                config.getLibLoader().loadLibrary(libName);
            }
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

    private static boolean loadLibrary() {
        return loadLibrary(null);
    }

    private static boolean isInitedOk() {
        if (inited) {
            return initErrno == ERRNO_OK;
        }

        if (!loadLibrary()) {
            return false;
        }

        try {
            int errno = nativeGetInitErrno();
            if (errno != ERRNO_UNINIT) {
                initErrno = errno;
                inited = true;
            }
            return errno == ERRNO_OK;
        } catch (Throwable ignored) {
            return false;
        }
    }

    private static native int nativeInit(int mode, boolean debuggable);

    private static native int nativeGetInitErrno();

    private static native int nativeGetMode();

    private static native boolean nativeGetDebuggable();

    private static native void nativeSetDebuggable(boolean debuggable);

    private static native boolean nativeGetRecordable();

    private static native void nativeSetRecordable(boolean recordable);

    private static native String nativeToErrmsg(int errno);

    private static native String nativeGetRecords(int itemFlags);

    private static native String nativeGetArch();

    private static final int recordItemAll = 0b1111111111;
    private static final int recordItemTimestamp = 1;
    private static final int recordItemCallerLibName = 1 << 1;
    private static final int recordItemOp = 1 << 2;
    private static final int recordItemLibName = 1 << 3;
    private static final int recordItemSymName = 1 << 4;
    private static final int recordItemSymAddr = 1 << 5;
    private static final int recordItemNewAddr = 1 << 6;
    private static final int recordItemBackupLen = 1 << 7;
    private static final int recordItemErrno = 1 << 8;
    private static final int recordItemStub = 1 << 9;

    public enum RecordItem {
        TIMESTAMP,
        CALLER_LIB_NAME,
        OP,
        LIB_NAME,
        SYM_NAME,
        SYM_ADDR,
        NEW_ADDR,
        BACKUP_LEN,
        ERRNO,
        STUB
    }

    public interface ILibLoader {
        void loadLibrary(String libName);
    }

    public static class Config {
        private ILibLoader libLoader;
        private int mode;
        private boolean debuggable;
        private boolean recordable;

        public Config() {
        }

        public void setLibLoader(ILibLoader libLoader) {
            this.libLoader = libLoader;
        }

        public ILibLoader getLibLoader() {
            return this.libLoader;
        }

        public void setMode(int mode) {
            this.mode = mode;
        }

        public int getMode() {
            return this.mode;
        }

        public void setDebuggable(boolean debuggable) {
            this.debuggable = debuggable;
        }

        public boolean getDebuggable() {
            return this.debuggable;
        }

        public void setRecordable(boolean recordable) {
            this.recordable = recordable;
        }

        public boolean getRecordable() {
            return this.recordable;
        }
    }

    public static class ConfigBuilder {

        private ILibLoader libLoader = defaultLibLoader;
        private int mode = defaultMode;
        private boolean debuggable = defaultDebuggable;
        private boolean recordable = defaultRecordable;

        public ConfigBuilder() {
        }

        public ConfigBuilder setLibLoader(ILibLoader libLoader) {
            this.libLoader = libLoader;
            return this;
        }

        public ConfigBuilder setMode(Mode mode) {
            this.mode = mode.getValue();
            return this;
        }

        public ConfigBuilder setDebuggable(boolean debuggable) {
            this.debuggable = debuggable;
            return this;
        }

        public ConfigBuilder setRecordable(boolean recordable) {
            this.recordable = recordable;
            return this;
        }

        public Config build() {
            Config config = new Config();
            config.setLibLoader(libLoader);
            config.setMode(mode);
            config.setDebuggable(debuggable);
            config.setRecordable(recordable);
            return config;
        }
    }

    public enum Mode {
        SHARED(0), UNIQUE(1);

        private final int value;
        Mode(int value) {
            this.value = value;
        }

        int getValue() {
            return value;
        }
    }
}
