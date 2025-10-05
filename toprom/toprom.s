    opt h-      ; no header
    opt f+      ; fill

    KIL = 2     ; opcode to trap emulator

    BASIC = $b800       ; basic310hi.rom

    OSFIND = $FFCE
    OSBPUT = $FFD4
    OSBGET = $FFD7
    OSARGS = $FFDA
    OSFILE = $FFDD
    OSRDCH = $FFE0
    OSASCI = $FFE3
    OSNEWL = $FFE7
    OSWRCH = $FFEE
    OSWORD = $FFF1
    OSBYTE = $FFF4
    OS_CLI = $FFF7

    BRKV   = $0202
    WRCHV  = $020E

    FAULT  = $FD

    .macro trap addr
        org :addr
        dta KIL
        rts
    .endm

    org $ff00

NMI:
    rti

IRQ:
    cld
    cli
    pla
    pla
    sec
    sbc #1
    sta FAULT+0
    pla
    sbc #0
    sta FAULT+1
    jmp (BRKV)

RESET:
    cld
    ldx #$ff
    txs

    mwa #OSWRCH WRCHV

    ldx #9
lp:
    lda BASIC,x
    beq done

    jsr OSWRCH

    inx
    bne lp

done:
    stx fault
    lda #>BASIC
    sta fault+1
    jsr OSNEWL

    lda #1
    jmp BASIC

    trap OSFIND 
    trap OSBPUT 
    trap OSBGET 
    trap OSARGS 
    trap OSFILE 
    trap OSRDCH 
    trap OSASCI 
    trap OSNEWL 
    trap OSWRCH 
    trap OSWORD 
    trap OSBYTE 
    trap OS_CLI 

    org $fffa

    dta a(NMI), a(RESET), a(IRQ)
