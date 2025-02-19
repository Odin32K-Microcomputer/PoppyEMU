#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "time.h"

#ifdef NDEBUG
    /* Disable verbose and stepping mode by default in release */
    #define VERBOSE 0
    #define STEP 0
    #define WAIT_AT_BEGIN 0
#else
    #define VERBOSE 1 /* 0-3 for amount of info */
    #define STEP 0 /* 1 to enable stepping mode, 0 to disable */
    #define WAIT_AT_BEGIN 1 /* 1 to wait at the beginning, 0 to immediately start */
    #define CLOCK_SPEED 8 /* override for debugging */
#endif

/* Timing */
#ifndef CLOCK_SPEED
    #define CLOCK_SPEED 4000000 /* 4 MHz */
#endif
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
} registers;

/* Status flags */
/* https://codebase64.org/doku.php?id=base:6502_registers */
#define FLAG_CARRY      (1U << 0)
#define FLAG_ZERO       (1U << 1)
#define FLAG_IRQDISABLE (1U << 2)
#define FLAG_DECIMAL    (1U << 3)
#define FLAG_BREAK      (1U << 4)
#define FLAG_ONE        (1U << 5)
#define FLAG_OVERFLOW   (1U << 6)
#define FLAG_NEGATIVE   (1U << 7)

