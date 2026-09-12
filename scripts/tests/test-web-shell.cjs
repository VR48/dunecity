// Run with node --test scripts/tests/test-web-shell.cjs.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

function shell(width, height, coarse = false) {
    const elements = {};
    for (const id of ['canvas', 'stage', 'loading', 'status', 'progress', 'reload', 'fullscreen']) {
        elements[id] = { width: 640, height: 480, clientWidth: width, clientHeight: height,
            style: {}, addEventListener() {}, focus() {}, classList: { add() {} } };
    }
    const context = vm.createContext({ URL, console, setInterval() {}, setTimeout() {},
        ResizeObserver: class { constructor(callback) { this.callback = callback; } observe() {} },
        MutationObserver: class { constructor(callback) { this.callback = callback; } observe() {} },
        document: { currentScript: { src: 'https://example.com/play/shell.js?v=654-hash' },
            getElementById: id => elements[id], addEventListener() {} },
        window: { innerWidth: width, matchMedia: () => ({ matches: coarse }), addEventListener() {} }
    });
    vm.runInContext(fs.readFileSync(path.join(__dirname, '../../web/shell.js'), 'utf8'), context);
    return { context, elements };
}

test('desktop default exceeds VGA; mobile keeps a readable smaller surface', () => {
    for (const [w, h, coarse, expected] of [[1366, 720, false, [1280,720]], [1920,1080,false,[1920,1080]], [800,600,false,[1280,720]], [844,348,true,[854,480]]]) {
        const { context } = shell(w, h, coarse);
        const size = context.Module.defaultVideoSize();
        assert.deepEqual([size.width, size.height], expected);
    }
});

test('canvas fits standard, widescreen and fullscreen stages without distortion', () => {
    const { context, elements } = shell(1600, 900);
    for (const [w,h,sw,sh] of [[640,480,1600,900], [1920,1080,1600,900], [3840,2160,1200,800], [1280,720,1920,1080]]) {
        Object.assign(elements.canvas, {width:w,height:h});
        Object.assign(elements.stage, {clientWidth:sw,clientHeight:sh});
        context.fitCanvas();
        const cw = parseFloat(elements.canvas.style.width), ch = parseFloat(elements.canvas.style.height);
        assert.ok(cw <= sw && ch <= sh);
        assert.ok(Math.abs(cw/ch-w/h) < .005);
    }
});

test('wasm and data use the same build token as the shell', () => {
    const { context } = shell(1280,720);
    assert.equal(context.Module.locateFile('dunecity.wasm','/play/'), '/play/dunecity.wasm?v=654-hash');
    assert.equal(context.Module.locateFile('dunecity.data',''), 'dunecity.data?v=654-hash');
});
