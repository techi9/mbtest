//
//

#include "libmodbus_backend.h"
#ifdef WITH_DUMMY_PROXY_BK
#include "emu.h"
#endif
#include <modbus.h>
#include <errno.h>

#include <string.h>
#include <unistd.h>

#include <stdio.h>
// for exit()
#include <stdlib.h>

/*
#define m_printf

static void dump(unsigned char *pB, unsigned short usLen)
{
    unsigned short u;
    for(u = 0; u < usLen; u++) {
        if (u > 0) m_printf(" ");
        m_printf("%02x", pB[u]);
    }
}
*/
static void crc_step(unsigned short *pCRC, unsigned char ucByte)
{
    register unsigned short temp = *pCRC;
    unsigned char j, flag;
    temp = temp ^ ucByte;
    for(j = 0; j < 8; j++) {
        flag = temp & 0x0001;
        temp >>=1;
        if (flag) temp ^= 0xA001;
    }
    *pCRC = temp;
}


#ifdef WITH_DUMMY_PROXY_BK

/////////////////////////////
//
// dummy backend (just dumps)

// maybe uplink as param here?
int dummy_bk_start(libmodbus_proxy_bk_ctx *pCtx)
{
    if (pCtx->ucState == 0) pCtx->ucState = 1;
    return 1;
}

#define P_ADD 7

