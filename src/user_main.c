/*
 * Copyright 2016 karawin (http://www.karawin.fr)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <espressif/esp_common.h>
#include <espressif/user_interface.h>
#include <espressif/esp_softap.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_system.h>
#include <etstimer.h>
#include <espressif/esp_timer.h>
#include <espressif/osapi.h>

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"

#include "el_uart.h"
#include "webserver.h"
#include "webclient.h"
#include "buffer.h"
#include "extram.h"
#include "vs1053.h"
#include "ntp.h"
#include "telnet.h"
#include "servers.h"

//#include "eeprom.h"
//#include <time.h>

#include "interface.h"

const char striDEF0[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "The default AP is WifiKaRadio. Connect your wifi to it.\nThen connect a webbrowser to 192.168.4.1 and go to Setting\nMay be long to load the first time.Be patient.%c"
};
const char striDEF1[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "Erase the database and set ssid, password and ip's field%c"
};
const char striAP[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "AP1: %s, AP2: %s\n"
};
const char striSTA1[] ICACHE_RODATA_ATTR STORE_ATTR = {
  " AP1:Station Ip: %d.%d.%d.%d\n"
};
const char striSTA2[] ICACHE_RODATA_ATTR STORE_ATTR = {
  " AP2:Station Ip: %d.%d.%d.%d\n"
};
const char striTRY[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "Trying AP%d %s ,  I: %d status: %d\n"
};
const char striTASK[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "%s task: %x\n"
};
const char striHEAP[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "Heap size: %d\n"
};
const char striUART[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "UART READY%c"
};
const char striWATERMARK[] ICACHE_RODATA_ATTR STORE_ATTR = {
  "watermark %s: %d  heap:%d\n"
};

typedef enum
{
    FLASH_SIZE_4M_MAP_256_256 = 0,  /**<  Flash size : 4Mbits. Map : 256KBytes + 256KBytes */
    FLASH_SIZE_2M,                  /**<  Flash size : 2Mbits. Map : 256KBytes */
    FLASH_SIZE_8M_MAP_512_512,      /**<  Flash size : 8Mbits. Map : 512KBytes + 512KBytes */
    FLASH_SIZE_16M_MAP_512_512,     /**<  Flash size : 16Mbits. Map : 512KBytes + 512KBytes */
    FLASH_SIZE_32M_MAP_512_512,     /**<  Flash size : 32Mbits. Map : 512KBytes + 512KBytes */
    FLASH_SIZE_16M_MAP_1024_1024,   /**<  Flash size : 16Mbits. Map : 1024KBytes + 1024KBytes */
    FLASH_SIZE_32M_MAP_1024_1024,   /**<  Flash size : 32Mbits. Map : 1024KBytes + 1024KBytes */
    FLASH_SIZE_32M_MAP_2048_2048,   /**<  attention: don't support now ,just compatible for nodemcu;
                                           Flash size : 32Mbits. Map : 2048KBytes + 2048KBytes */
    FLASH_SIZE_64M_MAP_1024_1024,   /**<  Flash size : 64Mbits. Map : 1024KBytes + 1024KBytes */
    FLASH_SIZE_128M_MAP_1024_1024   /**<  Flash size : 128Mbits. Map : 1024KBytes + 1024KBytes */

} flash_size_map;

//ip
static char localIp[20] = {"0.0.0.0"};
void sdk_uart_div_modify(int no, unsigned int freq);

//struct station_config config;
uint8_t FlashOn = 5, FlashOff = 5;
uint8_t FlashCount = 0xFF;
uint8_t FlashVolume = 0;
sdk_os_timer_t ledTimer;
//sc_status status = 0;

/*
void cb(sc_status stat, void *pdata)
{
	kprintf(PSTR("SmartConfig status received: %d\n",status));
	status = stat;
	if (stat == SC_STATUS_LINK_OVER) if (pdata) kprintf("SmartConfig: %d:%d:%d:%d\n",((char*) pdata)[0],((char*)pdata)[1],((char*)pdata)[2],((char*)pdata)[3]);
}
*/

char* getIp() { return (localIp); }


void testtask(void* p) {
  struct device_settings *device;
  /*
  	int uxHighWaterMark;
  	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
  	printf(striWATERMARK,"testtask",uxHighWaterMark,xPortGetFreeHeapSize( ));
  */

  //	printf("t0 end\n");
  vTaskDelete(NULL); // stop the task
}

