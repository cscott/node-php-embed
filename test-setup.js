var SegfaultHandler = require('segfault-handler');
// Listen for SIGSEGV.
SegfaultHandler.registerHandler("segfault.log");
// Listen for SIGABRT, too.
SegfaultHandler.registerHandler(SegfaultHandler.SIGABRT);
