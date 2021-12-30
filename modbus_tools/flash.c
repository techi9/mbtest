//
// Modbus flasher (flashes EEPROM via specially mapped modbus registers)
//

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// for micros
#include <time.h>

#include "libmodbus_backend.h"

// Be careful when changing these constants as default values are written in usage message.
const char *pcDefaultModbusDev = "/dev/ttyUSB0";
const unsigned short usDefaultReg = 0x300;

#define MODBUS_DEBUG 0

#define MODBUS_FLASH_TO  5000

static proxy_modbus_conf gpProxyConf[MAX_PROXIES_DEPTH];
static unsigned char gucProxies = 0;

//////////////////////////////////////////////////////////
//
// forwards

int parse_command_line(int argc, char *argv[], char **ppcDevice, unsigned *puBaudrate, unsigned char *pucOperation, unsigned char *pucSlaveID,
    unsigned short *pusReg, unsigned short *pusSize, char **ppcFile, unsigned *puAddr, unsigned *puFAddr, unsigned char *pucOverwrite);
int confirm(char *pcDevice, unsigned uBaudrate, unsigned char ucOperation, unsigned char ucSlaveID, unsigned short usReg,
    unsigned short usSize, char *pcFile, unsigned char ucFileExists, unsigned uAddr, unsigned uFAddr);
int check_modbus_device(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg);
int eeprom_write(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, FILE *pFILE, unsigned usEEPROMSize, unsigned uAddr, unsigned uFAddr);
int eeprom_read(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, FILE *pFILE, unsigned usEEPROMSize, unsigned uAddr, unsigned uFAddr);
int eeprom_verify(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, FILE *pFILE, unsigned usEEPROMSize, unsigned uAddr, unsigned uFAddr);


modbus_t *init_modbus(const char *pcDevice, unsigned uBaudrate)
{
    modbus_t *pCtx = modbus_new_rtu((pcDevice)? pcDevice : pcDefaultModbusDev, uBaudrate, 'N', 8, 1);
    if (!pCtx) {
        printf("Failed to create the context: %s\n", modbus_strerror(errno));
        return 0;
    }

#ifdef MODBUS_DEBUG
if (MODBUS_DEBUG)
    modbus_set_debug(pCtx, 1);
#endif

    if (modbus_rtu_set_serial_mode(pCtx, MODBUS_RTU_RS485) == -1) {
//        printf("Failed to set mode 485: %s\n", modbus_strerror(errno));
//        return 0;
    }

    if (modbus_connect(pCtx) == -1) {
        printf("Unable to connect: %s\n", modbus_strerror(errno));
        modbus_free(pCtx);
        return 0;
    }

    return pCtx;
}

void print_usage(void)
{
    const char *pcMessage = "Reads and writes EEPROM via modbus registers.\n"
"Current version assumes 0x300 (MSW) and 0x301 (LSW) registers to set EEPROM addressing and 0x302 register to control size.\n"
"EEPROM data should start from 0x303 register and at least 16 bytes (8 registers from 0x303 up to 0x30A should be accessible).\n"
"Data is MSB-first in modbus registers. So write 0x1234 to get 0x12 0x34 bytes in EEPROM.\n"
"\n"
"Usage: modbus_flasher [device=..] [baudrate=..] [proxy=..] read|write [-addr=..] SLAVEID [-reg=] EEPROM_SIZE FILENAME [-faddr=..]\n"
"  EEPROM_SIZE should be in bytes.\n"
"Parameters: \n"
"  device=xxx - set serial device to work with. Default is %s\n"
"  baudrate=yyy - set serial device baudrate. Default is 9600.\n"
"  proxy=ID:[REG]:[DATAREGS],ID:[REG][DATAREG]... - work via communication gates. Default REG is 0, default amount of DATAREGS is 7.\n"
"  -y - force running w/o confirmation (in any place in command line)\n"
"  -reg - start of registers block, default is 0x300 (See above).\n"
"  -addr - set address from which EEPROM will be writed from. Required in case of non-privileged access.\n"
"  -faddr - set file offset from which EEPROM will be writed from. If not set, it will be the same as \"addr\" param value.\n"
"\n"
"Example: \n"
"  modbus_flasher device=/dev/ttyUSB1 baudrate=115200 write -y 16 4096 firmware.bin\n"
"\n";
    printf(pcMessage, pcDefaultModbusDev);
}

