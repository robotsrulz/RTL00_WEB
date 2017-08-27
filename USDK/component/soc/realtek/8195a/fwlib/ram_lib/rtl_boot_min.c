/* 
 *  (SRAM) Debug BootLoader
 *  Created on: 12/02/2017
 *      Author: pvvx
 */

#include "platform_autoconf.h"
#include "rtl_bios_data.h"
#include "diag.h"
#include "rtl8195a/rtl8195a_sys_on.h"

//-------------------------------------------------------------------------
// Data declarations
//extern u32 STACK_TOP;
//extern volatile UART_LOG_CTL * pUartLogCtl;
#ifndef DEFAULT_BAUDRATE
#define DEFAULT_BAUDRATE UART_BAUD_RATE_38400
#endif

#define BOOT_RAM_TEXT_SECTION // __attribute__((section(".boot.text")))
//#define BOOT_RAM_RODATA_SECTION __attribute__((section(".boot.rodata")))
//#define BOOT_RAM_DATA_SECTION __attribute__((section(".boot.data")))
//#define BOOT_RAM_BSS_SECTION __attribute__((section(".boot.bss")))

//-------------------------------------------------------------------------
typedef struct _seg_header {
	uint32 size;
	uint32 ldaddr;
} IMGSEGHEAD, *PIMGSEGHEAD;

typedef struct _img2_header {
	IMGSEGHEAD seg;
	uint32 sign[2];
	void (*startfunc)(void);
	uint8 rtkwin[7];
	uint8 ver[13];
	uint8 name[32];
} IMG2HEAD, *PIMG2HEAD;

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 		4096
#endif
//-------------------------------------------------------------------------
// Function declarations
LOCAL void RtlBootToFlash(void); // image1
LOCAL void RtlBootToSram(void); // image1
LOCAL void EnterImage15(void); // image1
LOCAL void JtagOn(void); // image1

extern _LONG_CALL_ VOID HalCpuClkConfig(unsigned char CpuType);
extern _LONG_CALL_ VOID VectorTableInitRtl8195A(u32 StackP);
extern _LONG_CALL_ VOID HalInitPlatformLogUartV02(VOID);
extern _LONG_CALL_ VOID HalInitPlatformTimerV02(VOID);

//#pragma arm section code = ".boot.text";
//#pragma arm section rodata = ".boot.rodata", rwdata = ".boot.data", zidata = ".boot.bss";

typedef void (*START_FUNC)(void);

/* Start table: */
START_RAM_FUN_SECTION RAM_FUNCTION_START_TABLE __ram_start_table_start__ = {
		RtlBootToFlash + 1,	// StartFun(),	Run if ( v400001F4 & 0x8000000 ) && ( v40000210 & 0x80000000 )
		RtlBootToSram + 1,	// PatchWAKE(),	Run if ( v40000210 & 0x20000000 )
		RtlBootToSram + 1,	// PatchFun0(), Run if ( v40000210 & 0x10000000 )
		RtlBootToSram + 1,// PatchFun1(), Run if ( v400001F4 & 0x8000000 ) && ( v40000210 & 0x8000000 )
		RtlBootToFlash + 1 };// PatchFun2(), Run for Init console, if ( v40000210 & 0x4000000 )
//		EnterImage15 + 1};	// PatchFun2(), Run for Init console, if ( v40000210 & 0x4000000 )

/* Set Debug Flags */
LOCAL void BOOT_RAM_TEXT_SECTION SetDebugFlgs() {
#if CONFIG_DEBUG_LOG > 3
	CfgSysDebugWarn = -1;
	CfgSysDebugInfo = -1;
	CfgSysDebugErr = -1;
	ConfigDebugWarn = -1;
	ConfigDebugInfo = -1;
	ConfigDebugErr = -1;
#elif CONFIG_DEBUG_LOG > 1
	CfgSysDebugWarn = -1;
//	CfgSysDebugInfo = 0;
	CfgSysDebugErr = -1;
	ConfigDebugWarn = -1;
//	ConfigDebugInfo = 0;
	ConfigDebugErr = -1;
#elif CONFIG_DEBUG_LOG > 0
//	CfgSysDebugWarn = 0;
//	CfgSysDebugInfo = 0;
	CfgSysDebugErr = -1;
//	ConfigDebugWarn = 0;
//	ConfigDebugInfo = 0;
	ConfigDebugErr = -1;
#else
//	CfgSysDebugWarn = 0;
//	CfgSysDebugInfo = 0;
//	CfgSysDebugErr = 0;
//	ConfigDebugWarn = 0;
//	ConfigDebugInfo = 0;
//	ConfigDebugErr = 0;
#endif
}

