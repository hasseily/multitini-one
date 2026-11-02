; SPDX-License-Identifier: GPL-3.0-only
; GS/OS S16 entry. This is a takeover diagnostic: photograph, then reboot.
; No OS, ROM, disk, firmware, or interrupt calls after entry. Code runs in
; odd bank $05; all state written while switches vary is above $A000.
.setcpu "65816"
.smart
.segment "CODE"
.org $0000
.a16
.i16
.macro tax8
    rep #$20
    and #$00ff
    tax
    sep #$20
.endmacro
.macro tay8
    rep #$20
    and #$00ff
    tay
    sep #$20
.endmacro
EXEC = $05
DICT = $6000
EXTRA = $6800
CASES = $7000
PACKET = $8000
.include "generated.inc"

entry:
    sei
    clc
    xce
    cld
    rep #$30
    phk
    plb
    sep #$20
    lda f:$E0C029
    sta a:entry_regs
    lda f:$E0C035
    sta a:entry_regs+1
    lda f:$E0C036
    sta a:entry_regs+2
    lda f:$E0C068
    sta a:entry_regs+3
    lda f:$E0C02D
    sta a:entry_regs+4
    phk
    pla
    sta a:launch_bank
    sta a:copy_move+2
    lda #$01
    sta f:$E0C029
    lda #$7f
    sta f:$E0C035
    lda #$80
    sta f:$E0C036
    lda #$08
    sta f:$E0C068
    rep #$30
    ldx #0
    ldy #0
    lda #$ffff
copy_move:
    .byte $54,EXEC,$00          ; MVN dest,source; source patched from PBR
    jml EXEC*$10000+main

main:
    rep #$30
    lda #$bfff
    tcs
    lda #0
    tcd
    sep #$20
    lda #0
    sta flags
    sta dict_count
    sta last_vbl
    sta f:$E0C041             ; mask Mega II interrupts (SEI already set)
    sta f:$E0C023             ; mask video IRQs
    jsr baseline
    lda f:$FFFB59             ; Y low byte loaded by the ROM $FE1F ID routine
    sta rom_revision
    ldx #0
    lda #0
main_clear:
    sta PACKET,x
    sta EXTRA,x
    inx
    cpx #$1000
    bne main_clear
    ldx #0
main_clear_state:
    sta case_total,x
    inx
    cpx #32
    bne main_clear_state
    rep #$20
    lda #64
    sta index_position
    lda #0
    sta profile_number
    sta descriptor_position
    sta extra_position
    sta frames
    sta anomalies
    sep #$20
    jsr presence
main_profile:
    ldx descriptor_position
    ldy #0
main_descriptor:
    lda descriptors,x
    sta descriptor,y
    inx
    iny
    cpy #8
    bne main_descriptor
    stx descriptor_position
    jsr setup_addresses
    ldx #0
    lda #0
main_clear_cases:
    sta CASES,x
    inx
    cpx #128
    bne main_clear_cases
    stz case_number
main_case:
    jsr prepare_case
    jsr run_case
    ldx case_number
    lda result
    sta CASES,x
    inc case_total
    bne :+
    inc case_total+1
    bne :+
    inc case_total+2
:
    jsr tick
    inc case_number
    lda case_number
    cmp descriptor+2
    bne main_case
    jsr encode_profile
    rep #$20
    inc profile_number
    lda profile_number
    cmp #PROFILE_COUNT
    sep #$20
    beq main_finished
    lda flags
    and #$03                 ; time/dictionary limits produce explicit partial packet
    beq main_profile
main_finished:
    jsr extras
    jsr make_packet
    jsr render
done:
    bra done

; Baseline only changes documented switches needed to address the four stores.
; Never call a subroutine between selecting a tested mapping and restoring it.
baseline:
    lda #$01
    sta f:$E0C029
    lda #$7f
    sta f:$E0C035
    lda #$80
    sta f:$E0C036
    lda #$08
    sta f:$E0C068
    lda #0
    sta f:$E0C000             ; 80STORE off
    sta f:$E0C056             ; HIRES off
    rts

tick:
    lda f:$E0C019
    and #$80
    cmp last_vbl
    beq tick_done
    sta last_vbl
    bpl tick_done
    rep #$20
    inc frames
    lda frames
    cmp #2700                ; 54 s at 50 Hz; bounded final transport is separate
    sep #$20
    bcc tick_done
    lda flags
    ora #1
    sta flags
tick_done:
    rts

presence:
    ldx #0