int main(int argc, char *argv[])
{
    char *pcFileName = 0, *pcDevice = 0;
    unsigned char ucOperation, ucSlaveID, ucOverwrite = 0;
    unsigned uAddr = 0, uFAddr = 0, uBaudrate = 9600;
    unsigned short usReg = usDefaultReg, usSize = 0;
    FILE *pFile;

    if (argc < 2) {
        print_usage();
        return 0;
    }

    if (parse_command_line(argc, argv, &pcDevice, &uBaudrate, &ucOperation, &ucSlaveID, &usReg,
        &usSize, &pcFileName, &uAddr, &uFAddr, &ucOverwrite) <= 0) return 2;

// check addr,faddr!!!
    if (uAddr >= usSize) {
        printf("Address should be less then EEPROM size\n");
        return 1;
    }

    modbus_t *pM = init_modbus(pcDevice, uBaudrate);
    if (!pM) return 3;

    if (!check_modbus_device(pM, ucSlaveID, usReg)) {
        printf("Modbus device %hhu is not accessible or not work\n", ucSlaveID);
        return 3;
    }

    unsigned char ucFileExists = 0;
    pFile = fopen(pcFileName, "rb");
    if (pFile) {
        ucFileExists = 1;
        if (!ucOperation) {
            fclose(pFile); pFile = 0; // we will reopen the file for write
        }
    } else {
        // (if WRITE (to modbus) and file not exists - out)
        if (ucOperation) {
            printf("Source file %s does not exists.\n", pcFileName);
            return 4;
        }
    }

    if (!ucOverwrite) {
        if (!confirm(pcDevice, uBaudrate, ucOperation, ucSlaveID, usReg, usSize, pcFileName, ucFileExists, uAddr, uFAddr)) {
            printf("Abort.\n");
            return 5;
        }
    }

    if (ucOperation) {
        printf("Writing...\n");
        if (!eeprom_write(pM, ucSlaveID, usReg, pFile, usSize, uAddr, uFAddr)) {
            fclose(pFile);
            return 6;
        }

        fseek(pFile, 0, SEEK_SET);
        printf("Verifying...\n");
        if (!eeprom_verify(pM, ucSlaveID, usReg, pFile, usSize, uAddr, uFAddr)) {
            fclose(pFile);
            return 6;
        }
    } else {
        // invoke read
        pFile = fopen(pcFileName, "wb");
        if (!pFile) {
            printf("Error open file %s for writing.\n", pcFileName);
            return 4;
        }

        if (!eeprom_read(pM, ucSlaveID, usReg, pFile, usSize, uAddr, uFAddr)) {
            fclose(pFile);
            return 6;
        }
    }

    fclose(pFile);
    modbus_close(pM);
    modbus_free(pM);

    printf("Done\n");
    return 0;
}


//////////////////////////////////////////////////////////
//
// internals imp

int is_zero(const char *pcArg)
{
    int i, iLen = strlen(pcArg);
    for(i = 0; i < iLen; i++) {
        if ((pcArg[i] != '0') && (pcArg[i] != '.') && ((pcArg[i] != '-') || (i > 0))) return 0;
    }
    return 1;
}

