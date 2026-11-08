# Public domain. No rights reserved. Wholly AI-authored.
# OS/2 THK16_UNITHUNK guest entry. EAX is the flat target; EDX describes
# up to fourteen arguments, low nibble count then two bits per argument:
# 0=ULONG, 1=tiled far pointer, 2=USHORT, 3=SHORT. The next type is the result.
# The caller uses 16-bit Pascal order; the target uses 32-bit caller cleanup.
# Descriptor extension records (count 15) trap explicitly.
.section .rodata
.code16
.global twoine_unithunk_start, twoine_unithunk_end
.global twoine_unithunk_data, twoine_unithunk_flat_address
.global twoine_unithunk_flat_cs, twoine_unithunk_return_cs, twoine_unithunk_flat
 twoine_unithunk_start:
 pushl %ebp
 movzwl %sp,%ebp
 pushw %ss
 pushw %ds
 pushw %es
 pushw %fs
 pushl %ebx
 pushl %esi
 pushl %edi
 pushl %edx
 pushl %eax
 subw $8,%sp
 xorl %eax,%eax
 movw %ss,%ax
 shrl $3,%eax
 shll $16,%eax
 movzwl %sp,%edx
 orl %eax,%edx
 orl %eax,%ebp
 .byte 0xb8
 twoine_unithunk_data: .word 0
 movw %ax,%ds
 movw %ax,%es
 movw %ax,%ss
 movl %edx,%esp
 .byte 0x66,0xea
 twoine_unithunk_flat_address: .long 0
 twoine_unithunk_flat_cs: .word 0
.code32
 twoine_unithunk_flat:
 leal 8(%ebp),%esi
 movl -24(%ebp),%edx
 movl %edx,%ecx
 andl $15,%ecx
 shrl $4,%edx
 cmpl $15,%ecx
 je unsupported
 testl %ecx,%ecx
 jz call_target
next_argument:
 movl %edx,%edi
 andl $3,%edi
 shrl $2,%edx
 cmpl $2,%edi
 jae short_argument
 movl (%esi),%eax
 addl $4,%esi
 testl %edi,%edi
 jz push_argument
 # Translate an OS/2 tiled selector:offset, preserving a null pointer.
 movzwl %ax,%edi
 shrl $3,%eax
 andl $0x1fff0000,%eax
 orl %edi,%eax
 jmp push_argument
short_argument:
 movzwl (%esi),%eax
 addl $2,%esi
 cmpl $3,%edi
 jne push_argument
 cwtl
push_argument:
 pushl %eax
 loop next_argument
call_target:
 # EBX is callee-preserved and retains the result-format descriptor.
 movl %edx,%ebx
 call *-28(%ebp)
 andl $3,%ebx
 cmpl $1,%ebx
 jne result_ready
 movzwl %ax,%edi
 shll $3,%eax
 andl $0xfff80000,%eax
 testl %eax,%eax
 jz null_or_low_pointer
 orl $0x70000,%eax
null_or_low_pointer:
 orl %edi,%eax
result_ready:
 movl %eax,%edx
 shrl $16,%edx
 # Move the far return address past the consumed Pascal arguments.
 leal -8(%esi),%ecx
 subl %ebp,%ecx
 movl 4(%ebp),%edi
 movl %edi,4(%ebp,%ecx)
 leal 4(%ebp,%ecx),%ecx
 andl $0xffff,%ecx
 movl %ecx,-36(%ebp)
 movw -2(%ebp),%cx
 movw %cx,-32(%ebp)
 movw -4(%ebp),%ds
 movw -6(%ebp),%es
 movw -8(%ebp),%fs
 movl -12(%ebp),%ebx
 movl -16(%ebp),%esi
 movl -20(%ebp),%edi
 leal -36(%ebp),%ecx
 movl (%ebp),%ebp
 lssl %ss:(%ecx),%esp
 .byte 0x66,0xea
 .word finish-twoine_unithunk_start
 twoine_unithunk_return_cs: .word 0
unsupported:
 int3
.code16
finish:
 lret
.global twoine_unithunk_sel_to_flat
twoine_unithunk_sel_to_flat:
 pushl %ecx
 movzwl %ax,%ecx
 shrl $3,%eax
 andl $0x1fff0000,%eax
 orl %ecx,%eax
 popl %ecx
 lret
 twoine_unithunk_end:
.section .note.GNU-stack,"",@progbits
