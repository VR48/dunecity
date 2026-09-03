'use strict';

const canvas = document.getElementById('canvas');
const loading = document.getElementById('loading');
const statusNode = document.getElementById('status');
const progressNode = document.getElementById('progress');
let lastDependencyCount = 0;
let syncPending = false;
let gameReady = false;

var Module = {
    canvas,
    preRun: [function() {
        FS.mkdirTree('/home/web_user');
        FS.mount(IDBFS, {}, '/home/web_user');
        addRunDependency('dunecity-idbfs');
        FS.syncfs(true, function(error) {
            if (error) console.error('Could not restore browser saves:', error);
            removeRunDependency('dunecity-idbfs');
        });
    }],
    requestPersistentSync: function() {
        if (syncPending || typeof FS === 'undefined') return;
        syncPending = true;
        FS.syncfs(false, function(error) {
            syncPending = false;
            if (error) console.error('Could not save browser data:', error);
        });
    },
    markGameReady: function() {
        gameReady = true;
        loading.hidden = true;
        canvas.focus();
    },
    printErr: function(text) {
        if (String(text).includes('emscripten_set_main_loop_timing: Cannot set timing mode')) return;
        console.error(text);
    },
    setStatus: function(text) {
        if (!text) {
            if (!gameReady) statusNode.textContent = 'Loading graphics and sounds into browser memory.';
            return;
        }
        const match = text.match(/\((\d+(?:\.\d+)?)\/(\d+)\)/);
        if (match) {
            progressNode.max = Number(match[2]);
            progressNode.value = Number(match[1]);
        }
        statusNode.textContent = text.replace(/\s*\(\d+(?:\.\d+)?\/\d+\)\s*/, '');
    },
    monitorRunDependencies: function(count) {
        if (count > lastDependencyCount) lastDependencyCount = count;
        progressNode.max = Math.max(1, lastDependencyCount);
        progressNode.value = lastDependencyCount - count;
    },
    onAbort: function(reason) {
        loading.hidden = false;
        statusNode.classList.add('error');
        statusNode.textContent = 'The browser build stopped: ' + reason;
    }
};

window.addEventListener('error', function(event) {
    loading.hidden = false;
    statusNode.classList.add('error');
    statusNode.textContent = event.message || 'The browser build could not start.';
});
document.addEventListener('visibilitychange', function() {
    if (document.hidden) Module.requestPersistentSync();
});
window.addEventListener('pagehide', function() { Module.requestPersistentSync(); });
canvas.addEventListener('contextmenu', function(event) { event.preventDefault(); });
canvas.addEventListener('webglcontextlost', function(event) {
    event.preventDefault();
    loading.hidden = false;
    statusNode.classList.add('error');
    statusNode.textContent = 'Graphics context lost. Reload the game to continue.';
});
document.getElementById('reload').addEventListener('click', function() {
    Module.requestPersistentSync();
    setTimeout(function() { location.reload(); }, 120);
});
document.getElementById('fullscreen').addEventListener('click', function() {
    const stage = document.getElementById('stage');
    if (document.fullscreenElement) {
        document.exitFullscreen();
        return;
    }
    stage.requestFullscreen().then(function() {
        canvas.focus();
        if (screen.orientation && screen.orientation.lock) {
            screen.orientation.lock('landscape').catch(function() {});
        }
    });
});
setInterval(function() { Module.requestPersistentSync(); }, 30000);
