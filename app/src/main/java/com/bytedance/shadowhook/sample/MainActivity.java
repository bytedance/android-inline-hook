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

package com.bytedance.shadowhook.sample;

import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;

import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;

import com.bytedance.shadowhook.ShadowHook;
import com.bytedance.shadowhook.systest.SysTest;

import java.io.BufferedReader;
import java.io.FileReader;

public class MainActivity extends AppCompatActivity {

    private static final String tag = "shadowhook_tag";
    private boolean hookee2Loaded = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);

        findViewById(R.id.unitTestHookSymAddr).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                NativeHandler.nativeHookSymAddr(Build.VERSION.SDK_INT);
            }
        });

        findViewById(R.id.unitTestHookSymName).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                NativeHandler.nativeHookSymName(Build.VERSION.SDK_INT);
            }
        });

        findViewById(R.id.unitTestUnhook).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                NativeHandler.nativeUnhook();
            }
        });

        findViewById(R.id.unitTestLoad).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                if(!hookee2Loaded) {
                    hookee2Loaded = true;
                    System.loadLibrary("hookee2");
                }
            }
        });

        findViewById(R.id.unitTestRun).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                NativeHandler.nativeRun(hookee2Loaded);
            }
        });

        findViewById(R.id.systemtestTestHook).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                SysTest.hook();
            }
        });

        findViewById(R.id.systemtestTestUnhook).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                SysTest.unhook();
            }
        });

        findViewById(R.id.systemtestTestRun).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                SysTest.run();
            }
        });

        findViewById(R.id.getRecords).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                String records = ShadowHook.getRecords();
//                String records = ShadowHook.getRecords(ShadowHook.RecordItem.CALLER_LIB_NAME, ShadowHook.RecordItem.OP, ShadowHook.RecordItem.LIB_NAME, ShadowHook.RecordItem.SYM_NAME, ShadowHook.RecordItem.ERRNO, ShadowHook.RecordItem.STUB);
                if (records != null) {
                    for (String line : records.split("\n")) {
                        Log.i(tag, line);
                    }
                }
            }
        });

        findViewById(R.id.dumpRecords).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                String pathname = getApplicationContext().getFilesDir() + "/shadowhook_records.txt";
                NativeHandler.nativeDumpRecords(pathname);

                BufferedReader br = null;
                try {
                    br = new BufferedReader(new FileReader(pathname));
                    String line;
                    while ((line = br.readLine()) != null) {
                        Log.i(tag, line);
                    }
                } catch (Throwable ignored) {
                } finally {
                    if (br != null) {
                        try {
                            br.close();
                        } catch (Exception ignored) {
                        }
                    }
                }
            }
        });
    }
}
