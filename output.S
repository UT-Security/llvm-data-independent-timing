
playground/interproc_ops.hardened.o:	file format mach-o arm64

Disassembly of section __TEXT,__text:

0000000000000000 <ltmp0>:
       0: d65f03c0     	ret

0000000000000004 <_caller_simple>:
       4: d5033fdf     	isb
       8: d65f03c0     	ret

000000000000000c <_add_one>:
       c: d5033fdf     	isb
      10: 11000400     	add	w0, w0, #0x1
      14: d65f03c0     	ret

0000000000000018 <_double_it>:
      18: d5033fdf     	isb
      1c: 531f7800     	lsl	w0, w0, #1
      20: d65f03c0     	ret

0000000000000024 <_caller_chain>:
      24: a9bf7bfd     	stp	x29, x30, [sp, #-0x10]!
      28: 910003fd     	mov	x29, sp
      2c: d5033fdf     	isb
      30: 94000000     	bl	0x30 <_caller_chain+0xc>
      34: d5033fdf     	isb
      38: a8c17bfd     	ldp	x29, x30, [sp], #0x10
      3c: d5033fdf     	isb
      40: 14000000     	b	0x40 <_caller_chain+0x1c>

0000000000000044 <_write_to_ptr>:
      44: d5033fdf     	isb
      48: b9000001     	str	w1, [x0]
      4c: d5033fdf     	isb
      50: d65f03c0     	ret

0000000000000054 <_caller_ptr>:
      54: d10083ff     	sub	sp, sp, #0x20
      58: a9017bfd     	stp	x29, x30, [sp, #0x10]
      5c: 910043fd     	add	x29, sp, #0x10
      60: d5033fdf     	isb
      64: aa0003e1     	mov	x1, x0
      68: d5033fdf     	isb
      6c: d10013a0     	sub	x0, x29, #0x4
      70: d5033fdf     	isb
      74: 94000000     	bl	0x74 <_caller_ptr+0x20>
      78: b85fc3a0     	ldur	w0, [x29, #-0x4]
      7c: d5033fdf     	isb
      80: a9417bfd     	ldp	x29, x30, [sp, #0x10]
      84: 910083ff     	add	sp, sp, #0x20
      88: d5033fdf     	isb
      8c: d65f03c0     	ret

0000000000000090 <_ignore_arg>:
      90: aa0103e0     	mov	x0, x1
      94: d65f03c0     	ret

0000000000000098 <_caller_clean>:
      98: aa0103e0     	mov	x0, x1
      9c: d65f03c0     	ret

00000000000000a0 <_apply>:
      a0: aa0003e2     	mov	x2, x0
      a4: d5033fdf     	isb
      a8: aa0103e0     	mov	x0, x1
      ac: d61f0040     	br	x2

00000000000000b0 <_caller_indirect>:
      b0: d5033fdf     	isb
      b4: aa0003e1     	mov	x1, x0
      b8: d5033fdf     	isb
      bc: 90000000     	adrp	x0, 0x0 <ltmp0>
      c0: 91000000     	add	x0, x0, #0x0
      c4: d5033fdf     	isb
      c8: 14000000     	b	0xc8 <_caller_indirect+0x18>

00000000000000cc <_caller_external>:
      cc: d5033fdf     	isb
      d0: 14000000     	b	0xd0 <_caller_external+0x4>

00000000000000d4 <_factorial>:
      d4: d5033fdf     	isb
      d8: 7100081f     	cmp	w0, #0x2
      dc: d5033fdf     	isb
      e0: 5400006a     	b.ge	0xec <_factorial+0x18>
      e4: 52800028     	mov	w8, #0x1                ; =1
      e8: 1400006a     	b	0x290 <_factorial+0x1bc>
      ec: d5033fdf     	isb
      f0: 7100141f     	cmp	w0, #0x5
      f4: d5033fdf     	isb
      f8: 54000062     	b.hs	0x104 <_factorial+0x30>
      fc: 52800028     	mov	w8, #0x1                ; =1
     100: 1400005e     	b	0x278 <_factorial+0x1a4>
     104: d5033fdf     	isb
     108: 51000409     	sub	w9, w0, #0x1
     10c: d5033fdf     	isb
     110: 9000000a     	adrp	x10, 0x0 <ltmp0>
     114: d5033fdf     	isb
     118: 7100441f     	cmp	w0, #0x11
     11c: d5033fdf     	isb
     120: 54000122     	b.hs	0x144 <_factorial+0x70>
     124: d5033fdf     	isb
     128: 5280000b     	mov	w11, #0x0               ; =0
     12c: d5033fdf     	isb
     130: 52800028     	mov	w8, #0x1                ; =1
     134: d5033fdf     	isb
     138: aa0003ec     	mov	x12, x0
     13c: d5033fdf     	isb
     140: 14000032     	b	0x208 <_factorial+0x134>
     144: d5033fdf     	isb
     148: 4e040c00     	dup.4s	v0, w0
     14c: 3dc00141     	ldr	q1, [x10]
     150: 4ea18401     	add.4s	v1, v0, v1
     154: d5033fdf     	isb
     158: 4f000420     	movi.4s	v0, #0x1
     15c: d5033fdf     	isb
     160: 121e052d     	and	w13, w9, #0xc
     164: d5033fdf     	isb
     168: 6f000462     	mvni.4s	v2, #0x3
     16c: d5033fdf     	isb
     170: 121c6d2b     	and	w11, w9, #0xfffffff0
     174: 4b0b000c     	sub	w12, w0, w11
     178: d5033fdf     	isb
     17c: 6f0004e3     	mvni.4s	v3, #0x7
     180: 6f000564     	mvni.4s	v4, #0xb
     184: 6f0005e5     	mvni.4s	v5, #0xf
     188: d5033fdf     	isb
     18c: 121c6d28     	and	w8, w9, #0xfffffff0
     190: d5033fdf     	isb
     194: 4f000426     	movi.4s	v6, #0x1
     198: 4f000427     	movi.4s	v7, #0x1
     19c: 4f000430     	movi.4s	v16, #0x1
     1a0: d5033fdf     	isb
     1a4: 4ea28431     	add.4s	v17, v1, v2
     1a8: 4ea38432     	add.4s	v18, v1, v3
     1ac: 4ea48433     	add.4s	v19, v1, v4
     1b0: 4ea09c20     	mul.4s	v0, v1, v0
     1b4: 4ea69e26     	mul.4s	v6, v17, v6
     1b8: 4ea79e47     	mul.4s	v7, v18, v7
     1bc: 4eb09e70     	mul.4s	v16, v19, v16
     1c0: 4ea58421     	add.4s	v1, v1, v5
     1c4: 71004108     	subs	w8, w8, #0x10
     1c8: d5033fdf     	isb
     1cc: 54fffea1     	b.ne	0x1a0 <_factorial+0xcc>
     1d0: d5033fdf     	isb
     1d4: 4ea09cc0     	mul.4s	v0, v6, v0
     1d8: 4ea09ce0     	mul.4s	v0, v7, v0
     1dc: 4ea09e00     	mul.4s	v0, v16, v0
     1e0: 6e004001     	ext.16b	v1, v0, v0, #0x8
     1e4: 0ea19c00     	mul.2s	v0, v0, v1
     1e8: 1e260008     	fmov	w8, s0
     1ec: 0e0c3c0e     	mov.s	w14, v0[1]
     1f0: 1b0e7d08     	mul	w8, w8, w14
     1f4: 6b0b013f     	cmp	w9, w11
     1f8: d5033fdf     	isb
     1fc: 540004a0     	b.eq	0x290 <_factorial+0x1bc>
     200: d5033fdf     	isb
     204: 340004cd     	cbz	w13, 0x29c <_factorial+0x1c8>
     208: d5033fdf     	isb
     20c: 121e752d     	and	w13, w9, #0xfffffffc
     210: 4b0d0000     	sub	w0, w0, w13
     214: d5033fdf     	isb
     218: 4f000420     	movi.4s	v0, #0x1
     21c: d5033fdf     	isb
     220: 4e041d00     	mov.s	v0[0], w8
     224: 4e040d81     	dup.4s	v1, w12
     228: 3dc00142     	ldr	q2, [x10]
     22c: 4ea28421     	add.4s	v1, v1, v2
     230: 4b0d0168     	sub	w8, w11, w13
     234: d5033fdf     	isb
     238: 6f000462     	mvni.4s	v2, #0x3
     23c: d5033fdf     	isb
     240: 4ea09c20     	mul.4s	v0, v1, v0
     244: 4ea28421     	add.4s	v1, v1, v2
     248: 31001108     	adds	w8, w8, #0x4
     24c: d5033fdf     	isb
     250: 54ffff61     	b.ne	0x23c <_factorial+0x168>
     254: d5033fdf     	isb
     258: 6e004001     	ext.16b	v1, v0, v0, #0x8
     25c: 0ea19c00     	mul.2s	v0, v0, v1
     260: 0e0c3c08     	mov.s	w8, v0[1]
     264: 1e26000a     	fmov	w10, s0
     268: 1b087d48     	mul	w8, w10, w8
     26c: 6b0d013f     	cmp	w9, w13
     270: d5033fdf     	isb
     274: 540000e0     	b.eq	0x290 <_factorial+0x1bc>
     278: d5033fdf     	isb
     27c: 1b087c08     	mul	w8, w0, w8
     280: 7100081f     	cmp	w0, #0x2
     284: 51000400     	sub	w0, w0, #0x1
     288: d5033fdf     	isb
     28c: 54ffff68     	b.hi	0x278 <_factorial+0x1a4>
     290: d5033fdf     	isb
     294: aa0803e0     	mov	x0, x8
     298: d65f03c0     	ret
     29c: d5033fdf     	isb
     2a0: aa0c03e0     	mov	x0, x12
     2a4: d5033fdf     	isb
     2a8: 17fffff4     	b	0x278 <_factorial+0x1a4>

00000000000002ac <_caller_recursive>:
     2ac: d5033fdf     	isb
     2b0: 14000000     	b	0x2b0 <_caller_recursive+0x4>

00000000000002b4 <_main>:
     2b4: a9bf7bfd     	stp	x29, x30, [sp, #-0x10]!
     2b8: 910003fd     	mov	x29, sp
     2bc: 528006e0     	mov	w0, #0x37               ; =55
     2c0: 94000000     	bl	0x2c0 <_main+0xc>
     2c4: 528000a0     	mov	w0, #0x5                ; =5
     2c8: 94000000     	bl	0x2c8 <_main+0x14>
     2cc: 52800000     	mov	w0, #0x0                ; =0
     2d0: a8c17bfd     	ldp	x29, x30, [sp], #0x10
     2d4: d65f03c0     	ret
