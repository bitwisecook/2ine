/* Native Linux i386 execution adapter. See ../../../LICENSE.txt for licensing. */
/* Guest faults the backend emulates: thread-local FS accesses, flat selector loads and fatal SIGSEGV reports. */
#define _GNU_SOURCE 1
#include "emulator/execution/native_i386/internal.h"
#include "emulator/execution/core/x86/x86.h"

enum
{
    /* OS/2 TLS: 32 words after the TIB. */
    TLS_SLOTS = 32,
    /* OS/2's fixed flat data and TIB GDT selectors. */
    OS2_FLAT_DATA_SELECTOR = 0x53,
    OS2_TIB_SELECTOR = 0x150b,
    X86_VECTOR_GENERAL_PROTECTION = 13,
    /* OF, SF, ZF, AF, PF and CF. */
    X86_EFLAGS_ARITHMETIC = 0x8D5,

    /* ModRM fields */
    MODRM_MOD_MASK = 0xC0,
    MODRM_MOD_REGISTER = 0xC0,
    MODRM_REG_MASK = 0x38,
    MODRM_REG_SHIFT = 3,
    MODRM_RM_MASK = 7,
    MODRM_RM_SIB = 4,
    MODRM_RM_DISP32 = 5,
    /* The /7 extension of the 0x81 and 0x83 groups is CMP. */
    MODRM_REG_CMP = 0x38,

    OPCODE_MOV_STORE_IMM32 = 0xC7,
    OPCODE_MOV_STORE = 0x89,
    OPCODE_MOV_LOAD = 0x8B,
    OPCODE_GROUP1_IMM32 = 0x81,
    OPCODE_GROUP1_IMM8 = 0x83,
    OPCODE_POP_ES = 0x07,
    OPCODE_POP_DS = 0x1f,
    OPCODE_MOV_SREG = 0x8e,
    SREG_ES = 0,
    SREG_DS = 3,
    SREG_FS = 4
};

static void handleThreadLocalStorageAccess(const int slot, ucontext_t *uctx)
{
    greg_t *gregs = uctx->uc_mcontext.gregs;

    // Read FS from the faulting thread to locate its guest TIB2.
    void *ptib2 = NULL;
    __asm__ __volatile__("pushw %%fs            \n\t"
                         "movw %%ax, %%fs       \n\t"
                         "movl %%fs:0xC, %%eax  \n\t"
                         "popw %%fs             \n\t"
                         : "=a"(ptib2)
                         : "eax"(gregs[REG_FS]));

    /* The 32 TLS words follow TIB/TIB2 in guest memory. */
    uint32 *tls =
        (uint32 *)((uint8 *)ptib2 + Native->services.tls_offset - Native->services.tib2_offset);
    tls += slot;

    static const int x86RegisterToUContextEnum[] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX,
                                                    REG_ESP, REG_EBP, REG_ESI, REG_EDI};

    int handled = 1;
    uint8 *eip = (void *)(size_t)gregs[REG_EIP];
    const uint8 operand = eip[1];
    /* Only a plain [reg] operand can address the faulting TLS word directly. */
    if ((operand & MODRM_MOD_MASK) || ((operand & MODRM_RM_MASK) == MODRM_RM_SIB) ||
        ((operand & MODRM_RM_MASK) == MODRM_RM_DISP32))
    {
        fprintf(stderr, "Unsupported TLS memory addressing at %p\n", eip);
        abort();
    }
    switch (eip[0])
    {
        case OPCODE_MOV_STORE_IMM32: /* MOV immediate to TLS memory. */
            if (operand & MODRM_REG_MASK)
            {
                handled = 0;
                break;
            }
            *tls = *((uint32 *)(eip + 2));
            gregs[REG_EIP] += 6;
            break;

        case OPCODE_MOV_STORE: /* MOV register to TLS memory. */
            *tls = (uint32)
                gregs[x86RegisterToUContextEnum[(operand & MODRM_REG_MASK) >> MODRM_REG_SHIFT]];
            gregs[REG_EIP] += 2;
            break;

        case OPCODE_MOV_LOAD: /* MOV TLS memory to register. */
            gregs[x86RegisterToUContextEnum[(operand & MODRM_REG_MASK) >> MODRM_REG_SHIFT]] =
                (greg_t)*tls;
            gregs[REG_EIP] += 2;
            break;

        case OPCODE_GROUP1_IMM32:
        case OPCODE_GROUP1_IMM8: /* CMP TLS memory with a full or sign-extended immediate. */
        {
            if ((operand & MODRM_REG_MASK) != MODRM_REG_CMP)
            {
                handled = 0;
                break;
            }
            uint32 immediate, flags;
            if (*eip == OPCODE_GROUP1_IMM8)
                immediate = (uint32)(int32_t)(int8_t)eip[2];
            else
                memcpy(&immediate, eip + 2, 4);
            __asm__ __volatile__("cmpl %2, %1; pushfl; popl %0"
                                 : "=r"(flags)
                                 : "r"(*tls), "r"(immediate)
                                 : "cc");
            /* CMP changes OF, SF, ZF, AF, PF and CF; preserve all control flags. */
            gregs[REG_EFL] =
                (gregs[REG_EFL] & ~X86_EFLAGS_ARITHMETIC) | (flags & X86_EFLAGS_ARITHMETIC);
            gregs[REG_EIP] += *eip == OPCODE_GROUP1_IMM8 ? 3 : 6;
            break;
        }

        default:
            handled = 0;
            break;
    }

    if (!handled)
    {
        fprintf(stderr, "Oh no, unhandled opcode 0x%X at %p accessing TLS register! File a bug!\n",
                (uint)eip[0], eip);
    }
    else
    {
        /* The signal return restores both guest registers and arithmetic flags. */
        return;
    }

    fflush(stderr);
    abort();
}