/* RTL Console ROM */
LOCAL void BOOT_RAM_TEXT_SECTION RtlConsolRam(void) {
//	DiagPrintf("\r\nRTL Console ROM\r\n");
	pUartLogCtl->pTmpLogBuf->UARTLogBuf[0] = '?';
	pUartLogCtl->pTmpLogBuf->BufCount = 1;
	pUartLogCtl->ExecuteCmd = 1;
	RtlConsolTaskRom(pUartLogCtl);
}

/* JTAG On */
LOCAL void BOOT_RAM_TEXT_SECTION JtagOn(void) {
	ACTCK_VENDOR_CCTRL(ON);
	SLPCK_VENDOR_CCTRL(ON);
	HalPinCtrlRtl8195A(JTAG, 0, 1);
}

/* Enter Image 1.5 */
LOCAL void BOOT_RAM_TEXT_SECTION EnterImage15(void) {
	SetDebugFlgs();
	DBG_8195A(
			"\n===== Enter SRAM-Boot ====\nImg Sign: %s, Go @ 0x%08x\r\n",
			&__image2_validate_code__, __image2_entry_func__);
#if CONFIG_DEBUG_LOG > 2
	DBG_8195A("CPU CLK: %d Hz, SOC FUNC EN: %p\r\n", HalGetCpuClk(),
			HAL_PERI_ON_READ32(REG_SOC_FUNC_EN));
#endif
	if (_strcmp((const char *) &__image2_validate_code__, IMG2_SIGN_TXT)) {
		DBG_MISC_ERR("Invalid Image Signature!\n");
		RtlConsolRam();
	}
	__image2_entry_func__();
}

/* RtlBootToSram */
LOCAL void BOOT_RAM_TEXT_SECTION RtlBootToSram(void) {
	JtagOn(); /* JTAG On */
	_memset(&__rom_bss_start__, 0, &__rom_bss_end__ - &__rom_bss_start__);
	__asm__ __volatile__ ("cpsid f\n");
	HAL_SYS_CTRL_WRITE32(REG_SYS_SYSPLL_CTRL1,
			HAL_SYS_CTRL_READ32(REG_SYS_SYSPLL_CTRL1) & ( ~BIT_SYS_SYSPLL_DIV5_3));
	HalCpuClkConfig(2); // 41.666666 MHz
//	HAL_SYS_CTRL_WRITE32(REG_SYS_SYSPLL_CTRL1, HAL_SYS_CTRL_READ32(REG_SYS_SYSPLL_CTRL1) | BIT_SYS_SYSPLL_DIV5_3); // 50.000 MHz
	VectorTableInitRtl8195A(STACK_TOP);	// 0x1FFFFFFC
	HalInitPlatformLogUartV02();
	HalInitPlatformTimerV02();
	__asm__ __volatile__ ("cpsie f\n");
	//	SdrPowerOff();
	SDR_PIN_FCTRL(OFF);
	LDO25M_CTRL(OFF);
	HAL_PERI_ON_WRITE32(REG_SOC_FUNC_EN,
			HAL_PERI_ON_READ32(REG_SOC_FUNC_EN) | BIT(21));

	SpicInitRtl8195AV02(1, 0); // StartupSpicBaudRate InitBaudRate 1, SpicBitMode 1 StartupSpicBitMode
	EnterImage15();
}

/*-------------------------------------------------------------------------------------
 Копирует данные из области align(4) (flash, registers, ...) в область align(1) (ram)
 --------------------------------------------------------------------------------------*/
