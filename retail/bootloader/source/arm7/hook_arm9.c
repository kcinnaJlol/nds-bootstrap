#include <stdio.h>
#include <nds/ndstypes.h>
#include <nds/debug.h>
#include "patch.h"
#include "hook.h"
#include "common.h"
#include "cardengine_header_arm9.h"

#define b_expansionPakFound BIT(0)
#define b_extendedMemory BIT(1)
#define b_ROMinRAM BIT(2)
#define b_dsDebugRam BIT(3)
#define b_enableExceptionHandler BIT(4)
#define b_isSdk5 BIT(5)
#define b_overlaysInRam BIT(6)
#define b_cacheFlushFlag BIT(7)
#define b_cardReadFix BIT(8)


static const int MAX_HANDLER_LEN = 50;
static const int MAX_HANDLER_LEN_ALT = 0x200;

// same as arm7
static const u32 handlerStartSig[5] = {
	0xe92d4000, 	// push {lr}
	0xe3a0c301, 	// mov  ip, #0x4000000
	0xe28cce21,		// add  ip, ip, #0x210
	0xe51c1008,		// ldr	r1, [ip, #-8]
	0xe3510000		// cmp	r1, #0
};

// alt gsun
static const u32 handlerStartSigAlt[5] = {
	0xe3a0c301, 	// mov  ip, #0x4000000
	0xe5bc2208,		// 
	0xe1ec00d8,		//
	0xe3520000		// cmp	r2, #0
};

// same as arm7
static const u32 handlerEndSig[4] = {
	0xe59f1008, 	// ldr  r1, [pc, #8]	(IRQ Vector table address)
	0xe7910100,		// ldr  r0, [r1, r0, lsl #2]
	0xe59fe004,		// ldr  lr, [pc, #4]	(IRQ return address)
	0xe12fff10		// bx   r0
};

// alt gsun
static const u32 handlerEndSigAlt[4] = {
	0xe59f100C, 	// 
	0xe5813000,		// 
	0xe5813004,		// 
	0xeaffffb8		// 
};

static u32* hookInterruptHandler(const u32* start, size_t size) {
	// Find the start of the handler
	u32* addr = findOffset(
		start, size,
		handlerStartSig, 5
	);
	if (!addr) {
        addr = findOffset(
    		start, size,
    		handlerStartSigAlt, 4
    	);
        if (!addr) {
            dbg_printf("ERR_HOOK_9 : handlerStartSig\n");
    		return NULL;
        }
	}
    
    dbg_printf("handlerStartSig\n");
    dbg_hexa(addr);
    dbg_printf("\n");

	// Find the end of the handler
	u32* addr2 = findOffset(
		addr, MAX_HANDLER_LEN*sizeof(u32),
		handlerEndSig, 4
	);
	if (!addr2) {
        addr2 = findOffset(
    		addr, MAX_HANDLER_LEN_ALT*sizeof(u32),
    		handlerEndSigAlt, 4
    	);
        if (!addr2) {
            dbg_printf("ERR_HOOK_9 : handlerEndSig\n");
    		return NULL;
        }
    }
    
    
    
    dbg_printf("handlerEndSig\n");
    dbg_hexa(addr2);
    dbg_printf("\n");

	// Now find the IRQ vector table
	// Make addr point to the vector table address pointer within the IRQ handler
	addr = addr2 + sizeof(handlerEndSig)/sizeof(handlerEndSig[0]);

	// Use relative and absolute addresses to find the location of the table in RAM
	u32 tableAddr = addr[0];
    dbg_printf("tableAddr\n");
    dbg_hexa(tableAddr);
    dbg_printf("\n");
    
	u32 returnAddr = addr[1];
    dbg_printf("returnAddr\n");
    dbg_hexa(returnAddr);
    dbg_printf("\n");
    
	u32* actualReturnAddr = addr + 2;
	u32* actualTableAddr = actualReturnAddr + (tableAddr - returnAddr)/sizeof(u32);

	// The first entry in the table is for the Vblank handler, which is what we want
	return tableAddr;
	// 2     LCD V-Counter Match
}