int dummy_bk_ready(libmodbus_proxy_bk_ctx *pCtx)
{
    unsigned char ucCommand = pCtx->pL->uplink.pII->m_get_command(pCtx->pL->uplink.pICtx, &(pCtx->usSize)), x;

    unsigned short us, usCRC;
    int rc1;

    switch(ucCommand) {
        case 0: return 1; // all is done

        case PROXY_WRITE:
            // get command... get packet if write... 

            // TODO: check proxy algorithm. Following is specific for our v.1 proxy
            if (pCtx->usSize > (sizeof(pCtx->pbBuffer) - 2 - P_ADD))
                pCtx->usSize = sizeof(pCtx->pbBuffer) - 2 - P_ADD; // get place for register with size
            x = (pCtx->pL->ucDataRegisters << 1);
            if (pCtx->usSize > x) pCtx->usSize = x;
            rc1 = pCtx->pL->uplink.pII->m_fill_chunk(pCtx->pL->uplink.pICtx, pCtx->pbBuffer + 2 + P_ADD, &(pCtx->usSize));
            if (rc1 != 1) {
                // TODO: error, maybe just return -1, maybe retry or smth
            }
            pCtx->pbBuffer[P_ADD] = (pCtx->usSize >> 8);
            pCtx->pbBuffer[P_ADD + 1] = (pCtx->usSize & 0xFF);


// imvoke emu
    pCtx->pbBuffer[0] = pCtx->pL->ucProxyAddress;
    pCtx->pbBuffer[1] = 0x10;
    pCtx->pbBuffer[2] = (pCtx->pL->usReg >> 8);
    pCtx->pbBuffer[3] = (pCtx->pL->usReg & 0xFF);
    unsigned short sz_regs = ((pCtx->usSize + 3) >> 1);
    pCtx->pbBuffer[4] = (sz_regs >> 8);
    pCtx->pbBuffer[5] = (sz_regs & 0xFF);
    pCtx->pbBuffer[6] = (sz_regs << 1);

    usCRC = 0xFFFF; us = (sz_regs << 1) + 7;
    for(x = 0; x < us; x++) crc_step(&usCRC, pCtx->pbBuffer[x]);
    pCtx->pbBuffer[us] = (usCRC & 0xFF);
    pCtx->pbBuffer[us + 1] = (usCRC >> 8);

    rc1 = proxy_emu_exchange(pCtx->pEmu, pCtx->pbBuffer, us + 9, pCtx->pbBuffer, &us);

    if (rc1 == 1) {
        if (pCtx->pbBuffer[1] & 0x80) {
            rc1 = 0 - pCtx->pbBuffer[2];
        }
    }
    // TODO: interprete pCtx->pbBuffer (pCtx->pbBuffer[1] & 0x80 means eo
//            rc1 = 1;
            pCtx->pL->uplink.pII->m_notify_write_result(pCtx->pL->uplink.pICtx, rc1, 0);
            break;
        case PROXY_READ:
            // invoke backend (dummy - printf, libmodbus - send/receive)
            // size??? maybe 0 for "as much as possible"?
            us = ((pCtx->usSize + 1) >> 1); // amount of data registers to read

dbr_invoke:
            if (us > pCtx->pL->ucDataRegisters) us = pCtx->pL->ucDataRegisters;
            us++; // one register for size

// imvoke emu
    pCtx->pbBuffer[0] = pCtx->pL->ucProxyAddress;
    pCtx->pbBuffer[1] = 3;
    pCtx->pbBuffer[2] = (pCtx->pL->usReg >> 8);
    pCtx->pbBuffer[3] = (pCtx->pL->usReg & 0xFF);
    pCtx->pbBuffer[4] = (us >> 8);
    pCtx->pbBuffer[5] = (us & 0xFF);

    usCRC = 0xFFFF;
    for(x = 0; x < 6; x++) crc_step(&usCRC, pCtx->pbBuffer[x]);
    pCtx->pbBuffer[6] = (usCRC & 0xFF);
    pCtx->pbBuffer[7] = (usCRC >> 8);

    us = sizeof(pCtx->pbBuffer);
    rc1 = proxy_emu_exchange(pCtx->pEmu, pCtx->pbBuffer, 8, pCtx->pbBuffer, &us);

    if (rc1 == 1) {
        if (pCtx->pbBuffer[1] & 0x80) {
            rc1 = 0 - pCtx->pbBuffer[2];
        } else {

            if (ucCommand == PROXY_FLUSH) return 0;

            us = pCtx->pbBuffer[3]; us <<= 8; us += pCtx->pbBuffer[4];
if (!us) {
    return 1;
}
            if ((us + 2) > pCtx->pbBuffer[2]) {
                // modbus bytes not enough
if (pCtx->pbBuffer[2] < 2) {
return -1;
}
                us = pCtx->pbBuffer[2] - 2;
            }
                pCtx->pL->uplink.pII->m_notify_read_result(pCtx->pL->uplink.pICtx, rc1, pCtx->pbBuffer + 5, us, 1);

        }
    }

            break;
        case PROXY_AVAIL:
        case PROXY_FLUSH:

// invoke emu
    pCtx->pbBuffer[0] = pCtx->pL->ucProxyAddress;
    pCtx->pbBuffer[1] = 3;
    pCtx->pbBuffer[2] = (pCtx->pL->usReg >> 8);
    pCtx->pbBuffer[3] = (pCtx->pL->usReg & 0xFF);
    pCtx->pbBuffer[4] = 0;
    pCtx->pbBuffer[5] = 1;

    usCRC = 0xFFFF;
    for(x = 0; x < 6; x++) crc_step(&usCRC, pCtx->pbBuffer[x]);
    pCtx->pbBuffer[6] = (usCRC & 0xFF);
    pCtx->pbBuffer[7] = (usCRC >> 8);

    us = sizeof(pCtx->pbBuffer);
    rc1 = proxy_emu_exchange(pCtx->pEmu, pCtx->pbBuffer, 8, pCtx->pbBuffer, &us);

    if (rc1 == 1) {
        if (pCtx->pbBuffer[1] & 0x80) {
            rc1 = 0 - pCtx->pbBuffer[2];
        } else {
//            pCtx->uAvail = 0; // (may be able call async)

            us = pCtx->pbBuffer[3]; us <<= 8; us += pCtx->pbBuffer[4];
            if ((us) && (ucCommand == PROXY_FLUSH)) {
                // bytes to registers
                us = ((us + 1) >> 1);
                goto dbr_invoke;
            }

            pCtx->pL->uplink.pII->m_notify_avail(pCtx->pL->uplink.pICtx, rc1, us);

if (!us) return 1;
        }
    }

            break;
    }
    return 0;
}

#endif /* WITH_DUMMY_PROXY_BK */

