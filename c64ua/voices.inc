; SPDX-License-Identifier: GPL-3.0-only
; Generated synthetic samples occupy C64 $3000-$9FFF and REU $F00000-$F06FFF.
; Call returns carry clear when seven looping voices have started.
start_audio:
    lda #$ff
    sta $df3f
    lda $df20
    beq +
    jmp start_failed
+
    lda $df21
    cmp #$10
    beq +
    jmp start_failed
+
    lda #0
    sta $df02
    sta $df04
    sta $df05
    sta $df07
    sta $df09
    sta $df0a
    lda #$30
    sta $df03
    lda #$f0
    sta $df06
    lda #$70
    sta $df08
    lda #$90
    sta $df01
    lda $df00
    and #$40
    bne +
    jmp start_failed
+
!for voice,0,6 {
    lda #0
    sta $df20+voice*32
    sta $df26+voice*32
    sta $df27+voice*32
    sta $df29+voice*32
    sta $df2b+voice*32
    sta $df2e+voice*32
    sta $df31+voice*32
    sta $df32+voice*32
    sta $df33+voice*32
    sta $df35+voice*32
    sta $df37+voice*32
    lda #$f0
    sta $df25+voice*32
    lda #voice*16
    sta $df26+voice*32
    lda #$10
    sta $df2a+voice*32
    sta $df36+voice*32
    lda #8
    sta $df21+voice*32
    lda #(voice*5)&15
    sta $df22+voice*32
    lda #RATE
    sta $df2f+voice*32
    lda #$53
    sta $df20+voice*32
}
    clc
    rts
start_failed:
    sec
    rts
!if * >= $3000 { !error "Program overlaps sample data" }
!fill $3000-*,0
!binary "tones.bin"
