/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "../../../../inc/MarlinConfigPre.h"

#if HAS_TFT_LVGL_UI

#include "draw_ui.h"
#include "wifi_module.h"
#include "wifi_upload.h"
#include "SPI_TFT.h"

#if USE_WIFI_FUNCTION

#include "../../../../MarlinCore.h"
#include "../../../../module/temperature.h"
#include "../../../../gcode/queue.h"
#include "../../../../gcode/gcode.h"
#include "../../../../lcd/ultralcd.h"
#include "../../../../sd/cardreader.h"
#include "../../../../module/planner.h"
#if ENABLED(POWER_LOSS_RECOVERY)
#include "../../../../feature/powerloss.h"
#endif
#if ENABLED(PARK_HEAD_ON_PAUSE)
#include "../../../../feature/pause.h"
#endif

#define WIFI_SET()				WRITE(WIFI_RESET_PIN, HIGH);
#define WIFI_RESET()			WRITE(WIFI_RESET_PIN, LOW);
#define WIFI_IO1_SET()			WRITE(WIFI_IO1_PIN, HIGH);     
#define WIFI_IO1_RESET()		WRITE(WIFI_IO1_PIN, LOW);

extern uint8_t Explore_Disk (char* path , uint8_t recu_level);

extern uint8_t commands_in_queue;
extern uint8_t sel_id;

int usartFifoAvailable(SZ_USART_FIFO *fifo);
int readUsartFifo(SZ_USART_FIFO *fifo, int8_t *buf, int32_t len);
int writeUsartFifo(SZ_USART_FIFO * fifo, int8_t * buf, int32_t len);
extern unsigned int  getTickDiff(unsigned int curTick, unsigned int  lastTick);

volatile SZ_USART_FIFO  WifiRxFifo;

#define WAIT_ESP_TRANS_TIMEOUT_TICK	10500

int cfg_cloud_flag = 0;

extern PRINT_TIME print_time;

char wifi_firm_ver[20] = {0};
WIFI_GCODE_BUFFER espGcodeFifo; 
extern uint8_t pause_resum;

uint8_t wifi_connect_flg = 0;
extern volatile uint8_t get_temp_flag;

extern uint8_t public_buf[513];


#define WIFI_MODE	2	
#define WIFI_AP_MODE	3

int upload_result = 0;

uint32_t upload_time = 0;
uint32_t upload_size = 0;

volatile WIFI_STATE wifi_link_state;
WIFI_PARA wifiPara;
IP_PARA ipPara;
CLOUD_PARA cloud_para;

char wifi_check_time = 0;

extern uint8_t gCurDir[100];

extern uint32_t wifi_loop_cycle;

volatile TRANSFER_STATE esp_state;

uint8_t left_to_send = 0;
uint8_t left_to_save[96] = {0};

volatile WIFI_DMA_RCV_FIFO wifiDmaRcvFifo;

volatile WIFI_TRANS_ERROR wifiTransError;

static bool need_ok_later = false;

extern volatile WIFI_STATE wifi_link_state;
extern WIFI_PARA wifiPara;
extern IP_PARA ipPara;
extern CLOUD_PARA cloud_para;

extern uint8_t once_flag;
extern uint8_t flash_preview_begin;
extern uint8_t default_preview_flg;
extern uint8_t gcode_preview_over;

extern char flash_dma_mode;

extern uint8_t bmp_public_buf[14 * 1024];

uint32_t   getWifiTick(){
	return millis();
}

uint32_t  getWifiTickDiff(int32_t lastTick, int32_t  curTick) {
	if(lastTick <= curTick) {
		return (curTick - lastTick) * TICK_CYCLE;
	}
	else {
		return (0xffffffff - lastTick + curTick) * TICK_CYCLE;
	}
}

void wifi_delay(int n) {
	uint32_t begin = getWifiTick();
	uint32_t end = begin;

	while(getWifiTickDiff(begin, end) < (uint32_t)n) {
		end = getWifiTick();
	}
}

void wifi_reset() {
	uint32_t start, now;
	start = getWifiTick();
	now = start;
        WIFI_RESET();
	while(getWifiTickDiff(start, now) < 500) {
		now = getWifiTick();
	}
    WIFI_SET();
	
}

void mount_file_sys(uint8_t disk_type) {
	if(disk_type == FILE_SYS_SD){
		card.mount();
	}
	else if(disk_type == FILE_SYS_USB) {
		
	}
}

#include <libmaple/timer.h>
#include <libmaple/util.h>
#include <libmaple/rcc.h>

#include <boards.h>
#include <wirish.h>

#include <libmaple/dma.h>
#include <libmaple/bitband.h>

#include <libmaple/libmaple.h>
#include <libmaple/gpio.h>
#include <libmaple/usart.h>
#include <libmaple/ring_buffer.h>

void exchangeFlashMode(char dmaMode) {
	if(flash_dma_mode != dmaMode) {
		flash_dma_mode = dmaMode;
		if(flash_dma_mode == 1) {
			// uint8_t buf[2];
			// W25QXX.SPI_FLASH_BufferRead(buf, 0, 2);
			// ZERO(buf);
			// SPI_TFT.tftio.WriteSequence((uint16_t*)buf, 1);
		}
		else {
			// spi_tx_dma_disable(SPI2);
			// spi_rx_dma_disable(SPI2);
			dma_disable(DMA1, DMA_CH5);
			//dma_disable(DMA1, DMA_CH4);
			dma_clear_isr_bits(DMA1, DMA_CH4);
			//dma_clear_isr_bits(DMA1, DMA_CH5);	

			// dma_disable(DMA1, DMA_CH3);
			// dma_disable(DMA1, DMA_CH2);
			// dma_clear_isr_bits(DMA1, DMA_CH3);
			// dma_clear_isr_bits(DMA1, DMA_CH2);		
		}
	}
}

static bool longName2DosName(const char* longName, uint8_t* dosName) {   
  uint8_t i = 11;
  while (i) dosName[--i] = '\0';         
  while (*longName) {     
    uint8_t c = *longName++;  
    if (c == '.') {                   // For a dot...
	if(i == 0) { return false; }
	else { strcat((char *)dosName,".GCO"); return dosName[0] != '\0'; }
    }
    else {
      // Fail for illegal characters
      PGM_P p = PSTR("|<>^+=?/[];,*\"\\");
      while (uint8_t b = pgm_read_byte(p++)) if (b == c) return false;
      if (c < 0x21 || c == 0x7F) return false;           // Check size, non-printable characters
      dosName[i++] = (c < 'a' || c > 'z') ? (c) : (c + ('A' - 'a')); // Uppercase required for 8.3 name
    }
    if(i >= 5) { strcat((char *)dosName,"~1.GCO"); return dosName[0] != '\0'; }
  }
  return dosName[0] != '\0';              // Return true if any name was set
}

static int storeRcvData(volatile uint8_t *bufToCpy, int32_t len) {
	unsigned char tmpW = wifiDmaRcvFifo.write_cur;
	
	if(len > UDISKBUFLEN)
		return 0;
	
	if(wifiDmaRcvFifo.state[tmpW] == udisk_buf_empty) {
		memcpy((unsigned char *) wifiDmaRcvFifo.bufferAddr[tmpW], (uint8_t *)bufToCpy, len);
		wifiDmaRcvFifo.state[tmpW] = udisk_buf_full;
		wifiDmaRcvFifo.write_cur = (tmpW + 1) % TRANS_RCV_FIFO_BLOCK_NUM;

		return 1;
	}
	else
		return 0;
	
}

static void esp_dma_pre() {

	dma_channel_reg_map *channel_regs = dma_tube_regs(DMA1, DMA_CH5);

	channel_regs->CCR &= ~( 1 << 0 ) ; 
    channel_regs->CMAR = (uint32_t)WIFISERIAL.usart_device->rb->buf;
    channel_regs->CNDTR = 0x0000   ;
    channel_regs->CNDTR = UART_RX_BUFFER_SIZE ;
    DMA1->regs->IFCR = 0xF0000 ;
    channel_regs->CCR |= 1 << 0 ;

	/*
	dma_xfer_size dma_bit_size = DMA_SIZE_8BITS;
	dma_disable(DMA1, DMA_CH5);
	dma_setup_transfer(DMA1, DMA_CH5, &USART1_BASE->DR, dma_bit_size,
             (volatile void*)WIFISERIAL.usart_device->rb->buf, dma_bit_size, flags);
	dma_clear_isr_bits(DMA1, DMA_CH5);
	dma_enable(DMA1, DMA_CH5);	*/
}

