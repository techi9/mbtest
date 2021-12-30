//
// Modbus bg (provides commands to manipulate with modbus devices)
//

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// for micros
#include <time.h>

#include <modbus.h>

#include "libmodbus_backend.h"

const char *pcDefaultModbusDev = "/dev/ttyUSB0";

const char *pcCommands[] = { "help", "get", "set", "poll", "eeget", "eeset" };

#define MODBUS_MAXREGS        127
#define MODBUS_DEBUG          0

static proxy_modbus_conf gpProxyConf[MAX_PROXIES_DEPTH];
static unsigned char gucProxies = 0;

//////////////////////////////////////////////////////////
//
// forwards

int parse_command_line(int argc, char *argv[], char **ppcDevice, unsigned *puBaudrate,
        unsigned char *pucOperation, int *piCommandArgStart);
int check_modbus_device(modbus_t *pM);
int is_zero(const char *pcArg);
int process_get_poll(modbus_t *pM, unsigned char ucSlaveID, unsigned char ucArgsStart, char *argv[], int argc, int iIsPoll);
int process_set(modbus_t *pM, unsigned char ucSlaveID, unsigned char ucArgsStart, char *argv[], int argc);


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
    const char *pcMessage = "Provides common commands to manipulate with Baget modbus devices:\n"
"help, get, set, poll, eeget, eeset\n"
"\n"
"Usage: modbus_bg [device=..] [baudrate=..] [proxy=..] command [args]\n"
"Parameters: \n"
"  device=xxx - set serial device to work with. Default is %s\n"
"  baudrate=yyy - set serial device baudrate. Default is 9600.\n"
"  proxy=ID:[REG]:[DATAREGS],ID:[REG][DATAREG]... - work via communication gates. Default REG is 0, default amount of DATAREGS is 7.\n"
"Commands: \n"
"  help  - prints this information\n"
"  get SLAVEID REG AMOUNT - reads AMOUNT registers (starting from REG) from modbus device SLAVEID.\n"
"  set SLAVEID REG VAL1 [VAL2] ... [VALN] - writes values to registers (starting from REG) to modbus device SLAVEID.\n"
"  poll SLAVEID REG AMOUNT [PERIOD] - same as get, but queries registers continuosly with period PERIOD. Default period is 1 sec.\n"
"  eeget - TODO\n"
"  eeset - TODO\n"
"\n"
"Example: \n"
"  modbus_bg device=/dev/ttyUSB1 baudrate=115200 get 20 0x80 2\n"
"  modbus_bg device=/dev/ttyUSB1 baudrate=115200 set 20 0x100 0x0102 0x0304\n"
"\n";
    printf(pcMessage, pcDefaultModbusDev);
}