LOCAL unsigned int BOOT_RAM_TEXT_SECTION flashcpy(unsigned int faddr,
		void *dist, unsigned int size) {
	union {
		unsigned char uc[4];
		unsigned int ud;
	} tmp;
	if (faddr < SPI_FLASH_BASE)
		faddr += SPI_FLASH_BASE;
	unsigned char * pd = (unsigned char *) dist;
	unsigned int *p = (unsigned int *) ((unsigned int) faddr & (~3));
	unsigned int xlen = (unsigned int) faddr & 3;
	unsigned int len = size;

	if (xlen) {
		tmp.ud = *p++;
		while (len) {
			len--;
			*pd++ = tmp.uc[xlen++];
			if (xlen & 4)
				break;
		};
	};
	xlen = len >> 2;
	while (xlen) {
		tmp.ud = *p++;
		*pd++ = tmp.uc[0];
		*pd++ = tmp.uc[1];
		*pd++ = tmp.uc[2];
		*pd++ = tmp.uc[3];
		xlen--;
	};
	if (len & 3) {
		tmp.ud = *p;
		pd[0] = tmp.uc[0];
		if (len & 2) {
			pd[1] = tmp.uc[1];
			if (len & 1) {
				pd[2] = tmp.uc[2];
			};
		};
	};
	return size;
}

enum {
	SEG_ID_ERR,
	SEG_ID_SRAM,
	SEG_ID_TCM,
	SEG_ID_SDRAM,
	SEG_ID_SOC,
	SEG_ID_FLASH,
	SEG_ID_CPU,
	SEG_ID_ROM,
	SEG_ID_MAX
} SEG_ID;

LOCAL const char * const txt_tab_seg[] = {
		"UNK",		// 0
		"SRAM",		// 1
		"TCM",		// 2
		"SDRAM",	// 3
		"SOC",		// 4
		"FLASH",	// 5
		"CPU",		// 6
		"ROM"		// 7
		};

LOCAL const uint32 tab_seg_def[] = { 0x10000000, 0x10070000, 0x1fff0000,
		0x20000000, 0x30000000, 0x30200000, 0x40000000, 0x40800000, 0x98000000,
		0xA0000000, 0xE0000000, 0xE0010000, 0x00000000, 0x00050000 };

LOCAL uint32 BOOT_RAM_TEXT_SECTION get_seg_id(uint32 addr, int32 size) {
	uint32 ret = SEG_ID_ERR;
	uint32 * ptr = &tab_seg_def;
	if (size > 0) {
		do {
			ret++;
			if (addr >= ptr[0] && addr + size <= ptr[1]) {
				return ret;
			};
			ptr += 2;
		} while (ret < SEG_ID_MAX);
	};
	return 0;
}

LOCAL uint32 BOOT_RAM_TEXT_SECTION load_img2_head(uint32 faddr, PIMG2HEAD hdr) {
	flashcpy(faddr, hdr, sizeof(IMG2HEAD));
	uint32 ret = get_seg_id(hdr->seg.ldaddr, hdr->seg.size);
	if (hdr->sign[1] == IMG_SIGN2_RUN) {
		if (hdr->sign[0] == IMG_SIGN1_RUN) {
			ret |= 1 << 9;
		} else if (hdr->sign[0] == IMG_SIGN1_SWP) {
			ret |= 1 << 8;
		};
	}
	if (*(u32 *) (&hdr->rtkwin) == IMG2_SIGN_DW1_TXT) {
		ret |= 1 << 10;
	};
	return ret;
}

LOCAL uint32 BOOT_RAM_TEXT_SECTION load_segs(uint32 faddr, PIMG2HEAD hdr,
		uint8 flgload) {
	uint32 fnextaddr = faddr;
	uint8 segnum = 0;
	while (1) {
		uint32 seg_id = get_seg_id(hdr->seg.ldaddr, hdr->seg.size);
		if (flgload && (seg_id == SEG_ID_SRAM || seg_id == SEG_ID_TCM)) {
#if CONFIG_DEBUG_LOG > 1
			DBG_8195A("Load Flash seg%d: 0x%08x -> %s: 0x%08x, size: %d\n",
					segnum, faddr, txt_tab_seg[seg_id], hdr->seg.ldaddr,
					hdr->seg.size);
#endif
			fnextaddr += flashcpy(fnextaddr, hdr->seg.ldaddr, hdr->seg.size);
		} else if (seg_id) {
#if CONFIG_DEBUG_LOG > 2
			DBG_8195A("Skip Flash seg%d: 0x%08x -> %s: 0x%08x, size: %d\n", segnum,
					faddr, txt_tab_seg[seg_id], hdr->seg.ldaddr, hdr->seg.size);
#endif
			fnextaddr += hdr->seg.size;
		} else {
			break;
		}
		fnextaddr += flashcpy(fnextaddr, &hdr->seg, sizeof(IMGSEGHEAD));
		segnum++;
	}
	return fnextaddr;
}