int parse_command_line(int argc, char *argv[], char **ppcDevice, unsigned *puBaudrate, unsigned char *pucOperation, unsigned char *pucSlaveID,
    unsigned short *pusReg, unsigned short *pusSize, char **ppcFile, unsigned *puAddr, unsigned *puFAddr, unsigned char *pucOverwrite)
{
    int i, iTmp, iState = 0;
    char *pcToken;
    unsigned char addrs_set = 0;

    *pucOverwrite = 0;
    *ppcDevice = 0;
    for(i = 1; i < argc; i++) {
        pcToken = argv[i];
        if ((strcmp(pcToken, "-y") == 0) && (!(*pucOverwrite))) {
            *pucOverwrite = 1; continue;
        }

        switch(iState) {
            case 0:
                if (strstr(pcToken, "device=") == pcToken) {
                    *ppcDevice = strdup(pcToken + 7); continue;
                } else
                if (strstr(pcToken, "baudrate=") == pcToken) {
                    *puBaudrate = atol(pcToken + 9); continue;
                } else
                if (strstr(pcToken, "proxy=") == pcToken) {
                    gucProxies = MAX_PROXIES_DEPTH;
                    iTmp = proxy_parse_params(pcToken, strlen(pcToken), gpProxyConf, &gucProxies);
                    if (iTmp != 1) {
                        switch(iTmp) {
                            case -2: printf("Proxy depth is too much.\n");
                                break;
                            default: printf("\"proxy\" param parse error.\n");
                        }
                        return -1;
                    }
                    continue;
                } else
                if (strcmp(pcToken, "read") == 0) {
                    *pucOperation = 0; iState++; continue;
                } else 
                if (strcmp(pcToken, "write") == 0) {
                    *pucOperation = 1; iState++; continue;
                }
                break;
            case 1:
                iTmp = strtol(pcToken, NULL, 0);
                if ((iTmp == 0) && (!is_zero(pcToken))) goto addrs_label;

                if ((iTmp < 0) || (iTmp > 252)) {
                    printf("Slave id should be positive and less then 252\n");
                    return -1;
                }
                *pucSlaveID = iTmp;
                iState++;
                continue;
            case 2:
                iTmp = strtol(pcToken, NULL, 0);
                if (((iTmp == 0) && (!is_zero(pcToken))) || (iTmp <= 0)) goto addrs_label;
                *pusSize = iTmp;
                iState++;
                continue;
            case 3:
                if ((strstr(pcToken, "-addr=") == pcToken) || (strstr(pcToken, "-faddr=") == pcToken) || (strstr(pcToken, "-reg=") == pcToken)) goto addrs_label;
                *ppcFile = strdup(pcToken);
                iState++;
                continue;
            default:
addrs_label:
                if (strstr(pcToken, "-reg=") == pcToken) {
                    iTmp = strtol(pcToken + 5, NULL, 0);
                    if (((iTmp == 0) && (!is_zero(pcToken + 5))) || (iTmp < 0)) break;
                    *pusReg = iTmp;
                    continue;
                } else
                if (strstr(pcToken, "-addr=") == pcToken) {
                    iTmp = strtol(pcToken + 6, NULL, 0);
//printf("addr: %d\n", iTmp);
                    if (((iTmp == 0) && (!is_zero(pcToken + 6))) || (iTmp < 0)) break;
                    *puAddr = iTmp;
                    addrs_set |= 1;
                    continue;
                } else
                if (strstr(pcToken, "-faddr=") == pcToken) {
                    iTmp = strtol(pcToken + 7, NULL, 0);
                    if (((iTmp == 0) && (!is_zero(pcToken + 7))) || (iTmp < 0)) break;
                    *puFAddr = iTmp;
                    addrs_set |= 2;
                    continue;
                }
        }
        printf("Bad argument %d.\n", i);
        return -1;
    }
    if (iState < 4) {
        printf("Not enough arguments.\n");
        return -1;
    }

    if (addrs_set == 1) {
        *puFAddr = *puAddr;
    }
    return 1;
}

int check_modbus_device(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg)
{
    char pcError[255];
    // 19 registers. 2 address, 1 size and 16 data
    uint16_t regs[19] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    int r = libmodbus_bk_read_regs(pM, ucSlaveID, gpProxyConf, gucProxies, usReg, regs, 19, pcError, 255);
    if (r != 1) {
//        printf("%s\n", pcError);
        return 0;
    }

    return 1;
}

int mygetch(void) {
    struct termios oldt, newt;
    int ch;
    tcgetattr( STDIN_FILENO, &oldt );
    newt = oldt;
    newt.c_lflag &= ~( ICANON | ECHO );
    tcsetattr( STDIN_FILENO, TCSANOW, &newt );
    ch = getchar();
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt );
    return ch;
}

