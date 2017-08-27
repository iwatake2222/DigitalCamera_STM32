/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
  ******************************************************************************
  * This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether 
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * Copyright (c) 2017 STMicroelectronics International N.V. 
  * All rights reserved.
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/* 
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future. 
 * Kept to ensure backward compatibility with previous CubeMx versions when 
 * migrating projects. 
 * User code previously added there should be copied in the new user sections before 
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"

/* code for porting (refer to sample code by Chan-san) */
#include "main.h"
#include "stm32f4xx_hal.h"
#include "common.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* code for porting (refer to sample code(mmc_stm32f1.c) by Chan-san) */
#define CS_HIGH() HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
#define CS_LOW()  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
#define MMC_CD    1 /* Card detect (yes:true, no:false, default:true) -> always detect */
#define MMC_WP    0 /* Write protected (yes:true, no:false, default:false) */

/* MMC/SD command */
#define CMD0  (0)     /* GO_IDLE_STATE */
#define CMD1  (1)     /* SEND_OP_COND (MMC) */
#define ACMD41  (0x80+41) /* SEND_OP_COND (SDC) */
#define CMD8  (8)     /* SEND_IF_COND */
#define CMD9  (9)     /* SEND_CSD */
#define CMD10 (10)    /* SEND_CID */
#define CMD12 (12)    /* STOP_TRANSMISSION */
#define ACMD13  (0x80+13) /* SD_STATUS (SDC) */
#define CMD16 (16)    /* SET_BLOCKLEN */
#define CMD17 (17)    /* READ_SINGLE_BLOCK */
#define CMD18 (18)    /* READ_MULTIPLE_BLOCK */
#define CMD23 (23)    /* SET_BLOCK_COUNT (MMC) */
#define ACMD23  (0x80+23) /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24 (24)    /* WRITE_BLOCK */
#define CMD25 (25)    /* WRITE_MULTIPLE_BLOCK */
#define CMD32 (32)    /* ERASE_ER_BLK_START */
#define CMD33 (33)    /* ERASE_ER_BLK_END */
#define CMD38 (38)    /* ERASE */
#define CMD55 (55)    /* APP_CMD */
#define CMD58 (58)    /* READ_OCR */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC    0x01    /* MMC ver 3 */
#define CT_SD1    0x02    /* SD ver 1 */
#define CT_SD2    0x04    /* SD ver 2 */
#define CT_SDC    (CT_SD1|CT_SD2) /* SD */
#define CT_BLOCK  0x08    /* Block addressing */

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* code for porting (refer to sample code(mmc_stm32f1.c) by Chan-san) */
extern SPI_HandleTypeDef hspi1;
static BYTE CardType;      /* Card type flags */

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/
static void FCLK_SLOW()
{
  // there should be better way...
  hspi1.Instance->CR1 = (hspi1.Instance->CR1 & 0xffffffC7) | SPI_BAUDRATEPRESCALER_256;
}
static void FCLK_FAST()
{
  // there should be better way...
  hspi1.Instance->CR1 = (hspi1.Instance->CR1 & 0xffffffC7) | SPI_BAUDRATEPRESCALER_2;
}

/* Initialize MMC interface */
static
void init_spi (void)
{
  // done by MX_SPI1_Init
}


/* Exchange a byte */
static
BYTE xchg_spi (
  BYTE dat  /* Data to send */
)
{
  BYTE recvData;
  HAL_SPI_TransmitReceive(&hspi1, &dat, &recvData, 1, 100);
  return (BYTE)recvData;
}


/* Receive multiple byte */
static
void rcvr_spi_multi (
  BYTE *buff,   /* Pointer to data buffer */
  UINT btr    /* Number of bytes to receive (even number) */
)
{
  HAL_SPI_Receive(&hspi1, buff, btr, 100);
}