ICACHE_FLASH_ATTR void ledCallback(void *pArg) {
  struct device_settings *device;
  FlashCount++;
  if (FlashCount == FlashOn) {
    if (FlashVolume++ == 5) // every 10 sec
    {
      // save volume if changed
      FlashVolume = 0;
      if (xPortGetFreeHeapSize() > 4096) // don't worry it will be done later
      {
        device = getDeviceSettingsSilent();
        if (device != NULL) {
          if (device->vol != clientIvol)
            if (xPortGetFreeHeapSize() > 4096) {
              //printf("save vol %d\n",clientIvol);
              device->vol = clientIvol;
              saveDeviceSettings(device);
            }
          free(device);
        }
      }
    }
  }
}

ICACHE_FLASH_ATTR void initLed(void) {
  sdk_os_timer_disarm(&ledTimer);
  FlashCount = 0;
  sdk_os_timer_setfn(&ledTimer, ledCallback, NULL);
  sdk_os_timer_arm(&ledTimer, 10, true); //  and rearm
  vTaskDelay(1);
  //	printf("initLed done\n");
}

//-------------------------
// Wifi  management
//-------------------------
void initWifi() {
  //-------------------------
  // AP Connection management
  //-------------------------
  uint16_t ap = 0;
  int i = 0;
  char hostn[HOSTLEN];
  struct ip_info *info;
  struct device_settings *device;
  struct device_settings1* device1;
  struct sdk_station_config* config;
  //	wifi_station_set_hostname("WifiKaRadio");
  device = getDeviceSettings();
  device1 = getDeviceSettings1(); // extention of saved data
  config = malloc(sizeof(struct sdk_station_config));
  info = malloc(sizeof(struct ip_info));

  // if device1 not initialized, erase it and copy pass2 to the new place
  if (device1->cleared != 0xAABB) {
    eeErasesettings1();
    device1->cleared = 0xAABB; //marker init done
    memcpy(device1->pass2, device->pass2, 64);
    saveDeviceSettings1(device1);
  }
  sdk_wifi_set_opmode_current(STATION_MODE);
  sdk_wifi_station_set_auto_connect(false);
  sdk_wifi_get_ip_info(STATION_IF, info); // ip netmask gw
  sdk_wifi_station_get_config_default(config); //ssid passwd
  if ((device->ssid[0] == 0xFF) && (device->ssid2[0] == 0xFF)) {
    eeEraseAll();
    device = getDeviceSettings();
  } // force init of eeprom
  if (device->ssid2[0] == 0xFF) {
    device->ssid2[0] = 0;
    device1->pass2[0] = 0;
  }
  printf(striAP, device -> ssid, device -> ssid2);
  if ((strlen(device -> ssid) == 0) || (device -> ssid[0] == 0xff) /*||(device->ipAddr[0] ==0)*/ ) // first use
  {
    printf(PSTR("first use%c"), 0x0d);
    IP4_ADDR(&(info->ip), 192, 168, 1, 254);
    IP4_ADDR(&(info->netmask), 0xFF, 0xFF, 0xFF, 0);
    IP4_ADDR(&(info->gw), 192, 168, 1, 254);
    /*
    		IPADDR2_COPY(&device->ipAddr, &info->ip);
    		IPADDR2_COPY(&device->mask, &info->netmask);
    		IPADDR2_COPY(&device->gate, &info->gw);
    */
		memcpy(&device->ipAddr, &info->ip, sizeof(&device->ipAddr));    
    memcpy(&device->mask, &info->netmask, sizeof(&device->mask));   
    memcpy(&device->gate, &info->gw, sizeof(&device->gate));    

		strcpy(device->ssid,config->ssid);
		strcpy(device->pass,config->password);
		device->dhcpEn = true;
    sdk_wifi_set_ip_info(STATION_IF, info);
    saveDeviceSettings(device);
  }

  // set for AP1 //
	IP4_ADDR(&(info->ip), device->ipAddr[0], device->ipAddr[1],device->ipAddr[2], device->ipAddr[3]);
	IP4_ADDR(&(info->netmask), device->mask[0], device->mask[1],device->mask[2], device->mask[3]);
	IP4_ADDR(&(info->gw), device->gate[0], device->gate[1],device->gate[2], device->gate[3]);
	strcpy(config->ssid,device->ssid);
	strcpy(config->password,device->pass);
	sdk_wifi_station_set_config(config);
	if (!device->dhcpEn) {
    //		if ((strlen(device->ssid)!=0)&&(device->ssid[0]!=0xff)&&(!device->dhcpEn))
    //			conn = true; //static ip
    sdk_wifi_station_dhcpc_stop();
    sdk_wifi_set_ip_info(STATION_IF, info);
  }
	printf(striSTA1,(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));
  sdk_wifi_station_connect();

  //	printf("DHCP: 0x%x\n Device: Ip: %d.%d.%d.%d\n",device->dhcpEn,device->ipAddr[0], device->ipAddr[1], device->ipAddr[2], device->ipAddr[3]);
  //	printf("\nI: %d status: %d\n",i,wifi_station_get_connect_status());

  i = 0;
  while ((sdk_wifi_station_get_connect_status() != STATION_GOT_IP)) {
    printf(striTRY, ap + 1, config -> ssid, i, sdk_wifi_station_get_connect_status());
    FlashOn = FlashOff = 40;
    vTaskDelay(400); //  ms
    if ((strlen(config -> ssid) == 0) || (sdk_wifi_station_get_connect_status() == STATION_WRONG_PASSWORD) || (sdk_wifi_station_get_connect_status() == STATION_CONNECT_FAIL) || (sdk_wifi_station_get_connect_status() == STATION_NO_AP_FOUND)) {
      // try AP2 //
      if ((strlen(device -> ssid2) > 0) && (ap < 1)) {
        i = -1;
        sdk_wifi_station_disconnect();
        // set for AP2 //
        //-------------//
				IP4_ADDR(&(info->ip), device->ipAddr[0], device->ipAddr[1],device->ipAddr[2], device->ipAddr[3]);
				IP4_ADDR(&(info->netmask), device->mask[0], device->mask[1],device->mask[2], device->mask[3]);
				IP4_ADDR(&(info->gw), device->gate[0], device->gate[1],device->gate[2], device->gate[3]);
				strcpy(config->ssid,device->ssid2);
				strcpy(config->password,device1->pass2);
				sdk_wifi_station_set_config(config);
				if (!device->dhcpEn)
				{
					sdk_wifi_station_dhcpc_stop();
					sdk_wifi_set_ip_info(STATION_IF, info);
				}
				sdk_wifi_station_connect();
				printf(striSTA2,(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));
        //----------------
        ap++;
      }
      else i = 10; // go to SOFTAP_MODE
    }
    i++;
    if (i >= 10) { // AP mode
      printf(PSTR("%c"), 0x0d);
      sdk_wifi_station_disconnect();
      FlashOn = 10;
      FlashOff = 200;
      vTaskDelay(100);
      //printf(PSTR("Config not found%c%c"),0x0d,0x0d);
      saveDeviceSettings(device);
			printf(striDEF0,0x0d);
			printf(striDEF1,0x0d);
			struct sdk_softap_config *apconfig;
      apconfig = malloc(sizeof(struct sdk_softap_config));
      sdk_wifi_set_opmode_current(SOFTAP_MODE);
      vTaskDelay(10);
      sdk_wifi_softap_get_config(apconfig);
      vTaskDelay(10);
      strcpy(apconfig->ssid, "WifiKaRadio");
      apconfig->ssid_len = 0;
      //printf("passwd: %s\nhidden: %d\nmaxc: %d\nauth: %d\n",apconfig->password,apconfig->ssid_hidden,apconfig->max_connection,apconfig->authmode);
			if (sdk_wifi_softap_set_config(apconfig) != true)printf(PSTR("softap failed%c%c"),0x0d,0x0d);
      vTaskDelay(1);
      sdk_wifi_get_ip_info(1, info);
      //printf(striSTA1,(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));
      vTaskDelay(10);
      //			conn = true;
      free(apconfig);
      break;
    }
  }
  //wifi_station_set_reconnect_policy(true);
  // update device info
  if (sdk_wifi_get_opmode() == SOFTAP_MODE) sdk_wifi_get_ip_info(SOFTAP_IF, info);
  else sdk_wifi_get_ip_info(STATION_IF, info); // ip netmask gw
  sdk_wifi_station_get_config(config);
  /*
  	IPADDR2_COPY(&device->ipAddr, &info->ip);
  	IPADDR2_COPY(&device->mask, &info->netmask);
  	IPADDR2_COPY(&device->gate, &info->gw);
  */
	memcpy(&device->ipAddr, &info->ip, sizeof(&device->ipAddr));
	memcpy(&device->mask, &info->netmask, sizeof(&device->mask));
	memcpy(&device->gate, &info->gw, sizeof(&device->gate));

  saveDeviceSettings(device);
	printf(striSTA1,(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));
	kasprintf(localIp,PSTR("%d.%d.%d.%d"),(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));
	// set modem sleep per default
	//sdk_wifi_set_sleep_type(MODEM_SLEEP_T);
	if ((strlen(device1->hostname) >= HOSTLEN) ||
		(strlen(device1->hostname) == 0) || (device1->hostname[0] ==  0xff))
	{
		strcpy(hostn,"WifiKaRadio");
		strcpy(device1->hostname,hostn);
		saveDeviceSettings1(device1);
	}
	else strcpy(hostn,device1->hostname);
	printf(PSTR("HOSTNAME: %s\nLocal IP: %s\n"),hostn,localIp);

  free(info);
  free(device);
  free(device1);
  free(config);
}

