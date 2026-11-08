; Original, media-free 65C816 profiling workload. Load at $02:2000.
; ca65 defines: VOICES=0..32, ANIMATE=0|1, SHR640=0|1.
.setcpu "65816"
.segment "CODE"
.a8
.i8
start:
    sei
    clc
    xce
    rep #$30
.a16
.i16
    ldx #$1fff
    txs
    lda #0
    tcd
    sep #$20
.a8
    lda #0
    pha
    plb
    lda #$80
    sta $c036                  ; fast clock, no mounted disks
    lda #$3f
    sta $c035                  ; direct E1 video writes, disable shadow copies
    stz $c034                  ; black border
    lda #$c1
    sta $c029                  ; Super Hi-Res
    ; Fill all visible video bytes with a repeating palette-index pattern.
    ldx #0
fill_video:
    txa
    sta f:$e12000,x
    inx
    cpx #$7d00
    bne fill_video
    ; One palette, all scanlines. 640 mode selects four groups within it.
    ldx #0
    lda #SHR640*$80
scanlines:
    sta f:$e19d00,x
    inx
    cpx #200
    bne scanlines
    ldx #0
palette:
    lda f:palette_data+$020000,x
    sta f:$e19e00,x
    inx
    cpx #32
    bne palette
    ; Halt every DOC oscillator before filling sound RAM.
    lda #$07
    sta $c03c
    stz $c03f
.repeat 32, I
    lda #$a0+I
    sta $c03e
    lda #1
    sta $c03d
.endrepeat
    ; Original nonzero triangle: never hits the DOC zero-byte terminator.
    lda #$67
    sta $c03c
    stz $c03e
    stz $c03f
    ldx #0
wave_copy:
    lda f:wave+$020000,x
    sta $c03d
    inx
    cpx #256
    bne wave_copy
    lda #$07
    sta $c03c
.macro DOC_WRITE reg, value
    lda #reg
    sta $c03e
    lda #value
    sta $c03d
.endmacro
    ; Keep the scan denominator fixed at 32 enabled oscillators in all cases.
    DOC_WRITE $e1, $3e
.repeat 32, I
    DOC_WRITE $00+I, <($1400+I*97)
    DOC_WRITE $20+I, >($1400+I*97)
    DOC_WRITE $40+I, 4           ; low-volume tone for profiling
    DOC_WRITE $80+I, 0
    DOC_WRITE $c0+I, 0          ; 256-byte table, resolution 1
.if I < VOICES
    DOC_WRITE $a0+I, (I&1)*$10 ; free-running, alternating output channels
.endif
.endrepeat
    ; Record real GLU reads in ordinary RAM: debugger peeks avoid I/O.
    ldx #0
snapshot:
    txa
    ora #$a0
    sta $c03e
    lda $c03d
    lda $c03d
    sta f:$023f00,x
    txa
    ora #$40
    sta $c03e
    lda $c03d
    lda $c03d
    sta f:$023f40,x
    inx
    cpx #32
    bne snapshot
    lda #$e1
    sta $c03e
    lda $c03d
    lda $c03d
    sta f:$023f20
    lda #$47
    sta $c03c
    stz $c03f
    ldx #0
snapshot_wave:
    txa
    sta $c03e
    lda $c03d
    lda $c03d
    sta f:$023f80,x
    inx
    cpx #16
    bne snapshot_wave
    lda #7
    sta $c03c
    lda #$a5
    sta f:$023ff0              ; init sentinel, outside fixture code
    ldx #0
main:
.if ANIMATE
    lda f:$e12000,x
    clc
    adc #1
    sta f:$e12000,x
    inx
    cpx #$7d00
    bne main
    ldx #0
.endif
    bra main
palette_data:
.repeat 16, I
    .word (((I*3)&15)<<8)|(((I*5)&15)<<4)|((I*7)&15)
.endrepeat
wave:
.repeat 128, I
    .byte 1+I*2
.endrepeat
.repeat 128, I
    .byte 255-I*2
.endrepeat
