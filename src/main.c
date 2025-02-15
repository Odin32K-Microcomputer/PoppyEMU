#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

/* Memory Map */
uint8_t sysram[32768]; // System memory, $0000-$7FFF
uint8_t rom0[8192]; // ROM0, $E000-$FFFF
uint8_t rom1[8192]; // ROM1, $C000-$DFFF
uint16_t programCounter;
uint8_t xreg;
uint8_t yreg;
uint8_t areg;

static uint8_t readByte(uint16_t addr) {
    /* TODO: use up 1 cycle */

    uint8_t ret; /* byte to return */
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

    // TODO: reserved for cycle code too

    return ret;
}

/* Display help */
static void displayHelp(char* argv0) {
    printf("Usage: %s ROM0 [ROM1]", argv0);
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

    programCounter = rom0[0x1FFC] | (rom0[0x1FFD] << 8); /* Read the low byte and then the high byte */
    
    printf("ProgramCounter initialized at 0x%02X\n", programCounter);
    
    /* Begin reading instructions */ 
    while (true) {
        uint8_t ins1 = readByte(programCounter);
        printf("Current Byte: 0x%02X\tPC: 0x%02X\n", ins1, programCounter);
        ++programCounter;
    }
    #ifndef NDEBUG
    printf("DEBUG: End execution.\n");
    #endif
}