void uartInterfaceTask(void *pvParameters) {
  char tmp[255];
  struct device_settings *device;
  initWifi();
  uint16_t ap = 0;
  int i = 0;
  uint8_t maxap;

  /*	int uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
  	printf("watermark uartInterfaceTask: %d  %d\n","uartInterfaceTask",uxHighWaterMark,xPortGetFreeHeapSize( ));
  */
  int t = 0;
  for (t = 0; t < sizeof(tmp); t++) tmp[t] = 0;
  t = 0;
  uart_rx_init();
  printf(striUART, 0x0d);
  ap = 0;
  ap = sdk_system_adc_read();
  if (ap < 10) {
    adcdiv = 0; // no panel adc grounded
    kprintf(PSTR("No panel%c"), 0x0d);
  } else {
    // read adc to see if it is a nodemcu with adc dividor
    if (ap < 400) adcdiv = 3;
    else adcdiv = 1;
    kprintf(PSTR("ADC Div: %d from adc: %d\n"), adcdiv, ap);
  }
  FlashOn = 190;
  FlashOff = 10;
  initLed(); // start the timer for led. This will kill the ttest task to free memory
  //autostart
  device = getDeviceSettings();
	currentStation = device->currentstation;
	VS1053_I2SRate(device->i2sspeed);
	clientIvol = device->vol;
	if ((sdk_wifi_get_opmode() == STATION_MODE)&&(device->autostart ==1))
	{
		kprintf(PSTR("autostart: playing:%d, currentstation:%d\n"),device->autostart,device->currentstation);
		vTaskDelay(10);
		playStationInt(device->currentstation);
  }
  free(device);
  while (1) {
    while (1) {
      int c = uart_getchar_ms(100);
      if (c != -1) {
        if ((char) c == '\r') break;
        if ((char) c == '\n') break;
        tmp[t] = (char) c;
        t++;
        if (t == sizeof(tmp) - 1) t = 0;
      }
      switchCommand(); // hardware panel of command
    }
    checkCommand(t, tmp);
    /*	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
    	printf("watermark uartInterfaceTask: %d  heap:%d\n",uxHighWaterMark,xPortGetFreeHeapSize( ));
    */
    for (t = 0; t < sizeof(tmp); t++) tmp[t] = 0;
    t = 0;
  }
}

