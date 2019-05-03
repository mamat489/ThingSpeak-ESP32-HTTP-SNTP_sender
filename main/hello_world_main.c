/* HTTP GET Example using plain POSIX sockets
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_attr.h"
#include "esp_sleep.h"

#include "lwip/err.h"
#include "esp_sntp.h"
//#include "apps/sntp/sntp.h"
#include "sdkconfig.h"


#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASS
#define API_KEY CONFIG_API_KEY

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/




/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "api.thingspeak.com"
#define WEB_PORT 80
//#define WEB_URL "http://api.thingspeak.com/update.json?api_key=**API**&field1=142"
#define WEB_URL "http://api.thingspeak.com/update.json?api_key="  //API must be filled in menuconfig
#define WEB_URL_API WEB_URL API_KEY


static const char *TAG = "DEBUG: ";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

//static void http_get_task(void *pvParameters)
static void http_get_task(void * now)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    //struct in_addr *addr;
    int s, r;
    short sum=-50;
    short read_hall = 0;
    short read_temp = 0;
    short sum_temp = 140;
    short hall_sens_read(), temprature_sens_read();
    char recv_buf[64];
    char strftime_buf[64];
    struct tm timeinfo;

    while(1) {

    	for(int countdown = 11; countdown >= 0; countdown--) {
    		ESP_LOGI(TAG, "%d acquisitions left",countdown);
    	    read_hall=hall_sens_read();
    	    read_temp = temprature_sens_read();
    	    sum+=read_hall;
    	    sum_temp+=read_temp;
    	    printf("hall sensor: %hd \n",read_hall);
    	    printf("temperature sensor = %hd\n\n", read_temp);
    	    vTaskDelay(1300 / portTICK_PERIOD_MS);

    	}



    	time(now);
    	localtime_r(now, &timeinfo);
    	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

    	printf("\nAverage computation...\n");
    	sum=sum/(short)12;
    	sum_temp = sum_temp /(short)12;
    	printf("\thall sensor average: %hd \n",sum);
    	printf("\ttemp sensor average: %hd \n",sum_temp);
    	printf("\tData average was calculated on: %s \n\n", strftime_buf);

    	ESP_LOGI(TAG, "Average ok, data can be sent!");


    	//NOW WE SEND DATA TO TS

    	/* Wait for the callback to set the CONNECTED_BIT in the
           event group.
        */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                            false, true, portMAX_DELAY);
        ESP_LOGI(TAG, "Wifi still connected");

        /*int err =*/ getaddrinfo(WEB_SERVER, "80", &hints, &res);
/*
        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }*/


        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        /*char lien[] = "GET " WEB_URL "&field1=", sum, "\n"
            "Host: "WEB_SERVER"\r\n"
            "User-Agent: esp-idf/1.0 esp32\r\n"
            "\r\n";*/

        char lien[150] = "GET " WEB_URL_API "&field1=";
        char field_2[10] = "&field2=";
        char *write_letter_post = "\n"
                "Host: "WEB_SERVER"\r\n"
                "User-Agent: esp-idf/1.0 esp32\r\n"
                "\r\n";

        char buffer_hall[10];
        char buffer_temp[10];
        sprintf(buffer_hall, "%hd", sum);
        sprintf(buffer_temp, "%hd", sum_temp);

        strcat(lien, buffer_hall);
        strcat(lien, field_2);
        strcat(lien, buffer_temp);
        strcat(lien, write_letter_post);

        if (write(s, lien, strlen(lien)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... Data sent successfully to %s",WEB_SERVER);

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);

        //erase old values
        /*sum = hall_sens_read();
        sum_temp = temprature_sens_read();*/
        sum = 0;
        sum_temp=0;
        printf("\n");
        ESP_LOGI(TAG, "... done reading response from %s. Last read return=%d errno=%d\r",WEB_SERVER, r, errno);
        close(s);
        ESP_LOGI(TAG, "Close socket...\n");
        printf("\t--------------------------------------------------------------------------------------------\n");

    }

}

//---------------------------------------------//


static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void obtain_time(void)
{
    //ESP_ERROR_CHECK( nvs_flash_init() );
    //initialise_wifi();
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP");
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    //ESP_ERROR_CHECK( esp_wifi_stop() );
}



static void time_set(void * now){


	        struct tm timeinfo;
	        //time(&now);
	        localtime_r(now, &timeinfo);
	        // Is time set? If not, tm_year will be (1970 - 1900).

	        if (timeinfo.tm_year < (2016 - 1900)) {
	            ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
	            obtain_time();
	            // update 'now' variable with current time
	            time(now);
	        }

	        /*char strftime_buf[64];
	        // Set timezone to China Standard Time
	        setenv("TZ", "UTC-3", 1);
	        tzset();
	        localtime_r(now, &timeinfo);
	        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	        ESP_LOGI(TAG, "The current date/time in Tallinn is: %s", strftime_buf);*/
	        vTaskDelete(NULL);
}


void app_main()
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();


    // Set timezone to Tallinn
    setenv("TZ", "UTC-3", 1);
    tzset();
    time_t now;

    //time(&now);


    xTaskCreate(&time_set, "time_set", 4096, &now, 5, NULL); //set time and auto-delete
    xTaskCreate(&http_get_task, "http_get_task", 4096, &now, 5, NULL);

}
