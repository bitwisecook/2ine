/* Public domain. No rights reserved. Wholly AI-authored. */
/* MSGSEG32 supplies one hidden word before seven caller-owned arguments.
   Copy all eight into a C frame, then remove only the hidden word on return. */
__asm__(
    ".text\n"
    ".hidden lx_message_get\n"
    ".globl DosTrueGetMessage\n"
    ".type DosTrueGetMessage, @function\n"
    "DosTrueGetMessage:\n"
    "pushl %ebp\n"
    "movl %esp, %ebp\n"
    "andl $-16, %esp\n"
    "pushl 36(%ebp)\n"
    "pushl 32(%ebp)\n"
    "pushl 28(%ebp)\n"
    "pushl 24(%ebp)\n"
    "pushl 20(%ebp)\n"
    "pushl 16(%ebp)\n"
    "pushl 12(%ebp)\n"
    "pushl 8(%ebp)\n"
    "call lx_message_get\n"
    "leave\n"
    "ret $4\n"
    ".size DosTrueGetMessage, .-DosTrueGetMessage\n"
);