/*
UART_SetBaudrate(uint8_t uart_no, uint32_t baud_rate) {
	sdk_uart_div_modify(uart_no, UART_CLK_FREQ / baud_rate);
}
*/


/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
/*uint32_t user_rf_cal_sector_set(void) {
  uint32_t rf_cal_sec = 0;
  flash_size_map size_map = sdk_flashchip.chip_size;
  switch (size_map) {
  case FLASH_SIZE_4M_MAP_256_256:
    rf_cal_sec = 128 - 5;
    break;

  case FLASH_SIZE_8M_MAP_512_512:
    rf_cal_sec = 256 - 5;
    break;

  case FLASH_SIZE_16M_MAP_512_512:
  case FLASH_SIZE_16M_MAP_1024_1024:
    rf_cal_sec = 512 - 5;
    break;

  case FLASH_SIZE_32M_MAP_512_512:
  case FLASH_SIZE_32M_MAP_1024_1024:
    rf_cal_sec = 1024 - 5;
    break;

  default:
    rf_cal_sec = 0;
    break;
  }

  return rf_cal_sec;
}
*/

/******************************************************************************
 * FunctionName : test_upgrade
 * Description  : check if it is an upgrade. Convert if needed
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
/*void test_upgrade(void)
{
	uint8_t autotest;
	struct device_settings *settings;
	struct shoutcast_info* station;
	int j;
	eeGetOldData(0x0C070, &autotest, 1);
	printf ("Upgrade autotest before %d\n",autotest);
	if (autotest == 3) // old bin before 1.0.6
	{
		autotest = 0; //patch espressif 1.4.2 see http://bbs.espressif.com/viewtopic.php?f=46&t=2349
		eeSetOldData(0x0C070, &autotest, 1);
		printf ("Upgrade autotest after %d\n",autotest);
		settings = getOldDeviceSettings();
		saveDeviceSettings(settings);
		free(settings);
		eeEraseStations();
		for(j=0; j<192; j++){
			station = getOldStation(j);
			saveStation(station, j);
			free(station);
			vTaskDelay(1); // avoid watchdog
		}
	}
}
*/

