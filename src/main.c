#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "time.h"

#define VERBOSE 1

/* Timing */
//#define CLOCK_SPEED 4000000 /* 4 MHz */
#define CLOCK_SPEED 1 /* 1 Hz (for debugging) */
static const uint64_t clocktime = 1000000000 / CLOCK_SPEED;
static struct timespec targettime;
static inline void waitForCycles(unsigned n) {
    int64_t nanosec = clocktime * n;
    targettime.tv_nsec += nanosec % 1000000000;
    targettime.tv_sec += targettime.tv_nsec / 1000000000;
    targettime.tv_nsec %= 1000000000;
    targettime.tv_sec += nanosec / 1000000000;
    waitUntil(&targettime);
}

/* Memory Map */
static uint8_t sysram[32768]; // System memory, $0000-$7FFF
static uint8_t rom0[8192]; // ROM0, $E000-$FFFF
static uint8_t rom1[8192]; // ROM1, $C000-$DFFF

/* Registers */
static struct registers {
    uint16_t pc; // Program counter
    uint8_t sp; // Stack pointer
    uint8_t a; // Accumulator
    uint8_t x; // X register
    uint8_t y; // Y register
    uint8_t p; // Processor status
} registers = {
    .p = 0x20 // Bit 5 of the status is always 1
};

/* Status flags */
/* https://codebase64.org/doku.php?id=base:6502_registers */
#define FLAG_CARRY      (1U << 0)
#define FLAG_ZERO       (1U << 1)
#define FLAG_IRQDISABLE (1U << 2)
#define FLAG_DECIMAL    (1U << 3)
#define FLAG_BREAK      (1U << 4)
#define FLAG_OVERFLOW   (1U << 6)
#define FLAG_NEGATIVE   (1U << 7)

/* I/O */
static uint8_t readByte(uint16_t addr) {
    uint8_t ret;
    switch (addr >> 12) { /* switch case the top 4 bits (1 hex digit) */
        default: /* For unused stuff */
            ret = 0;
            break;
        case 0x0 ... 0x7: /* System memory */
            ret = sysram[addr];
            break;
        case 0x8: /* I/O controller (TODO) */
            ret = 0;
            break;
        case 0x9: /* Serial 0 (TODO) */
            ret = 0;
            break;
        case 0xA: /* Serial 1 (TODO) */
            ret = 0;
            break;
        case 0xC ... 0xD: /* ROM 1 */
            ret = rom1[addr & 0x1FFF]; /* zero out the top 3 bits of the address */
            break;
        case 0xE ... 0xF: /* ROM 0 */
            ret = rom0[addr & 0x1FFF];
            break;
    }
    waitForCycles(1); /* Reading takes 1 cycle */
    #if VERBOSE
    printf("R  --  0x%04X: 0x%02X\n", addr, ret);
    #endif
    return ret;
}
static void writeByte(uint16_t addr, uint8_t value) {
    switch (addr >> 12) { /* switch case the top 4 bits (1 hex digit) */
        case 0x0 ... 0x7: /* System memory */
            sysram[addr] = value;
            break;
        case 0x8: /* I/O controller (TODO) */
            break;
        case 0x9: /* Serial 0 (TODO) */
            break;
        case 0xA: /* Serial 1 (TODO) */
            break;
        case 0xC ... 0xD: /* ROM 1 */
            break;
        case 0xE ... 0xF: /* ROM 0 */
            break;
    }
    waitForCycles(1); /* Writing takes 1 cycle */
    #if VERBOSE
    printf("W  --  0x%04X: 0x%02X\n", addr, value);
    #endif
}

/* Microcode */
static inline void ucodeSetZNFlags(uint8_t value, uint8_t* flags) {
    if (value) {
        /* The value is non-zero so mask out the zero flag and do a negative check */
        *flags &= ~FLAG_ZERO;
        if (value & 0x80) { /* Check if bit 7 is 1 */
            *flags |= FLAG_NEGATIVE;
        } else {
            *flags &= ~FLAG_NEGATIVE;
        }
    } else {
        /* The value is zero and cannot be negative so mask out the negative flag */
        *flags |= FLAG_ZERO;
        *flags &= ~FLAG_NEGATIVE;
    }
}
/* https://www.masswerk.at/6502/6502_instruction_set.html#arithmetic */
/* https://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */
static inline uint8_t ucodeAddWithCarry(uint8_t a, uint8_t b, uint8_t* flags) {
    uint16_t result = a + b + ((*flags & FLAG_CARRY) != 0);
    ucodeSetZNFlags(result, &registers.p);
    if (result & 0x100) *flags |= FLAG_CARRY;
    else *flags &= ~FLAG_CARRY;
    if ((a ^ result) & (b ^ result) & 0x80) *flags |= FLAG_OVERFLOW;
    else *flags &= ~FLAG_OVERFLOW;
    return result;
}
static inline uint8_t ucodeSubWithCarry(uint8_t a, uint8_t b, uint8_t* flags) {
    b = 255 - b;
    uint16_t result = a + b + ((*flags & FLAG_CARRY) != 0);
    ucodeSetZNFlags(result, &registers.p);
    if (result & 0x100) *flags |= FLAG_CARRY;
    else *flags &= ~FLAG_CARRY;
    if ((a ^ result) & (b ^ result) & 0x80) *flags |= FLAG_OVERFLOW;
    else *flags &= ~FLAG_OVERFLOW;
    return result;
}
static inline void ucodePush(uint8_t* sp, uint8_t value) {
    writeByte(0x0100 | *sp, value);
    --*sp;
}
static inline uint8_t ucodePop(uint8_t* sp) {
    ++*sp;
    return readByte(0x0100 | *sp);
}

/* --- */

