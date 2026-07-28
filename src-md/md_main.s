| ---------------------------------------------------------------------------
| God of Thunder 32X - Genesis (68000) side support code
|
| Responsibilities:
|   * bring the MD hardware up to a known state (VDP off, Z80 halted, pads init)
|   * hand control of the Mars (32X) hardware to the SH2s
|   * every vblank, poll both controller ports and publish the button state
|     into a shared mailbox the SH2 can read without a handshake
|
| The SH2 reads the pad state from the COMM registers:
|   COMM8  (0xA15128 / 0x20004028) = controller 1 state, 0xF000 == absent
|   COMM10 (0xA1512A / 0x2000402A) = controller 2 state
|   COMM12 (0xA1512C / 0x2000402C) = frame counter, incremented each vblank
|
| Assembled to 0x880800 (ROM window) / runs with data in 0xFF0000 work RAM.
| ---------------------------------------------------------------------------

| The ROM header's 68000 vector table (built into crt0.s) dispatches to fixed
| addresses inside this blob, which is linked at 0x880800:
|     0x880800  reset / cold start
|     0x880840  generic exception (ignore and return)
|     0x880880  level 4  - HBlank
|     0x8808C0  level 6  - VBlank
|     0x880900  level 2  - external
| The pad_* stubs below are placed with .org so those offsets always line up.

        .text
        .global _start

        .equ MARS_ADAPTER,   0xA15100
        .equ MARS_INTCTL,    0xA15102
        .equ MARS_BANK,      0xA15104
        .equ MARS_RV,        0xA15107
        .equ MARS_COMM0,     0xA15120
        .equ MARS_COMM2,     0xA15122
        .equ MARS_COMM8,     0xA15128
        .equ MARS_COMM10,    0xA1512A
        .equ MARS_COMM12,    0xA1512C

        .equ VDP_DATA,       0xC00000
        .equ VDP_CTRL,       0xC00004

        .equ PAD1_DATA,      0xA10003
        .equ PAD2_DATA,      0xA10005
        .equ PAD1_CTRL,      0xA10009
        .equ PAD2_CTRL,      0xA1000B

        .org    0x0000
_start:
        bra.w   cold_start

        .org    0x0040                  /* 0x880840 - generic exception */
exc_generic:
        rte

        .org    0x0080                  /* 0x880880 - level 4, HBlank */
exc_hblank:
        rte

        .org    0x00C0                  /* 0x8808C0 - level 6, VBlank */
exc_vblank:
        bra.w   vert_blank

        .org    0x0100                  /* 0x880900 - level 2, external */
exc_extint:
        rte

        .org    0x0140
cold_start:
        move.w  #0x2700,sr              /* disable interrupts */

        bsr     init_hardware

        /* Let the SH2s out of the gate and wait for both to report in. */
        move.w  #0,MARS_BANK            /* cart bank 0 */
        move.b  #0,MARS_RV              /* clear RV: SH2 may access ROM */

1:      cmp.l   #0x4D5F4F4B,MARS_COMM0  /* 'M_OK' from master SH2 */
        bne.b   1b
2:      cmp.l   #0x535F4F4B,MARS_COMM0+4 /* 'S_OK' from slave SH2 */
        bne.b   2b

        /* Give the SH2 side ownership of the Mars hardware. */
        move.w  MARS_ADAPTER,d0
        or.w    #0x8000,d0              /* FM = 1 */
        move.w  d0,MARS_ADAPTER
        move.l  #0,MARS_COMM0           /* release master SH2 */

        /* Probe which ports actually have hardware attached. */
        bsr     chk_ports

        move.w  #0x8174,VDP_CTRL        /* display on, vblank IRQ enabled */
        move.w  #0x2000,sr              /* enable interrupts */

main_loop:
        stop    #0x2000                 /* idle until the next interrupt */
        bra.b   main_loop


| ---------------------------------------------------------------------------
| Vertical blank handler: sample both pads into the SH2 mailbox.
| ---------------------------------------------------------------------------
vert_blank:
        movem.l d0-d3/a0-a1,-(sp)

        lea     PAD1_DATA,a0
        bsr     read_pad
        move.w  d0,MARS_COMM8

        lea     PAD2_DATA,a0
        bsr     read_pad
        move.w  d0,MARS_COMM10

        addq.w  #1,MARS_COMM12          /* frame heartbeat for the SH2 */

        movem.l (sp)+,d0-d3/a0-a1
        rte


