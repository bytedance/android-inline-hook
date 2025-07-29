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

package com.bytedance.shadowhook;

public final class ShadowHook {
    private static final int ERRNO_OK = 0;
    private static final int ERRNO_UNINIT = 2;
    private static final int ERRNO_LOAD_LIBRARY_EXCEPTION = 100;
    private static final int ERRNO_INIT_EXCEPTION = 101;

    private static boolean loaded = false;
    private static boolean inited = false;
    private static int initErrno = ERRNO_UNINIT;
    private static long initCostMs = -1;

    private static final String libName = "shadowhook";

    private static final ILibLoader defaultLibLoader = null;
    private static final int defaultMode = Mode.SHARED.getValue();
    private static final boolean defaultDebuggable = false;
    private static final boolean defaultRecordable = false;
    private static final boolean defaultDisable = false;

    public static String getVersion() {
        return nativeGetVersion();
    }

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

        try {
            if (config.getDisable()) {
                nativeSetDisable(true);
            }
            if (config.getRecordable()) {
                nativeSetRecordable(config.getRecordable());
            }
            initErrno = nativeInit(config.getMode(), config.getDebuggable());
        } catch (Throwable ignored) {
            initErrno = ERRNO_INIT_EXCEPTION;
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
        if (loadLibrary()) {
            int nativeMode = nativeGetMode();
            if (Mode.UNIQUE.getValue() == nativeMode) {
                return Mode.UNIQUE;
            } else if (Mode.MULTI.getValue() == nativeMode) {
                return Mode.MULTI;
            } else {
                return Mode.SHARED;
            }
        }
        return Mode.SHARED;
    }

    public static boolean getDebuggable() {
        if (loadLibrary()) {
            return nativeGetDebuggable();
        }
        return false;
    }

    public static void setDebuggable(boolean debuggable) {
        if (loadLibrary()) {
            nativeSetDebuggable(debuggable);
        }
    }

    public static boolean getRecordable() {
        if (loadLibrary()) {
            return nativeGetRecordable();
        }
        return false;
    }

    public static void setRecordable(boolean recordable) {
        if (loadLibrary()) {
            nativeSetRecordable(recordable);
        }
    }

    public static boolean getDisable() {
        if (loadLibrary()) {
            return nativeGetDisable();
        }
        return false;
    }

    public static void setDisable(boolean disable) {
        if (loadLibrary()) {
            nativeSetDisable(disable);
        }
    }

    public static String toErrmsg(int errno) {
        if (errno == ERRNO_OK) {
            return "OK";
        } else if (errno == ERRNO_UNINIT) {
            return "Not initialized";
        } else if (errno == ERRNO_LOAD_LIBRARY_EXCEPTION) {
            return "Load libshadowhook.so failed";
        } else if (errno == ERRNO_INIT_EXCEPTION) {
            return "Init exception";
        } else {
            if (loadLibrary()) {
                return nativeToErrmsg(errno);
            }
            return "Unknown";
        }
    }

    public static String getRecords(RecordItem... recordItems) {
        if (loadLibrary()) {
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
                    case FLAGS:
                        itemFlags |= recordItemFlags;
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
        if (loadLibrary()) {
            return nativeGetArch();
        }
        return "unknown";
    }

    private static synchronized boolean loadLibrary(Config config) {
        if (loaded) {
            return true;
        }

        try {
            if (config == null || config.getLibLoader() == null) {
                System.loadLibrary(libName);
            } else {
                config.getLibLoader().loadLibrary(libName);
            }
            loaded = true;
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

    private static boolean loadLibrary() {
        return loadLibrary(null);
    }

    private static native String nativeGetVersion();

    private static native int nativeInit(int mode, boolean debuggable);

    private static native int nativeGetInitErrno();

    private static native int nativeGetMode();

    private static native boolean nativeGetDebuggable();

    private static native void nativeSetDebuggable(boolean debuggable);

    private static native boolean nativeGetRecordable();

    private static native void nativeSetRecordable(boolean recordable);

    private static native boolean nativeGetDisable();

    private static native void nativeSetDisable(boolean disable);

    private static native String nativeToErrmsg(int errno);

    private static native String nativeGetRecords(int itemFlags);

    private static native String nativeGetArch();

    private static final int recordItemAll = 0b11111111111;
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
    private static final int recordItemFlags = 1 << 10;

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
        STUB,
        FLAGS
    }

    public interface ILibLoader {
        void loadLibrary(String libName);
    }

    public static class Config {
        private ILibLoader libLoader;
        private int mode;
        private boolean debuggable;
        private boolean recordable;
        private boolean disable;

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

        public void setDisable(boolean disable) {
            this.disable = disable;
        }
        public boolean getDisable() {
            return this.disable;
        }
    }

    public static class ConfigBuilder {

        private ILibLoader libLoader = defaultLibLoader;
        private int mode = defaultMode;
        private boolean debuggable = defaultDebuggable;
        private boolean recordable = defaultRecordable;
        private boolean disable = defaultDisable;

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

        public ConfigBuilder setDisable(boolean disable) {
            this.disable = disable;
            return this;
        }

        public Config build() {
            Config config = new Config();
            config.setLibLoader(libLoader);
            config.setMode(mode);
            config.setDebuggable(debuggable);
            config.setRecordable(recordable);
            config.setDisable(disable);
            return config;
        }
    }

    public enum Mode {
        SHARED(0), UNIQUE(1), MULTI(2);

        private final int value;
        Mode(int value) {
            this.value = value;
        }

        int getValue() {
            return value;
        }
    }
}