static inline void printRegisters(struct registers* regs) {
    printf(
        "PC: 0x%04X  SP: 0x%02X  -  A: 0x%02X  X: 0x%02X  Y: 0x%02X  -  P:",
        regs->pc, regs->sp, regs->a, regs->x, regs->y
    );
    for (register int i = 7; i >= 0; --i) {
        const char flagchars[8] = {'C', 'Z', 'I', 'D', 'B', '1', 'V', 'N'};
        putchar(' ');
        putchar(flagchars[i]);
        putchar(':');
        putchar('0' + ((regs->p >> i) & 1));
    }
    putchar('\n');
};

static void displayHelp(char* argv0) {
    printf("Usage: %s ROM0 [ROM1]\n", argv0);
}

int main(int argc, char** argv) {
    puts("PoppyEMU - A research emulator for the Odin32K.");

    if (argc < 2 || argc > 3) {
        /* Show help if too many or too little arguments were given */
        displayHelp(argv[0]); /* argv[0] contains the name used to call the program */
        return 1;
    } else {
        /* Read in ROM0 */
        FILE* fp = fopen(argv[1], "rb");
        if (!fp) { /* Throw an error if file open failed */
            fprintf(stderr, "Failed to open '%s' for ROM0: %s\n", argv[1], strerror(errno));
            return 1;
        }
        fread(rom0, 1, 8192, fp);
        fclose(fp);

        if (argc == 3) {
            /* Read in ROM1 if given */
            fp = fopen(argv[1], "rb");
            if (!fp) {
                fprintf(stderr, "Failed to open '%s' for ROM0: %s\n", argv[1], strerror(errno));
                return 1;
            }
            fread(rom1, 1, 8192, fp);
            fclose(fp);
        }
    }

    /* Read the memory address at
     * the RESET vector 0xFFFC and 0xFFFD, 0x1FFC and 0x1FFD of ROM0 */
    registers.pc = rom0[0x1FFC] | (rom0[0x1FFD] << 8); /* Read the low byte and then the high byte */

    #if VERBOSE
    fputs("I  --  ", stdout);
    printRegisters(&registers);
    #endif
    
    /* Set up timing stuff */
    getTime(&targettime);

    /* Begin reading instructions */ 
    while (true) {
        uint8_t ins1 = readByte(registers.pc++);

        switch (ins1) {
            default: { /* Illegal instruction */
                --registers.pc; /* Put the program counter back to where it was */
                fprintf(stderr, "? (Illegal instruction: 0x%02X)\n", ins1);
                printRegisters(&registers);
                goto endexec; /* Crash */
            }
            case 0xEA: { /* NO OPERATION */
                #if VERBOSE
                puts("X  --  NOP");
                #endif
                waitForCycles(1);
            } break;

            /* TRANSFER */
            case 0xA2: { /* LOAD X REGISTER, IMMEDIATE */
                uint8_t ins2 = readByte(registers.pc++); /* Read in the byte and inc the program counter */
                #if VERBOSE
                printf("X  --  LDX #$%02X\n", ins2);
                #endif
                ucodeSetZNFlags(ins2, &registers.p);
                registers.x = ins2;
            } break;

            /* STACK */

            /* INC & DEC */
            case 0xE8: { /* INCREMENT X REGISTER, IMPLIED */
                #if VERBOSE
                puts("X  --  INX");
                #endif
                ++registers.x;
                ucodeSetZNFlags(registers.x, &registers.p);
                waitForCycles(1);
            } break;
            case 0xC8: { /* INCREMENT Y REGISTER, IMPLIED */
                #if VERBOSE
                puts("X  --  INY");
                #endif
                ++registers.y;
                ucodeSetZNFlags(registers.y, &registers.p);
                waitForCycles(1);
            } break;
            case 0xCA: { /* DECREMENT X REGISTER, IMPLIED */
                #if VERBOSE
                puts("X  --  DEX");
                #endif
                --registers.x;
                ucodeSetZNFlags(registers.x, &registers.p);
                waitForCycles(1);
            } break;
            case 0x88: { /* DECREMENT Y REGISTER, IMPLIED */
                #if VERBOSE
                puts("X  --  DEY");
                #endif
                --registers.y;
                ucodeSetZNFlags(registers.y, &registers.p);
                waitForCycles(1);
            } break;

            /* ARITHMETIC */

            /* LOGIC */

            /* SHIFT & ROTATE */

            /* FLAG */

            /* COMPARISONS */

            /* BRANCH */

            /* JUMPS */
            case 0x4C: { /* JUMP, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("X  --  JMP $%04X\n", ins23);
                #endif
                registers.pc = ins23;
            } break;
            case 0x20: { /* JUMP SAVING RETURN, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("X  --  JSR $%04X\n", ins23);
                #endif
                ucodePush(&registers.sp, registers.pc >> 8);
                ucodePush(&registers.sp, registers.pc & 0xFF);
                waitForCycles(1);
            } break;
            case 0x60: { /* RETURN FROM SUBROUTINE, IMPLIED */
                puts("X  --  RTS");
                registers.pc = ucodePop(&registers.sp);
                registers.pc |= (uint16_t)ucodePop(&registers.sp) << 8;
                waitForCycles(3);
            } break;

            /* INTERRUPTS */

            /* OTHER */

            /* CUSTOM */
            case 0x02: { /* UNUSED, USING AS CUSTOM EMULATOR HALT INSTRUCTION */
                #if VERBOSE
                puts("X  --  (CUSTOM) HALT");
                #endif
                goto endexec;
            }
        }

        #if VERBOSE
        fputs(">  --  ", stdout);
        printRegisters(&registers);
        #endif
    }
    endexec:;
    #ifndef NDEBUG
    printf("DEBUG: End execution.\n");
    #endif
}

