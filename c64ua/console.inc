; SPDX-License-Identifier: GPL-3.0-only
; Print zero-terminated PETSCII at A/Y; preserves the C64 BASIC pointer pair.
print:
    sta print_read+1
    sty print_read+2
print_read:
    lda $ffff
    beq print_done
    jsr $ffd2
    inc print_read+1
    bne print_read
    inc print_read+2
    bne print_read
print_done:
    rts
hex_byte:
    pha
    lsr
    lsr
    lsr
    lsr
    jsr hex_nibble
    pla
    and #15
hex_nibble:
    tax
    lda hex_chars,x
    jmp $ffd2
hex_chars: !text "0123456789ABCDEF"
