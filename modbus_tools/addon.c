//
// Modbus addon (provides non-standard commands to manipulate with baget modbus devices)
//

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <modbus.h>


const char *pcDefaultModbusDev = "/dev/ttyUSB0";

const char *pcCommands[] = { "help", "tmpaddr" };

#define MODBUS_MAXREGS        127
#define MODBUS_DEBUG          0

//////////////////////////////////////////////////////////
//
// forwards

int parse_command_line(int argc, char *argv[], char **ppcDevice, unsigned *puBaudrate,
        unsigned char *pucOperation, int *piCommandArgStart);
int check_modbus_device(modbus_t *pM);
int is_zero(const char *pcArg);
int process_tmpaddr(modbus_t *pM, unsigned char ucSlaveID, unsigned char ucArgsStart, char *argv[], int argc);


modbus_t *init_modbus(const char *pcDevice, unsigned uBaudrate, unsigned char ucNum)
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

    //Set the Modbus address of the remote slave (to ucNum)
    modbus_set_slave(pCtx, ucNum);
    return pCtx;
}

void print_usage(void)
{
    const char *pcMessage = "Provides additional and non-standard commands to manipulate with Baget modbus devices:\n"
"tmpaddr\n"
"\n"
"Usage: modbus_addon [device=..] [baudrate=..] command [args]\n"
"Parameters: \n"
"  device=xxx - set serial device to work with. Default is %s\n"
"  baudrate=yyy - set serial device baudrate. Default is 9600.\n"
"Commands: \n"
"  help  - prints this information\n"
"  tmpaddr SLAVEID START_TMP_ADDR - command for any (if SLAVEID is zero) or particular device\n"
"                                   to change its address to one started from START_TMP_ADDR.\n"
"\n"
"Example: \n"
"  modbus_addon device=/dev/ttyUSB1 baudrate=115200 tmpaddr 20 0x30\n"
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
        printf("Slave id should be positive (including zero) and less then 252\n");
        return 2;
    }
    ucSlaveID = iTmp;

    modbus_t *pM = init_modbus(pcDevice, uBaudrate, ucSlaveID);
    if (!pM) return 3;

    switch(ucOperation) {
        case 1: iTmp = process_tmpaddr(pM, ucSlaveID, iCommandArgs + 1, argv, argc);
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
    int i;
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

int process_tmpaddr(modbus_t *pM, unsigned char ucSlaveID, unsigned char ucArgsStart, char *argv[], int argc)
{
    unsigned short regs[1];
    fd_set rfds;
    struct timeval tv;
    unsigned char pcCustPacket[128]; // MODBUS_TCP_MAX_ADU_LENGTH (but RTU)
    unsigned char *pcC = pcCustPacket, x = ucArgsStart, ucTmpAddr;

    // get requires 2 arguments
    if (x >= argc) {
        printf("Error extract argument 2.\n");
        return -1;
    }

    int iTmp = strtol(argv[x], NULL, 0);
    if (iTmp == 0) {
        printf("Bad start tmp address num.\n");
        return -1;
    }
    ucTmpAddr = iTmp;

    int iIsPool = 1; // ? do I need a mode for only one request? Or maybe any timeout?

    while(1) {
        pcC = pcCustPacket;
        *pcC++ = ucSlaveID;
        *pcC++ = 0x99; // our custom command
        *pcC++ = 0;
        *pcC++ = 1;    // tmp addr command
        *pcC++ = 0;
        *pcC++ = ucTmpAddr;

        modbus_set_slave(pM, ucSlaveID);
//        int rc = 
            modbus_send_raw_request(pM, pcCustPacket, 6);
// check error?

        // unfortunately, this does not work, but needed to free receive buffer
//        rc = 
            modbus_receive_confirmation(pM, pcCustPacket);
//        rc = 
            modbus_receive_confirmation(pM, pcCustPacket);

        modbus_set_slave(pM, ucTmpAddr);
        int numm = modbus_read_registers(pM, 0, 1, regs);
        if (numm != 1) {  // number of read registers is not the one expected
            if (errno == EMBXSBUSY) {
                usleep(2000);
            }

            numm = modbus_read_registers(pM, 0, 1, regs);
        }

        if (numm == 1) {  // number of read registers is not the one expected again
            // address is got
printf("READY %hhu\n", ucTmpAddr);
            if (!iIsPool) return 1;

            ucTmpAddr++;
            if (ucTmpAddr > 254) {
                printf("Address too large. Break.\n");
                return 1;
            }
        }

        FD_ZERO(&rfds);
        FD_SET(0, &rfds);

        tv.tv_sec = 0;
        tv.tv_usec = 1000000;

        int retval = select(2, &rfds, NULL, NULL, &tv);
        if (retval) break;
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

