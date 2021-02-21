/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_camera.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "drawfont.h"

#define BOARD_ESP32CAM_AITHINKER

/* Values from menuconfig */
#define WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define WIFI_MAX_RETRY CONFIG_ESP_MAXIMUM_RETRY
#define MQTT_URI       CONFIG_ESP_MQTT_HOST_URI
#define MQTT_TOPIC     CONFIG_ESP_MQTT_TOPIC

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "cam2mqtt";

static int s_retry_num = 0;

RTC_DATA_ATTR static int boot_count = 0;

static void
wifi_event_handler(void *arg, esp_event_base_t event_base,
		   int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT
		   && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < WIFI_MAX_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG, "connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
							    ESP_EVENT_ANY_ID,
							    &wifi_event_handler,
							    NULL,
							    &instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
							    IP_EVENT_STA_GOT_IP,
							    &wifi_event_handler,
							    NULL,
							    &instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = WIFI_SSID,
			.password = WIFI_PASS,
			/* Setting a password implies station will connect to all security modes including WEP/WPA.
			 * However these modes are deprecated and not advisable to be used. Incase your Access point
			 * doesn't support WPA2, these mode can be enabled by commenting below line */
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				    .capable = true,
				    .required = false},
			},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
					       WIFI_CONNECTED_BIT |
					       WIFI_FAIL_BIT,
					       pdFALSE,
					       pdFALSE,
					       portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
			 WIFI_SSID, WIFI_PASS);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
			 WIFI_SSID, WIFI_PASS);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister
			(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister
			(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
}

void init_wifi(void)
{
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES
	    || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();
}

/* ---- */

const esp_mqtt_client_config_t mqtt_cfg = {
	.uri = MQTT_URI,
};

static esp_mqtt_client_handle_t mqtt_client;

void init_mqtt(void)
{
	mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	// esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
	esp_mqtt_client_start(mqtt_client);
}

/* ---- */

// ESP32Cam (AiThinker) PIN Map
#ifdef BOARD_ESP32CAM_AITHINKER

#define BLINK_GPIO 33
#define FLASHLIGHT_GPIO 4

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1	//software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#endif

static camera_config_t camera_config = {
	.pin_pwdn = CAM_PIN_PWDN,
	.pin_reset = CAM_PIN_RESET,
	.pin_xclk = CAM_PIN_XCLK,
	.pin_sscb_sda = CAM_PIN_SIOD,
	.pin_sscb_scl = CAM_PIN_SIOC,

	.pin_d7 = CAM_PIN_D7,
	.pin_d6 = CAM_PIN_D6,
	.pin_d5 = CAM_PIN_D5,
	.pin_d4 = CAM_PIN_D4,
	.pin_d3 = CAM_PIN_D3,
	.pin_d2 = CAM_PIN_D2,
	.pin_d1 = CAM_PIN_D1,
	.pin_d0 = CAM_PIN_D0,
	.pin_vsync = CAM_PIN_VSYNC,
	.pin_href = CAM_PIN_HREF,
	.pin_pclk = CAM_PIN_PCLK,

	//XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
	.xclk_freq_hz = 20000000,
	.ledc_timer = LEDC_TIMER_0,
	.ledc_channel = LEDC_CHANNEL_0,

	.pixel_format = PIXFORMAT_GRAYSCALE,	//YUV422,GRAYSCALE,RGB565,JPEG
	.frame_size = FRAMESIZE_SVGA,	//QQVGA-UXGA Do not use sizes above QVGA when not JPEG

	.jpeg_quality = 12,	//0-63 lower number means higher quality
	.fb_count = 1		//if more than one, i2s runs in continuous mode. Use only with JPEG
};

static esp_err_t init_camera()
{
	//initialize the camera
	esp_err_t err = esp_camera_init(&camera_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Camera Init Failed");
		return err;
	}

	return ESP_OK;
}

/* ---- */

void init_gpio()
{
	gpio_pad_select_gpio(BLINK_GPIO);
	gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

	gpio_pad_select_gpio(FLASHLIGHT_GPIO);
	gpio_set_direction(FLASHLIGHT_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(FLASHLIGHT_GPIO, 0);	// off
}

/* ---- */

void time_sync_notification_cb(struct timeval *tv)
{
	ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
	ESP_LOGI(TAG, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();
}

static void obtain_time(void)
{
	initialize_sntp();

	// wait for time to be set
	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET
	       && ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)",
			 retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}
	time(&now);
	localtime_r(&now, &timeinfo);
}

/* ---- */

void init_all()
{
	init_wifi();
	init_mqtt();
	init_camera();
	init_gpio();
}

void deinit_all()
{
	// rtc_gpio_isolate(BLINK_GPIO);
	esp_camera_deinit();
	esp_mqtt_client_destroy(mqtt_client);
	esp_wifi_stop();
}

/* ---- */

void app_main(void)
{
	init_all();
	gpio_set_level(BLINK_GPIO, 0);	// on

	++boot_count;
	ESP_LOGI(TAG, "Boot count: %d", boot_count);

	time_t now;
	struct tm timeinfo;
	ESP_LOGI(TAG, "Connecting to WiFi and getting time over NTP.");
	obtain_time();
	time(&now);

	char strftime_buf[64];
	setenv("TZ", "UTC-9", 1); // TODO: means UTC+9
	tzset();
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	/* Sat Feb 20 22:06:52 2021 */
	ESP_LOGI(TAG, "The current date/time in South Korea is: %s",
		 strftime_buf);

	gpio_set_level(FLASHLIGHT_GPIO, 1);	// on
	ESP_LOGI(TAG, "Taking picture...");
	camera_fb_t *fb = esp_camera_fb_get();
	gpio_set_level(FLASHLIGHT_GPIO, 0);	// off

	draw_string(fb->buf, 800, 600, 8 - 1, 8, strftime_buf, FONTCOLOR_BLACK);
	draw_string(fb->buf, 800, 600, 8 + 1, 8, strftime_buf, FONTCOLOR_BLACK);
	draw_string(fb->buf, 800, 600, 8, 8 - 1, strftime_buf, FONTCOLOR_BLACK);
	draw_string(fb->buf, 800, 600, 8, 8 + 1, strftime_buf, FONTCOLOR_BLACK);
	draw_string(fb->buf, 800, 600, 8, 8, strftime_buf, FONTCOLOR_WHITE);

	// grayscale bmp
	uint8_t *buf = NULL;
	size_t buf_len = 0;
	bool converted = frame2jpg(fb, 95, &buf, &buf_len);
	esp_camera_fb_return(fb);
	if (converted) {
		ESP_LOGI(TAG, "Sending it to MQTT topic %s ...", MQTT_TOPIC);
		esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC,
					(const char *)(buf), buf_len, 0, 0);
	}
	// process_image(fb->width, fb->height, fb->format, fb->buf, fb->len);

	// TODO: need free buf?

	gpio_set_level(BLINK_GPIO, 1);	// off

	deinit_all();

	const int deep_sleep_sec = 1 * 60 * 60;
	ESP_LOGI(TAG, "Entering deep sleep for %d seconds", deep_sleep_sec);
	esp_deep_sleep(1000000LL * deep_sleep_sec);
}
