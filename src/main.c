#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "time.h"

/* Timing */
//#define CLOCK_SPEED 4000000 /* 4 MHz */
#define CLOCK_SPEED 4 /* 4 Hz (for debugging) */
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
static struct {
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

static uint8_t readByte(uint16_t addr) {
    waitForCycles(1); /* Reading takes 1 cycle */
    switch (addr >> 12) { /* switch case the top 4 bits (1 hex digit) */
        default: /* For unused stuff */
            return 0;
        case 0x0 ... 0x7: /* System memory */
            return sysram[addr];
        case 0x8: /* I/O controller (TODO) */
            return 0;
        case 0x9: /* Serial 0 (TODO) */
            return 0;
        case 0xA: /* Serial 1 (TODO) */
            return 0;
        case 0xC ... 0xD: /* ROM 1 */
            return rom1[addr & 0x1FFF]; /* zero out the top 3 bits of the address */
        case 0xE ... 0xF: /* ROM 0 */
            return rom0[addr & 0x1FFF];
    }
}

static inline void doZNFlagCheck(uint8_t value) {
    if (value) {
        /* The value is non-zero so mask out the zero flag and do a negative check */
        registers.p &= ~FLAG_ZERO;
        if (value & 0x80) { /* Check if bit 7 is 1 */
            registers.p |= FLAG_NEGATIVE;
        } else {
            registers.p &= ~FLAG_NEGATIVE;
        }
    } else {
        /* The value is zero and cannot be negative so mask out the negative flag */
        registers.p |= FLAG_ZERO;
        registers.p &= ~FLAG_NEGATIVE;
    }
}

/* Display help */
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
    
    printf("Program counter initialized at 0x%02X\n", registers.pc);
    
    /* Set up timing stuff */
    getTime(&targettime);

    /* Begin reading instructions */ 
    while (true) {
        uint8_t ins1 = readByte(registers.pc);
        printf(
            "Current Byte: 0x%02X  -  PC: 0x%04X  SP: 0x%02X  -  A: 0x%02X  X: 0x%02X  Y: 0x%02X  -  P:",
            ins1, registers.pc, registers.sp, registers.a, registers.x, registers.y
        );
        for (register int i = 7; i >= 0; --i) {
            const char flagchars[8] = {'C', 'Z', 'I', 'D', 'B', '1', 'V', 'N'};
            putchar(' ');
            putchar(flagchars[i]);
            putchar(':');
            putchar('0' + ((registers.p >> i) & 1));
        }
        putchar('\n');
        ++registers.pc;

        switch (ins1) {
            case 0xEA: /* NO OPERATION */
                waitForCycles(1);
                break;
            case 0xA2: /* LOAD X REGISTER, IMMEDIATE */
                uint8_t ins2 = readByte(registers.pc++); /* Read in the byte and inc the program counter */
                doZNFlagCheck(ins2);
                registers.x = ins2;
                break;
            case 0x02: /* UNUSED, USING AS CUSTOM EMULATOR HALT INSTRUCTION */
                goto endexec;
            default: /* Illegal instruction */
                fprintf(stderr, "Illegal instruction: 0x%02X\n", ins1);
                goto endexec; /* Crash */
        }
    }
    endexec:;
    #ifndef NDEBUG
    printf("DEBUG: End execution.\n");
    #endif
}

