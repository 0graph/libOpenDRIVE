Module.initializeFileBridge = function() {
    const input = document.getElementById('file-input');
    async function open(file) {
        if (!file) return;
        const bytes = new Uint8Array(await file.arrayBuffer());
        const pointer = Module._malloc(bytes.length || 1);
        try {
            HEAPU8.set(bytes, pointer);
            Module.ccall('ReceiveFile', null, ['string', 'number', 'number'], [file.name, pointer, bytes.length]);
        } finally {
            Module._free(pointer);
        }
        Module.canvas.focus();
    }
    input.addEventListener('change', () => { open(input.files[0]); input.value = ''; });
    window.addEventListener('dragover', event => event.preventDefault());
    window.addEventListener('drop', event => { event.preventDefault(); open(event.dataTransfer.files[0]); });
    window.addEventListener('paste', event => {
        if (document.activeElement !== Module.canvas) return;
        event.preventDefault();
        const bytes = new TextEncoder().encode(event.clipboardData.getData('text/plain'));
        const pointer = Module._malloc(bytes.length + 1);
        try {
            HEAPU8.set(bytes, pointer);
            HEAPU8[pointer + bytes.length] = 0;
            Module.ccall('ReceiveText', null, ['number'], [pointer]);
        } finally {
            Module._free(pointer);
        }
    });
    window.addEventListener('beforeunload', event => {
        if (!Module.documentModified) return;
        event.preventDefault();
        event.returnValue = '';
    });
};
Module.copyText = function(text) {
    if (navigator.clipboard) navigator.clipboard.writeText(text).catch(() => {});
};
Module.downloadFile = function(name, pointer, size) {
    // Blob owns a copy; C++ can release the source bytes on return.
    const blob = new Blob([HEAPU8.slice(pointer, pointer + size)], {type: 'application/xml'});
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = UTF8ToString(name);
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
};