static void regs_to_bytes(unsigned short *puRegs, unsigned char ucRegs, unsigned char *pbBytes)
{
    unsigned char x, *pbB = pbBytes;
    unsigned short us;
    for(x = 0; x<ucRegs; x++) {
        us = puRegs[x];
        *pbB++ = (us >> 8);
        *pbB++ = (us & 0xFF);
    }
}

static void bytes_to_regs(unsigned char *pbBytes, unsigned short *puRegs, unsigned char ucRegs)
{
    unsigned char x, *pbB = pbBytes;
    unsigned short us;
    for(x = 0; x<ucRegs; x++) {
        us = *pbB++; us <<= 8;
        us += (*pbB++);
        puRegs[x] = us;
    }
}

unsigned short usTS = 0;

int libmodbus_bk_start(libmodbus_proxy_bk_ctx *pCtx)
{
    if (pCtx->ucState == 0) pCtx->ucState = 1;
usTS = 0;
    return 1;
}


int libmodbus_bk_ready(libmodbus_proxy_bk_ctx *pCtx)
{
    unsigned char ucCommand = pCtx->pL->uplink.pII->m_get_command(pCtx->pL->uplink.pICtx, &(pCtx->usSize)), x;

    unsigned short us, regs[128];
    int rc1, rc2, numm;

    switch(ucCommand) {
        case 0: return 1; // all is done

        case PROXY_WRITE:
            // get command... get packet if write... 

            // TODO: check proxy algorithm. Following is specific for our v.1 proxy
            if (pCtx->usSize > (sizeof(pCtx->pbBuffer) - 2))
                pCtx->usSize = sizeof(pCtx->pbBuffer) - 2; // get place for register with size
            x = (pCtx->pL->ucDataRegisters << 1);
            if (pCtx->usSize > x) pCtx->usSize = x;
            rc1 = pCtx->pL->uplink.pII->m_fill_chunk(pCtx->pL->uplink.pICtx, pCtx->pbBuffer + 2, &(pCtx->usSize));
            if (rc1 != 1) {
                // TODO: error, maybe just return -1, maybe retry or smth
            }
            pCtx->pbBuffer[0] = (pCtx->usSize >> 8);
            pCtx->pbBuffer[1] = (pCtx->usSize & 0xFF);

            unsigned short sz_regs = ((pCtx->usSize + 3) >> 1);
// TODO: switch lo-hi in buffer as libmodbus wants array ot shorts...
bytes_to_regs(pCtx->pbBuffer, regs, sz_regs);
            rc1 = -1;
            numm = modbus_write_registers(pCtx->pM, pCtx->pL->usReg, sz_regs, regs);

            if (numm == sz_regs) {
                rc1 = 1;
            } else {
                // TODO: check modbus errors and fit to -1 ...-8
                rc1 = -1;
printf("libmodbus write error. Need resend. %s\n", modbus_strerror(errno));
exit(1);
            }
    // TODO: interprete pCtx->pbBuffer (pCtx->pbBuffer[1] & 0x80 means eo

            pCtx->pL->uplink.pII->m_notify_write_result(pCtx->pL->uplink.pICtx, rc1, 0);
            break;
        case PROXY_READ:
            // invoke backend (dummy - printf, libmodbus - send/receive)
            // size??? maybe 0 for "as much as possible"?
            us = ((pCtx->usSize + 1) >> 1); // amount of data registers to read
            if (us > 122) us = 122; // maximum allowed number of registers in this scheme

lbr_invoke:
            if (us > pCtx->pL->ucDataRegisters) us = pCtx->pL->ucDataRegisters;
            us++; // one register for size

            rc1 = -1;
            numm = modbus_read_registers(pCtx->pM, pCtx->pL->usReg, us, regs);
            if (numm == us) {
                if (ucCommand == PROXY_FLUSH) return 0;

// TODO: hi-lo exchange
                us = regs[0];

                if (!us) {
//                    m_printf("DATA NOT READY.\n");
//return 1;
                } else {
    usTS = 0;
}
                if ((us + 2) > (numm << 1)) {
                    // modbus bytes not enough
                    us = (numm << 1) - 2;
                }
regs_to_bytes(&(regs[1]), numm - 1, pCtx->pbBuffer);
                pCtx->pL->uplink.pII->m_notify_read_result(pCtx->pL->uplink.pICtx, 1, pCtx->pbBuffer, us, 1);

            } else {
printf("Libmodbus read error %d %s\n", numm, modbus_strerror(errno));
exit(1);
            }

            break;
        case PROXY_AVAIL:
        case PROXY_FLUSH:
            rc1 = -1;
            numm = modbus_read_registers(pCtx->pM, pCtx->pL->usReg, 1, regs);
            if (numm == 1) {
                us = regs[0];

            if ((us) && (ucCommand == PROXY_FLUSH)) {
                // bytes to registers
                us = ((us + 1) >> 1);
                goto lbr_invoke;
            }

            rc2 = pCtx->pL->uplink.pII->m_notify_avail(pCtx->pL->uplink.pICtx, 1, us);
            if (rc2 < 0) return 1; // that's all
/*
if (!us) {
m_printf("TS=%hu\n", usTS);
    if (++usTS > 1)
return 1;
} else {
    usTS = 0;
}
*/
            } else {
printf("Libmodbus read (a) error %d %s\n", numm, modbus_strerror(errno));
exit(1);
            }

            break;
    }
    return 0;
}

