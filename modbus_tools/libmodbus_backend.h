/////////////////////
//
//  Modbus proxy - libmodbus backend part
//
/////////////////////

#ifndef __LIBMODBUS_PROXY_BACKEND_H_20201030__
#define __LIBMODBUS_PROXY_BACKEND_H_20201030__
 

#include "../../transport/modbus/modbus_proxy.h"
#include <modbus.h>
#ifdef WITH_DUMMY_PROXY_BK
#include "emu.h"
#endif

#ifndef MAX_PROXIES_DEPTH
#define MAX_PROXIES_DEPTH 10
#endif


/////////////////////////////
//
// proxy backend part

typedef struct libmodbus_proxy_bk_ctx_t {
    // logic & config
    proxy_modbus_logic *pL;

    // buffer
    unsigned char pbBuffer[256];
    unsigned short usSize;
    unsigned uAvail;

    unsigned char ucState; // = 0

    union {
#ifdef WITH_DUMMY_PROXY_BK
        proxy_emu *pEmu;
#endif
        modbus_t *pM;
    };
} libmodbus_proxy_bk_ctx;

// maybe uplink as param here?

#ifdef WITH_DUMMY_PROXY_BK
int dummy_proxy_bk_start(libmodbus_proxy_bk_ctx *pCtx);

int dummy_proxy_bk_ready(libmodbus_proxy_bk_ctx *pCtx);
#endif

int libmodbus_proxy_bk_start(libmodbus_proxy_bk_ctx *pCtx);

int libmodbus_proxy_bk_ready(libmodbus_proxy_bk_ctx *pCtx);

////////////////////////////
//
// common part

int libmodbus_bk_write_regs(modbus_t *pM, unsigned char ucSlaveID, proxy_modbus_conf *pCs, unsigned char ucPCount,
    unsigned short usReg, unsigned short *pRegs, unsigned short ucCount,
    char *pcErrorDescr, unsigned char ucErrorDescrSize);

int libmodbus_bk_read_regs(modbus_t *pM, unsigned char ucSlaveID, proxy_modbus_conf *pCs, unsigned char ucPCount,
    unsigned short usReg, unsigned short *pRegs, unsigned short ucCount,
    char *pcErrorDescr, unsigned char ucErrorDescrSize);

#endif  /* __LIBMODBUS_PROXY_BACKEND_H_20201030__ */
