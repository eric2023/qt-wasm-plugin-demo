#!/usr/bin/env node
'use strict';

/**
 * wasm_dtk_host.js — 长驻 Node.js 子进程，作为 WASM 进程插件运行时。
 *
 * 加载用 Emscripten 编译的 C++ WASM 插件。
 * WASM 插件声明的 DTK 宿主导入函数（extern "C" 声明，emcc 编译为 import）
 * 由本进程桥接到 C++ 父进程（DTK 宿主）。
 *
 * 架构：主线程 + Worker 线程
 *   主线程：异步处理 stdin/stdout JSON 行协议（与 C++ 父进程通信）
 *   Worker 线程：同步执行 WASM 导出函数
 *
 * 同步桥接：
 *   WASM 导入函数在 Worker 中执行，需要 C++ 宿主的返回值。
 *   Worker 通过 SharedArrayBuffer + Atomics.wait 阻塞等待主线程，
 *   主线程轮询 SharedArrayBuffer 检测 import 请求，转发给 C++，
 *   收到 import_result 后写入 SharedArrayBuffer 并 Atomics.notify 唤醒 Worker。
 *
 * 崩溃处理：
 *   WASM 崩溃（如 unreachable 指令）会导致 Worker 线程抛出异常。
 *   Worker 的 message handler 捕获异常并通过 'error' 消息通知主线程，
 *   主线程转发给 C++ 宿主。C++ 宿主可重新加载插件。
 */

const fs = require('fs');
const path = require('path');
const { Worker, isMainThread, workerData, parentPort } = require('worker_threads');

// SharedArrayBuffer 布局 — 扩展为 131108 字节以支持大文件内容传输
const SAB_SIZE = 131108;
const TAG_OFFSET = 0;          // int32: 0=idle, 1=import request
const FUNCLEN_OFFSET = 4;
const FUNC_OFFSET = 8;         // max 32 bytes
const ARG1_OFFSET = 40;
const ARG2_OFFSET = 44;
const ARG3_OFFSET = 48;        // int32: 3rd arg (for 3-arg imports)
const STRLEN_OFFSET = 52;
const STR_OFFSET = 56;         // max 65536 bytes (64KB)
const STR_MAX = 65536;
const RESULT_OFFSET = 65592;   // int32: return value
const RESULT_STRLEN_OFFSET = 65596; // int32: result string length
const RESULT_STR_OFFSET = 65600;    // max 65536 bytes (64KB)
const RESULT_STR_MAX = 65536;

// 带字符串参数的导入函数集合（字符串在 a1/a2 或 a2/a3）
const STR_FUNCS = new Set([
    'dtk_create_label', 'dtk_create_pushbutton',
    'dtk_set_window_title',
    'dtk_set_label_text', 'dtk_set_editor_text',
    'dtk_read_file', 'dtk_write_file',
    'dtk_add_context_menu_item'
]);

// 带字符串参数且字符串在 a2/a3（而非 a1/a2）的函数
const STR_AT_A2_FUNCS = new Set([
    'dtk_set_label_text'
]);

// dtk_read_file 有两个字符串参数: path(a1/a2) 和 data(a3/不预读)
// dtk_write_file 有两个字符串参数: path(a1/a2) 和 data(a3/result_str)

