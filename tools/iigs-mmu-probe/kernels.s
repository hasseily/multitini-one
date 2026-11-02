; SPDX-License-Identifier: GPL-3.0-only
; Included in probe.s. All experimental accesses are absolute long.
lc_seed:
    jsr baseline
    lda #0
    sta f:$E0C035
    lda descriptor
    cmp #3
    bne lc_seed_no_alt
    lda descriptor+6
    ora #$08
    sta f:$E0C068
lc_seed_no_alt:
    lda f:$E0C083
    lda f:$E0C083
    lda #$19
lc_seed_bank2:
    sta f:$00D800
    lda f:$E0C08B
    lda f:$E0C08B
    lda #$2b
lc_seed_bank1:
    sta f:$00D800
    lda #$3d
lc_seed_e:
    sta f:$00E800
    lda #$08
    sta f:$E0C068
    rts

lc_pair_case:
    jsr lc_seed
    lda f:$E0C082
    lda #0
lc_first:
    lda f:$00C080
lc_second:
    lda f:$00C080
    lda #$96
    sta f:$00D800
    lda f:$E0C068
    sta lc_state
    lda #$08
    sta f:$E0C068
    lda f:$E0C083
    lda f:$E0C083
    lda f:$00D800
    sta lc_d_result
    lda f:$E0C08B
    lda f:$E0C08B
    lda f:$00D800
    sta lc_e_result
    jsr baseline
    lda lc_state
    and #$0c
    lsr
    sta result
    lda lc_d_result
    cmp #$96
    beq lc_pair_written
    cmp #$19
    beq lc_pair_other
    ldy #$e0
    sty anomaly_which
    jsr anomaly
lc_pair_other:
    lda lc_e_result
    cmp #$96
    beq lc_pair_written
    cmp #$2b
    beq lc_pair_done
    ldy #$e1
    sty anomaly_which
    jsr anomaly
    rts
lc_pair_written:
    lda result
    ora #1
    sta result
lc_pair_done:
    rts

lc_window_case:
    jsr lc_seed
    lda f:$E0C082
    lda descriptor+6
    ora #$08
    sta f:$E0C068
    lda descriptor+7
    sta f:$E0C035
    lda #0
lc_window_switch:
    lda f:$E0C080
lc_window_switch2:
    lda f:$E0C080
lc_window_d_read:
    lda f:$00D800
    sta lc_d_read
lc_window_e_read:
    lda f:$00E800
    sta lc_e_read
    lda #$96
lc_window_d_write:
    sta f:$00D800
lc_window_e_write:
    sta f:$00E800
    lda f:$E0C068
    and #$84                 ; retain the observed bank and ALTZP, request RAM
    sta f:$E0C068
lc_window_d_after:
    lda f:$00D800
    sta lc_d_result
lc_window_e_after:
    lda f:$00E800
    sta lc_e_result
    lda #$08
    sta f:$E0C068
    jsr baseline
    stz result
    lda lc_d_read
    cmp #$19
    beq lc_window_d_classified
    inc result
    cmp #$2b
    beq lc_window_d_classified
    inc result
    cmp f:$FFD800
    beq lc_window_d_classified
    inc result
    ldy #$e2
    sty anomaly_which
    jsr anomaly
lc_window_d_classified:
    lda lc_e_read
    cmp #$3d
    beq lc_window_e_classified
    lda result
    ora #4
    sta result
    lda lc_e_read
    cmp f:$FFE800
    beq lc_window_e_classified
    lda result
    ora #8
    sta result
    lda lc_e_read
    ldy #$e3
    sty anomaly_which
    jsr anomaly
lc_window_e_classified:
    lda lc_d_result
    cmp #$96
    bne lc_window_not_d
    lda result
    ora #$10
    sta result
lc_window_not_d:
    lda lc_e_result
    cmp #$96
    bne lc_window_done
    lda result
    ora #$20
    sta result
lc_window_done:
    rts
