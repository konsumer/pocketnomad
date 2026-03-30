// Mount IDBFS at /sdcard before the module starts so file writes persist
// to IndexedDB across page reloads.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function() {
  FS.mkdir('/sdcard');
  FS.mount(IDBFS, {}, '/sdcard');
});
