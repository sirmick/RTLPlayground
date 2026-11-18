;--------------------------------------------------------
; File Created by SDCC : free open source ISO C Compiler
; Version 4.6.0 #16555 (Linux)
;--------------------------------------------------------
	.module spill
	
	.optsdcc -mmcs51 --model-small
;--------------------------------------------------------
; Public variables in this module
;--------------------------------------------------------
	.globl _spilling_step
	.globl _step_helper
	.globl _spilling_step_PARM_2
;--------------------------------------------------------
; special function registers
;--------------------------------------------------------
	.area RSEG    (ABS,DATA)
	.org 0x0000
;--------------------------------------------------------
; special function bits
;--------------------------------------------------------
	.area RSEG    (ABS,DATA)
	.org 0x0000
;--------------------------------------------------------
; overlayable register banks
;--------------------------------------------------------
	.area REG_BANK_0	(REL,OVR,DATA)
	.ds 8
;--------------------------------------------------------
; internal ram data
;--------------------------------------------------------
	.area DSEG    (DATA)
_spilling_step_PARM_2:
	.ds 2
_spilling_step_a_10000_2:
	.ds 2
_spilling_step_x_10000_3:
	.ds 4
;--------------------------------------------------------
; overlayable items in internal ram
;--------------------------------------------------------
;--------------------------------------------------------
; indirectly addressable internal ram data
;--------------------------------------------------------
	.area ISEG    (DATA)
;--------------------------------------------------------
; absolute internal ram data
;--------------------------------------------------------
	.area IABS    (ABS,DATA)
	.area IABS    (ABS,DATA)
;--------------------------------------------------------
; bit data
;--------------------------------------------------------
	.area BSEG    (BIT)
;--------------------------------------------------------
; paged external ram data
;--------------------------------------------------------
	.area PSEG    (PAG,XDATA)
;--------------------------------------------------------
; uninitialized external ram data
;--------------------------------------------------------
	.area XSEG    (XDATA)
;--------------------------------------------------------
; absolute external ram data
;--------------------------------------------------------
	.area XABS    (ABS,XDATA)
;--------------------------------------------------------
; initialized external ram data
;--------------------------------------------------------
	.area XISEG   (XDATA)
	.area HOME    (CODE)
	.area GSINIT0 (CODE)
	.area GSINIT1 (CODE)
	.area GSINIT2 (CODE)
	.area GSINIT3 (CODE)
	.area GSINIT4 (CODE)
	.area GSINIT5 (CODE)
	.area GSINIT  (CODE)
	.area GSFINAL (CODE)
	.area CSEG    (CODE)
;--------------------------------------------------------
; global & static initialisations
;--------------------------------------------------------
	.area HOME    (CODE)
	.area GSINIT  (CODE)
	.area GSFINAL (CODE)
	.area GSINIT  (CODE)
;--------------------------------------------------------
; Home
;--------------------------------------------------------
	.area HOME    (CODE)
	.area HOME    (CODE)
;--------------------------------------------------------
; code
;--------------------------------------------------------
	.area CSEG    (CODE)
;------------------------------------------------------------
;Allocation info for local variables in function 'spilling_step'
;------------------------------------------------------------
;b             Allocated with name '_spilling_step_PARM_2'
;a             Allocated with name '_spilling_step_a_10000_2'
;x             Allocated with name '_spilling_step_x_10000_3'
;y             Allocated to registers r0 r1 r4 r5 
;------------------------------------------------------------
;	spill.c:6: uint16_t spilling_step(uint16_t a, uint16_t b)
;	-----------------------------------------
;	 function spilling_step
;	-----------------------------------------
_spilling_step:
	ar7 = 0x07
	ar6 = 0x06
	ar5 = 0x05
	ar4 = 0x04
	ar3 = 0x03
	ar2 = 0x02
	ar1 = 0x01
	ar0 = 0x00
	mov	_spilling_step_a_10000_2,dpl
	mov	(_spilling_step_a_10000_2 + 1),dph
;	spill.c:8: uint32_t x = (uint32_t)a * 300;
	mov	__mullong_PARM_2,_spilling_step_a_10000_2
	mov	(__mullong_PARM_2 + 1),(_spilling_step_a_10000_2 + 1)
	mov	(__mullong_PARM_2 + 2),#0x00
	mov	(__mullong_PARM_2 + 3),#0x00
;	spill.c:9: uint32_t y = (uint32_t)b * 7;
	mov	dptr,#0x012c
	clr	a
	mov	b,a
	lcall	__mullong
	mov	_spilling_step_x_10000_3,dpl
	mov	(_spilling_step_x_10000_3 + 1),dph
	mov	(_spilling_step_x_10000_3 + 2),b
	mov	(_spilling_step_x_10000_3 + 3),a
	mov	__mullong_PARM_2,_spilling_step_PARM_2
	mov	(__mullong_PARM_2 + 1),(_spilling_step_PARM_2 + 1)
	mov	(__mullong_PARM_2 + 2),#0x00
	mov	(__mullong_PARM_2 + 3),#0x00
;	spill.c:10: step_helper();
	mov	dptr,#0x0007
	clr	a
	mov	b,a
	lcall	__mullong
	mov	r0, dpl
	mov	r1, dph
	mov	r4, b
	mov	r5, a
	push	ar5
	push	ar4
	push	ar1
	push	ar0
	lcall	_step_helper
	pop	ar0
	pop	ar1
	pop	ar4
	pop	ar5
;	spill.c:11: x += y;
	mov	a,r0
	add	a, _spilling_step_x_10000_3
	mov	r2,a
	mov	a,r1
	addc	a, (_spilling_step_x_10000_3 + 1)
	mov	r3,a
	mov	a,r4
	addc	a, (_spilling_step_x_10000_3 + 2)
	mov	r6,a
	mov	a,r5
	addc	a, (_spilling_step_x_10000_3 + 3)
	mov	r7,a
;	spill.c:12: step_helper();
	push	ar7
	push	ar6
	push	ar5
	push	ar4
	push	ar3
	push	ar2
	push	ar1
	push	ar0
	lcall	_step_helper
	pop	ar0
	pop	ar1
	pop	ar2
	pop	ar3
	pop	ar4
	pop	ar5
	pop	ar6
	pop	ar7
;	spill.c:13: return (uint16_t)(x >> 4) + (uint16_t)y + a;
	mov	a,r3
	swap	a
	xch	a,r2
	swap	a
	anl	a,#0x0f
	xrl	a,r2
	xch	a,r2
	anl	a,#0x0f
	xch	a,r2
	xrl	a,r2
	xch	a,r2
	mov	r3,a
	mov	a,r6
	swap	a
	anl	a,#0xf0
	orl	a,r3
	mov	r3,a
	mov	a,r7
	swap	a
	xch	a,r6
	swap	a
	anl	a,#0x0f
	xrl	a,r6
	xch	a,r6
	anl	a,#0x0f
	xch	a,r6
	xrl	a,r6
	xch	a,r6
	mov	a,r0
	add	a, r2
	mov	r2,a
	mov	a,r1
	addc	a, r3
	mov	r3,a
	mov	a,_spilling_step_a_10000_2
	add	a, r2
	mov	dpl,a
	mov	a,(_spilling_step_a_10000_2 + 1)
	addc	a, r3
	mov	dph,a
;	spill.c:14: }
	ret
	.area CSEG    (CODE)
	.area CONST   (CODE)
	.area XINIT   (CODE)
	.area CABS    (ABS,CODE)