int confirm(char *pcDevice, unsigned uBaudrate, unsigned char ucOperation, unsigned char ucSlaveID, unsigned short usReg,
    unsigned short usSize, char *pcFile, unsigned char ucFileExists, unsigned uAddr, unsigned uFAddr)
{
    char c, buf1[32], buf2[32];
    const char *pcMessage = "Requested operation:\n"
"   %s\n"
"   Modbus connected to %s at baudrate %u\n"
"   Slave %hhu (FOUND, WORKS), start reg 0x%hx\n"
"   EEPROM size %hu%s\n"
"   %s file %s (%s)%s\n"
"   Continue? (y/N)\n";

    buf1[0] = 0; buf2[0] = 0;
    if (uAddr) {
        snprintf(buf1, sizeof(buf1), (ucOperation == 0)? ", read from %u" : ", write from %u", uAddr);
    }
    if ((uAddr) || (uFAddr)) {
        snprintf(buf2, sizeof(buf2), ", from offset %u", uFAddr);
    }

    printf(pcMessage, (ucOperation == 0)? "READ (from modbus to file)" : "WRITE (from file to modbus)",
        (pcDevice)? pcDevice : pcDefaultModbusDev,
        uBaudrate,
        ucSlaveID,
        usReg,
        usSize, buf1,
        (ucOperation == 0)? "Write to" : "Read from",
        pcFile,
        (ucFileExists)? "EXISTS" : "NOT EXISTS, will be created", buf2);

    c = mygetch();
    if ((c == 'y') || (c == 'Y')) return 1;
    return 0;
}

// ucSize must be EVEN as it will be placed to 2-bytes modbus registers and must be less then 32
int eeprom_write_chunk(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, unsigned long ulEEPROMAddress, unsigned char *pbBuffer, unsigned char ucSize)
{
    char pcError[255];
    if ((ucSize == 0) || (ucSize > 32)) return 0;

    // prepare buffer
    uint16_t puPacket[19], *puP = puPacket, u, w;
    unsigned uRegisters = 3 + (ucSize >> 1);
    unsigned char x, cnt;
    unsigned short usSleep = 0;
    *puP++ = (ulEEPROMAddress >> 16);
    *puP++ = (ulEEPROMAddress & 0xFFFF);
    *puP++ = ucSize;

    for(x = 0; x < ucSize; x++) {
        u = puPacket[3 + (x >> 1)];
        if (x & 1) {
            w = (u & 0xFF00); w += pbBuffer[x];
        } else {
            w = pbBuffer[x]; w <<= 8; w += (u & 0xFF);
        }
        puPacket[3 + (x >> 1)] = w;
    }

    cnt = 2;
    for(x = 0; x < cnt; x++) {
        if (usSleep) usleep(usSleep);

        int r = libmodbus_bk_write_regs(pM, ucSlaveID, gpProxyConf, gucProxies, usReg, puPacket, uRegisters, pcError, 255);
        if (r == 1) return 1;

        // TODO: maybe additionally analyze modbus code and try again
        usSleep = MODBUS_FLASH_TO;
    }

    printf("Modbus write error: %s\n", pcError);

    return 0;
}

// ucSize must be EVEN as it will be placed to 2-bytes modbus registers and must be less then 32
int eeprom_read_chunk(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, unsigned long ulEEPROMAddress, unsigned char *pbBuffer, unsigned char ucSize)
{
    char pcError[255];
    if ((ucSize == 0) || (ucSize > 32)) return 0;

    // prepare buffer
    uint16_t puPacket[19], *puP = puPacket, u;
    unsigned uRegisters = 3 + (ucSize >> 1);
    unsigned char x, cnt;
    unsigned short usSleep = 0;
    *puP++ = (ulEEPROMAddress >> 16);
    *puP++ = (ulEEPROMAddress & 0xFFFF);
    *puP++ = ucSize;

    // positioning
    int r = 0;
    cnt = 3;
    for(x = 0; x < cnt; x++) {
        if (usSleep) usleep(usSleep);

        r = libmodbus_bk_write_regs(pM, ucSlaveID, gpProxyConf, gucProxies, usReg, puPacket, 3, pcError, 255);
        if (r == 1) break;

        // TODO: maybe additionally analyze modbus code and try again
        usSleep = MODBUS_FLASH_TO;
    }

    if (r != 1) {
        printf("Modbus write error: %s\n", pcError);
        return 0;
    }

    // now read
    r = 0;
    cnt = 3;
    for(x = 0; x < cnt; x++) {
        if (usSleep) usleep(usSleep);

        r = libmodbus_bk_read_regs(pM, ucSlaveID, gpProxyConf, gucProxies, usReg, puPacket, uRegisters, pcError, 255);
        if (r == 1) break;

        // TODO: maybe additionally analyze modbus code and try again
        usSleep = MODBUS_FLASH_TO;
    }

    if (r != 1) {
        printf("Modbus read error: %s\n", pcError);
        return 0;
    }

    // check the read data is what we've expected
    unsigned long ulAddrCheck = puPacket[0];
    ulAddrCheck <<= 16;
    ulAddrCheck += puPacket[1];
    if (ulAddrCheck != ulEEPROMAddress) {
        printf("Address read does not equals expected\n");
        return 0;
    }

    if (puPacket[2] != ucSize) {
        printf("Amount read does not equals expected\n");
        return 0;
    }

    for(x = 0; x < ucSize; x++) {
        u = puPacket[3 + (x >> 1)];
        if (x & 1) {
            pbBuffer[x] = (u & 0xFF);
        } else {
            pbBuffer[x] = (u >> 8);
        }
    }

    return 1;
}