/******************************************************************************
 * FunctionName : checkUart
 * Description  : Check for a valid uart baudrate
 * Parameters   : baud
 * Returns      : baud
 *******************************************************************************/
uint32_t checkUart(uint32_t speed) {
  uint32_t valid[] = {
    1200,
    2400,
    4800,
    9600,
    14400,
    19200,
    28800,
    38400,
    57600,
    76880,
    115200,
    230400
  };
  int i = 0;
  for (i; i < 12; i++) {
    if (speed == valid[i]) return speed;
  }
  return 115200; // default
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
void user_init(void) {
  struct device_settings *device;
  uint32_t uspeed;
  //	REG_SET_BIT(0x3ff00014, BIT(0));
  //	system_update_cpu_freq(SYS_CPU_160MHZ);
  //	system_update_cpu_freq(160); //- See more at: http://www.esp8266.com/viewtopic.php?p=8107#p8107
  TaskHandle_t pxCreatedTask;
  Delay(100);
  getFlashChipRealSize();
  device = getDeviceSettings();
	uspeed = device->uartspeed;
  free(device);
  uspeed = checkUart(uspeed);
  sdk_uart_div_modify(0, UART_CLK_FREQ / uspeed); //UART_SetBaudrate(0,uspeed);
  uart_rx_init();
  VS1053_HW_init(); // init spi
  extramInit();
  printf(PSTR("\nuart speed: %d\n"), uspeed);
  initBuffer();
  printf(PSTR("Release %s, Revision %s\n"), RELEASE, REVISION);
  printf(PSTR("SDK %s\n"), sdk_system_get_sdk_version());
  sdk_system_print_meminfo();
  printf(PSTR("Heap size: %d\n"), xPortGetFreeHeapSize());
  clientInit();
  //Delay(10);
	flash_size_map size_map = sdk_flashchip.chip_size;
	printf (PSTR("size_map: %d\n"),size_map);
	printf(PSTR("Flash size: %d\n"),getFlashChipRealSize());
	xTaskCreate(testtask, "t0", 140, NULL, 1, &pxCreatedTask); // DEBUG/TEST 130
	printf(striTASK,"t0",pxCreatedTask);
	xTaskCreate(uartInterfaceTask, "t1", 370, NULL, 2, &pxCreatedTask); // 350
	printf(striTASK, "t1",pxCreatedTask);
	xTaskCreate(vsTask, "t2", 240, NULL,5, &pxCreatedTask); //380 230
	printf(striTASK,"t2",pxCreatedTask);
	xTaskCreate(clientTask, "t3", 500, NULL, 6, &pxCreatedTask); // 340
	printf(striTASK,"t3",pxCreatedTask);
	xTaskCreate(serversTask, "t4", 370, NULL, 4, &pxCreatedTask); //380
	printf(striTASK,"t4",pxCreatedTask);
	printf (striHEAP,xPortGetFreeHeapSize( ));
}
