#!/usr/bin/env node
'use strict';

/**
 * build_plugins.js — Compiles all .wat plugin source files to .wasm binaries
 * using the wabt npm package (JavaScript build of WebAssembly Binary Toolkit).
 *
 * Usage:  node build_plugins.js
 *
 * Reads:   plugins/*.wat
 * Writes:  plugins/*.wasm
 */

const fs = require('fs');
const path = require('path');
const wabtFactory = require('wabt');

const pluginsDir = path.join(__dirname, '..', 'plugins');

async function main() {
    const wabt = await wabtFactory();

    const watFiles = fs.readdirSync(pluginsDir)
        .filter(f => f.endsWith('.wat'))
        .map(f => path.join(pluginsDir, f));

    if (watFiles.length === 0) {
        console.error('No .wat files found in', pluginsDir);
        process.exit(1);
    }

    for (const watPath of watFiles) {
        const watSource = fs.readFileSync(watPath, 'utf8');
        const basename = path.basename(watPath);
        const wasmPath = watPath.replace(/\.wat$/, '.wasm');

        try {
            const module = wabt.parseWat(basename, watSource, {
                mutable_globals: true,
                multi_value: true,
            });
            module.validate();
            const { buffer } = module.toBinary({ log: false });
            fs.writeFileSync(wasmPath, buffer);
            console.log(`  ✓ ${basename} → ${path.basename(wasmPath)} (${buffer.length} bytes)`);
            module.destroy();
        } catch (err) {
            console.error(`  ✗ ${basename}: ${err.message}`);
            process.exit(1);
        }
    }

    console.log('All plugins compiled successfully.');
}

main().catch(err => {
    console.error('Build failed:', err);
    process.exit(1);
});
