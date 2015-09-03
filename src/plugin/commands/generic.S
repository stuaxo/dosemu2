#
# (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
#
# for details see file COPYING in the DOSEMU distribution
#

#
# generic vm86 starter code for '32bit DOS code' running  via
# coopthreads pluggin.
#
# (C) Copyright 2001, Hans Lermen <lermen@fgan.de>
#

#include <doshelpers.h>
#include "config/plugin_config.h"

.code16
.text
	.globl	_start16
_start16:

#define stack_size		0x100
#define stack			(stack_start + stack_size)
#define psp_size		0x100

	jmp	start
	.align  4,0
	.word	0xd0c0		# magic
	.word	heap		# offset of heap buffer

	.align	128,0
start:
#include "detect.h"

	movw	$stack, %sp
				# first deallocate the unused memory, ES=PSP now
	movb	$0x4a, %ah
	movw	$(stack - _start16 + psp_size), %bx
	shrw	$4, %bx
	incw	%bx
	int	$0x21

	movb	$DOS_HELPER_COMMANDS, %al
	movb	$BUILTINS_PLUGIN_VERSION, %ah
	int	$DOS_HELPER_INT
				# back, thread deleted
	movb	$DOS_HELPER_COMMANDS_DONE, %al
	movb	$BUILTINS_PLUGIN_VERSION, %ah
	int	$DOS_HELPER_INT

	int	$0x20

stack_start:
				# and if we come here we have AL = exitcode
	movb	$0x4c, %ah
	int	$0x21		# exit

#ifdef TEST
	.align	16,0
called:
	movw	$DOS_HELPER_COMMANDS, %ax
	movb	$3, %ah
	int	$DOS_HELPER_INT
	ret
	.align	16,0
called2:
	lret
#endif

normal_start:			# we are started as a .com file
				# check if we really are running DOSEMU
#if 0
#include "detect.h"
#endif

	.align	16,0
heap:

#ifdef TEST
	.ascii	",0x700,1,test"
#endif
	.byte	0

heap_end:
