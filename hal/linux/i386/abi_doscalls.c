/* Derived from original 2ine; zlib-style license. See ../../../LICENSE.txt. */
/* Register argument entries for the Linux i386 worker. */
/* CSet/2 passes EAX and requires ECX preservation across the host call. */
__asm__(".hidden _DosFlatToSel\n.hidden _DosSelToFlat\n"
        ".globl DosFlatToSel  \n\t"
        ".type	DosFlatToSel, @function \n\t"
        "DosFlatToSel:  \n\t"
        "    pushl %ebx  \n\t"
        "    pushl %ecx  \n\t"
        "    pushl %edx  \n\t"
        "    pushl %edi  \n\t"
        "    pushl %esi  \n\t"
        "    pushl %eax  \n\t"
        "    call _DosFlatToSel  \n\t"
        "    addl $4, %esp  \n\t"
        "    popl %esi  \n\t"
        "    popl %edi  \n\t"
        "    popl %edx  \n\t"
        "    popl %ecx  \n\t"
        "    popl %ebx  \n\t"
        "    ret  \n\t"
        ".size	DosFlatToSel, .-DosFlatToSel  \n\t");

/* The selector argument arrives in EAX. */
__asm__(

    ".hidden _DosFlatToSel\n.hidden _DosSelToFlat\n"
    ".globl DosSelToFlat  \n\t"
    ".type	DosSelToFlat, @function \n\t"
    "DosSelToFlat:  \n\t"
    "    pushl %ecx  \n\t"
    "    pushl %edx  \n\t"
    "    pushl %eax  \n\t"
    "    call _DosSelToFlat  \n\t"
    "    addl $4, %esp  \n\t"
    "    popl %edx  \n\t"
    "    popl %ecx  \n\t"
    "    ret  \n\t"
    ".size	DosSelToFlat, .-DosSelToFlat  \n\t");