presence_bank:
    lda #$c7
    sta f:$05B400
    txa
    sta presence_write+3
    sta presence_read+3
    cmp #EXEC
    beq presence_present             ; execution bank is reserved, never overwritten
    lda #$55
    jsr presence_check
    bne presence_absent
    lda #$aa
    jsr presence_check
    bne presence_absent
    lda f:$05B400
    cmp #$c7
    beq presence_present
    lda flags
    ora #$10                 ; an address aliases our bank; do not probe its code
    sta flags
    bra presence_absent
presence_present:
    txa
    and #7
    tay8
    lda bit_masks,y
    sta temp
    txa
    lsr
    lsr
    lsr
    tay8
    lda present_bits,y
    ora temp
    sta present_bits,y
presence_absent:
    inx
    cpx #128
    bne presence_bank
    rts
presence_check:
presence_write:
    sta f:$00B400
presence_read:
    cmp f:$00B400
    rts

setup_addresses:
.include "setup.inc"
    rts

seed:
    jsr baseline
    lda #$19
seed_even:
    sta f:$000400
    lda #$2b
seed_odd:
    sta f:$010400
    lda #$3d
seed_slow0:
    sta f:$E00400
    lda #$4f
seed_slow1:
    sta f:$E10400
    rts

source_write:
    sta f:$000400
    rts
source_read:
    lda f:$000400
    rts

; Read all physical stores only after restoring the baseline. Results contain
; observed marker destinations, never expected destinations. Unexpected bytes
; are recorded separately and make the result explicitly non-clean.
capture:
    jsr baseline
    stz result
read_even:
    lda f:$000400
    ldy #0
    jsr classify
read_odd:
    lda f:$010400
    ldy #1
    jsr classify
read_slow0:
    lda f:$E00400
    ldy #2
    jsr classify
read_slow1:
    lda f:$E10400
    ldy #3
    jsr classify
    rts
classify:
    cmp marker
    bne classify_unchanged
    lda result
    ora bit_masks,y
    sta result
    rts
classify_unchanged:
    cmp sentinels,y
    beq classify_done
    sty anomaly_which
    jsr anomaly
classify_done:
    rts
anomaly:
    sta anomaly_value
    lda flags
    ora #$04
    sta flags
    rep #$20
    inc anomalies
    lda anomalies
    cmp #17
    sep #$20
    bcs anomaly_done
    ldx anomaly_position
    lda profile_number
    sta anomaly_records,x
    lda profile_number+1
    sta anomaly_records+1,x
    lda case_number
    sta anomaly_records+2,x
    lda anomaly_which
    sta anomaly_records+3,x
    lda anomaly_value
    sta anomaly_records+4,x
    lda phase
    sta anomaly_records+5,x
    rep #$20
    txa
    clc
    adc #6
    sta anomaly_position
    sep #$20
anomaly_done:
    rts

run_case:
    stz result
    lda descriptor
    beq shadow_case
    cmp #1
    bne :+
    jmp aux_case
:
    cmp #2
    bne :+
    jmp lc_pair_case
:
    cmp #3
    bne :+
    jmp lc_window_case
:
    jmp register_case

shadow_case:
    lda descriptor+3
    and #$fe
    tax8
    and #7
    tay8
    txa
    lsr
    lsr
    lsr
    tax8
    lda present_bits,x
    and bit_masks,y
    beq shadow_case_skip
    lda present_bits,x
    and odd_bit_masks,y
    beq shadow_case_skip
    stz phase
shadow_case_phase:
    jsr seed
    ldx phase
    lda markers,x
    sta marker
    lda case_number
    sta f:$E0C035
    lda descriptor+7
    sta f:$E0C036
    lda descriptor+6
    sta f:$E0C068
    lda marker
    ; Inline long store: a JSR would itself exercise RAMWRT/ALTZP on stack.
shadow_store:
    sta f:$000400
    lda #$08
    sta f:$E0C068
    jsr capture
    lda phase
    bne shadow_case_compare
    lda result
    sta first_result
    inc phase
    bra shadow_case_phase
shadow_case_compare:
    lda result
    cmp first_result
    beq shadow_case_done
    ldy #$fe
    sty anomaly_which
    jsr anomaly
    lda flags
    ora #$08
    sta flags
shadow_case_done:
    lda first_result
    sta result
    rts
shadow_case_skip:
    inc skipped_cases
    bne :+
    inc skipped_cases+1
:
    rts

aux_case:
    jsr seed
    lda #$96
    sta marker
    ldx case_number
    lda aux_state,x
    sta critical_state
    lda aux_video,x
    sta f:$E0C029
    lda aux_store80,x
    beq aux_case_store80_off
    sta f:$E0C001
    bra aux_case_store80_done
aux_case_store80_off:
    sta f:$E0C000
aux_case_store80_done:
    lda aux_hires,x
    beq :+
    sta f:$E0C057
:
    lda critical_state
    sta f:$E0C068
