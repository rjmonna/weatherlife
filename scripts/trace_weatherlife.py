"""Trace the original Weather-Life HTTP-to-USB path with Frida."""

import argparse
import frida
import json
from pathlib import Path
import sys


DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "traces" / "weather-trace.jsonl"


AGENT = r"""
'use strict';

var redirectBase = %REDIRECT_BASE%;
var getLastError = null;
if (Module.findExportByName) {
    var getLastErrorAddress = Module.findExportByName('kernel32.dll', 'GetLastError');
    if (getLastErrorAddress)
        getLastError = new NativeFunction(getLastErrorAddress, 'uint', []);
}

function event(payload) {
    payload.pid = Process.id;
    payload.time = Date.now();
    send(payload);
}

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

function callStack(context) {
    try {
        return Thread.backtrace(context, Backtracer.ACCURATE)
            .slice(0, 8)
            .map(DebugSymbol.fromAddress)
            .map(function (symbol) { return symbol.toString(); });
    } catch (_) {
        return [];
    }
}

function hookExport(moduleName, symbolName, callbacks) {
    var address = null;
    if (Module.findExportByName) {
        address = Module.findExportByName(moduleName, symbolName);
    } else if (Module.findGlobalExportByName) {
        address = Module.findGlobalExportByName(symbolName);
    }
    if (address) Interceptor.attach(address, callbacks);
}

// Maps HANDLE values (as strings) to the path passed to CreateFileA/W, so
// later WriteFile/ReadFile/HidD_* calls can be attributed to a real HID device
// path instead of an opaque handle.
var handlePaths = {};
var hookedResolvedFunctions = {};

function hookCalUsbWrite(address) {
    if (address.isNull() || hookedResolvedFunctions[address.toString()]) return;
    hookedResolvedFunctions[address.toString()] = true;
    Interceptor.attach(address, {
        onEnter: function (args) {
            event({api: 'CAL_USB_WRITE-call',
                  args: [args[0].toString(), args[1].toString(),
                         args[2].toString(), args[3].toString()],
                  arg1: readBytes(args[1], 64),
                  arg2: readBytes(args[2], 64),
                  arg3: readBytes(args[3], 64),
                  stack: callStack(this.context)});
        },
        onLeave: function (retval) {
            event({api: 'CAL_USB_WRITE-return', result: retval.toString()});
        }
    });
}

function hookCreateFile(symbolName, wide) {
    hookExport('kernel32.dll', symbolName, {
        onEnter: function (args) {
            this.path = args[0].isNull() ? '' : (wide ? args[0].readUtf16String() : args[0].readCString());
        },
        onLeave: function (retval) {
            if (!retval.isNull() && retval.toInt32() !== -1) {
                handlePaths[retval.toString()] = this.path;
            }
            event({api: symbolName, path: this.path, handle: retval.toString()});
        }
    });
}

hookCreateFile('CreateFileA', false);
hookCreateFile('CreateFileW', true);

function pathForHandle(handle) {
    return handlePaths[handle.toString()] || '';
}

hookExport('wininet.dll', 'InternetReadFile', {
    onEnter: function (args) {
        this.buffer = args[1];
        this.length = args[2].toInt32();
        this.received = args[3];
          event({api: 'InternetReadFile', requested: this.length,
              stack: callStack(this.context)});
    },
    onLeave: function (retval) {
        var received = this.received.readU32();
          event({api: 'InternetReadFile', ok: !retval.isNull(), bytes: received,
              buffer: readBytes(this.buffer, Math.min(received, 512)),
              stack: callStack(this.context)});
    }
});

function hookHttpSendRequest(symbolName, wide) {
    hookExport('wininet.dll', symbolName, {
        onEnter: function (args) {
            this.headers = args[1].isNull() ? '' : (wide ? args[1].readUtf16String() : args[1].readCString());
            this.optional = args[4].toInt32();
            this.body = args[2];
            event({api: symbolName, headers: this.headers, body: readBytes(this.body, Math.min(this.optional, 4096))});
        },
        onLeave: function (retval) {
            event({api: symbolName + '-return', ok: !retval.isNull()});
        }
    });
}

hookHttpSendRequest('HttpSendRequestA', false);
hookHttpSendRequest('HttpSendRequestW', true);

function hookInternetOpenUrl(symbolName, wide) {
    hookExport('wininet.dll', symbolName, {
        onEnter: function (args) {
            this.url = args[1].isNull() ? '' : (wide ? args[1].readUtf16String() : args[1].readCString());
            if (redirectBase && this.url) {
                var pathStart = this.url.indexOf('/', this.url.indexOf('://') + 3);
                var path = pathStart >= 0 ? this.url.substring(pathStart) : '/';
                var redirected = redirectBase + path;
                args[1] = wide ? Memory.allocUtf16String(redirected) : Memory.allocUtf8String(redirected);
                this.redirectedUrl = redirected;
            }
            event({api: symbolName, url: this.url,
                  redirectedUrl: this.redirectedUrl || '',
                  stack: callStack(this.context)});
        },
        onLeave: function (retval) {
            var ok = !retval.isNull();
            event({api: symbolName + '-return', ok: ok, url: this.url,
                lastError: ok || !getLastError ? 0 : getLastError()});
        }
    });
}

hookInternetOpenUrl('InternetOpenUrlA', false);
hookInternetOpenUrl('InternetOpenUrlW', true);

hookExport('kernel32.dll', 'GetProcAddress', {
    onEnter: function (args) {
        this.name = args[1].isNull() ? '' : args[1].readCString();
    },
    onLeave: function (retval) {
        if (this.name && /usbdeviceread|CAL_USB|DeviceIni/i.test(this.name)) {
            event({api: 'GetProcAddress', name: this.name, address: retval});
            if (!retval.isNull() && /CAL_USB_WRITE/i.test(this.name))
                hookCalUsbWrite(retval);
        }
    }
});

var directCalUsbWrite = null;
if (Module.findExportByName)
    directCalUsbWrite = Module.findExportByName('onlywell.dll', 'CAL_USB_WRITE');
else if (Module.findGlobalExportByName)
    directCalUsbWrite = Module.findGlobalExportByName('CAL_USB_WRITE');
if (directCalUsbWrite) hookCalUsbWrite(directCalUsbWrite);

hookExport('kernel32.dll', 'WriteFile', {
    onEnter: function (args) {
        this.lengthPointer = args[3];
        var length = args[2].toInt32();
          event({api: 'WriteFile', length: length,
              buffer: readBytes(args[1], Math.min(length, 512)),
              path: pathForHandle(args[0]),
              stack: callStack(this.context)});
    },
    onLeave: function (retval) {
        event({api: 'WriteFile-return', ok: !retval.isNull(),
              bytes: this.lengthPointer.isNull() ? 0 : this.lengthPointer.readU32(),
              lastError: retval.isNull() && getLastError ? getLastError() : 0});
    }
});

hookExport('kernel32.dll', 'ReadFile', {
    onEnter: function (args) {
        this.buffer = args[1];
        this.length = args[2].toInt32();
        this.received = args[3];
        this.path = pathForHandle(args[0]);
    },
    onLeave: function (retval) {
        var received = this.received.readU32();
          event({api: 'ReadFile', ok: !retval.isNull(), bytes: received,
              buffer: readBytes(this.buffer, Math.min(received, 512)),
              path: this.path});
    }
});

var usbRead = null;
if (Module.findExportByName) {
    usbRead = Module.findExportByName('usbwr.dll', 'usbdeviceread');
} else if (Module.findGlobalExportByName) {
    usbRead = Module.findGlobalExportByName('usbdeviceread');
}
if (usbRead) {
    Interceptor.attach(usbRead, {
        onEnter: function (args) {
            event({api: 'usbdeviceread', args: [args[0], args[1], args[2], args[3]]});
        },
        onLeave: function (retval) {
            event({api: 'usbdeviceread-return', result: retval});
        }
    });
}

hookExport('hid.dll', 'HidD_GetFeature', {
    onEnter: function (args) {
        this.report = args[1];
        this.length = args[2].toInt32();
        this.path = pathForHandle(args[0]);
          event({api: 'HidD_GetFeature', length: this.length,
              before: readBytes(this.report, Math.min(this.length, 256)),
              path: this.path});
    },
    onLeave: function (retval) {
          event({api: 'HidD_GetFeature-return', ok: !retval.isNull(),
              report: readBytes(this.report, Math.min(this.length, 256)),
              path: this.path});
    }
});

hookExport('hid.dll', 'HidD_SetFeature', {
    onEnter: function (args) {
        this.report = args[1];
        this.length = args[2].toInt32();
        this.path = pathForHandle(args[0]);
          event({api: 'HidD_SetFeature', length: this.length,
              report: readBytes(this.report, Math.min(this.length, 256)),
              path: this.path});
    },
    onLeave: function (retval) {
        event({api: 'HidD_SetFeature-return', ok: !retval.isNull(), path: this.path});
    }
});
"""