int eeprom_write(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, FILE *pFILE, unsigned usEEPROMSize, unsigned uAddr, unsigned uFAddr)
{
    unsigned char pcChunk[32];
    unsigned long ulAddr = uAddr;
    unsigned uChunk;
    int n;

    if (uFAddr) {
        if (fseek(pFILE, uFAddr, SEEK_SET) != 0) {
            printf("Error positioning file to %u.\n", uFAddr);
            return 0;
        }
    }

    do {
        uChunk = usEEPROMSize - ulAddr; if (uChunk > 32) uChunk = 32;
        if (uChunk == 0) break;

        n = fread(pcChunk, 1, uChunk, pFILE);
        if (n <= 0) break;

        if (!eeprom_write_chunk(pM, ucSlaveID, usReg, ulAddr, pcChunk, n)) {
            printf("Error write to EEPROM.\n");
            return 0;
        }

        ulAddr += n;
    } while (!feof(pFILE));

    printf("%lu bytes written to EEPROM.\n", ulAddr);
    return 1;
}

// uFAddr is ignored here as we create empty file
int eeprom_read(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, FILE *pFILE, unsigned usEEPROMSize, unsigned uAddr, unsigned uFAddr)
{
    unsigned char pcChunk[32];
    unsigned long ulAddress = uAddr;
    unsigned uChunk;
    int n;
    while(ulAddress < usEEPROMSize) {
        uChunk = usEEPROMSize - ulAddress;
        if (uChunk > 32) uChunk = 32;

        if (eeprom_read_chunk(pM, ucSlaveID, usReg, ulAddress, pcChunk, uChunk) != 1) {
            printf("Error read data from modbus.\n");
            return 0;
        }

        n = fwrite(pcChunk, 1, uChunk, pFILE);
        if (n != uChunk) {
            printf("Error save data to file.\n");
            return 0;
        }

        ulAddress += uChunk;
    }
    return 1;
}

int eeprom_verify(modbus_t *pM, unsigned char ucSlaveID, unsigned short usReg, FILE *pFILE, unsigned usEEPROMSize, unsigned uAddr, unsigned uFAddr)
{
    unsigned char pcChunkM[32], pcChunkF[32], x;
    unsigned long ulAddr = uAddr;
    unsigned uChunk;
    int n;

    if (uFAddr) {
        if (fseek(pFILE, uFAddr, SEEK_SET) != 0) {
            printf("Error positioning file to %u.\n", uFAddr);
            return 0;
        }
    }

    do {
        uChunk = usEEPROMSize - ulAddr; if (uChunk > 32) uChunk = 32;
        if (uChunk == 0) break;

        n = fread(pcChunkF, 1, uChunk, pFILE);
        if (n <= 0) break;

        if (eeprom_read_chunk(pM, ucSlaveID, usReg, ulAddr, pcChunkM, n) != 1) {
            printf("Error read data from modbus.\n");
            return 0;
        }

        for(x = 0; x < n; x++) {
            if (pcChunkF[x] != pcChunkM[x]) {
                printf("Error verification at byte %lu.\n", ulAddr + x);
                return 0;
            }
        }

        ulAddr += n;
    } while (!feof(pFILE));

    printf("%lu bytes verified.\n", ulAddr);
    return 1;
}

// TODO: to ARCH

unsigned long __micros()
{
    unsigned long res;
    struct timespec t={0,0};
    clock_gettime(CLOCK_MONOTONIC, &t);

    res = (double)t.tv_sec * 1000000;
    res += (1.0e-3*t.tv_nsec);
    return res;
}

