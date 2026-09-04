; SPDX-License-Identifier: GPL-3.0-only
; Shared constants of the Multitini fast-disk protocol (KERNAL, DOS, engine).
ENGINE_BASE         = $9af0      ; extram: JMP entry, then 'M','T',version
ENGINE_MAGIC0       = $9af3
ENGINE_MAGIC1       = $9af4
ENGINE_VERSION_ADDR = $9af5
ENGINE_VERSION      = $01

CMD_PLAIN  = $00                 ; per-byte fast mode
CMD_STREAM = $01                 ; framed stream to end of file (LOAD)
CMD_UPLOAD = $02                 ; engine upload follows: len lo, len hi, bytes

MODE_PLAIN  = $01
MODE_STREAM = $02

FRAME_RAW  = $80                 ; frame header bit7: 325 GCR bytes follow
FRAME_LAST = $40                 ; frame header bit6: last frame of the file