/* Resolve a fault address to its guest module/object for diagnostics. */
static int describeModuleAddress(const void *ptr, char *buffer, size_t size)
{
    if (!Native->services.describe_address)
    {
        *buffer = '\0';
        return 0;
    }
    return Native->services.describe_address(Native->services.userdata, (uint32)(uintptr_t)ptr,
                                             buffer, size);
}

/* OS/2's fixed GDT selectors cannot be installed by an unprivileged Linux task.
   Translate data and TIB selector loads to their host equivalents. */
static int handleFlatDataSelector(ucontext_t *context)
{
    greg_t *registers = context->uc_mcontext.gregs;
    if (registers[REG_TRAPNO] != X86_VECTOR_GENERAL_PROTECTION)
        return 0;
    uint16_t cs = registers[REG_CS], ss = registers[REG_SS];
    int code16 = (cs & X86_SELECTOR_LDT) != 0, stack16 = (ss & X86_SELECTOR_LDT) != 0;
    uint32_t code_base = code16 ? Native->ldt[cs >> X86_SELECTOR_INDEX_SHIFT] : 0;
    uint32_t stack_base = stack16 ? Native->ldt[ss >> X86_SELECTOR_INDEX_SHIFT] : 0;
    if ((code16 && !code_base) || (stack16 && !stack_base) ||
        (!code16 && cs != Native->original_cs))
        return 0;
    uint32_t ip = code16 ? (uint16_t)registers[REG_EIP] : registers[REG_EIP];
    const uint8_t *instruction = (const uint8_t *)(uintptr_t)(code_base + ip);
    uint32_t bytes = 1, selector;
    int destination;
    if (*instruction == OPCODE_POP_DS || *instruction == OPCODE_POP_ES)
    {
        uint32_t sp = stack16 ? (uint16_t)registers[REG_ESP] : registers[REG_ESP];
        if (stack16 && sp > 0xfffe)
            return 0;
        uint16_t word;
        memcpy(&word, (const void *)(uintptr_t)(stack_base + sp), sizeof word);
        selector = word;
        destination = *instruction == OPCODE_POP_DS ? REG_DS : REG_ES;
        if (selector != OS2_FLAT_DATA_SELECTOR)
            return 0;
        uint32_t step = code16 ? 2 : 4;
        registers[REG_ESP] =
            stack16 ? (registers[REG_ESP] & ~0xffffu) | (uint16_t)(sp + step) : sp + step;
    }
    else if (*instruction == OPCODE_MOV_SREG &&
             (instruction[1] & MODRM_MOD_MASK) == MODRM_MOD_REGISTER)
    {
        unsigned segment = (instruction[1] & MODRM_REG_MASK) >> MODRM_REG_SHIFT;
        if (segment != SREG_ES && segment != SREG_DS && segment != SREG_FS)
            return 0;
        static const int source[] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX,
                                     REG_ESP, REG_EBP, REG_ESI, REG_EDI};
        selector = (uint16_t)registers[source[instruction[1] & MODRM_RM_MASK]];
        if (selector != (segment == SREG_FS ? OS2_TIB_SELECTOR : OS2_FLAT_DATA_SELECTOR))
            return 0;
        destination = segment == SREG_FS ? REG_FS : segment == SREG_DS ? REG_DS : REG_ES;
        bytes = 2;
    }
    else
        return 0;
    registers[destination] =
        destination == REG_FS
            ? (Native->tib_gdt_entry << X86_SELECTOR_INDEX_SHIFT) | X86_SELECTOR_RPL_MASK
            : Native->original_ds;
    registers[REG_EIP] = code16 ? (uint16_t)(ip + bytes) : ip + bytes;
    return 1;
}