static void dma_ch5_irq_handle() {
    uint8 status_bits = dma_get_isr_bits(DMA1, DMA_CH5);
    dma_clear_isr_bits(DMA1, DMA_CH5);
    if (status_bits & 0x8) {
		// DMA transmit Error
	
    } else if (status_bits & 0x2) {
		// DMA transmit complete
		if(esp_state == TRANSFER_IDLE) {
			esp_state = TRANSFERING;
		}
		if(storeRcvData(WIFISERIAL.usart_device->rb->buf, UART_RX_BUFFER_SIZE)) {
			esp_dma_pre();
       		if(wifiTransError.flag != 0x1) {
				WIFI_IO1_RESET();
			}
		}
		else {
            WIFI_IO1_SET();
			esp_state = TRANSFER_STORE;
		}
    } else if (status_bits & 0x4) {
		// DMA transmit half
		WIFI_IO1_SET();
    }
}
static void wifi_usart_dma_init() {

	dma_init(DMA1);
	uint32_t flags = ( DMA_MINC_MODE | DMA_TRNS_CMPLT | DMA_HALF_TRNS | DMA_TRNS_ERR);
	dma_xfer_size dma_bit_size = DMA_SIZE_8BITS;
  	dma_setup_transfer(DMA1, DMA_CH5, &USART1_BASE->DR, dma_bit_size,
             (volatile void*)WIFISERIAL.usart_device->rb->buf, dma_bit_size, flags);// Transmit buffer DMA
	dma_set_priority(DMA1, DMA_CH5, DMA_PRIORITY_LOW);
	dma_attach_interrupt(DMA1, DMA_CH5, &dma_ch5_irq_handle);

	dma_clear_isr_bits(DMA1, DMA_CH5);
	dma_set_num_transfers(DMA1, DMA_CH5, UART_RX_BUFFER_SIZE);

	bb_peri_set_bit(&USART1_BASE->CR3, USART_CR3_DMAR_BIT, 1);
	dma_enable(DMA1, DMA_CH5);   // enable transmit

	for(uint8_t i = 0; i < TRANS_RCV_FIFO_BLOCK_NUM; i++) {
		wifiDmaRcvFifo.bufferAddr[i] = &bmp_public_buf[1024 * i];		
		wifiDmaRcvFifo.state[i] = udisk_buf_empty;	
	}
	
	memset(wifiDmaRcvFifo.bufferAddr[0], 0, 1024 * TRANS_RCV_FIFO_BLOCK_NUM);
	wifiDmaRcvFifo.read_cur = 0;
	wifiDmaRcvFifo.write_cur = 0;
}

void esp_port_begin(uint8_t interrupt) {
	#if 1
	//NVIC_InitTypeDef NVIC_InitStructure;

	//USART_InitTypeDef USART_InitStructure;
	//GPIO_InitTypeDef GPIO_InitStruct;
	
	WifiRxFifo.uart_read_point = 0;
	WifiRxFifo.uart_write_point = 0;
	//memset((uint8_t*)WifiRxFifo.uartTxBuffer, 0, sizeof(WifiRxFifo.uartTxBuffer));
	
	if(interrupt) {
		#if USE_WIFI_FUNCTION
			WIFISERIAL.end();
			for(uint16_t i=0;i<65535;i++);
			WIFISERIAL.begin(WIFI_BAUDRATE);
			uint32_t serial_connect_timeout = millis() + 1000UL;
	    	while (/*!WIFISERIAL && */PENDING(millis(), serial_connect_timeout)) { /*nada*/ }
			//for(uint8_t i=0;i<100;i++)WIFISERIAL.write(0x33);
		#endif
	}
	else{
		#if USE_WIFI_FUNCTION
			WIFISERIAL.end();
			WIFISERIAL.usart_device->regs->CR1 &= ~USART_CR1_RXNEIE;
			//for(uint16_t i=0;i<65535;i++);
			WIFISERIAL.begin(WIFI_UPLOAD_BAUDRATE);
			//uint32_t serial_connect_timeout = millis() + 1000UL;
	    		//while (/*!WIFISERIAL && */PENDING(millis(), serial_connect_timeout)) { /*nada*/ }
			wifi_usart_dma_init();
		#endif

	}
	#else
	WifiRxFifo.uart_read_point = 0;
	WifiRxFifo.uart_write_point = 0;
	if(interrupt) {
		#if USE_WIFI_FUNCTION
			WIFISERIAL.end();
			for(uint16_t i=0;i<65535;i++);
			WIFISERIAL.begin(WIFI_BAUDRATE);
			uint32_t serial_connect_timeout = millis() + 1000UL;
	    	while (/*!WIFISERIAL && */PENDING(millis(), serial_connect_timeout)) { /*nada*/ }
			//for(uint8_t i=0;i<100;i++)WIFISERIAL.write(0x33);
		#endif
	}
	else {
		#if USE_WIFI_FUNCTION
			WIFISERIAL.end();
			for(uint16_t i=0;i<65535;i++);
			WIFISERIAL.begin(WIFI_UPLOAD_BAUDRATE);
			uint32_t serial_connect_timeout = millis() + 1000UL;
	    	while (/*!WIFISERIAL && */PENDING(millis(), serial_connect_timeout)) { /*nada*/ }
			//for(uint16_t i=0;i<65535;i++);//WIFISERIAL.write(0x33);
		#endif
		wifi_usart_dma_init();
	}
	#endif
}

#if USE_WIFI_FUNCTION

int raw_send_to_wifi(char *buf, int len) {
	int i;

	if(buf == 0  ||  len <= 0) {
		return 0;
	}
	for(i = 0; i < len; i++) {
	 	WIFISERIAL.write(*(buf+i));
	}

	return len;

}

#endif //USE_WIFI_FUNCTION

void wifi_ret_ack() {}

char buf_to_wifi[256];
int index_to_wifi = 0;
int package_to_wifi(WIFI_RET_TYPE type,char *buf, int len) {
	char wifi_ret_head = 0xa5;
	char wifi_ret_tail = 0xfc;

	if(type == WIFI_PARA_SET){
 		int data_offset = 4;
		int apLen = strlen((const char *)uiCfg.wifi_name);
		int keyLen = strlen((const char *)uiCfg.wifi_key);
		
 		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
		index_to_wifi = 0;

		buf_to_wifi[data_offset] = gCfgItems.wifi_mode_sel;
		buf_to_wifi[data_offset + 1]  = apLen;
		strncpy(&buf_to_wifi[data_offset + 2], (const char *)uiCfg.wifi_name, apLen);
		buf_to_wifi[data_offset + apLen + 2]  = keyLen;
		strncpy(&buf_to_wifi[data_offset + apLen + 3], (const char *)uiCfg.wifi_key, keyLen);
		buf_to_wifi[data_offset + apLen + keyLen + 3] = wifi_ret_tail;

		index_to_wifi = apLen + keyLen + 3;

		buf_to_wifi[0] = wifi_ret_head;
		buf_to_wifi[1] = type;
		buf_to_wifi[2] = index_to_wifi & 0xff;
		buf_to_wifi[3] = (index_to_wifi >> 8) & 0xff;

		raw_send_to_wifi(buf_to_wifi, 5 + index_to_wifi);

		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
		index_to_wifi = 0;
		
 	}	
	else if(type == WIFI_TRANS_INF) {
		if(len > (int)(sizeof(buf_to_wifi) - index_to_wifi - 5)) {
			memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
			index_to_wifi = 0;
			return 0;
		}
		
		 if(len > 0) {		
		 	memcpy(&buf_to_wifi[4 + index_to_wifi], buf, len);
			index_to_wifi += len;
		 
			if(index_to_wifi < 1)
				return 0;

			 if(buf_to_wifi[index_to_wifi + 3] == '\n') {	
			 	//mask "wait" "busy" "X:"
			 	if(((buf_to_wifi[4] == 'w') && (buf_to_wifi[5] == 'a') && (buf_to_wifi[6] == 'i')  && (buf_to_wifi[7] == 't') )
					|| ((buf_to_wifi[4] == 'b') && (buf_to_wifi[5] == 'u') && (buf_to_wifi[6] == 's')  && (buf_to_wifi[7] == 'y') )
					|| ((buf_to_wifi[4] == 'X') && (buf_to_wifi[5] == ':') )
					){
			 		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
				 	index_to_wifi = 0;
					return 0;
			 	}

				buf_to_wifi[0] = wifi_ret_head;
				buf_to_wifi[1] = type;
				buf_to_wifi[2] = index_to_wifi & 0xff;
				buf_to_wifi[3] = (index_to_wifi >> 8) & 0xff;	
				buf_to_wifi[4 + index_to_wifi] = wifi_ret_tail;

				raw_send_to_wifi(buf_to_wifi, 5 + index_to_wifi);

				memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
				 index_to_wifi = 0;
			 }
		}
	}
	else if(type == WIFI_EXCEP_INF) {
		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));		

		buf_to_wifi[0] = wifi_ret_head;
		buf_to_wifi[1] = type;
		buf_to_wifi[2] = 1;
		buf_to_wifi[3] = 0;
		buf_to_wifi[4] = *buf;
		buf_to_wifi[5] = wifi_ret_tail;

		raw_send_to_wifi(buf_to_wifi, 6);

		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
		index_to_wifi = 0;
	}
	else if(type == WIFI_CLOUD_CFG) {
		int data_offset = 4;
		int urlLen = strlen((const char *)uiCfg.cloud_hostUrl);
		
 		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
		index_to_wifi = 0;

		if(gCfgItems.cloud_enable == true)
			buf_to_wifi[data_offset] = 0x0a;
		else
			buf_to_wifi[data_offset] = 0x05;
		
		buf_to_wifi[data_offset + 1]  = urlLen;
		strncpy(&buf_to_wifi[data_offset + 2], (const char *)uiCfg.cloud_hostUrl, urlLen);
		buf_to_wifi[data_offset + urlLen + 2]  = uiCfg.cloud_port & 0xff;
		buf_to_wifi[data_offset + urlLen + 3]  = (uiCfg.cloud_port >> 8) & 0xff;
		buf_to_wifi[data_offset + urlLen + 4] = wifi_ret_tail;

		index_to_wifi = urlLen + 4;

		buf_to_wifi[0] = wifi_ret_head;
		buf_to_wifi[1] = type;
		buf_to_wifi[2] = index_to_wifi & 0xff;
		buf_to_wifi[3] = (index_to_wifi >> 8) & 0xff;

		raw_send_to_wifi(buf_to_wifi, 5 + index_to_wifi);

		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
		index_to_wifi = 0;
	}
	else if(type == WIFI_CLOUD_UNBIND) {
		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));		

		buf_to_wifi[0] = wifi_ret_head;
		buf_to_wifi[1] = type;
		buf_to_wifi[2] = 0;
		buf_to_wifi[3] = 0;
		buf_to_wifi[4] = wifi_ret_tail;

		raw_send_to_wifi(buf_to_wifi, 5);

		memset(buf_to_wifi, 0, sizeof(buf_to_wifi));
		index_to_wifi = 0;
	}
	return 1;
}