/* I/O */
static uint8_t readByte(uint16_t addr) {
    uint8_t ret;
    switch (addr >> 12) { /* switch case the top 4 bits (1 hex digit) */
        default: /* For unused stuff (floating) */
            ret = rand() ^ rand();
            break;
        case 0x0 ... 0x7: /* System memory */
            ret = sysram[addr];
            break;
        #if 0 /* TODO */
        case 0x8: /* I/O controller (TODO) */
            ret = 0;
            break;
        case 0x9: /* Serial 0 (TODO) */
            ret = 0;
            break;
        case 0xA: /* Serial 1 (TODO) */
            ret = 0;
            break;
        #endif
        case 0xC ... 0xD: /* ROM 1 */
            ret = rom1[addr & 0x1FFF]; /* zero out the top 3 bits of the address */
            break;
        case 0xE ... 0xF: /* ROM 0 */
            ret = rom0[addr & 0x1FFF];
            break;
    }
    waitForCycles(1); /* Reading takes 1 cycle */
    #if VERBOSE >= 3
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
    #if VERBOSE >= 3
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
        static const char flagchars[8] = {'C', 'Z', 'I', 'D', 0, 0, 'V', 'N'};
        if (!flagchars[i]) continue;
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

    /* Set up timing stuff */
    getTime(&targettime);

    /* Set up RAM and devices */
    srand(targettime.tv_nsec);
    for (unsigned i = 0; i < 32768; ++i) {
        sysram[i] = rand() & rand();
    }

    /* Read the memory address at
     * the RESET vector 0xFFFC and 0xFFFD, 0x1FFC and 0x1FFD of ROM0 */
    registers.pc = rom0[0x1FFC] | (rom0[0x1FFD] << 8); /* Read the low byte and then the high byte */

    #if VERBOSE
    fputs("I  --  ", stdout);
    printRegisters(&registers);
    #endif
    #if STEP || WAIT_AT_BEGIN
    fputs("--- Press ENTER to begin ---", stdout);
    fflush(stdout);
    while (getchar() != '\n') {}
    getTime(&targettime);
    #endif

    /* Begin reading instructions */
    /* Timing references: https://www.nesdev.org/6502_cpu.txt, https://www.masswerk.at/6502/6502_instruction_set.html */
    while (true) {
        #if VERBOSE == 1
        printf("X  --  $%04X: ", registers.pc);
        #elif VERBOSE > 1
        fputs("X  --  ", stdout);
        #endif
        uint8_t ins1 = readByte(registers.pc++);

        switch (ins1) {
            /* TRANSFER */
            case 0xA2: { /* LOAD X REGISTER, IMMEDIATE */
                uint8_t ins2 = readByte(registers.pc++); /* Read in the byte and inc the program counter */
                #if VERBOSE
                printf("LDX #$%02X\n", ins2);
                #endif
                ucodeSetZNFlags(ins2, &registers.p);
                registers.x = ins2;
            } break;
            case 0xAA: { /* TRANSFER ACCUMULATOR TO X REGISTER, IMPLIED */
                #if VERBOSE
                puts("TAX");
                #endif
                readByte(registers.pc);
                registers.x = registers.a;
            } break;
            case 0xA8: { /* TRANSFER ACCUMULATOR TO Y REGISTER, IMPLIED */
                #if VERBOSE
                puts("TAY");
                #endif
                readByte(registers.pc);
                registers.y = registers.a;
            } break;
            case 0xBA: { /* TRANSFER STACK POINTER TO X REGISTER, IMPLIED */
                #if VERBOSE
                puts("TSX");
                #endif
                readByte(registers.pc);
                registers.x = registers.sp;
            } break;
            case 0x8A: { /* TRANSFER X REGISTER TO ACCUMULATOR, IMPLIED */
                #if VERBOSE
                puts("TXA");
                #endif
                readByte(registers.pc);
                registers.a = registers.x;
            } break;
            case 0x9A: { /* TRANSFER X REGISTER TO STACK POINTER, IMPLIED */
                #if VERBOSE
                puts("TXS");
                #endif
                readByte(registers.pc);
                registers.sp = registers.x;
            } break;
            case 0x98: { /* TRANSFER Y REGISTER TO ACCUMULATOR, IMPLIED */
                #if VERBOSE
                puts("TYA");
                #endif
                readByte(registers.pc);
                registers.a = registers.y;
            } break;

            /* STACK */
            case 0x48: {
                #if VERBOSE
                puts("PHA");
                #endif
                readByte(registers.pc);
                ucodePush(&registers.sp, registers.a);
            } break;
            case 0x08: {
                #if VERBOSE
                puts("PHP");
                #endif
                readByte(registers.pc);
                ucodePush(&registers.sp, registers.p | FLAG_BREAK | FLAG_ONE);
            } break;
            case 0x68: {
                #if VERBOSE
                puts("PLA");
                #endif
                readByte(registers.pc);
                readByte(0x0100 | registers.sp);
                registers.a = ucodePop(&registers.sp);
            } break;
            case 0x28: {
                #if VERBOSE
                puts("PLP");
                #endif
                readByte(registers.pc);
                readByte(0x0100 | registers.sp);
                registers.p = ucodePop(&registers.sp);
            } break;

            /* INC & DEC */
            case 0xE6: { /* INCREMENT MEMORY, ZEROPAGE */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("INC $%02X\n", ins2);
                #endif
                readByte(ins2);
                uint8_t value = readByte(ins2);
                ++value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins2, value);
            } break;
            case 0xF6: { /* INCREMENT MEMORY, ZEROPAGE,X */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("INC $%02X,X\n", ins2);
                #endif
                readByte(ins2);
                readByte(ins2);
                ins2 += registers.x;
                uint8_t value = readByte(ins2);
                ++value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins2, value);
            } break;
            case 0xEE: { /* INCREMENT MEMORY, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("INC $%04X\n", ins23);
                #endif
                readByte(ins23);
                uint8_t value = readByte(ins23);
                ++value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins23, value);
            } break;
            case 0xFE: { /* INCREMENT MEMORY, ABSOLUTE,X */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("INC $%04X,X\n", ins23);
                #endif
                readByte(ins23);
                uint16_t ins23x = ins23 + registers.x;
                if ((ins23x & 0xFF00) != (ins23 & 0xFF00)) readByte(registers.pc);
                uint8_t value = readByte(ins23x);
                ++value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins23x, value);
            } break;
            case 0xE8: { /* INCREMENT X REGISTER, IMPLIED */
                #if VERBOSE
                puts("INX");
                #endif
                readByte(registers.pc);
                ++registers.x;
                ucodeSetZNFlags(registers.x, &registers.p);
            } break;
            case 0xC8: { /* INCREMENT Y REGISTER, IMPLIED */
                #if VERBOSE
                puts("INY");
                #endif
                readByte(registers.pc);
                ++registers.y;
                ucodeSetZNFlags(registers.y, &registers.p);
            } break;
            case 0xC6: { /* DECREMENT MEMORY, ZEROPAGE */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("DEC $%02X\n", ins2);
                #endif
                readByte(ins2);
                uint8_t value = readByte(ins2);
                --value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins2, value);
            } break;
            case 0xD6: { /* DECREMENT MEMORY, ZEROPAGE,X */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("DEC $%02X,X\n", ins2);
                #endif
                readByte(ins2);
                readByte(ins2);
                ins2 += registers.x;
                uint8_t value = readByte(ins2);
                --value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins2, value);
            } break;
            case 0xCE: { /* DECREMENT MEMORY, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("DEC $%04X\n", ins23);
                #endif
                readByte(ins23);
                uint8_t value = readByte(ins23);
                --value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins23, value);
            } break;
            case 0xDE: { /* DECREMENT MEMORY, ABSOLUTE,X */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("DEC $%04X,X\n", ins23);
                #endif
                readByte(ins23);
                uint16_t ins23x = ins23 + registers.x;
                if ((ins23x & 0xFF00) != (ins23 & 0xFF00)) readByte(registers.pc);
                uint8_t value = readByte(ins23x);
                --value;
                ucodeSetZNFlags(value, &registers.p);
                writeByte(ins23x, value);
            } break;
            case 0xCA: { /* DECREMENT X REGISTER, IMPLIED */
                #if VERBOSE
                puts("DEX");
                #endif
                readByte(registers.pc);
                --registers.x;
                ucodeSetZNFlags(registers.x, &registers.p);
            } break;
            case 0x88: { /* DECREMENT Y REGISTER, IMPLIED */
                #if VERBOSE
                puts("DEY");
                #endif
                readByte(registers.pc);
                --registers.y;
                ucodeSetZNFlags(registers.y, &registers.p);
            } break;

            /* ARITHMETIC */
            case 0x69: { /* ADD WITH CARRY TO ACCUMULATOR, IMMEDIATE */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("ADC #$%02X\n", ins2);
                #endif
                registers.a = ucodeAddWithCarry(registers.a, ins2, &registers.p);
            } break;
            case 0x65: { /* ADD WITH CARRY TO ACCUMULATOR, ZEROPAGE */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("ADC $%02X\n", ins2);
                #endif
                registers.a = ucodeAddWithCarry(registers.a, readByte(ins2), &registers.p);
            } break;
            case 0x75: { /* ADD WITH CARRY TO ACCUMULATOR, ZEROPAGE,X */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("ADC $%02X,X\n", ins2);
                #endif
                readByte(ins2);
                ins2 += registers.x;
                registers.a = ucodeAddWithCarry(registers.a, readByte(ins2), &registers.p);
            } break;
            case 0x6D: { /* ADD WITH CARRY TO ACCUMULATOR, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("ADC $%04X\n", ins23);
                #endif
                registers.a = ucodeAddWithCarry(registers.a, readByte(ins23), &registers.p);
            } break;
            case 0x7D: { /* ADD WITH CARRY TO ACCUMULATOR, ABSOLUTE,X */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("ADC $%04X,X\n", ins23);
                #endif
                uint16_t ins23x = ins23 + registers.x;
                if ((ins23x & 0xFF00) != (ins23 & 0xFF00)) readByte(registers.pc);
                ++registers.pc;
                registers.a = ucodeAddWithCarry(registers.a, readByte(ins23x), &registers.p);
            } break;
            case 0x79: { /* ADD WITH CARRY TO ACCUMULATOR, ABSOLUTE,Y */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("ADC $%04X,Y\n", ins23);
                #endif
                uint16_t ins23y = ins23 + registers.y;
                if ((ins23y & 0xFF00) != (ins23 & 0xFF00)) readByte(registers.pc);
                ++registers.pc;
                registers.a = ucodeAddWithCarry(registers.a, readByte(ins23y), &registers.p);
            } break;
            case 0x61: { /* ADD WITH CARRY TO ACCUMULATOR, (INDIRECT,X) */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("ADC ($%02X,X)\n", ins2);
                #endif
                readByte(ins2);
                ins2 += registers.x;
                uint16_t addr = readByte(ins2++);
                addr |= (uint16_t)readByte(ins2) << 8;
                registers.a = ucodeAddWithCarry(registers.a, readByte(addr), &registers.p);
            } break;
            case 0x71: { /* ADD WITH CARRY TO ACCUMULATOR, (INDIRECT),Y */
                uint8_t ins2 = readByte(registers.pc);
                #if VERBOSE
                printf("ADC ($%02X),Y\n", ins2);
                #endif
                uint16_t addr = readByte(ins2++);
                addr |= (uint16_t)readByte(ins2) << 8;
                uint16_t addry = addr + registers.y;
                if ((addry & 0xFF00) != (addr & 0xFF00)) readByte(registers.pc);
                ++registers.pc;
                registers.a = ucodeAddWithCarry(registers.a, readByte(addry), &registers.p);
            } break;
            case 0x72: { /* ADD WITH CARRY TO ACCUMULATOR, (ZEROPAGE) */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("ADC ($%02X)\n", ins2);
                #endif
                uint16_t addr = readByte(ins2++);
                addr |= (uint16_t)readByte(ins2) << 8;
                registers.a = ucodeAddWithCarry(registers.a, readByte(addr), &registers.p);
            } break;
            case 0xE9: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, IMMEDIATE */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("SBC #$%02X\n", ins2);
                #endif
                registers.a = ucodeSubWithCarry(registers.a, ins2, &registers.p);
            } break;
            case 0xE5: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, ZEROPAGE */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("SBC $%02X\n", ins2);
                #endif
                registers.a = ucodeSubWithCarry(registers.a, readByte(ins2), &registers.p);
            } break;
            case 0xF5: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, ZEROPAGE,X */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("SBC $%02X,X\n", ins2);
                #endif
                readByte(ins2);
                ins2 += registers.x;
                registers.a = ucodeSubWithCarry(registers.a, readByte(ins2), &registers.p);
            } break;
            case 0xED: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("SBC $%04X\n", ins23);
                #endif
                registers.a = ucodeSubWithCarry(registers.a, readByte(ins23), &registers.p);
            } break;
            case 0xFD: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, ABSOLUTE,X */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("SBC $%04X,X\n", ins23);
                #endif
                uint16_t ins23x = ins23 + registers.x;
                if ((ins23x & 0xFF00) != (ins23 & 0xFF00)) readByte(registers.pc);
                ++registers.pc;
                registers.a = ucodeSubWithCarry(registers.a, readByte(ins23x), &registers.p);
            } break;
            case 0xF9: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, ABSOLUTE,Y */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("SBC $%04X,Y\n", ins23);
                #endif
                uint16_t ins23y = ins23 + registers.y;
                if ((ins23y & 0xFF00) != (ins23 & 0xFF00)) readByte(registers.pc);
                ++registers.pc;
                registers.a = ucodeSubWithCarry(registers.a, readByte(ins23y), &registers.p);
            } break;
            case 0xE1: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, (INDIRECT,X) */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("SBC ($%02X,X)\n", ins2);
                #endif
                readByte(ins2);
                ins2 += registers.x;
                uint16_t addr = readByte(ins2++);
                addr |= (uint16_t)readByte(ins2) << 8;
                registers.a = ucodeSubWithCarry(registers.a, readByte(addr), &registers.p);
            } break;
            case 0xF1: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, (INDIRECT),Y */
                uint8_t ins2 = readByte(registers.pc);
                #if VERBOSE
                printf("SBC ($%02X),Y\n", ins2);
                #endif
                uint16_t addr = readByte(ins2++);
                addr |= (uint16_t)readByte(ins2) << 8;
                uint16_t addry = addr + registers.y;
                if ((addry & 0xFF00) != (addr & 0xFF00)) readByte(registers.pc);
                ++registers.pc;
                registers.a = ucodeSubWithCarry(registers.a, readByte(addry), &registers.p);
            } break;
            case 0xF2: { /* SUBTRACT WITH BORROW FROM ACCUMULATOR, (ZEROPAGE) */
                uint8_t ins2 = readByte(registers.pc++);
                #if VERBOSE
                printf("ADC ($%02X)\n", ins2);
                #endif
                uint16_t addr = readByte(ins2++);
                addr |= (uint16_t)readByte(ins2) << 8;
                registers.a = ucodeSubWithCarry(registers.a, readByte(addr), &registers.p);
            } break;

            /* LOGIC */

            /* SHIFT & ROTATE */

            /* FLAG */
            case 0x18: { /* CLEAR CARRY FLAG, IMPLIED */
                #if VERBOSE
                puts("CLC");
                #endif
                readByte(registers.pc);
                registers.p &= ~FLAG_CARRY;
            } break;
            case 0xD8: { /* CLEAR DECIMAL MODE FLAG, IMPLIED */
                #if VERBOSE
                puts("CLD");
                #endif
                readByte(registers.pc);
                registers.p &= ~FLAG_DECIMAL;
            } break;
            case 0x58: { /* CLEAR INTERRUPT DISABLE FLAG, IMPLIED */
                #if VERBOSE
                puts("CLI");
                #endif
                readByte(registers.pc);
                registers.p &= ~FLAG_IRQDISABLE;
            } break;
            case 0xB8: { /* CLEAR OVERFLOW FLAG, IMPLIED */
                #if VERBOSE
                puts("CLV");
                #endif
                readByte(registers.pc);
                registers.p &= ~FLAG_OVERFLOW;
            } break;
            case 0x38: { /* SET CARRY FLAG, IMPLIED */
                #if VERBOSE
                puts("SEC");
                #endif
                readByte(registers.pc);
                registers.p |= FLAG_CARRY;
            } break;
            case 0xF8: { /* SET DECIMAL MODE FLAG, IMPLIED */
                #if VERBOSE
                puts("SED");
                #endif
                readByte(registers.pc);
                registers.p |= FLAG_DECIMAL;
            } break;
            case 0x78: { /* SET INTERRUPT DISABLE FLAG, IMPLIED */
                #if VERBOSE
                puts("SEI");
                #endif
                readByte(registers.pc);
                registers.p |= FLAG_IRQDISABLE;
            } break;

            /* COMPARISONS */

            /* BRANCH */

            /* JUMPS */
            case 0x4C: { /* JUMP, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc++) << 8;
                #if VERBOSE
                printf("JMP $%04X\n", ins23);
                #endif
                registers.pc = ins23;
            } break;
            case 0x6C: { /* JUMP, (ABSOLUTE) */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("JMP ($%04X)\n", ins23);
                #endif
                readByte(ins23);
                uint16_t addr = readByte(ins23++);
                addr |= (uint16_t)readByte(ins23) << 8;
                registers.pc = addr;
            } break;
            case 0x7C: { /* JUMP, (ABSOLUTE,X) */
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("JMP ($%04X,X)\n", ins23);
                #endif
                readByte(ins23);
                ins23 += registers.x;
                uint16_t addr = readByte(ins23++);
                addr |= (uint16_t)readByte(ins23) << 8;
                registers.pc = addr;
            } break;
            case 0x20: { /* JUMP SAVING RETURN, ABSOLUTE */
                uint16_t ins23 = readByte(registers.pc++);
                readByte(0x0100 | registers.sp);
                ucodePush(&registers.sp, registers.pc >> 8);
                ucodePush(&registers.sp, registers.pc);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                #if VERBOSE
                printf("JSR $%04X\n", ins23);
                #endif
                registers.pc = ins23;
            } break;
            case 0x60: { /* RETURN FROM SUBROUTINE, IMPLIED */
                #if VERBOSE
                puts("RTS");
                #endif
                readByte(registers.pc);
                readByte(0x0100 | registers.sp);
                registers.pc = ucodePop(&registers.sp);
                registers.pc |= (uint16_t)ucodePop(&registers.sp) << 8;
                readByte(registers.pc++);
            } break;

            /* INTERRUPTS */
            case 0x00: { /* BREAK, IMPLIED */
                #if VERBOSE
                puts("BRK");
                #endif
                readByte(registers.pc++);
                ucodePush(&registers.sp, registers.pc >> 8);
                ucodePush(&registers.sp, registers.pc);
                ucodePush(&registers.sp, registers.p | FLAG_BREAK | FLAG_ONE);
                registers.pc = readByte(0xFFFE);
                registers.pc |= (uint16_t)readByte(0xFFFF) << 8;
            } break;
            case 0x40: { /* RETURN FROM INTERRUPT, IMPLIED */
                #if VERBOSE
                puts("RTI");
                #endif
                readByte(registers.pc);
                readByte(0x0100 | registers.sp);
                registers.p = ucodePop(&registers.sp);
                registers.pc = ucodePop(&registers.sp);
                registers.pc |= (uint16_t)ucodePop(&registers.sp) << 8;
            } break;

            /* OTHER */
            case 0xEA: { /* NO OPERATION */
                #if VERBOSE
                puts("NOP");
                #endif
                readByte(registers.pc);
            } break;

            /* ILLEGAL */
            default: { /* 1 BYTE, 1 CYCLE */
                #if VERBOSE
                printf("ILLEGAL 0x%02X (1 byte 1 cycle NOP)\n", ins1);
                #endif
            } break;
            case 0x02:
            case 0x22:
            case 0x42:
            case 0x62:
            case 0x82:
            case 0xC2:
            case 0xE2: { /* 2 BYTES, 2 CYCLES */
                #if VERBOSE
                printf("ILLEGAL 0x%02X (2 byte 2 cycle NOP)\n", ins1);
                #endif
                readByte(registers.pc++);
            } break;
            case 0x44: { /* 2 BYTES, 3 CYCLES */
                #if VERBOSE
                printf("ILLEGAL 0x%02X (2 byte 3 cycle NOP)\n", ins1);
                #endif
                uint8_t ins2 = readByte(registers.pc++);
                readByte(ins2);
            } break;
            case 0x54:
            case 0xD4:
            case 0xF4: { /* 2 BYTES, 4 CYCLES */
                #if VERBOSE
                printf("ILLEGAL 0x%02X (2 byte 4 cycle NOP)\n", ins1);
                #endif
                uint8_t ins2 = readByte(registers.pc++);
                readByte(ins2);
                ins2 += registers.x;
                readByte(ins2);
            } break;
            case 0xDC:
            case 0xFC: { /* 3 BYTES, 4 CYCLES */
                #if VERBOSE
                printf("ILLEGAL 0x%02X (3 byte 4 cycle NOP)\n", ins1);
                #endif
                uint16_t ins23 = readByte(registers.pc++);
                ins23 |= (uint16_t)readByte(registers.pc) << 8;
                uint16_t ins23x = ins23 + registers.x;
                ++registers.pc;
                readByte(ins23x);
            } break;
            case 0x5C: { /* 3 BYTES, 8 CYCLES */
                #if VERBOSE
                printf("ILLEGAL 0x%02X (3 byte 8 cycle NOP)\n", ins1);
                #endif
                readByte(registers.pc++);
                readByte(registers.pc++);
                waitForCycles(5);
            } break;
        }

        #if VERBOSE >= 2
        fputs(">  --  ", stdout);
        printRegisters(&registers);
        #endif
        #if STEP
        fputs("--- Press ENTER to continue ---", stdout);
        fflush(stdout);
        while (getchar() != '\n') {}
        getTime(&targettime);
        #endif
    }
    #ifndef NDEBUG
    printf("DEBUG: End execution.\n");
    #endif
}

