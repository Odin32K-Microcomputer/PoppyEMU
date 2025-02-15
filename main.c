#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Memory Map */
uint8_t sysram[32768]; // System memory, $0000-$7FFF
uint8_t rom0[8192]; // ROM0, $E000-$FFFF

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

    uint16_t programCounter;
    programCounter = rom0[0x1FFC] + (0x100 * rom0[0x1FFD]);

    printf("0x%02X\n", programCounter);

  
}

