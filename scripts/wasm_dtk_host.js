#!/usr/bin/env node
'use strict';

/**
 * wasm_dtk_host.js — 长驻 Node.js 子进程，作为 WASM 进程插件运行时。
 *
 * 加载用 Emscripten 编译的 C++/Qt WASM 插件。
 * WASM 插件声明的 DTK 宿主导入函数（extern "C" 声明，emcc 编译为 import）
 * 由本进程桥接到 C++ 父进程（DTK 客户端）。
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
 */

const fs = require('fs');
const path = require('path');
const { Worker, isMainThread, workerData, parentPort } = require('worker_threads');

// SharedArrayBuffer 布局
const SAB_SIZE = 4120;
const TAG_OFFSET = 0;          // int32: 0=idle, 1=import request
const FUNCLEN_OFFSET = 4;
const FUNC_OFFSET = 8;         // max 32 bytes
const ARG1_OFFSET = 40;
const ARG2_OFFSET = 44;
const STRLEN_OFFSET = 48;
const STR_OFFSET = 52;         // max 4096 bytes
const STR_MAX = 4096;
const RESULT_OFFSET = 4116;

// 带字符串参数的导入函数集合
const STR_FUNCS = new Set([
    'dtk_create_label', 'dtk_create_lineedit', 'dtk_create_pushbutton',
    'dtk_set_window_title', 'dtk_append_message'
]);

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
                i32[ARG1_OFFSET / 4] = a1;
                i32[ARG2_OFFSET / 4] = a2;

                // 预读字符串参数
                if (STR_FUNCS.has(funcName) && args.length >= 2 && memory) {
                    const str = readMemStr(a1, a2, memory);
                    const strBytes = Buffer.from(str, 'utf8');
                    const strLen = Math.min(strBytes.length, STR_MAX);
                    i32[STRLEN_OFFSET / 4] = strLen;
                    u8.set(strBytes.subarray(0, strLen), STR_OFFSET);
                } else {
                    i32[STRLEN_OFFSET / 4] = 0;
                }

                Atomics.store(i32, TAG_OFFSET / 4, 1);
                Atomics.notify(i32, TAG_OFFSET / 4, 1);

                Atomics.wait(i32, TAG_OFFSET / 4, 1);
                return Atomics.load(i32, RESULT_OFFSET / 4);
            };
        }

        // 先读取 WASM 二进制以获取 import 列表
        const wasmBinary = new Uint8Array(fs.readFileSync(wasmPath));

        // 创建一个可变内存引用，供 import 函数读取 WASM 内存
        let wasmMemory = null;
        const memoryGetter = () => wasmMemory;

        // 构建导入对象
        const dtkFuncs = {};
        const dtkImportNames = [
            'dtk_create_label', 'dtk_create_textedit', 'dtk_create_lineedit',
            'dtk_create_pushbutton', 'dtk_create_hbox', 'dtk_add_to_layout',
            'dtk_set_window_title', 'dtk_set_window_size',
            'dtk_get_theme', 'dtk_set_theme', 'dtk_append_message'
        ];
        for (const name of dtkImportNames) {
            dtkFuncs[name] = makeDtkImport(name, memoryGetter);
        }

        // 使用 WebAssembly 直接实例化（不依赖 Emscripten JS 胶水）
        // 因为我们用 -nostdlib -noentry，WASM 是纯模块，无 JS 运行时依赖
        const importObject = { env: dtkFuncs };

        // 也尝试 'dtk' 模块名（取决于 emcc 的 import 路径）
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
                // 自动检测 import 模块名
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
                parentPort.postMessage({ type: 'error', error: e.message });
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

    // 参数: wasm_dtk_host.js <wasm_dir> <wasm_name>
    // wasm_name 是不含扩展名的基础名（如 "chat_ui_plugin"）
    const wasmDir = process.argv[2] || 'plugins';
    const wasmName = process.argv[3] || 'chat_ui_plugin';

    const sab = new SharedArrayBuffer(SAB_SIZE);
    const i32 = new Int32Array(sab);
    const u8 = new Uint8Array(sab);

    let workerReady = false;
    let waitingImportResult = false;

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
        sendToParent({ type: 'error', error: 'Worker error: ' + err.message });
    });

    worker.on('exit', (code) => {
        process.stderr.write(`[wasm_dtk_host] Worker exited with code ${code}\n`);
    });

    function pollImport() {
        if (Atomics.load(i32, TAG_OFFSET / 4) === 1) {
            const funcLen = i32[FUNCLEN_OFFSET / 4];
            const funcName = Buffer.from(u8.subarray(FUNC_OFFSET, FUNC_OFFSET + funcLen)).toString('utf8');
            const a1 = i32[ARG1_OFFSET / 4];
            const a2 = i32[ARG2_OFFSET / 4];
            const strLen = i32[STRLEN_OFFSET / 4];
            const str = strLen > 0 ? Buffer.from(u8.subarray(STR_OFFSET, STR_OFFSET + strLen)).toString('utf8') : '';

            sendToParent({ type: 'import', func: funcName, args: [a1, a2], str: str });
            waitingImportResult = true;
            return;
        }
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
                Atomics.store(i32, TAG_OFFSET / 4, 0);
                Atomics.notify(i32, TAG_OFFSET / 4, 1);
                waitingImportResult = false;
                setImmediate(pollImport);
            }
            return;
        }

        if (!workerReady) {
            sendToParent({ type: 'error', error: 'Worker not ready' });
            return;
        }

        if (req.type === 'call' || req.type === 'read_memory' || req.type === 'write_memory') {
            worker.postMessage(req);
            pollImport();
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