int send_to_wifi(char *buf, int len) { return package_to_wifi(WIFI_TRANS_INF, buf, len); }

void set_cur_file_sys(int fileType) {
	gCfgItems.fileSysType = fileType;
}

void get_file_list(char *path) {
	if( path == 0) {
		return;
	}
	
	if(gCfgItems.fileSysType == FILE_SYS_SD) {
		#if ENABLED (SDSUPPORT)
		card.mount();
		#endif
	}
	else if(gCfgItems.fileSysType == FILE_SYS_USB) {
		//udisk
	}		
	Explore_Disk(path, 0);
}

char wait_ip_back_flag = 0;

typedef struct {
	//char write_buf[513];
	int write_index;	
	uint8_t saveFileName[30];
	uint8_t fileTransfer;
	uint32_t fileLen;
	uint32_t tick_begin;
	uint32_t tick_end;
} FILE_WRITER;

FILE_WRITER file_writer;

int32_t lastFragment = 0;

//char lastBinaryCmd[50] = {0};

//int total_write = 0;	
//char binary_head[2] = {0, 0};
//unsigned char binary_data_len = 0;

//static uint32_t write_pos;

char saveFilePath[50];

static SdFile upload_file, *upload_curDir;
static filepos_t pos;

int write_to_file(char *buf, int len) {
	int i;
	int res = 0;

	for(i = 0; i < len; i++) {
		public_buf[file_writer.write_index++] = buf[i];
		if(file_writer.write_index >= 512) {
			//res = card.write(public_buf, file_writer.write_index);
			res = upload_file.write(public_buf, file_writer.write_index);

			if(res == -1) {
				upload_file.close();
				const char * const fname = card.diveToFile(true, upload_curDir, saveFilePath);
	
				if (upload_file.open(upload_curDir, fname, O_WRITE)) {
					upload_file.setpos(&pos);
					res = upload_file.write(public_buf, file_writer.write_index);
				}
			}
			if(res == -1) {
				//WRITE(BEEPER_PIN, HIGH);
				return  -1;
			}
			//write_pos += 512;
			upload_file.getpos(&pos);
			file_writer.write_index = 0;		
		}
	}
	// if(len > 512) {
	// 	res = card.write(buf, 512);
	// 	if(res == -1) {
	// 		card.closefile();
	// 		SdFile file, *curDir;
	// 		const char * const fname = card.diveToFile(true, curDir, saveFilePath);
	// 		if (file.open(curDir, fname, O_RDWR)) {
	// 			res = file.write(buf, 512);
	// 		}
	// 	}
	// 	write_pos += 512;
	// 	res = card.write(&buf[512], (len - 512));
	// 	if(res == -1) {
	// 		card.closefile();
	// 		SdFile file, *curDir;
	// 		const char * const fname = card.diveToFile(true, curDir, saveFilePath);
	// 		if (file.open(curDir, fname, O_RDWR)) {
	// 			res = file.write(&buf[512], (len - 512));
	// 		}
	// 	}
	// 	write_pos += (len - 512);
	// }
	// else {
	// 	res = card.write(buf, len);
	// 	if(res == -1) {
	// 		card.closefile();
	// 		SdFile file, *curDir;
	// 		const char * const fname = card.diveToFile(true, curDir, saveFilePath);
	// 		if (file.open(curDir, fname, O_RDWR)) {
	// 			res = file.write(buf, len);
	// 		}
	// 	}
	// 	write_pos += len;
	// }
	

	if(res == -1) {
		memset(public_buf, 0, sizeof(public_buf));
		file_writer.write_index = 0;
		return  -1;
	}
			
	return 0;
}

#define ESP_PROTOC_HEAD	(uint8_t)0xa5
#define ESP_PROTOC_TAIL		(uint8_t)0xfc

#define ESP_TYPE_NET				(uint8_t)0x0
#define ESP_TYPE_GCODE				(uint8_t)0x1
#define ESP_TYPE_FILE_FIRST			(uint8_t)0x2
#define ESP_TYPE_FILE_FRAGMENT		(uint8_t)0x3

#define ESP_TYPE_WIFI_LIST		(uint8_t)0x4

uint8_t esp_msg_buf[UART_RX_BUFFER_SIZE] = {0}; 
uint16_t esp_msg_index = 0; 

typedef struct {
	uint8_t head; 
	uint8_t type; 
	uint16_t dataLen;
	uint8_t *data; 
	uint8_t tail;
} ESP_PROTOC_FRAME;


static int cut_msg_head(uint8_t *msg, uint16_t msgLen, uint16_t cutLen) {
	int i;
	
	if(msgLen < cutLen){
		return 0;
	}
	else if(msgLen == cutLen){
		memset(msg, 0, msgLen);
		return 0;
	}
	for(i = 0; i < (msgLen - cutLen); i++) {
		msg[i] = msg[cutLen + i];
	}
	memset(&msg[msgLen - cutLen], 0, cutLen);
	
	return msgLen - cutLen;
	
}


uint8_t Explore_Disk (char* path , uint8_t recu_level) {
  	char tmp[200];
  	char Fstream[200];
  
	if(path == 0)return 0;

	const uint8_t fileCnt = card.get_num_Files();

	for (uint8_t i = 0; i < fileCnt; i++) {
		const uint16_t nr =
		      #if ENABLED(SDCARD_RATHERRECENTFIRST) && DISABLED(SDCARD_SORT_ALPHA)
		          fileCnt - 1 -
		        #endif
		      i;

		      #if ENABLED(SDCARD_SORT_ALPHA)
		        card.getfilename_sorted(nr);
		      #else
		        card.getfilename_sorted(nr);
		      #endif
			memset(tmp, 0, sizeof(tmp));
			//if(card.longFilename[0] == 0)
				strcpy(tmp, card.filename);
			//else
				//strcpy(tmp, card.longFilename);

			memset(Fstream, 0, sizeof(Fstream));
			strcpy(Fstream, tmp);

		      if (card.flag.filenameIsDir && (recu_level <= 10)) {
				strcat(Fstream, ".DIR\r\n");
				send_to_wifi(Fstream, strlen(Fstream));
		      }
		      else {
		      		strcat(Fstream, "\r\n");
				send_to_wifi(Fstream, strlen(Fstream));
		      }
	}
		
 	return fileCnt;
}