int main(int argc, char *argv[])
{
    char *pcDevice = 0;
    unsigned char ucOperation = 0, ucSlaveID;
    unsigned uBaudrate = 9600;
    int iCommandArgs, iTmp;

    if (argc < 2) {
        print_usage();
        return 0;
    }

    if (parse_command_line(argc, argv, &pcDevice, &uBaudrate, &ucOperation, &iCommandArgs) <= 0) return 2;

    if (ucOperation == 0) {
        print_usage();
        return 0;
    }

    // for all the commands we need slave id as first arg
    if (iCommandArgs >= argc) {
        printf("Slave ID required\n");
        return 2;
    }

    iTmp = strtol(argv[iCommandArgs], NULL, 0);
    if ((iTmp == 0) && (!is_zero(argv[iCommandArgs]))) {
        printf("Non-digit slave id.\n");
        return 2;
    }

    if ((iTmp < 0) || (iTmp > 252)) {
        printf("Slave id should be positive and less then 252\n");
        return 2;
    }
    ucSlaveID = iTmp;

    modbus_t *pM = init_modbus(pcDevice, uBaudrate);
    if (!pM) return 3;

    switch(ucOperation) {
        case 1: iTmp = process_get_poll(pM, ucSlaveID, iCommandArgs + 1, argv, argc, 0);
                break;
        case 2: iTmp = process_set(pM, ucSlaveID, iCommandArgs + 1, argv, argc);
                break;
        case 3: iTmp = process_get_poll(pM, ucSlaveID, iCommandArgs + 1, argv, argc, 1);
                break;
        default:
            printf("Unsupported operation.\n");
            iTmp = -1;
    }

    modbus_close(pM);
    modbus_free(pM);
    return (iTmp == 1)? 0 : 3;
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

int parse_command_line(int argc, char *argv[], char **ppcDevice, unsigned *puBaudrate,
        unsigned char *pucOperation, int *piCommandArgStart)
{
    int i, r;
    char *pcToken;
    unsigned char x;

    *ppcDevice = 0;
    for(i = 1; i < argc; i++) {
        pcToken = argv[i];
        if (strstr(pcToken, "device=") == pcToken) {
            *ppcDevice = strdup(pcToken + 7); continue;
        } else
        if (strstr(pcToken, "baudrate=") == pcToken) {
            *puBaudrate = atol(pcToken + 9); continue;
        } else
        if (strstr(pcToken, "proxy=") == pcToken) {
            gucProxies = MAX_PROXIES_DEPTH;
            r = proxy_parse_params(pcToken, strlen(pcToken), gpProxyConf, &gucProxies);
            if (r != 1) {
                switch(r) {
                    case -2: printf("Proxy depth is too much.\n");
                        break;
                    default: printf("\"proxy\" param parse error.\n");
                }
                return -1;
            }
            continue;
        } else
        {
            for(x = 0; x < (sizeof(pcCommands) / sizeof(const char *)); x++) {
                if (strcmp(pcToken, pcCommands[x]) == 0) {
                    *pucOperation = x;
                    *piCommandArgStart = i + 1;
                    return 1;
                }
            }
            printf("Unrecognized command. Use help to check.\n");
            return -1;
        }
    }
    return -1;
}

int process_get_poll(modbus_t *pM, unsigned char ucSlaveID, unsigned char ucArgsStart, char *argv[], int argc, int iIsPoll)
{
    char pcError[255];
    unsigned short regs[MODBUS_MAXREGS];
    fd_set rfds;
    struct timeval tv;

    // get requires 2 arguments
    unsigned char x = ucArgsStart;
    if (x >= argc) {
        printf("Error extract argument 2.\n");
        return -1;
    }

    int iTmp = strtol(argv[x], NULL, 0);
    if ((iTmp == 0) && (!is_zero(argv[x]))) {
        printf("Bad register num.\n");
        return -1;
    }
    int iReg = iTmp; x++;
    if (x >= argc) {
        printf("Error extract argument 2.\n");
        return -1;
    }

    iTmp = strtol(argv[x], NULL, 0);
    if ((iTmp < 0) || ((iTmp == 0) && (!is_zero(argv[x])))) {
        printf("Bad amount num.\n");
        return -1;
    }

    if (iTmp > MODBUS_MAXREGS) {
        printf("Too much registers. Max is %d\n", MODBUS_MAXREGS);
        return -1;
    }
    x++;

    int iPollTO = 1000;
    if ((iIsPoll) && (x < argc)) {
        iPollTO = strtol(argv[x], NULL, 0);
        if ((iPollTO < 0) || ((iPollTO == 0) && (!is_zero(argv[x])))) {
            printf("Bad poll timeout value.\n");
            return -1;
        }
    }

    if (iIsPoll) {
        printf("GOT: %hhu 0x%04x %d %d READY\n", ucSlaveID, iReg, iTmp, iPollTO);
    } else {
        printf("GOT: %hhu 0x%04x %d READY\n", ucSlaveID, iReg, iTmp);
    }


    while(1) {

        int r = libmodbus_bk_read_regs(pM, ucSlaveID, gpProxyConf, gucProxies, iReg, regs, iTmp, pcError, 255);

        if (r != 1) {
            printf("%s\n", pcError);
            if (!iIsPoll) return 0;
        } else {
            for(r = 0; r < iTmp; r++) {
                if (r > 0) printf(" ");
                printf("0x%04x", regs[r]);
            }
            printf("\n");
        }

        if (!iIsPoll) break;

        FD_ZERO(&rfds);
        FD_SET(0, &rfds);

        tv.tv_sec = iPollTO / 1000;
        tv.tv_usec = (iPollTO % 1000) * 1000;

        int retval = select(2, &rfds, NULL, NULL, &tv);
        if (retval) break;
    }
    return 1;
}

int process_set(modbus_t *pM, unsigned char ucSlaveID, unsigned char ucArgsStart, char *argv[], int argc)
{
    char pcError[255];
    unsigned short regs[MODBUS_MAXREGS];

    unsigned char x = ucArgsStart, y;
    if (x >= argc) {
        printf("Error extract argument 2.\n");
        return -1;
    }

    int iTmp = strtol(argv[x], NULL, 0);
    if ((iTmp == 0) && (!is_zero(argv[x]))) {
        printf("Bad register num.\n");
        return -1;
    }
    int iReg = iTmp; x++;

    for(y = 0; x < argc; x++, y++) {
        if (y >= MODBUS_MAXREGS) {
            printf("Too much registers. Max is %d\n", MODBUS_MAXREGS);
            return -1;
        }

        iTmp = strtol(argv[x], NULL, 0);
        if ((iTmp < 0) || ((iTmp == 0) && (!is_zero(argv[x])))) {
            printf("Bad argument %d.\n", x + 2);
            return -1;
        }
        regs[y] = (iTmp & 0xFFFF);
    }

    if (y == 0) {
        printf("Error extract argument 3.\n");
        return -1;
    }

    printf("GOT: %hhu 0x%04x %u bytes READY\n", ucSlaveID, iReg, ((unsigned)y) * 2);

    int r = libmodbus_bk_write_regs(pM, ucSlaveID, gpProxyConf, gucProxies, iReg, regs, y, pcError, 255);
    if (r != 1) {
        printf("%s\n", pcError);
        return 0;
    }

    printf("OK.\n");
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
