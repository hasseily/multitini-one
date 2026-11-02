; SPDX-License-Identifier: GPL-3.0-only
; Fixed raw supplemental observations, followed by first 16 exception records.
extras:
    jsr baseline
    ldx #0
extras_rom:
    lda rom_sample_addresses,x
    sta extra_read+1
    lda rom_sample_addresses+1,x
    sta extra_read+2
    lda rom_sample_addresses+2,x
    sta extra_read+3
    jsr extra_read
    jsr emit_extra
    inx
    inx
    inx
    cpx #48
    bne extras_rom
    ldx #0
extras_absent:
    lda absent_addresses,x
    sta extra_read+1
    lda absent_addresses+1,x
    sta extra_read+2
    lda absent_addresses+2,x
    sta extra_read+3
    jsr extra_read
    jsr emit_extra
    inx
    inx
    inx
    cpx #96
    bne extras_absent
    stz extra_case
extras_flat:
    lda #3
    sta descriptor
    ldx extra_case
    lda flat_banks,x
    sta descriptor+3
    sta lc_seed_bank2+3
    sta lc_seed_bank1+3
    sta lc_seed_e+3
    sta flat_read_c+3
    sta flat_read_d+3
    lda flat_alts,x
    sta descriptor+6
    jsr lc_seed
    lda descriptor+6
    ora #8
    sta f:$E0C068
    lda f:$E0C083
    lda #$7f
    sta f:$E0C035
flat_read_c:
    lda f:$00C800
    sta lc_d_read
flat_read_d:
    lda f:$00D800
    sta lc_e_read
    lda f:$E0C08B
    ; Repeat the same two reads after changing the LC bank while inhibited.
    lda flat_read_c+3
    sta flat_read_c2+3
    sta flat_read_d2+3
flat_read_c2:
    lda f:$00C800
    sta lc_d_result
flat_read_d2:
    lda f:$00D800
    sta lc_e_result
    lda #8
    sta f:$E0C068
    jsr baseline
    lda lc_d_read
    jsr emit_extra
    lda lc_e_read
    jsr emit_extra
    lda lc_d_result
    jsr emit_extra
    lda lc_e_result
    jsr emit_extra
    inc extra_case
    lda extra_case
    cmp #4
    beq :+
    jmp extras_flat
:
    stz extra_case
extras_extended_io:
    jsr baseline
    ldx extra_case
    lda extended_io_banks,x
    sta extended_io_seed+3
    sta extended_io_write+3
    sta extended_io_switch+3
    sta extended_io_read+3
    lda #0
extended_io_seed:
    sta f:$02C010
    lda #0
    sta f:$E0C035
    lda extended_io_speed,x
    sta f:$E0C036
    lda #$12
extended_io_write:
    sta f:$02C010
extended_io_switch:
    sta f:$02C005
    lda f:$E0C068
    sta lc_d_read
    lda #8
    sta f:$E0C068
    jsr baseline
extended_io_read:
    lda f:$02C010
    jsr emit_extra
    lda lc_d_read
    jsr emit_extra
    inc extra_case
    lda extra_case
    cmp #16
    bne extras_extended_io
    stz extra_case
extras_text2:
    jsr baseline
    ldx extra_case
    txa
    lsr
    lsr
    sta text2_store+3
    lda #0
    sta f:$E00800
    sta f:$E10800
    txa
    and #1
    beq extras_text2_enabled
    lda #$3f
    bra extras_text2_shadow
extras_text2_enabled:
    lda #$1f
extras_text2_shadow:
    sta f:$E0C035
    txa
    and #2
    asl
    asl
    asl
    ora #$c0
    sta f:$E0C036
    lda #$96
text2_store:
    sta f:$000800
    jsr baseline
    lda f:$E00800
    jsr emit_extra
    lda f:$E10800
    jsr emit_extra
    inc extra_case
    lda extra_case
    cmp #16
    bne extras_text2
    stz extra_case