static proxy_modbus_logic gpPs[MAX_PROXIES_DEPTH];
static unsigned char gpLbmBuffer[256 + 9];

DECLARE_FINAL_I(final_i);

//TODO:  STRING CONSTANTS!!!

int libmodbus_proxy_exchange(modbus_t *pM, final_uplink *pU, char *pcErrorDescr, unsigned char ucErrorDescrSize)
{
    int iRes;

    libmodbus_proxy_bk_ctx PCtx;
    PCtx.pL = &(gpPs[0]);
    PCtx.usSize = 0;
    PCtx.uAvail = 0;
    PCtx.ucState = 0;

    PCtx.pM = pM;

    libmodbus_bk_start(&PCtx);
    while((iRes = libmodbus_bk_ready(&PCtx)) == 0) { __asm("nop"); }

    if (iRes < 0) {
        // TODO: get error description from proxies logic
        if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Proxy send error.\n");
        return -1;
    }

    pU->ucTask = PROXY_READ;
    pU->ulReadTO = 0;
    pU->pbBuffer = gpLbmBuffer;
    pU->usBufferLen = sizeof(gpLbmBuffer);
    pU->usCurPos = 0;
    pU->usNotAckSize = 0;

    libmodbus_bk_start(&PCtx);
    while((iRes = libmodbus_bk_ready(&PCtx)) == 0) { __asm("nop"); }

    if (iRes < 0) {
        // TODO: get error description from proxies logic
        if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Proxy receive error.\n");
        return -1;
    }

    return 1;
}