def on_message(message, _data, output):
    if message["type"] == "send":
        payload = message["payload"]
        print(payload, flush=True)
        if output:
            output.write(json.dumps(payload) + "\n")
            output.flush()
    else:
        print(message, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("program", nargs="?", help="Path to weather.exe to spawn")
    parser.add_argument("--pid", action="append", type=int, default=[],
                        help="PID to attach to; may be supplied more than once")
    parser.add_argument("--process", action="append", default=[],
                        help="Process name to attach to; may be supplied more than once")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT),
                        help="Write JSONL trace events to this file")
    parser.add_argument("--redirect-base", help="Explicitly redirect legacy HTTP URLs to this local base URL")
    args = parser.parse_args()

    sessions = []
    spawned_pids = []
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output = output_path.open("a", encoding="utf-8")
    redirect_base = json.dumps(args.redirect_base) if args.redirect_base else "null"
    script_source = AGENT.replace("%REDIRECT_BASE%", redirect_base)
    target_pids = list(args.pid)
    if args.process:
        wanted = {name.lower().removesuffix('.exe') for name in args.process}
        local_device = frida.get_local_device()
        target_pids.extend(
            process.pid for process in local_device.enumerate_processes()
            if process.name.lower().removesuffix('.exe') in wanted
        )

    if args.program:
        spawned_pids.append(frida.spawn([args.program]))
        target_pids.extend(spawned_pids)

    if not target_pids:
        parser.error('provide a program, --pid, or --process')

    for target_pid in dict.fromkeys(target_pids):
        try:
            session = frida.attach(target_pid)
        except frida.ProcessNotFoundError:
            print(f'Process {target_pid} exited before Frida could attach; skipping.',
                  file=sys.stderr, flush=True)
            continue
        except frida.PermissionDeniedError as error:
            print(f'Frida permission denied for process {target_pid}: {error}',
                  file=sys.stderr, flush=True)
            continue
        except frida.OperationCancelledError as error:
            print(f'Frida attach was cancelled for process {target_pid}: {error}',
                  file=sys.stderr, flush=True)
            continue
        except frida.NotSupportedError as error:
            print(f'Frida could not inject into process {target_pid}: {error}',
                  file=sys.stderr, flush=True)
            print('Run this tracer with the same or higher Windows integrity level '
                  'as the target process.', file=sys.stderr, flush=True)
            continue
        script = session.create_script(script_source)
        script.on("message", lambda message, data: on_message(message, data, output))
        script.load()
        sessions.append(session)

    if not sessions:
        parser.error('could not attach to any target process; restart the target and retry')

    for target_pid in spawned_pids:
        frida.resume(target_pid)

    print("Tracing. Press Ctrl+C to stop.", flush=True)
    try:
        sys.stdin.read()
    except KeyboardInterrupt:
        pass
    finally:
        for session in sessions:
            session.detach()
        if output:
            output.close()


if __name__ == "__main__":
    main()
