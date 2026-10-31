/* Register argument entries for the Linux i386 worker. */
// DosFlatToSel() passes its argument in %eax, so a little asm to bridge that...
// Note that this isn't syscall calling conventions at all; CSet/2 generates
//  thunking code that expects this function to preserve %ecx, so we'll just
//  go ahead and save a bunch of stuff, just in case.
__asm__ (
    ".hidden _DosFlatToSel\n.hidden _DosSelToFlat\n"
    ".globl DosFlatToSel  \n\t"
    ".type	DosFlatToSel, @function \n\t"
    "DosFlatToSel:  \n\t"
    "    pushl %ebx  \n\t"  // save off a bunch of stuff. Better safe than sorry.
    "    pushl %ecx  \n\t"
    "    pushl %edx  \n\t"
    "    pushl %edi  \n\t"
    "    pushl %esi  \n\t"
    "    pushl %eax  \n\t"  // the actual argument to DosFlatToSel.
    "    call _DosFlatToSel  \n\t"
    "    addl $4, %esp  \n\t"  // clear argument to DosFlatToSel.
    "    popl %esi  \n\t"
    "    popl %edi  \n\t"
    "    popl %edx  \n\t"  // save off a bunch of stuff.
    "    popl %ecx  \n\t"
    "    popl %ebx  \n\t"
    "    ret  \n\t"
	".size	DosFlatToSel, .-DosFlatToSel  \n\t"
);

// DosSelToFlat() passes its argument in %eax, so a little asm to bridge that...
__asm__ (
    // !!! FIXME: this needs to protect more registers.
    ".hidden _DosFlatToSel\n.hidden _DosSelToFlat\n"
    ".globl DosSelToFlat  \n\t"
    ".type	DosSelToFlat, @function \n\t"
    "DosSelToFlat:  \n\t"
    "    pushl %eax  \n\t"
    "    call _DosSelToFlat  \n\t"
    "    addl $4, %esp  \n\t"
    "    ret  \n\t"
	".size	DosSelToFlat, .-DosSelToFlat  \n\t"
);