static void wifi_gcode_exec(uint8_t *cmd_line) {
	int8_t  tempBuf[100] = {0};
	uint8_t *tmpStr = 0;
	int  cmd_value;
	volatile int print_rate;
	if((strstr((char *)&cmd_line[0], "\n") != 0) && ((strstr((char *)&cmd_line[0], "G") != 0) || (strstr((char *)&cmd_line[0], "M") != 0) || (strstr((char *)&cmd_line[0], "T") != 0) )) {
		
		tmpStr = (uint8_t *)strstr((char *)&cmd_line[0], "\n");
		if(tmpStr) {
			*tmpStr = '\0';
		}
		tmpStr = (uint8_t *)strstr((char *)&cmd_line[0], "\r");
		if(tmpStr) {
			*tmpStr = '\0';
		}
		tmpStr = (uint8_t *)strstr((char *)&cmd_line[0], "*");
		if(tmpStr) {
			*tmpStr = '\0';
		}
		tmpStr = (uint8_t *)strstr((char *)&cmd_line[0], "M");
		if( tmpStr) {
			cmd_value = atoi((char *)(tmpStr + 1));
			tmpStr = (uint8_t *)strstr((char *)tmpStr, " ");

			switch(cmd_value) {
				
				case 20: //print sd / udisk file
					file_writer.fileTransfer = 0;
					if(uiCfg.print_state == IDLE) {		
						int index = 0;
						
						if(tmpStr == 0) {
							gCfgItems.fileSysType = FILE_SYS_SD;	
							send_to_wifi((char *)"Begin file list\r\n", strlen("Begin file list\r\n"));

							get_file_list((char *)"0:/");

							send_to_wifi((char *)"End file list\r\n", strlen("End file list\r\n"));

							send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
							break;
						}
						
						while(tmpStr[index] == ' ')
							index++;

						if(gCfgItems.wifi_type == ESP_WIFI) {
							char *path = (char *)tempBuf;
							
							if(strlen((char *)&tmpStr[index]) < 80) {
								send_to_wifi((char *)"Begin file list\r\n", strlen("Begin file list\r\n"));
								
								if(strncmp((char *)&tmpStr[index], "1:", 2) == 0) {
									gCfgItems.fileSysType = FILE_SYS_SD;									
									
								}
	 							else if(strncmp((char *)&tmpStr[index], "0:", 2) == 0) {
	 								gCfgItems.fileSysType = FILE_SYS_USB;								
								}
								strcpy((char *)path, (char *)&tmpStr[index]);	
								get_file_list(path);
								send_to_wifi((char *)"End file list\r\n", strlen("End file list\r\n"));
							}
							send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
						}
					}
					break;

				case 21:
					/*init sd card*/
					send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
					break;

				case 23:					
					/*select the file*/
					if(uiCfg.print_state == IDLE) {
						int index = 0;
						while(tmpStr[index] == ' ')
							index++;

						if(strstr((char *)&tmpStr[index], ".g") || strstr((char *)&tmpStr[index], ".G")) {
							if(strlen((char *)&tmpStr[index]) < 80) {
								memset(list_file.file_name[sel_id], 0, sizeof(list_file.file_name[sel_id]));
								memset(list_file.long_name[sel_id], 0, sizeof(list_file.long_name[sel_id]));

								if(gCfgItems.wifi_type == ESP_WIFI) {
									if(strncmp((char *)&tmpStr[index], "1:", 2) == 0) {
										gCfgItems.fileSysType = FILE_SYS_SD;									
										
									}
		 							else if(strncmp((char *)&tmpStr[index], "0:", 2) == 0) {
		 								gCfgItems.fileSysType = FILE_SYS_USB;									
									}
									else {
										if(tmpStr[index] != '/')
											strcat((char *)list_file.file_name[0], "/");
									}
									if(file_writer.fileTransfer == 1) {
										uint8_t dosName[FILENAME_LENGTH];
										uint8_t fileName[sizeof(list_file.file_name[sel_id])];
										fileName[0] = '\0';
										strcat((char *)fileName, (char *)&tmpStr[index]);
										if(!longName2DosName((const char *)fileName, dosName)) {
											strcpy(list_file.file_name[sel_id], "notValid");
										}
										strcat((char *)list_file.file_name[sel_id], (char *)dosName);
										strcat((char *)list_file.long_name[sel_id], (char *)dosName);
									}
									else {
										strcat((char *)list_file.file_name[sel_id], (char *)&tmpStr[index]);
										strcat((char *)list_file.long_name[sel_id], (char *)&tmpStr[index]);
									}
								}
								else {
									strcpy(list_file.file_name[sel_id], (char *)&tmpStr[index]);
								}
								
								char *cur_name=strrchr(list_file.file_name[sel_id],'/');
								
								card.openFileRead(cur_name);
								
								if(card.isFileOpen()) {
									send_to_wifi((char *)"File selected\r\n", strlen("File selected\r\n"));
									
								}
								else {
									send_to_wifi((char *)"file.open failed\r\n", strlen("file.open failed\r\n"));
									strcpy(list_file.file_name[sel_id], "notValid");
								}
								send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
								
							}
							
						
						}
					}
					break;

				case 24:
					if(strcmp(list_file.file_name[sel_id], "notValid") != 0) {
						if(uiCfg.print_state == IDLE) {
							clear_cur_ui();
							reset_print_time();
							start_print_time();
							preview_gcode_prehandle(list_file.file_name[sel_id]);
							uiCfg.print_state = WORKING;
							lv_draw_printing();
							
							if(gcode_preview_over != 1) {
								#if ENABLED (SDSUPPORT)
								char *cur_name;
								cur_name=strrchr(list_file.file_name[sel_id],'/');

								SdFile file;
								SdFile *curDir;
								card.endFilePrint();
								const char * const fname = card.diveToFile(true, curDir, cur_name);
								if (!fname) return;
								if (file.open(curDir, fname, O_READ)){
									gCfgItems.curFilesize = file.fileSize();
									file.close();
									update_spi_flash();
								}
								card.openFileRead(cur_name);
								if(card.isFileOpen()) {
								    	    feedrate_percentage = 100;
						                        //saved_feedrate_percentage = feedrate_percentage;
						                        planner.flow_percentage[0] = 100;
						                        planner.e_factor[0]= planner.flow_percentage[0]*0.01;
						                        if(EXTRUDERS==2){
						                            planner.flow_percentage[1] = 100;
						                            planner.e_factor[1]= planner.flow_percentage[1]*0.01;  
						                        }                            
									    card.startFileprint();
									    #if ENABLED(POWER_LOSS_RECOVERY)
					      				recovery.prepare();
					    				#endif
									    once_flag = 0;
								}
								#endif

							}
						}
						else if(uiCfg.print_state == PAUSED) {
							uiCfg.print_state = RESUMING;
							clear_cur_ui();
							start_print_time();

							if(gCfgItems.from_flash_pic==1)
								flash_preview_begin = 1;
							else
								default_preview_flg = 1;							
                            			lv_draw_printing();
						}
						else if(uiCfg.print_state == REPRINTING) {
							uiCfg.print_state = REPRINTED;
							clear_cur_ui();
							start_print_time();
							if(gCfgItems.from_flash_pic==1)
								flash_preview_begin = 1;
							else
								default_preview_flg = 1;
							lv_draw_printing();
						}		
					}
					send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
					break;

				case 25:
					/*pause print file*/						
					if(uiCfg.print_state == WORKING) {
						stop_print_time();							

						clear_cur_ui();
						
						#if ENABLED(SDSUPPORT)
						 card.pauseSDPrint();
						 uiCfg.print_state = PAUSING;
						 #endif
						if(gCfgItems.from_flash_pic==1)
							flash_preview_begin = 1;
						else
							default_preview_flg = 1;							
						lv_draw_printing();
						send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
					}
					break;
					
				case 26:
					/*stop print file*/	
					if((uiCfg.print_state == WORKING) || (uiCfg.print_state == PAUSED) || (uiCfg.print_state == REPRINTING)) {
						stop_print_time();							

						clear_cur_ui();
                        			#if ENABLED (SDSUPPORT)
						uiCfg.print_state = IDLE;
						card.flag.abort_sd_printing = true;
						#endif

						lv_draw_ready_print();

						send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
					}
					break;

				case 27:
					/*report print rate*/
					if((uiCfg.print_state == WORKING) || (uiCfg.print_state == PAUSED)|| (uiCfg.print_state == REPRINTING)) {
						print_rate = uiCfg.totalSend;
					
						memset((char *)tempBuf, 0, sizeof(tempBuf));

						sprintf((char *)tempBuf, "M27 %d\r\n", print_rate);

						send_to_wifi((char *)tempBuf, strlen((char *)tempBuf));
						
					}
					
					break;

				case 28:
					/*begin to transfer file to filesys*/
					if(uiCfg.print_state == IDLE) {
						
						int index = 0;
						while(tmpStr[index] == ' ')
							index++;

						if(strstr((char *)&tmpStr[index], ".g") || strstr((char *)&tmpStr[index], ".G")) {
							strcpy((char *)file_writer.saveFileName, (char *)&tmpStr[index]);
							
							if(gCfgItems.fileSysType == FILE_SYS_SD) {
								memset(tempBuf, 0, sizeof(tempBuf));
								sprintf((char *)tempBuf, "%s", file_writer.saveFileName);
							}
							else if(gCfgItems.fileSysType == FILE_SYS_USB) {
								memset(tempBuf, 0, sizeof(tempBuf));
								sprintf((char *)tempBuf, "%s", (char *)file_writer.saveFileName);
							}
							mount_file_sys(gCfgItems.fileSysType);
							
							#if ENABLED (SDSUPPORT)
							char *cur_name=strrchr(list_file.file_name[sel_id],'/');
							card.openFileWrite(cur_name);
							if(card.isFileOpen()) {
								memset(file_writer.saveFileName, 0, sizeof(file_writer.saveFileName));
								strcpy((char *)file_writer.saveFileName, (char *)&tmpStr[index]);
								memset(tempBuf, 0, sizeof(tempBuf));
								sprintf((char *)tempBuf, "Writing to file: %s\r\n", (char *)file_writer.saveFileName);
								wifi_ret_ack();
								send_to_wifi((char *)tempBuf, strlen((char *)tempBuf));

								//total_write = 0;	
								wifi_link_state = WIFI_WAIT_TRANS_START;
								
							}
							else{
								wifi_link_state = WIFI_CONNECTED;
								clear_cur_ui();
								lv_draw_dialog(DIALOG_TRANSFER_NO_DEVICE);
							}
							#endif
						
						}
						
					}
					break;
				case 105:
				case 991:
					memset(tempBuf, 0, sizeof(tempBuf));
					if(cmd_value == 105) {
						send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));
						sprintf((char *)tempBuf,"T:%.1f /%.1f B:%.1f /%.1f T0:%.1f /%.1f T1:%.1f /%.1f @:0 B@:0\r\n",
						
						(float)thermalManager.temp_hotend[0].celsius,(float)thermalManager.temp_hotend[0].target,
						#if HAS_HEATED_BED
						(float)thermalManager.temp_bed.celsius,(float)thermalManager.temp_bed.target,
						#else
						(float)0,(float)0,
						#endif
						(float)thermalManager.temp_hotend[0].celsius,(float)thermalManager.temp_hotend[0].target,
						#if !defined(SINGLENOZZLE) && EXTRUDERS >= 2
						(float)thermalManager.temp_hotend[1].celsius,(float)thermalManager.temp_hotend[1].target
						#else
						(float)0,(float)0
						#endif
						);
					}
					else {
						sprintf((char *)tempBuf,"T:%d /%d B:%d /%d T0:%d /%d T1:%d /%d @:0 B@:0\r\n", 
						
						(int)thermalManager.temp_hotend[0].celsius,(int)thermalManager.temp_hotend[0].target,
						#if HAS_HEATED_BED
						(int)thermalManager.temp_bed.celsius,(int)thermalManager.temp_bed.target,
						#else
						0,0,
						#endif
						(int)thermalManager.temp_hotend[0].celsius,(int)thermalManager.temp_hotend[0].target,
						#if !defined(SINGLENOZZLE) && EXTRUDERS >= 2
						(int)thermalManager.temp_hotend[1].celsius,(int)thermalManager.temp_hotend[1].target
						#else
						0,0
						#endif
						);
					}

					send_to_wifi((char *)tempBuf, strlen((char *)tempBuf));
					
					queue.enqueue_one_P(PSTR("M105"));
					
					break;
				case 992:
					if((uiCfg.print_state == WORKING) || (uiCfg.print_state == PAUSED)) {
						memset(tempBuf,0,sizeof(tempBuf));
						sprintf((char *)tempBuf, "M992 %d%d:%d%d:%d%d\r\n", print_time.hours/10, print_time.hours%10, print_time.minutes/10, print_time.minutes%10,	print_time.seconds/10, print_time.seconds%10);
						wifi_ret_ack();
						send_to_wifi((char *)tempBuf, strlen((char *)tempBuf));	
					}
											
					break;
				case 994:
					if((uiCfg.print_state == WORKING) || (uiCfg.print_state == PAUSED)) {
						memset(tempBuf,0,sizeof(tempBuf));
						if(strlen((char *)list_file.file_name[sel_id]) > (100-1)) {
							return;
						}
						sprintf((char *)tempBuf, "M994 %s;%d\n", list_file.file_name[sel_id],(int)gCfgItems.curFilesize);
						wifi_ret_ack();
						send_to_wifi((char *)tempBuf, strlen((char *)tempBuf));	
					}
					break;
				case 997:
					if(uiCfg.print_state == IDLE) {
						wifi_ret_ack();
						send_to_wifi((char *)"M997 IDLE\r\n", strlen("M997 IDLE\r\n"));
					}
					else if(uiCfg.print_state == WORKING) {
						wifi_ret_ack();
						send_to_wifi((char *)"M997 PRINTING\r\n", strlen("M997 PRINTING\r\n"));
					}
					else if(uiCfg.print_state == PAUSED) {
						wifi_ret_ack();
						send_to_wifi((char *)"M997 PAUSE\r\n", strlen("M997 PAUSE\r\n"));
					}
					else if(uiCfg.print_state == REPRINTING) {
						wifi_ret_ack();
						send_to_wifi((char *)"M997 PAUSE\r\n", strlen("M997 PAUSE\r\n"));
					}		
					if(uiCfg.command_send == 0) get_wifi_list_command_send();
					break;

				case 998:
					if(uiCfg.print_state == IDLE) {
						if(atoi((char *)tmpStr) == 0) {
							set_cur_file_sys(0);
						}
						else if(atoi((char *)tmpStr) == 1) {
							set_cur_file_sys(1);
						}
						wifi_ret_ack();
					}
					break;

				case 115:
					memset(tempBuf,0,sizeof(tempBuf));
					send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));					
					send_to_wifi((char *)"FIRMWARE_NAME:Robin_nano\r\n", strlen("FIRMWARE_NAME:Robin_nano\r\n"));					
					break;

				default:
					strcat((char *)cmd_line, "\n");

    					uint32_t left;

    					if(espGcodeFifo.wait_tick> 5){

    						if(espGcodeFifo.r >  espGcodeFifo.w)
    							left =  espGcodeFifo.r - espGcodeFifo.w - 1;
    						else
    							left = WIFI_GCODE_BUFFER_SIZE + espGcodeFifo.r - espGcodeFifo.w - 1;
    						if(left >= strlen((const char *)cmd_line)){
    							uint32_t index = 0;
    							while(index < strlen((const char *)cmd_line)){
    								espGcodeFifo.Buffer[espGcodeFifo.w] = cmd_line[index] ;
    								espGcodeFifo.w =  (espGcodeFifo.w + 1) % WIFI_GCODE_BUFFER_SIZE;
    								index++;
    							}
							if(left - WIFI_GCODE_BUFFER_LEAST_SIZE >= strlen((const char *)cmd_line))
								send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));   
							else
								need_ok_later = true;
							
    						}
                            		
    					}
					break;
					
			}
		}
		else{
			strcat((char *)cmd_line, "\n");
			uint32_t left_g;

    		if(espGcodeFifo.wait_tick > 5) {

				if(espGcodeFifo.r >  espGcodeFifo.w)
					left_g =  espGcodeFifo.r - espGcodeFifo.w - 1;
				else
					left_g = WIFI_GCODE_BUFFER_SIZE + espGcodeFifo.r - espGcodeFifo.w - 1;
				if(left_g >= strlen((const char *)cmd_line)) {
					uint32_t index = 0;
					while(index < strlen((const char *)cmd_line)) {
						espGcodeFifo.Buffer[espGcodeFifo.w] = cmd_line[index] ;
						espGcodeFifo.w =  (espGcodeFifo.w + 1) % WIFI_GCODE_BUFFER_SIZE;
						index++;
					}
				if(left_g - WIFI_GCODE_BUFFER_LEAST_SIZE >= strlen((const char *)cmd_line))
					send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));  
				else
					need_ok_later = true;
				
				}
			}	
		}
	}
}