int libmodbus_bk_write_regs(modbus_t *pM, unsigned char ucSlaveID, proxy_modbus_conf *pCs, unsigned char ucPCount,
    unsigned short usReg, unsigned short *pRegs, unsigned short ucCount,
    char *pcErrorDescr, unsigned char ucErrorDescrSize)
{
    int iRes = 0, iNumm;
    unsigned short us, usCRC;
    unsigned char x;

    if ((pCs == 0) || (ucPCount == 0)) {
        if (modbus_set_slave(pM, ucSlaveID) != 0) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Set slave id error: %s\n", modbus_strerror(errno));
            return -1;
        }

        iNumm = modbus_write_registers(pM, usReg, ucCount, pRegs);
        if (iNumm != ucCount) {  // number of read registers is not the one expected
            if (errno == EMBXSBUSY) {
                usleep(2000);
            }

            iNumm = modbus_write_registers(pM, usReg, ucCount, pRegs);
        }

        if (iNumm != ucCount) {  // number of read registers is not the one expected again
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Modbus read error: %s\n", modbus_strerror(errno));
            return -1;
        }
    } else {
        if (ucPCount > MAX_PROXIES_DEPTH) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Max proxies depth should not exceed %hu. %hu passed.\n", MAX_PROXIES_DEPTH, ucPCount);
            return -1;
        }

        if (ucCount > 128) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Too mush registers (256 MAX supported.\n");
            return -1;
        }

        // prepare send modbus packet
        gpLbmBuffer[0] = ucSlaveID;
        gpLbmBuffer[1] = 0x10;
        gpLbmBuffer[2] = (usReg >> 8);
        gpLbmBuffer[3] = (usReg & 0xFF);
        gpLbmBuffer[4] = 0;
        gpLbmBuffer[5] = ucCount;
        gpLbmBuffer[6] = (ucCount << 1);

        regs_to_bytes(pRegs, ucCount, gpLbmBuffer + 7);

        usCRC = 0xFFFF; us = (ucCount << 1) + 7;
        for(x = 0; x < us; x++) crc_step(&usCRC, gpLbmBuffer[x]);
        gpLbmBuffer[us] = (usCRC & 0xFF);
        gpLbmBuffer[us + 1] = (usCRC >> 8);

        final_uplink UCtx;
        UCtx.ucTask = PROXY_WRITE;
        UCtx.ucLevel = ucPCount;
        UCtx.ulReadTO = 0;
        UCtx.pbBuffer = gpLbmBuffer;
        UCtx.usBufferLen = us + 2;
        UCtx.usCurPos = 0;
        UCtx.usNotAckSize = 0;

        if (proxy_prepare(pCs, ucPCount, &final_i, &UCtx, 1, gpPs) != 1) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Proxy preparation error.\n");
            return -1;
        }

        if (modbus_set_slave(pM, pCs->ucSlaveID) != 0) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Set slave id error: %s\n", modbus_strerror(errno));
            return -1;
        }

        iRes = libmodbus_proxy_exchange(pM, &UCtx, pcErrorDescr, ucErrorDescrSize);
        if (iRes != 1) return -1; // error description is already in error descr

        // decode answer...
        // write regs packet has 8 (success) or 5 (failure) bytes length
        if ((UCtx.usCurPos == 5) || (UCtx.usCurPos == 8)) {
            usCRC = 0xFFFF;
            for(x = 0; x < (UCtx.usCurPos - 2); x++) crc_step(&usCRC, UCtx.pbBuffer[x]);
            if ((UCtx.pbBuffer[UCtx.usCurPos - 2] != (usCRC & 0xFF)) ||
                (UCtx.pbBuffer[UCtx.usCurPos - 1] != (usCRC >> 8))) {

                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "CRC Error.\n");
                return -1;
            }
            if (UCtx.pbBuffer[0] != ucSlaveID) {
                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Answer from incorrect slave.\n");
                return -1;
            }

            if (UCtx.usCurPos == 8) return 1; // success

            if (UCtx.pbBuffer[1] & 0x80) {
                // maybe retry if it is error 6 (temporary busy)

                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Error %hhu received.\n", UCtx.pbBuffer[2]);
                return -1; //? maybe modbus error?
            } else {
                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Bad answer received (%hu bytes).\n", UCtx.usCurPos);
                return -1;
            }
        } else {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Bad answer received (%hu bytes).\n", UCtx.usCurPos);
            return -1;
        }
    }
    return 1;
}

