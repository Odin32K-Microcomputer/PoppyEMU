#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define START_ROM0 0xDFFF

/* Memory Map */
uint8_t sysram[32768]; // System memory, $0000-$7FFF
uint8_t rom0[8192]; // ROM0, $E000-$FFFF
uint16_t programCounter;
uint8_t xreg;
uint8_t yreg;
uint8_t areg;

int main() {

    /* opens the file rom.bin found in the same directory as the executable, checks if it is equal to NULL, and then reads it into an array of uint8_t*/
    printf("PoppyEMU - A research emulator for the Odin32K.\n");
    FILE* binPtr;
    binPtr = fopen("rom.bin", "rb");
    
    if (binPtr == NULL) {
    printf("No ROM file has been detected. It should be a file named 'rom.bin' in the directory of the executable. Exiting.\n");
    exit(1);
    }

    fread(rom0, 1, 8192, binPtr);

    /* Read the memory address at
     *the RESET vector 0xFFFC and 0xFFFD, 0x1FFC and 0x1FFD of ROM0 */


    programCounter = rom0[0x1FFC] + (0x100 * rom0[0x1FFD]);
    
    printf("ProgramCounter initialized at 0x%02X\n", programCounter);
    
    /* Begin reading instructions */ 
    while (true) {
        /* If the program counter resets to 0x0000, the program will segfault because the lack of a proper mapper */
        // removing this causes a segfault for some reason
        if (programCounter == 0xFFFF) {
            break;
        }
        /* the three possible bytes of the instruction */
        uint8_t ins1 = rom0[programCounter - START_ROM0];
        // uint8_t ins2 = rom0[programCounter + 1 - START_ROM0];
        // uint8_t ins3 = rom0[programCounter + 2 - START_ROM0];
        printf("Current Byte: 0x%02X\t PC: 0x%02X\n", ins1, programCounter);



        ++programCounter;

    }
    printf("DEBUG: End execution.\n");
}

