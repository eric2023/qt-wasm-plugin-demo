#!/usr/bin/env node
'use strict';

/**
 * wasm_runner.js — 服务器端 WASM 插件桥接脚本。
 *
 * 由 ChatServer 通过 QProcess 调用。从 stdin 读取 JSON 请求，
 * 加载 Emscripten 编译的 WASM 插件，执行请求的函数，返回 JSON。
 */

const fs = require('fs');

let inputBuffer = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => { inputBuffer += chunk; });
process.stdin.on('end', async () => {
    try {
        const req = JSON.parse(inputBuffer);
        const result = await handleRequest(req);
        process.stdout.write(JSON.stringify(result));
    } catch (err) {
        process.stdout.write(JSON.stringify({ success: false, error: err.message }));
    }
});

async function handleRequest(req) {
    const wasmPath = req.wasm_path;
    if (!wasmPath || !fs.existsSync(wasmPath)) {
        return { success: false, error: `WASM file not found: ${wasmPath}` };
    }

    const wasmBinary = new Uint8Array(fs.readFileSync(wasmPath));

    // 后端插件自包含（无 import）
    const result = await WebAssembly.instantiate(wasmBinary, {});
    const instance = result.instance || result;
    const exports = instance.exports;

    switch (req.action) {
        case 'generate_reply': {
            const seed = req.seed | 0;
            const resultPtr = exports.generate_reply(seed);
            const replyLen = exports.get_reply_len(seed);
            const resultBytes = new Uint8Array(exports.memory.buffer, resultPtr, replyLen);
            const replyStr = Buffer.from(resultBytes).toString('utf8');
            return { success: true, result: replyStr };
        }
        default:
            return { success: false, error: `Unknown action: ${req.action}` };
    }
}