extras_slot:
    jsr baseline
    lda #0
    sta f:$E0C035
    lda f:$E0CFFF             ; release previous internal/expansion ownership
    lda extra_case
    and #2
    beq extras_slot3_internal
    lda #0
    sta f:$E0C00B
    bra extras_slot3_done
extras_slot3_internal:
    lda #0
    sta f:$E0C00A
extras_slot3_done:
    lda extra_case
    and #4
    asl
    sta f:$E0C02D             ; other slots internal; slot 3 bit varied last
    lda extra_case
    and #1
    ora #8
    sta f:$E0C068
    lda extra_case
    lsr
    lsr
    lsr
    and #7
    asl
    tax8
    jsr (slot_triggers,x)
    lda f:$E0C800
    jsr emit_extra
    lda f:$E0C801
    jsr emit_extra
    lda f:$E0CFFF             ; read observes old selection and then releases
    jsr emit_extra
    lda f:$E0C800
    jsr emit_extra
    lda f:$E0C801
    jsr emit_extra
    jsr tick
    inc extra_case
    lda extra_case
    cmp #64
    bne extras_slot
    jsr baseline
    ldx #0
extras_anomalies:
    lda anomaly_records,x
    jsr emit_extra
    inx
    cpx #96
    bne extras_anomalies
    rts

extra_read:
    lda f:$FF0000
    rts
emit_extra:
    phy
    ldy extra_position
    sta EXTRA,y
    iny
    sty extra_position
    ply
    rts

slot_none:
    rts
slot_read3:
    lda f:$E0C300
    rts
slot_write3:
    lda #0
    sta f:$E0C300
    rts
slot_read1:
    lda f:$E0C100
    rts
slot_read7:
    lda f:$E0C700
    rts
slot_sticky:
    lda f:$E0C300
    lda f:$E0C100
    rts
slot_release_read:
    lda f:$E0C300
    lda f:$E0CFFF
    rts
slot_release_write:
    lda f:$E0C300
    lda #0
    sta f:$E0CFFF
    rts

make_packet:
    ldx #0
make_packet_signature:
    lda signature,x
    sta PACKET,x
    inx
    cpx #5
    bne make_packet_signature
    lda flags
    sta PACKET+5
    lda rom_revision
    sta PACKET+6
    lda launch_bank
    sta PACKET+7
    rep #$20
    lda #INDEX_COUNT
    sta PACKET+10
    lda dict_count
    and #$ff
    sta PACKET+12
    lda extra_position
    sta PACKET+14
    lda frames
    sta PACKET+20
    lda anomalies
    sta PACKET+22
    lda skipped_cases
    sta PACKET+48
    lda #64+INDEX_COUNT
    sta packet_length
    sep #$20
    ldx #0
make_packet_count:
    lda case_total,x
    sta PACKET+16,x
    inx
    cpx #4
    bne make_packet_count
    ldx #0
make_packet_entry:
    lda entry_regs,x
    sta PACKET+24,x
    inx
    cpx #5
    bne make_packet_entry
    ldx #0
make_packet_presence:
    lda present_bits,x
    sta PACKET+32,x
    inx
    cpx #16
    bne make_packet_presence
    rep #$20
    lda dict_count
    and #$ff
    asl
    asl
    asl
    asl
    sta temp2
    ldx #0
    ldy packet_length
    sep #$20
make_packet_dict:
    cpx temp2
    beq make_packet_dict_done
    lda DICT,x
    sta PACKET,y
    inx
    iny
    bra make_packet_dict
make_packet_dict_done:
    ldx #0
make_packet_extra:
    cpx extra_position
    beq make_packet_extra_done
    lda EXTRA,x
    sta PACKET,y
    inx
    iny
    bra make_packet_extra
make_packet_extra_done:
    sty packet_length
    rep #$20
    tya
    clc
    adc #4
    sta PACKET+8
    sep #$20
    lda #$ff
    sta crc32
    sta crc32+1
    sta crc32+2
    sta crc32+3
    ldx #0