static int32_t charAtArray(const uint8_t *_array, uint32_t _arrayLen, uint8_t _char) {
	uint32_t i;
	for(i = 0; i < _arrayLen; i++) {
		if(*(_array + i) == _char) {
			return i;
		}
	}
	
	return -1;
}

void get_wifi_list_command_send() {
	char buf[6]={0};
	buf[0] = 0xA5;
	buf[1] = 0x07;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0xFC;
	raw_send_to_wifi(buf, 5);
}

static void net_msg_handle(uint8_t * msg, uint16_t msgLen) {
	int wifiNameLen, wifiKeyLen, hostLen, id_len, ver_len;
	
	if(msgLen <= 0) return;
	//ip
	sprintf(ipPara.ip_addr, "%d.%d.%d.%d", msg[0], msg[1], msg[2], msg[3]);

	//port
	//connect state
	if(msg[6] == 0x0a) {
		wifi_link_state = WIFI_CONNECTED;
	}
	else if(msg[6] == 0x0e) {
		wifi_link_state = WIFI_EXCEPTION;
	}
	else {
		wifi_link_state = WIFI_NOT_CONFIG;
	}

	//mode
	wifiPara.mode = msg[7];
	

	//wifi name
	wifiNameLen = msg[8];
	wifiKeyLen = msg[9 + wifiNameLen];
	if(wifiNameLen < 32) {
		memset(wifiPara.ap_name, 0, sizeof(wifiPara.ap_name));
		memcpy(wifiPara.ap_name, &msg[9], wifiNameLen);

		memset(&wifi_list.wifiConnectedName,0,sizeof(wifi_list.wifiConnectedName));
		memcpy(&wifi_list.wifiConnectedName,&msg[9],wifiNameLen);

		//wifi key		
		if(wifiKeyLen < 64) {			
			memset(wifiPara.keyCode, 0, sizeof(wifiPara.keyCode));
			memcpy(wifiPara.keyCode, &msg[10 + wifiNameLen], wifiKeyLen);
		}
	}

	
	cloud_para.state =msg[10 + wifiNameLen + wifiKeyLen];
	hostLen = msg[11 + wifiNameLen + wifiKeyLen];
	if(cloud_para.state) {			
		if(hostLen < 96) {
			memset(cloud_para.hostUrl, 0, sizeof(cloud_para.hostUrl));
			memcpy(cloud_para.hostUrl, &msg[12 + wifiNameLen + wifiKeyLen], hostLen);
		}
		cloud_para.port = msg[12 + wifiNameLen + wifiKeyLen + hostLen] + (msg[13 + wifiNameLen + wifiKeyLen + hostLen] << 8);
				
	}

	// id
	id_len = msg[14 + wifiNameLen + wifiKeyLen + hostLen];
	if(id_len == 20) {
		memset(cloud_para.id, 0, sizeof(cloud_para.id));
		memcpy(cloud_para.id, (const char *)&msg[15 + wifiNameLen + wifiKeyLen + hostLen], id_len);
	}
	ver_len = msg[15 + wifiNameLen + wifiKeyLen + hostLen + id_len];
	if(ver_len < 20) {
		memset(wifi_firm_ver, 0, sizeof(wifi_firm_ver));
		memcpy(wifi_firm_ver, (const char *)&msg[16 + wifiNameLen + wifiKeyLen + hostLen + id_len], ver_len);
	}

	if(uiCfg.configWifi == 1) {
		if((wifiPara.mode != gCfgItems.wifi_mode_sel)
			|| (strncmp(wifiPara.ap_name, (const char *)uiCfg.wifi_name, 32) != 0)
			|| (strncmp(wifiPara.keyCode, (const char *)uiCfg.wifi_key, 64) != 0)) {
			package_to_wifi(WIFI_PARA_SET, (char *)0, 0);
		}
		else uiCfg.configWifi = 0;
	}
	if(cfg_cloud_flag == 1) {
		if(((cloud_para.state >> 4) != (char)gCfgItems.cloud_enable)
			|| (strncmp(cloud_para.hostUrl, (const char *)uiCfg.cloud_hostUrl, 96) != 0)
			|| (cloud_para.port != uiCfg.cloud_port)) {
			package_to_wifi(WIFI_CLOUD_CFG, (char *)0, 0);
		}
		else cfg_cloud_flag = 0;	
	}
}