static void segfault_catcher(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uctx = (ucontext_t *)ctx;
    if (handleFlatDataSelector(uctx))
        return;
    const uint32 *addr = (const uint32 *)info->si_addr;
    const uint32 *tlspage = (const uint32 *)(uintptr_t)Native->execution->tls_address;
    greg_t *gregs = uctx->uc_mcontext.gregs;

    if (tlspage && (addr >= tlspage))
    {
        // was the app accessing one of the OS/2 TLS slots?
        const int slot = (int)(addr - tlspage);
        if (slot < TLS_SLOTS)
        {
            handleThreadLocalStorageAccess(slot, uctx);
            return;
        }
    }

    static int faults = 0;
    faults++;
    switch (faults)
    {
        case 1:
        {
            char eipmod[512], addrmod[512];
            const void *eip = (const void *)(size_t)gregs[REG_EIP];
            describeModuleAddress(eip, eipmod, sizeof(eipmod));
            describeModuleAddress(addr, addrmod, sizeof(addrmod));
            if (!*eipmod)
            {
                char line[512];
                lx_host_memory_mapping(eip, line, sizeof(line));
                snprintf(eipmod, sizeof(eipmod), "%s", *line ? line : "unmapped");
            }
            if (!*addrmod)
            {
                char line[512];
                lx_host_memory_mapping(addr, line, sizeof(line));
                snprintf(addrmod, sizeof(addrmod), "%s", *line ? line : "unmapped");
            }
            fprintf(stderr,
                    "SIGSEGV at %p (%s) eip=%p (%s) eax=%08X ebx=%08X "
                    "ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X\n",
                    addr, (*addrmod) ? addrmod : "unmapped", eip, (*eipmod) ? eipmod : "unmapped",
                    (uint)gregs[REG_EAX], (uint)gregs[REG_EBX], (uint)gregs[REG_ECX],
                    (uint)gregs[REG_EDX], (uint)gregs[REG_ESI], (uint)gregs[REG_EDI],
                    (uint)gregs[REG_EBP], (uint)gregs[REG_ESP]);
            {
                char line[512];
                lx_host_memory_mapping(eip, line, sizeof(line));
                if (*line)
                {
                    const uint8 *bytes = (const uint8 *)(size_t)eip;
                    fprintf(stderr, "  eip bytes:");
                    for (int i = 0; i < 16; i++)
                        fprintf(stderr, " %02X", bytes[i]);
                    fprintf(stderr, "\n");
                }
            }
            {
                const uint32 *stack = (const uint32 *)(size_t)gregs[REG_ESP];
                fprintf(stderr, "  near-stack:");
                for (int i = -8; i < 8; i++)
                    fprintf(stderr, " [%08X]=%08X", (uint)(stack + i), (uint)stack[i]);
                fprintf(stderr, "\n");
            }
            {
                const uint32 *stack = (const uint32 *)(size_t)gregs[REG_ESP];
                fprintf(stderr, "  stack:");
                for (int i = 0; i < 12; i++)
                {
                    char mod[320];
                    describeModuleAddress((const void *)(size_t)stack[i], mod, sizeof(mod));
                    fprintf(stderr, " [%08X%s%s]", (uint)stack[i], (*mod) ? "=" : "", mod);
                }
                fprintf(stderr, "\n");
            }
            break;
        }
        case 2:
            write(2, "SIGSEGV, aborting.\n", 19);
            break;
        default:
            break;
    }

    abort();
}

int nativeInstallSignalHandlers(void)
{
    struct sigaction action;

    memset(&action, '\0', sizeof(action));
    action.sa_sigaction = segfault_catcher;
    action.sa_flags = SA_NODEFER | SA_SIGINFO | SA_ONSTACK;

    if (sigaction(SIGSEGV, &action, &Native->previous_segv) == -1)
    {
        fprintf(stderr, "Couldn't install SIGSEGV handler! (%s)\n", strerror(errno));
        return 0;
    }

    return 1;
}