int libmodbus_bk_read_regs(modbus_t *pM, unsigned char ucSlaveID, proxy_modbus_conf *pCs, unsigned char ucPCount,
    unsigned short usReg, unsigned short *pRegs, unsigned short ucCount,
    char *pcErrorDescr, unsigned char ucErrorDescrSize)
{
    int iRes = 0, iNumm;
    unsigned short usCRC, x;

    if ((pCs == 0) || (ucPCount == 0)) {
        if (modbus_set_slave(pM, ucSlaveID) != 0) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Set slave id error: %s\n", modbus_strerror(errno));
            return -1;
        }

        iNumm = modbus_read_registers(pM, usReg, ucCount, pRegs);
        if (iNumm != ucCount) {  // number of read registers is not the one expected
            if (errno == EMBXSBUSY) {
                usleep(2000);
            }

            iNumm = modbus_read_registers(pM, usReg, ucCount, pRegs);
        }

        if (iNumm != ucCount) {  // number of read registers is not the one expected again
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Modbus read error: %s\n", modbus_strerror(errno));
            return -1;
        }
    } else {
        if (ucPCount > MAX_PROXIES_DEPTH) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Max proxies depth should not exceed %hu. %hu passed.\n", MAX_PROXIES_DEPTH, ucPCount);
            return -1;
        }

        if (ucCount > 128) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Too mush registers (256 MAX supported.\n");
            return -1;
        }

        // prepare rcvt modbus packet
        gpLbmBuffer[0] = ucSlaveID;
        gpLbmBuffer[1] = 3;
        gpLbmBuffer[2] = (usReg >> 8);
        gpLbmBuffer[3] = (usReg & 0xFF);
        gpLbmBuffer[4] = 0;
        gpLbmBuffer[5] = ucCount;

        usCRC = 0xFFFF;
        for(x = 0; x < 6; x++) crc_step(&usCRC, gpLbmBuffer[x]);
        gpLbmBuffer[6] = (usCRC & 0xFF);
        gpLbmBuffer[7] = (usCRC >> 8);

        final_uplink UCtx;
        UCtx.ucTask = PROXY_WRITE;
        UCtx.ucLevel = ucPCount;
        UCtx.ulReadTO = 0;
        UCtx.pbBuffer = gpLbmBuffer;
        UCtx.usBufferLen = 8;
        UCtx.usCurPos = 0;
        UCtx.usNotAckSize = 0;

        if (proxy_prepare(pCs, ucPCount, &final_i, &UCtx, 1, gpPs) != 1) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Proxy preparation error.\n");
            return -1;
        }

        if (modbus_set_slave(pM, pCs->ucSlaveID) != 0) {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Set slave id error: %s\n", modbus_strerror(errno));
            return -1;
        }

        iRes = libmodbus_proxy_exchange(pM, &UCtx, pcErrorDescr, ucErrorDescrSize);
        if (iRes != 1) return -1; // error description is already in error descr

        // decode answer...
        // read answer must be (5 + ucCount * 2) if success or 5 if failure
        if ((UCtx.usCurPos == 5) || (UCtx.usCurPos == ((((unsigned short)ucCount) << 1) + 5))) {

            usCRC = 0xFFFF;
            for(x = 0; x < (UCtx.usCurPos - 2); x++) crc_step(&usCRC, UCtx.pbBuffer[x]);
            if ((UCtx.pbBuffer[UCtx.usCurPos - 2] != (usCRC & 0xFF)) ||
                (UCtx.pbBuffer[UCtx.usCurPos - 1] != (usCRC >> 8))) {

                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "CRC Error.\n");
                return -1;
            }
            if (UCtx.pbBuffer[0] != ucSlaveID) {
                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Answer from incorrect slave.\n");
                return -1;
            }

            if (UCtx.usCurPos == 5) {
                if (UCtx.pbBuffer[1] & 0x80) {
                    // maybe retry if it is error 6 (temporary busy)

                    if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Error %hhu received.\n", UCtx.pbBuffer[2]);
                    return -1; //? maybe modbus error?
                } else {
                    if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Bad answer received (%hu bytes).\n", UCtx.usCurPos);
                    return -1;
                }
            }

            // potentially good answer
            if (UCtx.pbBuffer[2] == (ucCount << 1)) {
                bytes_to_regs(UCtx.pbBuffer + 3, pRegs, ucCount);
                return 1;
            } else {
                if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Bad payload size (%hhu bytes instead of %hhu).\n", UCtx.pbBuffer[2], (ucCount << 1));
                return -1;
            }
        } else {
            if (pcErrorDescr) snprintf(pcErrorDescr, ucErrorDescrSize, "Bad answer received (%hu bytes).\n", UCtx.usCurPos);
            return -1;
        }

    }
    return 1;
}