static void wifi_list_msg_handle(uint8_t * msg, uint16_t msgLen) {
	int wifiNameLen,wifiMsgIdex=1;
	int8_t wifi_name_is_same=0;
	int8_t i,j;
	int8_t wifi_name_num=0;
	uint8_t *str=0;
	int8_t valid_name_num;
	
	if(msgLen <= 0)
		return;
	if(disp_state == KEY_BOARD_UI)
		return;

	wifi_list.getNameNum = msg[0];

	if(wifi_list.getNameNum < 20) {
		uiCfg.command_send = 1;
		
		memset(wifi_list.wifiName,0,sizeof(wifi_list.wifiName));
		
		wifi_name_num = wifi_list.getNameNum;
		
		valid_name_num=0;
		str = wifi_list.wifiName[valid_name_num];
		
		if(wifi_list.getNameNum > 0) wifi_list.currentWifipage = 1;
		
		for(i=0;i<wifi_list.getNameNum;i++) {
			wifiNameLen = msg[wifiMsgIdex];
			wifiMsgIdex  +=  1;
			if(wifiNameLen < 32) {
				memset(str, 0, WIFI_NAME_BUFFER_SIZE);
				memcpy(str, &msg[wifiMsgIdex], wifiNameLen);
				for(j=0;j<valid_name_num;j++) {
					if(strcmp((const char *)str,(const char *)wifi_list.wifiName[j]) == 0) {
						wifi_name_is_same = 1;
						break;
					}
				}
				if(wifi_name_is_same != 1) {
					//for(j=0;j<wifiNameLen;j++)
					//{
						if(str[0] > 0x80) {
							wifi_name_is_same = 1;
							//break;
						}
					//}
				}
				if(wifi_name_is_same == 1) {
					wifi_name_is_same = 0;
					wifiMsgIdex  +=  wifiNameLen;
					//wifi_list.RSSI[i] = msg[wifiMsgIdex];
					wifiMsgIdex  +=  1;
					wifi_name_num--;
					//i--;
					continue;
				}
				if(i < WIFI_TOTAL_NUMBER-1) {
					str = wifi_list.wifiName[++valid_name_num];
				}
			}
			wifiMsgIdex  +=  wifiNameLen;
			wifi_list.RSSI[i] = msg[wifiMsgIdex];
			wifiMsgIdex  +=  1;
		}
		wifi_list.getNameNum = wifi_name_num;
		if(wifi_list.getNameNum % NUMBER_OF_PAGE == 0) {
			wifi_list.getPage = wifi_list.getNameNum/NUMBER_OF_PAGE;
		}
		else {
			wifi_list.getPage = wifi_list.getNameNum/NUMBER_OF_PAGE + 1;
		}
		wifi_list.nameIndex = 0;
		if(disp_state == WIFI_LIST_UI)
		disp_wifi_list();
	}
}

static void gcode_msg_handle(uint8_t * msg, uint16_t msgLen) {
	uint8_t gcodeBuf[100] = {0};
	char *index_s;
	char *index_e;
	
	if(msgLen <= 0)
		return;

	index_s = (char *)msg;
	index_e = (char *)strstr((char *)msg, "\n");
	if(*msg == 'N') {
		index_s = (char *)strstr((char *)msg, " ");
		while((*index_s) == ' ') {
			index_s++;
		}
	}
	while((index_e != 0) && ((int)index_s < (int)index_e)) {
		if((int)(index_e - index_s) < (int)sizeof(gcodeBuf)){
			memset(gcodeBuf, 0, sizeof(gcodeBuf));
			
			memcpy(gcodeBuf, index_s, index_e - index_s + 1);
		
			wifi_gcode_exec(gcodeBuf);
		}
		while((*index_e == '\r') || (*index_e == '\n'))
			index_e++;

		index_s = index_e;
		index_e = (char *)strstr(index_s, "\n");
	}
}

void utf8_2_unicode(uint8_t *source,uint8_t Len) {
	uint8_t  i=0,char_i=0,char_byte_num=0;
	uint16_t  u16_h,u16_m,u16_l,u16_value;
	uint8_t FileName_unicode[30];
	
 	memset(FileName_unicode, 0, sizeof(FileName_unicode));
	
	while(1) {
		char_byte_num = source[i] & 0xF0;
		if(source[i] < 0X80) {
			//ASCII --1byte
			FileName_unicode[char_i] = source[i];
			i += 1;
			char_i += 1;
		}
		else if(char_byte_num == 0XC0 || char_byte_num == 0XD0) {
			//--2byte
			
			u16_h = (((uint16_t)source[i] <<8) & 0x1f00) >> 2;
			u16_l = ((uint16_t)source[i+1] & 0x003f);
			u16_value = (u16_h | u16_l);
			FileName_unicode[char_i] = (uint8_t)((u16_value & 0xff00) >> 8);
			FileName_unicode[char_i + 1] = (uint8_t)(u16_value & 0x00ff);
			i += 2;
			char_i += 2;
		}
		else if(char_byte_num == 0XE0) {
			//--3byte
			u16_h = (((uint16_t)source[i] <<8 ) & 0x0f00) << 4;
			u16_m = (((uint16_t)source[i+1] << 8) & 0x3f00) >> 2;
			u16_l = ((uint16_t)source[i+2] & 0x003f);
			u16_value = (u16_h | u16_m | u16_l);
			FileName_unicode[char_i] = (uint8_t)((u16_value & 0xff00) >> 8);
			FileName_unicode[char_i + 1] = (uint8_t)(u16_value & 0x00ff);
			i += 3;
			char_i += 2;
		}
		else if(char_byte_num == 0XF0) {
			//--4byte
			i += 4;
			//char_i += 3;
		}
		else {
			break;
		}
		if(i >= Len || i >= 255)break;
	}
	memcpy(source, FileName_unicode, sizeof(FileName_unicode));
}



static void file_first_msg_handle(uint8_t * msg, uint16_t msgLen) {
	uint8_t fileNameLen = *msg;
	
	if(msgLen != fileNameLen + 5) {
		return;
	}
	
	file_writer.fileLen = *((uint32_t *)(msg + 1));
	ZERO(file_writer.saveFileName);

	memcpy(file_writer.saveFileName, msg + 5, fileNameLen);

	utf8_2_unicode(file_writer.saveFileName,fileNameLen);

	memset(public_buf, 0, sizeof(public_buf));

	if(strlen((const char *)file_writer.saveFileName) > sizeof(saveFilePath))
		return;

	memset(saveFilePath, 0, sizeof(saveFilePath));
	
	if(gCfgItems.fileSysType == FILE_SYS_SD) {
		//sprintf((char *)saveFilePath, "/%s", file_writer.saveFileName);
		card.mount();

		//ZERO(list_file.long_name[sel_id]);
		//memcpy(list_file.long_name[sel_id],file_writer.saveFileName,sizeof(list_file.long_name[sel_id]));
	}
	else if(gCfgItems.fileSysType == FILE_SYS_USB) {
		
	}
	file_writer.write_index = 0;
	lastFragment = -1;

	wifiTransError.flag = 0;
	wifiTransError.start_tick = 0;
	wifiTransError.now_tick = 0;

	#if ENABLED (SDSUPPORT)
	card.closefile();
	#endif

	wifi_delay(1000);
	
	#if ENABLED (SDSUPPORT)
	
	uint8_t dosName[FILENAME_LENGTH];

	if(!longName2DosName((const char *)file_writer.saveFileName,dosName)) {
		clear_cur_ui();
		upload_result = 2;

		wifiTransError.flag = 1;
		wifiTransError.start_tick = getWifiTick();	
		
		lv_draw_dialog(DIALOG_TYPE_UPLOAD_FILE);
		
		return;
	}
	sprintf((char *)saveFilePath, "%s", dosName);

	//ZERO(list_file.long_name[sel_id]);
	//ZERO(list_file.file_name[sel_id]);
	//sprintf_P(list_file.long_name[sel_id], PSTR("/%s"), dosName);
	//sprintf_P(list_file.file_name[sel_id], PSTR("/%s"), dosName);
	//int res;

	card.cdroot();
	upload_file.close();
	const char * const fname = card.diveToFile(true, upload_curDir, saveFilePath);
	
	//card.openFileWrite(saveFilePath);
	if (!upload_file.open(upload_curDir, fname, O_WRITE | O_CREAT)) {
		//SERIAL_ECHOLNPAIR("Failed to open ", fname, " to write.");
		clear_cur_ui();
		upload_result = 2;

		wifiTransError.flag = 1;
		wifiTransError.start_tick = getWifiTick();	
		
		lv_draw_dialog(DIALOG_TYPE_UPLOAD_FILE);
		return;
	}
	// res = card.write(public_buf, 512);
	// if(res == -1) {
	// 	return;
	// }
	
	// char *cur_name=strrchr((const char *)saveFilePath,'/');

	// SdFile *curDir;
	// SdFile file;
	// card.endFilePrint();
	
	
	// file_writer.write_index = 0;
	// const char * const fname = card.diveToFile(true, curDir, cur_name);

	
	// if (!fname) return;
	// if (file.open(curDir, fname, O_CREAT | O_APPEND | O_WRITE | O_TRUNC)) {
	// 	gCfgItems.curFilesize = file.fileSize();
	// }
	// else {
	// 	clear_cur_ui();
	// 	upload_result = 2;

	// 	wifiTransError.flag = 1;
	// 	wifiTransError.start_tick = getWifiTick();	
		
	// 	lv_draw_dialog(DIALOG_TYPE_UPLOAD_FILE);
		
	// 	return;
	// }
	#endif
							
	wifi_link_state = WIFI_TRANS_FILE;

	upload_result = 1;

	clear_cur_ui();
	lv_draw_dialog(DIALOG_TYPE_UPLOAD_FILE);

	lv_task_handler();

	file_writer.tick_begin = getWifiTick();

	file_writer.fileTransfer = 1;	
}

#define FRAG_MASK	~(1 << 31)