// ═══════════════════════════════════════════════════════════════
// Worker 线程：加载并同步执行 WASM
// ═══════════════════════════════════════════════════════════════
if (!isMainThread) {
    const sab = workerData.sab;
    const wasmDir = workerData.wasmDir;
    const wasmName = workerData.wasmName;
    const i32 = new Int32Array(sab);
    const u8 = new Uint8Array(sab);

    async function main() {
        const wasmPath = path.join(wasmDir, wasmName + '.wasm');

        function readMemStr(ptr, len, memory) {
            if (!memory) return '';
            const bytes = new Uint8Array(memory.buffer, ptr, len);
            return Buffer.from(bytes).toString('utf8');
        }

        function makeDtkImport(funcName, memoryGetter) {
            return (...args) => {
                const memory = memoryGetter();
                const funcBytes = Buffer.from(funcName, 'utf8');
                i32[FUNCLEN_OFFSET / 4] = funcBytes.length;
                u8.set(funcBytes, FUNC_OFFSET);

                const a1 = args.length > 0 ? args[0] : 0;
                const a2 = args.length > 1 ? args[1] : 0;
                const a3 = args.length > 2 ? args[2] : 0;
                i32[ARG1_OFFSET / 4] = a1;
                i32[ARG2_OFFSET / 4] = a2;
                i32[ARG3_OFFSET / 4] = a3;

                // 预读字符串参数
                if (STR_FUNCS.has(funcName) && memory) {
                    let strPtr, strLen;
                    if (STR_AT_A2_FUNCS.has(funcName)) {
                        // dtk_set_label_text(labelId, ptr, len): 字符串在 a2/a3
                        strPtr = a2; strLen = a3;
                    } else {
                        // 其他 STR_FUNCS: 字符串在 a1/a2
                        strPtr = a1; strLen = a2;
                    }
                    if (strLen > 0) {
                        const str = readMemStr(strPtr, strLen, memory);
                        const strBytes = Buffer.from(str, 'utf8');
                        const slen = Math.min(strBytes.length, STR_MAX);
                        i32[STRLEN_OFFSET / 4] = slen;
                        u8.set(strBytes.subarray(0, slen), STR_OFFSET);
                    } else {
                        i32[STRLEN_OFFSET / 4] = 0;
                    }
                } else {
                    i32[STRLEN_OFFSET / 4] = 0;
                }

                Atomics.store(i32, TAG_OFFSET / 4, 1);
                Atomics.notify(i32, TAG_OFFSET / 4, 1);

                Atomics.wait(i32, TAG_OFFSET / 4, 1);
                const result = Atomics.load(i32, RESULT_OFFSET / 4);

                // 结果字符串返回：宿主将字符串写入 SAB result-string 区域，
                // Worker 将其复制到 WASM 内存目标位置
                // dtk_get_editor_text(ptr, maxlen): a1=dest ptr
                // dtk_open_file_dialog(ptr, maxlen): a1=dest ptr
                // dtk_save_file_dialog(ptr, maxlen): a1=dest ptr
                // dtk_read_file(path, pathLen, destPtr, maxlen): a3=dest ptr
                if (memory && result > 0) {
                    const rsLen = Atomics.load(i32, RESULT_STRLEN_OFFSET / 4);
                    if (rsLen > 0 && rsLen <= RESULT_STR_MAX) {
                        const rsBytes = u8.subarray(RESULT_STR_OFFSET, RESULT_STR_OFFSET + rsLen);
                        const destPtr = (funcName === 'dtk_read_file') ? a3 : a1;
                        const wasmMem = new Uint8Array(memory.buffer, destPtr, rsLen);
                        wasmMem.set(rsBytes);
                    }
                }

                return result;
            };
        }

        // 读取 WASM 二进制
        const wasmBinary = new Uint8Array(fs.readFileSync(wasmPath));

        // 可变内存引用
        let wasmMemory = null;
        const memoryGetter = () => wasmMemory;

        // 构建导入对象
        const dtkFuncs = {};
        const dtkImportNames = [
            'dtk_create_label', 'dtk_create_textedit', 'dtk_create_pushbutton',
            'dtk_create_hbox', 'dtk_add_to_layout',
            'dtk_set_window_title', 'dtk_set_window_size',
            'dtk_get_theme', 'dtk_set_theme',
            'dtk_set_label_text', 'dtk_set_widget_enabled',
            'dtk_get_editor_text', 'dtk_set_editor_text',
            'dtk_get_editor_text_length', 'dtk_clear_editor',
            'dtk_open_file_dialog', 'dtk_save_file_dialog',
            'dtk_read_file', 'dtk_write_file',
            'dtk_subscribe_event', 'dtk_add_context_menu_item'
        ];
        for (const name of dtkImportNames) {
            dtkFuncs[name] = makeDtkImport(name, memoryGetter);
        }

        const importObject = { env: dtkFuncs };
        const importObjectBoth = { env: dtkFuncs, dtk: dtkFuncs };

        let instance;
        try {
            const result = await WebAssembly.instantiate(wasmBinary, importObject);
            instance = result.instance || result;
        } catch (e) {
            try {
                const result = await WebAssembly.instantiate(wasmBinary, importObjectBoth);
                instance = result.instance || result;
            } catch (e2) {
                const module = await WebAssembly.compile(wasmBinary);
                const imports = WebAssembly.Module.imports(module);
                const importMap = {};
                for (const imp of imports) {
                    if (!importMap[imp.module]) importMap[imp.module] = {};
                    importMap[imp.module][imp.name] = dtkFuncs[imp.name] || makeDtkImport(imp.name, memoryGetter);
                }
                const result = await WebAssembly.instantiate(module, importMap);
                instance = result.instance || result;
            }
        }

        const exports = instance.exports;
        wasmMemory = exports.memory;

        parentPort.postMessage({ type: 'loaded' });

        parentPort.on('message', (msg) => {
            try {
                if (msg.type === 'call') {
                    const fn = exports[msg.func];
                    if (!fn) {
                        parentPort.postMessage({ type: 'call_result', error: `Unknown export: ${msg.func}` });
                        return;
                    }
                    const result = fn(...(msg.args || []));
                    parentPort.postMessage({ type: 'call_result', value: result });
                } else if (msg.type === 'read_memory') {
                    const mem = new Uint8Array(wasmMemory.buffer, msg.offset, msg.len);
                    parentPort.postMessage({ type: 'call_result', hex: Buffer.from(mem).toString('hex') });
                } else if (msg.type === 'write_memory') {
                    const data = Buffer.from(msg.hex, 'hex');
                    const mem = new Uint8Array(wasmMemory.buffer, msg.offset, data.length);
                    mem.set(data);
                    parentPort.postMessage({ type: 'call_result', value: data.length });
                } else if (msg.type === 'quit') {
                    process.exit(0);
                }
            } catch (e) {
                // WASM 崩溃（如 unreachable）会抛出 RuntimeError
                parentPort.postMessage({ type: 'error', error: 'WASM crash: ' + e.message, crash: true });
            }
        });
    }

    main().catch(err => {
        process.stderr.write(`[wasm_dtk_host worker] Fatal: ${err.message}\n`);
        process.exit(1);
    });
} else {
// ═══════════════════════════════════════════════════════════════
// 主线程：处理 stdin/stdout IPC，转发 import 请求
// ═══════════════════════════════════════════════════════════════

    const wasmDir = process.argv[2] || 'plugins';
    const wasmName = process.argv[3] || 'text_editor_plugin';

    const sab = new SharedArrayBuffer(SAB_SIZE);
    const i32 = new Int32Array(sab);
    const u8 = new Uint8Array(sab);

    let workerReady = false;
    let waitingImportResult = false;
    let pollActive = false;  // 防止 pollImport 重复调度

    function sendToParent(obj) {
        process.stdout.write(JSON.stringify(obj) + '\n');
    }

    const worker = new Worker(__filename, { workerData: { sab: sab, wasmDir: wasmDir, wasmName: wasmName } });

    worker.on('message', (msg) => {
        if (msg.type === 'loaded') {
            workerReady = true;
            process.stderr.write('[wasm_dtk_host] WASM module loaded, waiting for commands...\n');
        } else if (msg.type === 'call_result' || msg.type === 'error') {
            sendToParent(msg);
        }
    });

    worker.on('error', (err) => {
        process.stderr.write(`[wasm_dtk_host] Worker error: ${err.message}\n`);
        sendToParent({ type: 'error', error: 'Worker error: ' + err.message, crash: true });
    });

    worker.on('exit', (code) => {
        process.stderr.write(`[wasm_dtk_host] Worker exited with code ${code}\n`);
        if (code !== 0) {
            sendToParent({ type: 'error', error: 'Worker exited unexpectedly (code ' + code + ')', crash: true });
        }
    });

    function pollImport() {
        // 用 Atomics.wait 短超时等待 Worker 的 import 请求，避免 setImmediate 忙轮询导致 CPU 100%。
        // 超时 10ms 后让出事件循环，处理 Worker 的 call_result 消息和 stdin 消息。
        // Worker 调用 import 时设 TAG=1 + Atomics.notify → 本 wait 立即返回。
        // Worker 完成 WASM 调用时发 postMessage(call_result) → 需要让出事件循环处理。
        if (Atomics.load(i32, TAG_OFFSET / 4) === 1) {
            const funcLen = i32[FUNCLEN_OFFSET / 4];
            const funcName = Buffer.from(u8.subarray(FUNC_OFFSET, FUNC_OFFSET + funcLen)).toString('utf8');
            const a1 = i32[ARG1_OFFSET / 4];
            const a2 = i32[ARG2_OFFSET / 4];
            const a3 = i32[ARG3_OFFSET / 4];
            const strLen = i32[STRLEN_OFFSET / 4];
            const str = strLen > 0 ? Buffer.from(u8.subarray(STR_OFFSET, STR_OFFSET + strLen)).toString('utf8') : '';

            sendToParent({ type: 'import', func: funcName, args: [a1, a2, a3], str: str });
            waitingImportResult = true;
            pollActive = false;
            return;
        }
        Atomics.wait(i32, TAG_OFFSET / 4, 0, 10);
        setImmediate(pollImport);
    }

    const readline = require('readline');
    const rl = readline.createInterface({ input: process.stdin, output: null, terminal: false });

    rl.on('line', (line) => {
        if (!line.trim()) return;

        let req;
        try {
            req = JSON.parse(line);
        } catch (e) {
            sendToParent({ type: 'error', error: 'Invalid JSON: ' + line });
            return;
        }

        if (req.type === 'import_result') {
            if (waitingImportResult) {
                const value = req.value || 0;
                Atomics.store(i32, RESULT_OFFSET / 4, value);
                if (req.str) {
                    const strBytes = Buffer.from(req.str, 'utf8');
                    const strLen = Math.min(strBytes.length, RESULT_STR_MAX);
                    Atomics.store(i32, RESULT_STRLEN_OFFSET / 4, strLen);
                    u8.set(strBytes.subarray(0, strLen), RESULT_STR_OFFSET);
                } else {
                    Atomics.store(i32, RESULT_STRLEN_OFFSET / 4, 0);
                }
                Atomics.store(i32, TAG_OFFSET / 4, 0);
                Atomics.notify(i32, TAG_OFFSET / 4, 1);
                waitingImportResult = false;
                if (!pollActive) { pollActive = true; setImmediate(pollImport); }
            }
            return;
        }

        if (!workerReady) {
            sendToParent({ type: 'error', error: 'Worker not ready' });
            return;
        }

        if (req.type === 'call' || req.type === 'read_memory' || req.type === 'write_memory') {
            worker.postMessage(req);
            if (!pollActive) { pollActive = true; pollImport(); }
        } else if (req.type === 'quit') {
            worker.postMessage({ type: 'quit' });
            process.exit(0);
        }
    });

    rl.on('close', () => {
        process.stderr.write('[wasm_dtk_host] stdin EOF, exiting.\n');
        process.exit(0);
    });
}