| ---------------------------------------------------------------------------
| read_pad: a0 -> port data register.
|
| This is Chilly Willy's / d32xr's get_pad sequence rather than a hand-rolled
| one: the Genesis 3/6-button handshake is fiddly (the D-pad and the
| A/Start bits arrive in different TH phases, and the 6-button extras only
| appear on the third TH falling edge), and an earlier bespoke version here
| silently dropped the B button.
|
| Returns d0 = 0 0 0 1 M X Y Z S A C B R L D U  (active high),
|      or 0xF000 when nothing is plugged in.
| ---------------------------------------------------------------------------
read_pad:
        bsr.b   get_input       /* - 0 s a 0 0 d u - 1 c b r l d u */
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   rp_no_pad
        bsr.b   get_input
        bsr.b   get_input       /* - 0 s a 0 0 0 0 - 1 c b m x y z */
        move.w  d0,d2
        bsr.b   get_input       /* - 0 s a 1 1 1 1 - 1 c b r l d u */
        andi.w  #0x0F00,d0
        cmpi.w  #0x0F00,d0
        beq.b   rp_common       /* six button pad */
        move.w  #0x010F,d2      /* three button pad */
rp_common:
        lsl.b   #4,d2
        lsl.w   #4,d2
        andi.w  #0x303F,d1
        move.b  d1,d2
        lsr.w   #6,d1
        or.w    d1,d2
        eori.w  #0x1FFF,d2      /* 0 0 0 1 M X Y Z S A C B R L D U */
        move.w  d2,d0
        rts

rp_no_pad:
        move.w  #0xF000,d0
        rts

| Read one TH phase pair: high byte = TH low, low byte = TH high.
get_input:
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d0
        move.b  #0x40,(a0)
        lsl.w   #8,d0
        move.b  (a0),d0
        rts

| ---------------------------------------------------------------------------
| chk_ports: identify attached hardware; store 0xF000 in the mailbox when a
| port is empty so the SH2 can ignore it.
| ---------------------------------------------------------------------------
chk_ports:
        lea     PAD1_DATA,a0
        bsr.b   get_port
        move.w  d0,MARS_COMM8
        lea     PAD2_DATA,a0
        bsr.b   get_port
        move.w  d0,MARS_COMM10
        rts

| Probe a port once; read_pad already reports 0xF000 for an empty port.
get_port:
        bsr     read_pad
        rts


| ---------------------------------------------------------------------------
| init_hardware
| ---------------------------------------------------------------------------
init_hardware:
        move.w  #0x8104,VDP_CTRL        /* display off, vblank disabled */
        move.w  VDP_CTRL,d0             /* clear pending status */

        /* joypad ports: TH as output, rest input */
        move.b  #0x40,PAD1_CTRL
        move.b  #0x40,PAD2_CTRL
        move.b  #0x40,PAD1_DATA
        move.b  #0x40,PAD2_DATA

        /* VDP registers */
        lea     VDP_CTRL,a0
        move.w  #0x8004,(a0)            /* no HBL int */
        move.w  #0x8114,(a0)            /* display off, DMA on, V28 */
        move.w  #0x8230,(a0)            /* plane A @ 0xC000 */
        move.w  #0x832C,(a0)            /* window  @ 0xB000 */
        move.w  #0x8407,(a0)            /* plane B @ 0xE000 */
        move.w  #0x8554,(a0)            /* sprites @ 0xA800 */
        move.w  #0x8600,(a0)
        move.w  #0x8700,(a0)            /* backdrop colour 0 */
        move.w  #0x8800,(a0)
        move.w  #0x8900,(a0)
        move.w  #0x8A01,(a0)            /* hint counter */
        move.w  #0x8B00,(a0)            /* full screen scroll */
        move.w  #0x8C81,(a0)            /* 40 cell, no shadow/hilite */
        move.w  #0x8D2B,(a0)            /* hscroll @ 0xAC00 */
        move.w  #0x8E00,(a0)
        move.w  #0x8F02,(a0)            /* autoincrement 2 */
        move.w  #0x9001,(a0)            /* 64x32 map */
        move.w  #0x9100,(a0)
        move.w  #0x9200,(a0)

        /* Silence the PSG. */
        lea     0xC00011,a1
        move.b  #0x9F,(a1)
        move.b  #0xBF,(a1)
        move.b  #0xDF,(a1)
        move.b  #0xFF,(a1)

        /* Halt the Z80 and leave it stopped: the 32X drives audio later. */
        move.w  #0x0100,0xA11100        /* request Z80 bus */
        move.w  #0x0100,0xA11200        /* release Z80 reset */
1:      btst    #0,0xA11100
        bne.b   1b
        move.w  #0x0000,0xA11200        /* assert reset */
        move.w  #0x0000,0xA11100        /* release bus */

        rts