static void file_fragment_msg_handle(uint8_t * msg, uint16_t msgLen) {
	uint32_t frag = *((uint32_t *)msg);

	if((frag & FRAG_MASK) != (uint32_t)(lastFragment  + 1)) {
		memset(public_buf, 0, sizeof(public_buf));
		file_writer.write_index = 0;
		
		wifi_link_state = WIFI_CONNECTED;	

		upload_result = 2;
		//WRITE(BEEPER_PIN, HIGH);
	}
	else {
		if(write_to_file((char *)msg + 4, msgLen - 4) < 0) {
			memset(public_buf, 0, sizeof(public_buf));
			file_writer.write_index = 0;
			
			wifi_link_state = WIFI_CONNECTED;	

			upload_result = 2; 
			

			return;
		}
		lastFragment = frag;
		
		if((frag & (~FRAG_MASK)) != 0) {
			//int res;
			//int res = card.write(file_writer.write_buf, file_writer.write_index);
			int res = upload_file.write(public_buf, file_writer.write_index);
			if(res == -1) {
				upload_file.close();
				const char * const fname = card.diveToFile(true, upload_curDir, saveFilePath);
	
				if (upload_file.open(upload_curDir, fname, O_WRITE)) {
					upload_file.setpos(&pos);
					res = upload_file.write(public_buf, file_writer.write_index);
				}
			}
			
			//card.closefile();
			upload_file.close();

			// card.openFileRead(saveFilePath);
								
			// if(card.isFileOpen()) {
			// 	gCfgItems.curFilesize = card.getFileSize();

			// 	SdFile dir, root = card.getroot();
			// 	dir.rename(&root, (const char *)file_writer.saveFileName);
			// }
			// else {
			// 	gCfgItems.curFilesize = card.getFileSize();
			// }

			SdFile file, *curDir;
			//ZERO(list_file.file_name[sel_id]);
			//sprintf_P(list_file.file_name[sel_id], PSTR("/%s"), saveFilePath);
          	//char *cur_name = strrchr(list_file.file_name[sel_id], '/');
			const char * const fname = card.diveToFile(true, curDir, saveFilePath);
			//if (!fname) res = -1;
			if (file.open(curDir, fname, O_RDWR)) {
				gCfgItems.curFilesize = file.fileSize();
				file.close();
			}
			else {
				memset(public_buf, 0, sizeof(public_buf));
				file_writer.write_index = 0;
				
				wifi_link_state = WIFI_CONNECTED;	

				upload_result = 2; 

				return;
			}
			memset(public_buf, 0, sizeof(public_buf));
			file_writer.write_index = 0;

			file_writer.tick_end = getWifiTick();

			upload_time = getWifiTickDiff(file_writer.tick_begin, file_writer.tick_end) / 1000;

			upload_size = gCfgItems.curFilesize;
			
			wifi_link_state = WIFI_CONNECTED;	

			upload_result = 3; 
		}
		
	}
}


void esp_data_parser(char *cmdRxBuf, int len) {
	int32_t head_pos;
	int32_t tail_pos;
	uint16_t cpyLen;
	int16_t leftLen = len;
	uint8_t loop_again = 0;

	ESP_PROTOC_FRAME esp_frame;

	while((leftLen > 0) || (loop_again == 1)) {
		loop_again = 0;
		
		if(esp_msg_index != 0) {
			
			head_pos = 0;
			cpyLen = (leftLen < (int16_t)((sizeof(esp_msg_buf) - esp_msg_index)) ? leftLen : sizeof(esp_msg_buf) - esp_msg_index);
			
			memcpy(&esp_msg_buf[esp_msg_index], cmdRxBuf + len - leftLen, cpyLen);			

			esp_msg_index += cpyLen;

			leftLen = leftLen - cpyLen;
			tail_pos = charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL);
			
				
			if(tail_pos == -1) {
				if(esp_msg_index >= sizeof(esp_msg_buf)) {
					memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
					esp_msg_index = 0;
				}
			
				return;
			}
		}
		else{
			head_pos = charAtArray((uint8_t const *)&cmdRxBuf[len - leftLen], leftLen, ESP_PROTOC_HEAD);
			if(head_pos == -1) {
				return;
			}
			else {
				memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
				memcpy(esp_msg_buf, &cmdRxBuf[len - leftLen + head_pos], leftLen - head_pos);

				esp_msg_index = leftLen - head_pos;

				leftLen = 0;

				head_pos = 0;
				
				tail_pos = charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL);
				
				if(tail_pos == -1) {	
					if(esp_msg_index >= sizeof(esp_msg_buf)) {
						memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
						esp_msg_index = 0;
					}
					return;
				}
				
			}
		}
		
		esp_frame.type = esp_msg_buf[1];
		if((esp_frame.type != ESP_TYPE_NET) && (esp_frame.type != ESP_TYPE_GCODE)
			 && (esp_frame.type != ESP_TYPE_FILE_FIRST) && (esp_frame.type != ESP_TYPE_FILE_FRAGMENT)
			 &&(esp_frame.type != ESP_TYPE_WIFI_LIST)) {
			memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
			esp_msg_index = 0;
			return;
		}
		
		esp_frame.dataLen = esp_msg_buf[2] + (esp_msg_buf[3] << 8);

		if((int)(4 + esp_frame.dataLen) > (int)(sizeof(esp_msg_buf))) {
			memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
			esp_msg_index = 0;
			return;
		}

		if(esp_msg_buf[4 + esp_frame.dataLen] != ESP_PROTOC_TAIL) {
			if(esp_msg_index >= sizeof(esp_msg_buf)) {
				memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
				esp_msg_index = 0;
			}
			return;
		}
			
		esp_frame.data = &esp_msg_buf[4];
		switch(esp_frame.type) {
			case ESP_TYPE_NET:
				net_msg_handle(esp_frame.data, esp_frame.dataLen);
				break;

			case ESP_TYPE_GCODE:
				gcode_msg_handle(esp_frame.data, esp_frame.dataLen);
				break;

			case ESP_TYPE_FILE_FIRST:
				file_first_msg_handle(esp_frame.data, esp_frame.dataLen);
				break;

			case ESP_TYPE_FILE_FRAGMENT:
				file_fragment_msg_handle(esp_frame.data, esp_frame.dataLen);
				break;
			case ESP_TYPE_WIFI_LIST:
				wifi_list_msg_handle(esp_frame.data, esp_frame.dataLen);
				break;
			
			default:
				break;
				
		}

		esp_msg_index = cut_msg_head(esp_msg_buf, esp_msg_index, esp_frame.dataLen  + 5);
		if(esp_msg_index > 0) {
			if(charAtArray(esp_msg_buf, esp_msg_index,  ESP_PROTOC_HEAD) == -1) {
				memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
				esp_msg_index = 0;
				return;
			}
			
			if((charAtArray(esp_msg_buf, esp_msg_index,  ESP_PROTOC_HEAD) != -1) && (charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL) != -1)) {
				loop_again = 1;
			}
		}
	}
}


int32_t tick_net_time1, tick_net_time2;

int32_t readWifiFifo(uint8_t *retBuf, uint32_t bufLen) {
	unsigned char tmpR = wifiDmaRcvFifo.read_cur;
	
	if(bufLen < UDISKBUFLEN)
		return 0;

	if(wifiDmaRcvFifo.state[tmpR] == udisk_buf_full) {		

		memcpy(retBuf, (unsigned char *)wifiDmaRcvFifo.bufferAddr[tmpR], UDISKBUFLEN);

		wifiDmaRcvFifo.state[tmpR] = udisk_buf_empty;

		wifiDmaRcvFifo.read_cur = (tmpR + 1) % TRANS_RCV_FIFO_BLOCK_NUM;


		 return UDISKBUFLEN;
	}
	else return 0;
		
}


void stopEspTransfer() {
	if(wifi_link_state == WIFI_TRANS_FILE)
	wifi_link_state = WIFI_CONNECTED;
					
	#if ENABLED (SDSUPPORT)
	upload_file.close();
	#endif

	if(upload_result != 3) {
		wifiTransError.flag = 1;
		wifiTransError.start_tick = getWifiTick();
		card.removeFile((const char *)saveFilePath);
	}
	else {

	}
	wifi_delay(200);

    WIFI_IO1_SET();

	// disable dma
	dma_clear_isr_bits(DMA1, DMA_CH5);
	bb_peri_set_bit(&USART1_BASE->CR3, USART_CR3_DMAR_BIT, 0);
	dma_disable(DMA1, DMA_CH5);
    
	exchangeFlashMode(1);  //change spi flash to use dma mode

	W25QXX.init(SPI_QUARTER_SPEED);

	#if ENABLED(TFT_LVGL_UI_SPI)
		SPI_TFT.spi_init(SPI_FULL_SPEED);
	#endif

	wifi_delay(200);

	esp_port_begin(1);
	
	if(wifiTransError.flag != 0x1) {
		WIFI_IO1_RESET();
	}
}

void wifi_rcv_handle() {
	 int32_t len = 0;
	 uint8_t ucStr[(UART_RX_BUFFER_SIZE) + 1] = {0};
	 int8_t getDataF = 0;
	 
	if(wifi_link_state == WIFI_TRANS_FILE) {
		#if 0
		if(WIFISERIAL.available() == UART_RX_BUFFER_SIZE) {
			for(uint16_t i=0;i<UART_RX_BUFFER_SIZE;i++) {
				ucStr[i] = WIFISERIAL.read();
				len++;
			}
		}
		#else
		len = readWifiFifo(ucStr, UART_RX_BUFFER_SIZE);
		#endif
		if(len > 0) {
			esp_data_parser((char *)ucStr, len);
			if(wifi_link_state == WIFI_CONNECTED) {
				clear_cur_ui();
				lv_draw_dialog(DIALOG_TYPE_UPLOAD_FILE);
				stopEspTransfer();
			}
			getDataF = 1;
		}
		if(esp_state == TRANSFER_STORE) {
			if(storeRcvData(WIFISERIAL.usart_device->rb->buf, UART_RX_BUFFER_SIZE)) {
				esp_state = TRANSFERING;

				esp_dma_pre();
				
				if(wifiTransError.flag != 0x1) {
					WIFI_IO1_RESET();
				}
			}
			else {
	        	WIFI_IO1_SET();
			}
		}
	}
	else {
		//len = readUsartFifo((SZ_USART_FIFO *)&WifiRxFifo, (int8_t *)ucStr, UART_RX_BUFFER_SIZE);
		len = readWifiBuf((int8_t *)ucStr, UART_RX_BUFFER_SIZE);
		if(len > 0) {
			esp_data_parser((char *)ucStr, len);
			
			if(wifi_link_state == WIFI_TRANS_FILE) {
				exchangeFlashMode(0);  //change spi flash not use dma mode
				
				wifi_delay(10);
				
				esp_port_begin(0);
				
				wifi_delay(10);

				tick_net_time1 = 0;
				
			}
			if(wifiTransError.flag != 0x1) {
				WIFI_IO1_RESET();
			}
			getDataF = 1;
		}
		if(need_ok_later &&  (queue.length < BUFSIZE)) {
			need_ok_later = false;
			send_to_wifi((char *)"ok\r\n", strlen("ok\r\n"));   
		}		
	}

	if(getDataF == 1) {
		tick_net_time1 = getWifiTick();
	}	
	else {
		tick_net_time2 = getWifiTick();
		
		if(wifi_link_state == WIFI_TRANS_FILE) {
			if((tick_net_time1 != 0) && (getWifiTickDiff(tick_net_time1, tick_net_time2) > 8000)) {	
				wifi_link_state = WIFI_CONNECTED;

				upload_result = 2;

				clear_cur_ui();
				
				stopEspTransfer();
				
				lv_draw_dialog(DIALOG_TYPE_UPLOAD_FILE);
			}
		}
		if((tick_net_time1 != 0) && (getWifiTickDiff(tick_net_time1, tick_net_time2) > 10000)) {	
			wifi_link_state = WIFI_NOT_CONFIG;
		}
		if((tick_net_time1 != 0) && (getWifiTickDiff(tick_net_time1, tick_net_time2) > 120000)) {	
			wifi_link_state = WIFI_NOT_CONFIG;
			
			wifi_reset();

			tick_net_time1 = getWifiTick();
		}
	}

	if(wifiTransError.flag == 0x1) {
		wifiTransError.now_tick = getWifiTick();
		if(getWifiTickDiff(wifiTransError.start_tick, wifiTransError.now_tick) > WAIT_ESP_TRANS_TIMEOUT_TICK) {
			wifiTransError.flag = 0;
			WIFI_IO1_RESET();
		}
	}	
}

