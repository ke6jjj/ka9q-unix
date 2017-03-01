/* Poll keyboard through BIOS. Returns ascii char in low byte, scan code
 * in high byte. If low byte == 0, character is "extended ascii"
 */
	.global	_kbraw
_kbraw:

	movb	$1,%ah
	int	$0x16
	je	L0
	movb	$0,%ah
	int	$0x16
	andl	$0x0000ffff,%eax
	ret
L0:	xorl	%eax,%eax
	ret