make_packet_crc:
    lda PACKET,x
    jsr crc32_update
    inx
    cpx packet_length
    bne make_packet_crc
    ldy #0
make_packet_finish_crc:
    lda crc32,y
    eor #$ff
    sta PACKET,x
    inx
    iny
    cpy #4
    bne make_packet_finish_crc
    stx packet_length
    rts

crc32_update:
    eor crc32
    sta crc32
    lda #8
    sta crc_bits
crc32_update_bit:
    lsr crc32+3
    ror crc32+2
    ror crc32+1
    ror crc32
    bcc crc32_update_next
    lda crc32
    eor #$20
    sta crc32
    lda crc32+1
    eor #$83
    sta crc32+1
    lda crc32+2
    eor #$b8
    sta crc32+2
    lda crc32+3
    eor #$ed
    sta crc32+3
crc32_update_next:
    dec crc_bits
    bne crc32_update_bit
    rts

crc16_update:
    eor crc16+1
    sta crc16+1
    lda #8
    sta crc_bits
crc16_update_bit:
    asl crc16
    rol crc16+1
    bcc crc16_update_next
    lda crc16
    eor #$21
    sta crc16
    lda crc16+1
    eor #$10
    sta crc16+1
crc16_update_next:
    dec crc_bits
    bne crc16_update_bit
    rts

render:
    jsr baseline
    lda #$81                 ; select SHR/linear mapping BEFORE every video write
    sta f:$E0C029
    lda #0
    sta f:$E0C034
    ldx #0
render_clear:
    sta f:$E12000,x
    inx
    cpx #32000
    bne render_clear
    lda #$80
    ldx #0
render_scb:
    sta f:$E19D00,x
    inx
    cpx #200
    bne render_scb
    ldx #0
render_palette:
    txa
    and #6
    bne render_white
    lda #0
    bra render_colour
render_white:
    lda #$ff
render_colour:
    sta f:$E19E00,x
    and #$0f
    sta f:$E19E01,x
    inx
    inx
    cpx #32
    bne render_palette
    ldx #0
    lda flags
    bne render_warning_banner
render_banner:
    lda banner,x
    sta f:$E12000,x
    inx
    cpx #1280
    bne render_banner
    bra render_banner_done
render_warning_banner:
    lda warning_banner,x
    sta f:$E12000,x
    inx
    cpx #1280
    bne render_warning_banner
render_banner_done:
    lda #$81
    sta f:$E0C029
    rep #$20
    lda #1280
    sta screen_position
    lda #0
    sta row_number
    sta extra_position
    sep #$20
render_row:
    lda #$a5
    sta row_buffer
    sta row_buffer+39
    lda #$5a
    sta row_buffer+1
    lda row_number
    sta row_buffer+2
    ldx extra_position
    ldy #0
render_payload:
    lda PACKET,x
    sta row_buffer+3,y
    inx
    iny
    cpy #34
    bne render_payload
    stx extra_position
    lda #$ff
    sta crc16
    sta crc16+1
    ldy #0
render_crc:
    lda row_buffer+2,y
    jsr crc16_update
    iny
    cpy #35
    bne render_crc
    lda crc16
    sta row_buffer+37
    lda crc16+1
    sta row_buffer+38
    stz row_byte
    ldx screen_position
render_byte:
    ldy row_byte
    lda row_buffer,y
    sta pixel_byte
    lda #4
    sta pixel_bits
render_pair:
    stz pixel_pair
    asl pixel_byte
    bcc :+
    lda #$f0
    sta pixel_pair
:
    asl pixel_byte
    bcc :+
    lda pixel_pair
    ora #$0f
    sta pixel_pair
:
    lda pixel_pair
    sta f:$E12000,x
    sta f:$E120A0,x
    inx
    dec pixel_bits
    bne render_pair
    inc row_byte
    lda row_byte
    cmp #40
    bne render_byte
    rep #$20
    txa
    clc
    adc #160
    sta screen_position
    inc row_number
    lda row_number
    cmp #96
    sep #$20
    beq :+
    jmp render_row
:
    rts
