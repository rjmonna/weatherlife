"""Trace the original Weather-Life HTTP-to-USB path with Frida."""

import argparse
import frida
import sys


AGENT = r"""
'use strict';

function readBytes(pointer, length) {
    if (pointer.isNull() || length <= 0 || length > 4096) return '';
    try {
        return Array.from(new Uint8Array(pointer.readByteArray(length)))
            .map(function (value) { return ('0' + value.toString(16)).slice(-2); })
            .join(' ');
    } catch (_) {
        return '<unreadable>';
    }
}

function hookExport(moduleName, symbolName, callbacks) {
    var address = Module.findExportByName(moduleName, symbolName);
    if (address) Interceptor.attach(address, callbacks);
}

hookExport('wininet.dll', 'InternetReadFile', {
    onEnter: function (args) {
        this.buffer = args[1];
        this.length = args[2].toInt32();
        this.received = args[3];
        send({api: 'InternetReadFile', requested: this.length});
    },
    onLeave: function (retval) {
        var received = this.received.readU32();
        send({api: 'InternetReadFile', ok: !retval.isNull(), bytes: received,
              buffer: readBytes(this.buffer, Math.min(received, 512))});
    }
});

hookExport('kernel32.dll', 'GetProcAddress', {
    onEnter: function (args) {
        this.name = args[1].isNull() ? '' : args[1].readCString();
    },
    onLeave: function (retval) {
        if (this.name && /usbdeviceread|CAL_USB|DeviceIni/i.test(this.name))
            send({api: 'GetProcAddress', name: this.name, address: retval});
    }
});

hookExport('kernel32.dll', 'WriteFile', {
    onEnter: function (args) {
        var length = args[2].toInt32();
        send({api: 'WriteFile', length: length,
              buffer: readBytes(args[1], Math.min(length, 512))});
    }
});

var usbRead = Module.findExportByName('usbwr.dll', 'usbdeviceread');
if (usbRead) {
    Interceptor.attach(usbRead, {
        onEnter: function (args) {
            send({api: 'usbdeviceread', args: [args[0], args[1], args[2], args[3]]});
        },
        onLeave: function (retval) {
            send({api: 'usbdeviceread-return', result: retval});
        }
    });
}

hookExport('hid.dll', 'HidD_GetFeature', {
    onEnter: function (args) {
        this.report = args[1];
        this.length = args[2].toInt32();
        send({api: 'HidD_GetFeature', length: this.length,
              before: readBytes(this.report, Math.min(this.length, 256))});
    },
    onLeave: function (retval) {
        send({api: 'HidD_GetFeature-return', ok: !retval.isNull(),
              report: readBytes(this.report, Math.min(this.length, 256))});
    }
});
"""


def on_message(message, _data):
    if message["type"] == "send":
        print(message["payload"], flush=True)
    else:
        print(message, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("program", help="Path to weather.exe")
    args = parser.parse_args()
    pid = frida.spawn([args.program])
    session = frida.attach(pid)
    script = session.create_script(AGENT)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)
    print("Tracing. Press Ctrl+C to stop.", flush=True)
    try:
        sys.stdin.read()
    except KeyboardInterrupt:
        pass
    finally:
        session.detach()


if __name__ == "__main__":
    main()