aux_read:
    lda f:$000400
    sta read_value
    lda marker
aux_write:
    sta f:$000400
    lda #$08
    sta f:$E0C068
    jsr capture
    ldy #0
aux_case_read_class:
    lda read_value
    cmp sentinels,y
    beq aux_case_found
    iny
    cpy #4
    bne aux_case_read_class
    ldy #$fd
    sty anomaly_which
    jsr anomaly
    rts
aux_case_found:
    tya
    asl
    asl
    asl
    asl
    ora result
    sta result
    rts

; The LC kernels live in generated code because their four long operands must
; all refer to the same selected logical bank. Setup never assumes C/D layout.
.include "kernels.s"

register_case:
    jsr baseline
    lda case_number
    ora descriptor+6
register_store:
    sta f:$E0C029
register_read:
    lda f:$E0C029
    sta read_value
    lda #$08
    sta f:$E0C068
    jsr baseline
    lda read_value
    sta result
    rts

; Transpose measured bytes into bit planes, deduplicating exact 128-bit tables.
; This is lossless; it does not fit observations to a preferred MMU model.
encode_profile:
    stz plane_number
encode_profile_plane:
    ldx #0
    lda #0
encode_profile_clear:
    sta plane_buffer,x
    inx
    cpx #16
    bne encode_profile_clear
    ldx #0
encode_profile_bit:
    ldy plane_number
    lda CASES,x
    and bit_masks,y
    beq encode_profile_next
    txa
    and #7
    tay8
    lda bit_masks,y
    sta temp
    txa
    lsr
    lsr
    lsr
    tay8
    lda plane_buffer,y
    ora temp
    sta plane_buffer,y
encode_profile_next:
    inx
    cpx #128
    bne encode_profile_bit
    ldx #0
    stz dictionary_id
encode_profile_search:
    lda dictionary_id
    cmp dict_count
    beq encode_profile_new
    ldy #0
encode_profile_compare:
    lda DICT,x
    cmp plane_buffer,y
    bne encode_profile_different
    inx
    iny
    cpy #16
    bne encode_profile_compare
    bra encode_profile_index
encode_profile_different:
    rep #$20
    lda dictionary_id
    and #$ff
    inc
    asl
    asl
    asl
    asl
    tax
    sep #$20
    inc dictionary_id
    bra encode_profile_search
encode_profile_new:
    cmp #48
    bcc :+
    lda flags
    ora #2
    sta flags
    stz dictionary_id
    bra encode_profile_index
:
    ldy #0
encode_profile_copy:
    lda plane_buffer,y
    sta DICT,x
    inx
    iny
    cpy #16
    bne encode_profile_copy
    inc dict_count
encode_profile_index:
    ldx index_position
    lda dictionary_id
    sta PACKET,x
    inx
    stx index_position
    jsr tick
    inc plane_number
    lda plane_number
    cmp descriptor+1
    beq :+
    jmp encode_profile_plane
:
    rts

.include "transport.s"

; Constant input tables and original glyphs; generated by build.py.
.include "tables.inc"
.assert * < $6000, error, "code overlaps observation dictionary"
.res $b000-*, 0
; All mutable CPU state used while experimental mapping is active stays outside
; video shadow ranges and outside the Cxxx/Dxxx language-card windows.
entry_regs: .res 5
launch_bank: .res 1
flags: .res 1
rom_revision: .res 1
descriptor: .res 8
descriptor_position: .res 2
profile_number: .res 2
case_number: .word 0
result: .res 1
read_value: .res 1
critical_state: .res 1
marker: .res 1
phase: .word 0
first_result: .res 1
temp: .word 0
temp2: .word 0
plane_number: .word 0
dictionary_id: .word 0
dict_count: .word 0
index_position: .res 2
extra_position: .res 2
anomaly_position: .word 0
anomaly_which: .word 0
anomaly_value: .res 1
anomaly_records: .res 96
plane_buffer: .res 16
present_bits: .res 16,0
frames: .word 0
anomalies: .word 0
last_vbl: .res 1
case_total: .res 4
skipped_cases: .res 2
scratch: .res 26
packet_length: .res 2
crc16: .res 2
crc32: .res 4
crc_byte: .res 1
crc_bits: .res 1
row_number: .word 0
row_buffer: .res 40
screen_position: .res 2
pixel_byte: .res 1
pixel_bits: .res 1
pixel_pair: .res 1
row_byte: .word 0
extra_case: .word 0
lc_state: .res 1
lc_d_read: .res 1
lc_e_read: .res 1
lc_d_result: .res 1
lc_e_result: .res 1
lc_read_class: .res 1
.assert * < $bc00, error, "critical state overlaps stack guard"