int hookNdsRetailArm9(
	cardengineArm9* ce9,
	const module_params_t* moduleParams,
	u32 fileCluster,
	u32 saveCluster,
	u32 romFatTableCache,
	u32 savFatTableCache,
	u32 ramDumpCluster,
	u32 srParamsFileCluster,
	u32 pageFileCluster,
	bool expansionPakFound,
	bool extendedMemory,
	bool ROMinRAM,
	bool dsDebugRam,
	u8 enableExceptionHandler,
	u32 overlaysSize,
	u32 ioverlaysSize,
	u32 maxClusterCacheSize,
    u32 fatTableAddr
) {
	nocashMessage("hookNdsRetailArm9");

	const char* romTid = getRomTid(ndsHeader);
	extern u32 romSizeLimit;

	ce9->fileCluster            = fileCluster;
	ce9->saveCluster            = saveCluster;
	ce9->romFatTableCache       = romFatTableCache;
	ce9->savFatTableCache       = savFatTableCache;
	ce9->ramDumpCluster         = ramDumpCluster;
	ce9->srParamsCluster        = srParamsFileCluster;
	ce9->pageFileCluster        = pageFileCluster;
	if (expansionPakFound) {
		ce9->valueBits |= b_expansionPakFound;
	}
	if (extendedMemory) {
		ce9->valueBits |= b_extendedMemory;
	}
	if (ROMinRAM) {
		ce9->valueBits |= b_ROMinRAM;
	}
	if (dsDebugRam) {
		ce9->valueBits |= b_dsDebugRam;
	}
	if (enableExceptionHandler) {
		ce9->valueBits |= b_enableExceptionHandler;
	}
	if (isSdk5(moduleParams)) {
		ce9->valueBits |= b_isSdk5;
	}
	if ((expansionPakFound || (extendedMemory && !dsDebugRam && strncmp(romTid, "UBRP", 4) != 0)) && ce9->overlaysSize < romSizeLimit) {
		ce9->valueBits |= b_overlaysInRam;
	}
	if (strncmp(romTid, "CLJ", 3) == 0) {
		ce9->valueBits |= b_cacheFlushFlag;
	}
	ce9->overlaysSize           = overlaysSize;
	ce9->ioverlaysSize          = ioverlaysSize;
	if (extendedMemory && !dsDebugRam) {
		ce9->romLocation = 0x0C800000;
	} else {
		extern u32 romLocation;
		ce9->romLocation = romLocation;
	}

	if (strncmp(romTid, "IPK", 3) == 0 || strncmp(romTid, "IPG", 3) == 0 || strncmp(romTid, "B4T", 3) == 0) {
		ce9->valueBits |= b_cardReadFix;
	}

	extern u32 iUncompressedSize;

    u32* tableAddr = patchOffsetCache.a9IrqHandlerOffset;
 	if (!tableAddr) {
		tableAddr = hookInterruptHandler((u32*)ndsHeader->arm9destination, iUncompressedSize);
	}

    if (!tableAddr) {
		dbg_printf("ERR_HOOK_9\n");
		return ERR_HOOK;
	}

    dbg_printf("hookLocation arm9: ");
	dbg_hexa((u32)tableAddr);
	dbg_printf("\n\n");
	patchOffsetCache.a9IrqHandlerOffset = tableAddr;

    /*u32* vblankHandler = hookLocation;
    u32* dma0Handler = hookLocation + 8;
    u32* dma1Handler = hookLocation + 9;
    u32* dma2Handler = hookLocation + 10;
    u32* dma3Handler = hookLocation + 11;
    u32* ipcSyncHandler = hookLocation + 16;
    u32* cardCompletionIrq = hookLocation + 19;*/
    
    ce9->irqTable = tableAddr;

	nocashMessage("ERR_NONE");
	return ERR_NONE;
}