/*-------------------------------------------------------------------------------------
 * 0 - default image (config data + 0), 1 - image N1, 2 - image N2, ...
 --------------------------------------------------------------------------------------*/
LOCAL int BOOT_RAM_TEXT_SECTION loadUserImges(int imgnum) {
	IMG2HEAD hdr;
	int imagenum = 1;
	uint32 faddr = 0xb000; // start image2 in flash
	DBG_8195A("Selected Image %d.\n", imgnum);

	while (1) {
		faddr = (faddr + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
		uint32 img_id = load_img2_head(faddr, &hdr);
		if ((img_id >> 8) > 4 || (uint8) img_id != 0) {
			faddr = load_segs(faddr + 0x10, &hdr.seg, imagenum == imgnum);
			if (imagenum == imgnum) {
//				DBG_8195A("Image%d: %s\n", imgnum, hdr.name);
				break;
			}
			imagenum++;
		} else if (imagenum) {
			DBG_8195A("No Image%d! Trying Image0...\n", imgnum);
			// пробуем загрузить image по умолчанию, по записи в секторе установок
			flashcpy(FLASH_SYSTEM_DATA_ADDR, &faddr, sizeof(faddr));
			if (faddr < 0x8000000)
				faddr += SPI_FLASH_BASE;
			if (get_seg_id(faddr, 0x100) == SEG_ID_FLASH) {
				imagenum = 0;
				imgnum = 0;
			} else {
				DBG_8195A("No Image0!\n");
				imagenum = -1;
				break;
			};
		} else {
			imagenum = -1;
			break;
		}
	};
	return imagenum;
}
;

extern PHAL_GPIO_ADAPTER _pHAL_Gpio_Adapter;
extern HAL_GPIO_ADAPTER gBoot_Gpio_Adapter;
//----- IsForceLoadDefaultImg2
LOCAL uint8 BOOT_RAM_TEXT_SECTION IsForceLoadDefaultImg2(void) {
	uint8 gpio_pin[4];
	HAL_GPIO_PIN GPIO_Pin;
	HAL_GPIO_PIN_STATE flg;
	int result = 0;
	flashcpy(FLASH_SYSTEM_DATA_ADDR + 0x08, &gpio_pin, sizeof(gpio_pin)); // config data + 8
	_pHAL_Gpio_Adapter = &gBoot_Gpio_Adapter;
	for (int i = 1; i; i--) {
		uint8 x = gpio_pin[i];
		result <<= 1;
		if (x != 0xff) {
			GPIO_Pin.pin_name = HAL_GPIO_GetIPPinName_8195a(x & 0x7F);
			if (x & 0x80) {
				GPIO_Pin.pin_mode = DIN_PULL_LOW;
				flg = GPIO_PIN_HIGH;
			} else {
				GPIO_Pin.pin_mode = DIN_PULL_HIGH;
				flg = GPIO_PIN_LOW;
			}
			HAL_GPIO_Init_8195a(&GPIO_Pin);
			if (HAL_GPIO_ReadPin_8195a(&GPIO_Pin) == flg) {
				result |= 1;
			}
			HAL_GPIO_DeInit_8195a(&GPIO_Pin);
		}
	}
	_pHAL_Gpio_Adapter->IrqHandle.IrqFun = NULL;
	return result;
}

LOCAL void BOOT_RAM_TEXT_SECTION RtlBootToFlash(void) {

	JtagOn(); /* JTAG On */
	SetDebugFlgs();
	DBG_8195A("===== Enter FLASH-Boot ====\n");
	if (HAL_PERI_ON_READ32(REG_SOC_FUNC_EN) & (1 << BIT_SOC_FLASH_EN)) {
		SPI_FLASH_PIN_FCTRL(ON);
		/*
		 if(!SpicCmpDataForCalibrationRtl8195A()) {
		 DBG_8195A("ReInit Spic DIO...\n");
		 SpicInitRtl8195AV02(1, SpicDualBitMode);
		 }
		 */
		loadUserImges(IsForceLoadDefaultImg2() + 1);
	};
	if (_strcmp((const char *) &__image2_validate_code__, IMG2_SIGN_TXT)) {
		DBG_8195A("Invalid Image Signature!\n");
		RtlConsolRam();
	} else
		DBG_8195A("Go @ 0x%08x\r\n", __image2_entry_func__);
	__image2_entry_func__();
}