#if _USE_WRITE
/* Send multiple byte */
static
void xmit_spi_multi (
  const BYTE *buff, /* Pointer to the data */
  UINT btx      /* Number of bytes to send (even number) */
)
{
  HAL_SPI_Transmit(&hspi1, buff, btx, 100);
}
#endif


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static
int wait_ready (  /* 1:Ready, 0:Timeout */
  UINT wt     /* Timeout [ms] */
)
{
  BYTE d;

  uint32_t start = HAL_GetTick();
  do {
    d = xchg_spi(0xFF);
    /* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
  } while (d != 0xFF && ((HAL_GetTick() - start) < wt));  /* Wait for card goes ready or timeout */

  return (d == 0xFF) ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/
static
void fs_deselect (void)
{
  CS_HIGH();    /* Set CS# high */
  xchg_spi(0xFF); /* Dummy clock (force DO hi-z for multiple slave SPI) */
}

/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/
static
int fs_select (void) /* 1:OK, 0:Timeout */
{
  CS_LOW();   /* Set CS# low */
  xchg_spi(0xFF); /* Dummy clock (force DO enabled) */
  if (wait_ready(500)) return 1;  /* Wait for card ready */

  fs_deselect();
  return 0; /* Timeout */
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/
static
int rcvr_datablock (  /* 1:OK, 0:Error */
  BYTE *buff,     /* Data buffer */
  UINT btr      /* Data block length (byte) */
)
{
  BYTE token;

  uint32_t start = HAL_GetTick();
  do {              /* Wait for DataStart token in timeout of 200ms */
    token = xchg_spi(0xFF);
    /* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
  } while ((token == 0xFF) && ((HAL_GetTick() - start) < 200));
  if(token != 0xFE) return 0;   /* Function fails if invalid DataStart token or timeout */

  rcvr_spi_multi(buff, btr);    /* Store trailing data to the buffer */
  xchg_spi(0xFF); xchg_spi(0xFF);     /* Discard CRC */

  return 1;           /* Function succeeded */
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to the MMC                                         */
/*-----------------------------------------------------------------------*/
#if _USE_WRITE
static
int xmit_datablock (  /* 1:OK, 0:Failed */
  const BYTE *buff, /* Ponter to 512 byte data to be sent */
  BYTE token      /* Token */
)
{
  BYTE resp;


  if (!wait_ready(500)) return 0;   /* Wait for card ready */

  xchg_spi(token);          /* Send token */
  if (token != 0xFD) {        /* Send data if token is other than StopTran */
    xmit_spi_multi(buff, 512);    /* Data */
    xchg_spi(0xFF); xchg_spi(0xFF); /* Dummy CRC */

    resp = xchg_spi(0xFF);        /* Receive data resp */
    if ((resp & 0x1F) != 0x05) return 0;  /* Function fails if the data packet was not accepted */
  }
  return 1;
}
#endif

/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/
static
BYTE send_cmd (   /* Return value: R1 resp (bit7==1:Failed to send) */
  BYTE cmd,   /* Command index */
  DWORD arg   /* Argument */
)
{
  BYTE n, res;

  if (cmd & 0x80) { /* Send a CMD55 prior to ACMD<n> */
    cmd &= 0x7F;
    res = send_cmd(CMD55, 0);
    if (res > 1) return res;
  }

  /* Select the card and wait for ready except to stop multiple block read */
  if (cmd != CMD12) {
    fs_deselect();
    if (!fs_select()) return 0xFF;
  }

  /* Send command packet */
  xchg_spi(0x40 | cmd);       /* Start + command index */
  xchg_spi((BYTE)(arg >> 24));    /* Argument[31..24] */
  xchg_spi((BYTE)(arg >> 16));    /* Argument[23..16] */
  xchg_spi((BYTE)(arg >> 8));     /* Argument[15..8] */
  xchg_spi((BYTE)arg);        /* Argument[7..0] */
  n = 0x01;             /* Dummy CRC + Stop */
  if (cmd == CMD0) n = 0x95;      /* Valid CRC for CMD0(0) */
  if (cmd == CMD8) n = 0x87;      /* Valid CRC for CMD8(0x1AA) */
  xchg_spi(n);

  /* Receive command resp */
  if (cmd == CMD12) xchg_spi(0xFF); /* Diacard following one byte when CMD12 */
  n = 10;               /* Wait for response (10 bytes max) */
  do {
    res = xchg_spi(0xFF);
  } while ((res & 0x80) && --n);

  return res;             /* Return received response */
}

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
           
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);  
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read, 
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */  
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
  /* code for porting (refer to sample code(mmc_stm32f1.c) by Chan-san) */
  BYTE n, cmd, ty, ocr[4];


  if (pdrv) return STA_NOINIT;     /* Supports only drive 0 */
  init_spi();             /* Initialize SPI */

  if (Stat & STA_NODISK) return Stat; /* Is card existing in the soket? */

  FCLK_SLOW();
  for (n = 10; n; n--) xchg_spi(0xFF);  /* Send 80 dummy clocks */

  ty = 0;
  if (send_cmd(CMD0, 0) == 1) {     /* Put the card SPI/Idle state */
    uint32_t start = HAL_GetTick();            /* Initialization timeout = 1 sec */
    if (send_cmd(CMD8, 0x1AA) == 1) { /* SDv2? */
      for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);  /* Get 32 bit return value of R7 resp */
      if (ocr[2] == 0x01 && ocr[3] == 0xAA) {       /* Is the card supports vcc of 2.7-3.6V? */
        while (((HAL_GetTick() - start) < 1000) && send_cmd(ACMD41, 1UL << 30)) ; /* Wait for end of initialization with ACMD41(HCS) */
        if (((HAL_GetTick() - start) < 1000) && send_cmd(CMD58, 0) == 0) {    /* Check CCS bit in the OCR */
          for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
          ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;  /* Card id SDv2 */
        }
      }
    } else {  /* Not SDv2 card */
      if (send_cmd(ACMD41, 0) <= 1)   { /* SDv1 or MMC? */
        ty = CT_SD1; cmd = ACMD41;  /* SDv1 (ACMD41(0)) */
      } else {
        ty = CT_MMC; cmd = CMD1;  /* MMCv3 (CMD1(0)) */
      }
      while (((HAL_GetTick() - start) < 1000) && send_cmd(cmd, 0)) ;    /* Wait for end of initialization */
      if (!((HAL_GetTick() - start) < 1000) || send_cmd(CMD16, 512) != 0) /* Set block length: 512 */
        ty = 0;
    }
  }
  CardType = ty;  /* Card type */
  fs_deselect();

  if (ty) {     /* OK */
    FCLK_FAST();      /* Set fast clock */
    Stat &= ~STA_NOINIT;  /* Clear STA_NOINIT flag */
  } else {      /* Failed */
    Stat = STA_NOINIT;
  }

  return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status 
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
  if (pdrv) return STA_NOINIT;   /* Supports only drive 0 */
  return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s) 
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
  /* code for porting (refer to sample code(mmc_stm32f1.c) by Chan-san) */
  if (pdrv || !count) return RES_PARERR;   /* Check parameter */
  if (Stat & STA_NOINIT) return RES_NOTRDY; /* Check if drive is ready */

  if (!(CardType & CT_BLOCK)) sector *= 512;  /* LBA ot BA conversion (byte addressing cards) */

  if (count == 1) { /* Single sector read */
    if ((send_cmd(CMD17, sector) == 0)  /* READ_SINGLE_BLOCK */
      && rcvr_datablock(buff, 512)) {
      count = 0;
    }
  }
  else {        /* Multiple sector read */
    if (send_cmd(CMD18, sector) == 0) { /* READ_MULTIPLE_BLOCK */
      do {
        if (!rcvr_datablock(buff, 512)) break;
        buff += 512;
      } while (--count);
      send_cmd(CMD12, 0);       /* STOP_TRANSMISSION */
    }
  }
  fs_deselect();

  return count ? RES_ERROR : RES_OK;  /* Return result */
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)  
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{ 
  /* USER CODE BEGIN WRITE */
  /* USER CODE HERE */
  /* code for porting (refer to sample code(mmc_stm32f1.c) by Chan-san) */
  if (pdrv || !count) return RES_PARERR;   /* Check parameter */
  if (Stat & STA_NOINIT) return RES_NOTRDY; /* Check drive status */
  if (Stat & STA_PROTECT) return RES_WRPRT; /* Check write protect */

  if (!(CardType & CT_BLOCK)) sector *= 512;  /* LBA ==> BA conversion (byte addressing cards) */

  if (count == 1) { /* Single sector write */
    if ((send_cmd(CMD24, sector) == 0)  /* WRITE_BLOCK */
      && xmit_datablock(buff, 0xFE)) {
      count = 0;
    }
  }
  else {        /* Multiple sector write */
    if (CardType & CT_SDC) send_cmd(ACMD23, count); /* Predefine number of sectors */
    if (send_cmd(CMD25, sector) == 0) { /* WRITE_MULTIPLE_BLOCK */
      do {
        if (!xmit_datablock(buff, 0xFC)) break;
        buff += 512;
      } while (--count);
      if (!xmit_datablock(0, 0xFD)) count = 1;  /* STOP_TRAN token */
    }
  }
  fs_deselect();

  return count ? RES_ERROR : RES_OK;  /* Return result */
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation  
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
  /* code for porting (refer to sample code(mmc_stm32f1.c) by Chan-san) */
  DRESULT res;
  BYTE n, csd[16];
  DWORD *dp, st, ed, csize;

  if (pdrv) return RES_PARERR;         /* Check parameter */
  if (Stat & STA_NOINIT) return RES_NOTRDY; /* Check if drive is ready */

  res = RES_ERROR;

  switch (cmd) {
  case CTRL_SYNC :    /* Wait for end of internal write process of the drive */
    if (fs_select()) res = RES_OK;
    break;

  case GET_SECTOR_COUNT : /* Get drive capacity in unit of sector (DWORD) */
    if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
      if ((csd[0] >> 6) == 1) { /* SDC ver 2.00 */
        csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
        *(DWORD*)buff = csize << 10;
      } else {          /* SDC ver 1.XX or MMC ver 3 */
        n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
        csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
        *(DWORD*)buff = csize << (n - 9);
      }
      res = RES_OK;
    }
    break;

  case GET_BLOCK_SIZE : /* Get erase block size in unit of sector (DWORD) */
    if (CardType & CT_SD2) {  /* SDC ver 2.00 */
      if (send_cmd(ACMD13, 0) == 0) { /* Read SD status */
        xchg_spi(0xFF);
        if (rcvr_datablock(csd, 16)) {        /* Read partial block */
          for (n = 64 - 16; n; n--) xchg_spi(0xFF); /* Purge trailing data */
          *(DWORD*)buff = 16UL << (csd[10] >> 4);
          res = RES_OK;
        }
      }
    } else {          /* SDC ver 1.XX or MMC */
      if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {  /* Read CSD */
        if (CardType & CT_SD1) {  /* SDC ver 1.XX */
          *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
        } else {          /* MMC */
          *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
        }
        res = RES_OK;
      }
    }
    break;

  case CTRL_TRIM :  /* Erase a block of sectors (used when _USE_ERASE == 1) */
    if (!(CardType & CT_SDC)) break;        /* Check if the card is SDC */
    if (disk_ioctl(pdrv, MMC_GET_CSD, csd)) break; /* Get CSD */
    if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break; /* Check if sector erase can be applied to the card */
    dp = buff; st = dp[0]; ed = dp[1];        /* Load sector block */
    if (!(CardType & CT_BLOCK)) {
      st *= 512; ed *= 512;
    }
    if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) { /* Erase sector block */
      res = RES_OK; /* FatFs does not check result of this command */
    }
    break;

  default:
    res = RES_PARERR;
  }

  fs_deselect();

  return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