void wifi_looping() {
	 do {
	 	wifi_rcv_handle();
	 } 
	 while(wifi_link_state == WIFI_TRANS_FILE);
}

void mks_esp_wifi_init() {
	wifi_link_state = WIFI_NOT_CONFIG;

	SET_OUTPUT(WIFI_RESET_PIN);

	WIFI_SET();

	SET_OUTPUT(WIFI_IO1_PIN);

	SET_INPUT_PULLUP(WIFI_IO0_PIN);

	WIFI_IO1_SET();

	esp_state = TRANSFER_IDLE;

	esp_port_begin(1);		

	wifi_reset();

	#if 0
	if(update_flag == 0) {
		res = f_open(&esp_upload.uploadFile, ESP_WEB_FIRMWARE_FILE,  FA_OPEN_EXISTING | FA_READ);

		if(res ==  FR_OK) {
			f_close(&esp_upload.uploadFile);

			wifi_delay(2000);

			if(usartFifoAvailable((SZ_USART_FIFO *)&WifiRxFifo) < 20) {
				return;
			}

			clear_cur_ui();

			draw_dialog(DIALOG_TYPE_UPDATE_ESP_FIRMARE);
			if(wifi_upload(1) >= 0) {					
			
				f_unlink("1:/MKS_WIFI_CUR");
				f_rename(ESP_WEB_FIRMWARE_FILE,"/MKS_WIFI_CUR");
			}
			draw_return_ui();
			update_flag = 1;
		}
		
	}
	if(update_flag == 0){
		res = f_open(&esp_upload.uploadFile, ESP_WEB_FILE,  FA_OPEN_EXISTING | FA_READ);
		if(res ==  FR_OK) {
			f_close(&esp_upload.uploadFile);

			wifi_delay(2000);

			if(usartFifoAvailable((SZ_USART_FIFO *)&WifiRxFifo) < 20) {
				return;
			}

			clear_cur_ui();
			
			draw_dialog(DIALOG_TYPE_UPDATE_ESP_DATA);

			if(wifi_upload(2) >= 0) {								

				f_unlink("1:/MKS_WEB_CONTROL_CUR");
				f_rename(ESP_WEB_FILE,"/MKS_WEB_CONTROL_CUR");
			}
			draw_return_ui();
		}
	}
	#endif
	wifiPara.decodeType = WIFI_DECODE_TYPE;
	wifiPara.baud = 115200;
	
	wifi_link_state = WIFI_NOT_CONFIG;
}


void mks_wifi_firmware_upddate() {
	card.openFileRead((char *)ESP_FIRMWARE_FILE);
	
	if (card.isFileOpen()) {

		card.closefile();

		wifi_delay(2000);

		if(usartFifoAvailable((SZ_USART_FIFO *)&WifiRxFifo) < 20) {
			return;
		}

		clear_cur_ui();

		lv_draw_dialog(DIALOG_TYPE_UPDATE_ESP_FIRMARE);

		lv_task_handler();
		
		if(wifi_upload(0) >= 0) {
			
			card.removeFile((char *)ESP_FIRMWARE_FILE_RENAME);

			SdFile file, *curDir;
			const char * const fname = card.diveToFile(true, curDir, ESP_FIRMWARE_FILE);
			if (file.open(curDir, fname, O_READ)) {
				file.rename(curDir, (char *)ESP_FIRMWARE_FILE_RENAME);
				file.close();
			}
			
		}
		clear_cur_ui();
	}
}


#define BUF_INC_POINTER(p)	((p + 1 == UART_FIFO_BUFFER_SIZE) ? 0:(p + 1))

int usartFifoAvailable(SZ_USART_FIFO *fifo) {
	return WIFISERIAL.available();
}

// int readUsartFifo(SZ_USART_FIFO *fifo, int8_t *buf, int32_t len) {
// 	int i = 0 ;

// 	while(i < len ) {
// 		if(fifo->uart_read_point != fifo->uart_write_point) {
// 			buf[i] = fifo->uartTxBuffer[fifo->uart_read_point];
// 			fifo->uart_read_point = BUF_INC_POINTER(fifo->uart_read_point);
// 			i++;	
// 		}
// 		else {
// 			break;
// 		}
// 	}
// 	return i;
	
// }

// int writeUsartFifo(SZ_USART_FIFO *fifo, int8_t *buf, int32_t len) {
// 	int i = 0 ;
	
// 	if((buf == 0) || (len <= 0)) {
// 		return -1;
// 	}
// 	while(i < len ) {
// 		if(fifo->uart_read_point != BUF_INC_POINTER(fifo->uart_write_point)){
// 			fifo->uartTxBuffer[fifo->uart_write_point] = buf[i] ;
// 			fifo->uart_write_point = BUF_INC_POINTER(fifo->uart_write_point);
// 			i++;
// 		}
// 		else {
// 			break;
// 		}
// 	}
// 	return i;
// }

void get_wifi_commands() {

  static char wifi_line_buffer[MAX_CMD_SIZE];
  static bool wifi_comment_mode = false;
  static int wifi_read_count = 0;

  if(espGcodeFifo.wait_tick > 5){
	  while ((queue.length < BUFSIZE) && (espGcodeFifo.r != espGcodeFifo.w)) {

	    espGcodeFifo.wait_tick = 0;
		
	    char wifi_char = espGcodeFifo.Buffer[espGcodeFifo.r];

	    espGcodeFifo.r = (espGcodeFifo.r + 1) % WIFI_GCODE_BUFFER_SIZE;

	    /**
	     * If the character ends the line
	     */
	    if (wifi_char == '\n' || wifi_char == '\r') {

	      wifi_comment_mode = false; // end of line == end of comment

	      if (!wifi_read_count) continue; // skip empty lines

	      wifi_line_buffer[wifi_read_count] = 0; // terminate string
	      wifi_read_count = 0; //reset buffer

	      char* command = wifi_line_buffer;

	      while (*command == ' ') command++; // skip any leading spaces	    

	      // Movement commands alert when stopped
	      if (IsStopped()) {
	      char* gpos = strchr(command, 'G');
	      if (gpos) {
	        switch (strtol(gpos + 1, nullptr, 10)) {
	          case 0: case 1:
	          #if ENABLED(ARC_SUPPORT)
	            case 2: case 3:
	          #endif
	          #if ENABLED(BEZIER_CURVE_SUPPORT)
	            case 5:
	          #endif
	            SERIAL_ECHOLNPGM(STR_ERR_STOPPED);
	            LCD_MESSAGEPGM(MSG_STOPPED);
	            break;
	        }
	      }
	    }

	        #if DISABLED(EMERGENCY_PARSER)
	          // Process critical commands early
	          if (strcmp(command, "M108") == 0) {
	            wait_for_heatup = false;
	            #if HAS_LCD_MENU
	              wait_for_user = false;
	            #endif
	          }
	          if (strcmp(command, "M112") == 0) kill(M112_KILL_STR, nullptr, true);
	          if (strcmp(command, "M410") == 0) quickstop_stepper();
	        #endif

	      	// Add the command to the queue
	      	queue.enqueue_one_P(wifi_line_buffer);
	    }
	    else if (wifi_read_count >= MAX_CMD_SIZE - 1) {
	      
	    }
	    else { // it's not a newline, carriage return or escape char
	      if (wifi_char == ';') wifi_comment_mode = true;
	      if (!wifi_comment_mode) wifi_line_buffer[wifi_read_count++] = wifi_char;
	    }
	  }
    }// queue has space, serial has data
    else {
    	espGcodeFifo.wait_tick++;
    }
}

int readWifiBuf(int8_t *buf, int32_t len) {
	int i = 0 ;

	while(i < len) {
		if (WIFISERIAL.available()) {
			buf[i] = WIFISERIAL.read();
			i++;	
		}
		else {
			break;
		}
	}
	return i;
}

#endif //USE_WIFI_FUNCTION

#endif	// HAS_TFT_LVGL_UI

