#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production BOT admission, WRITE/reset and CH9 failure paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CLASS = 'firmware/platform/xilinx/xusbps_class_storage.c'
CH9 = 'firmware/platform/xilinx/xusbps_ch9.c'
NAMES = ('ResetStorageState', 'XUsbPs_StorageReset', 'StorageValidateCbw',
         'XUsbPs_StorageRxError',
         'StorageFlushEp1In',
         'WritePipeHasFreeSlot', 'WritePipeFlushOne', 'XUsbPs_StorageFlushPending',
         'XUsbPs_StorageHasPending', 'WritePipeAllocSlot', 'WritePipeReleaseActive',
         'WritePipeCommitActive', 'CbwRequestedBytes', 'StorageStartWrite',
         'StorageHandleWriteData', 'StorageOpcodeNeedsDefer',
         'XUsbPs_StorageTryFastRx', 'XUsbPs_ClassReq_Storage')
INPUTS = (CLASS, CH9, 'firmware/platform/xilinx/xusbps_class_storage.h',
          'scripts/test_usb_storage_protocol.py', 'tests/host/test_usb_storage_protocol.c',
          'tests/host/test_usb_storage_ch9.c')


def function(source, name):
    match = re.search(r'^(?:static\s+)?(?:void|int|u32)\s+' + name +
                      r'\([^;]*?\)\s*\{', source, re.M)
    if match is None:
        raise ValueError('Missing production function: ' + name)
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def descriptor(source):
    body = function(source, 'XUsbPs_StdDevReq')
    begin = body.index('\t\tcase XUSBPS_REQ_GET_DESCRIPTOR:')
    end = body.index('\t\tcase XUSBPS_REQ_SET_CONFIGURATION:', begin)
    post = body[body.index('\t/* Set the send stall bit if there was an error */'):]
    return ('static void descriptor_request(XUsbPs *InstancePtr, XUsbPs_SetupData *SetupData)\n'
            '{ int Status, Error = 0, ReplyLen; u8 Reply[512] = {0};\n'
            'switch (SetupData->bRequest) {\n' + body[begin:end] +
            'default: assert(0); }\n' + post)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/gcc.exe' if os.name == 'nt' else 'cc')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/usb-storage-protocol')
    parser.add_argument('--ch9-before', type=Path, help='Captured old CH9 source; must time out on failed send')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in INPUTS}
    source = (ROOT / CLASS).read_text()
    # Bind both public admission paths, not only the isolated validator.
    handler = function(source, 'XUsbPs_HandleStorageReq')
    command = handler[handler.index('if (USB_EP_STATE_COMMAND == phase)'):]
    assert command.index('StorageValidateCbw(') < command.index('CBW = (USB_CBW *)')
    fast = function(source, 'XUsbPs_StorageTryFastRx')
    assert fast.index('StorageValidateCbw(') < fast.index('CBW = (USB_CBW *)')
    (out / 'usb_storage_protocol.inc').write_text('\n\n'.join(function(source, n) for n in NAMES))
    (out / 'usb_storage_ch9.inc').write_text(descriptor((ROOT / CH9).read_text()))
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/ucrt64/bin' + os.pathsep + env.get('PATH', '')
    log = []

    def execute(test, directory, timeout=10):
        binary = directory / (test + ('.exe' if os.name == 'nt' else ''))
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(directory),
                   str(ROOT / 'tests/host' / (test + '.c')), '-o', str(binary)]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        log.append('$ ' + ' '.join(command) + '\n' + result.stdout + result.stderr)
        if result.returncode:
            raise ValueError('Compile failed: ' + result.stderr)
        try:
            result = subprocess.run([str(binary)], cwd=ROOT, env=env, timeout=timeout,
                                    capture_output=True, text=True)
        except subprocess.TimeoutExpired:
            log.append('$ ' + str(binary) + '\nEXPECTED_TIMEOUT\n')
            return None
        log.append('$ ' + str(binary) + '\n' + result.stdout + result.stderr)
        return result

    try:
        results = []
        for test in ('test_usb_storage_protocol', 'test_usb_storage_ch9'):
            result = execute(test, out)
            if result is None or result.returncode:
                raise ValueError('Production regression failed: ' + test)
            results.append(result.stdout.strip())
        negative = None
        if args.ch9_before:
            before = out / 'negative-before'
            before.mkdir(exist_ok=True)
            (before / 'usb_storage_ch9.inc').write_text(descriptor(args.ch9_before.read_text(encoding='utf-8-sig')))
            result = execute('test_usb_storage_ch9', before, timeout=1)
            if result is not None:
                raise ValueError('Old descriptor failure unexpectedly returned')
            negative = {'status': 'EXPECTED_TIMEOUT', 'seconds': 1,
                        'source_sha256': hashlib.sha256(args.ch9_before.read_bytes()).hexdigest()}
        assert hashes == {n: hashlib.sha256((ROOT / n).read_bytes()).hexdigest() for n in INPUTS}
        (out / 'validation.json').write_text(json.dumps({
            'status': 'PASS', 'files': hashes, 'checks': results, 'ch9_negative_before': negative,
            'scope': 'Actual extracted class functions and complete descriptor request/error blocks; USB/DMA/SD mocked, no physical transfer claim.'
        }, indent=2) + '\n')
        print('\n'.join(results))
    finally:
        (out / 'run.log').write_text('\n'.join(log))


if __name__ == '__main__':
    main()
