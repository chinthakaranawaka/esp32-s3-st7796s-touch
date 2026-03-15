#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch_xpt2046.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

// Pin Definitions
#define EXAMPLE_PIN_NUM_MOSI       11
#define EXAMPLE_PIN_NUM_CLK        12
#define EXAMPLE_PIN_NUM_CS         10
#define EXAMPLE_PIN_NUM_DC         9
#define EXAMPLE_PIN_NUM_RST        8
#define EXAMPLE_PIN_NUM_BACKLIGHT  7

// Touch Pins
#define EXAMPLE_PIN_NUM_MISO        13
#define EXAMPLE_PIN_NUM_TOUCH_CS    6
#define EXAMPLE_PIN_NUM_TOUCH_IRQ   5

// Display Configuration
#define EXAMPLE_LCD_CMD_BITS       8
#define EXAMPLE_LCD_PARAM_BITS     8
#define EXAMPLE_LCD_H_RES          320
#define EXAMPLE_LCD_V_RES          480
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1

// LVGL buffer size
#define EXAMPLE_LVGL_BUF_SIZE      (EXAMPLE_LCD_H_RES * 50)

static const char *TAG = "ST7796S_XPT2046";

// Touch handle (global for LVGL callback)
static esp_lcd_touch_handle_t touch_handle = NULL;

// Button press counter
static int press_count = 0;

// Event handler function prototypes
static void btn_event_handler(lv_event_t *e);
static void slider_event_handler(lv_event_t *e);

/**
 * LVGL touch read callback - UPDATED with correct API
 */
static void touch_driver_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static esp_lcd_touch_point_data_t touch_points[1];  // New structure for touch points
    uint8_t touch_cnt = 0;

    // Read touch data from controller
    esp_err_t res = esp_lcd_touch_read_data(touch_handle);
    if (res != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    // Get coordinates using the new API (4 parameters, not 6)
    res = esp_lcd_touch_get_data(touch_handle, touch_points, &touch_cnt, 1);
    
    if (res == ESP_OK && touch_cnt > 0) {
        data->point.x = touch_points[0].x;
        data->point.y = touch_points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
        
        ESP_LOGD(TAG, "Touch: X=%d, Y=%d", touch_points[0].x, touch_points[0].y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Button event handler
static void btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *label = (lv_obj_t*)lv_event_get_user_data(e);
    
    if (code == LV_EVENT_CLICKED) {
        press_count++;
        lv_label_set_text_fmt(label, "Button pressed: %d times", press_count);
    }
}

// Slider event handler
static void slider_event_handler(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t*)lv_event_get_user_data(e);
    int32_t val = lv_slider_get_value(slider);
    lv_label_set_text_fmt(label, "Slider: %ld", val);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ST7796S with XPT2046 touch example");
    
    // --- 1. Initialize LVGL library ---
    lv_init();

    // --- 2. Initialize SPI bus (shared between display and touch) ---
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t bus_config = {
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .sclk_io_num = EXAMPLE_PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EXAMPLE_LVGL_BUF_SIZE * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO));

    // --- 3. Create display panel IO (SPI) handle ---
    ESP_LOGI(TAG, "Create display panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io_handle));

    // --- 4. Create ST7796 LCD panel handle ---
    ESP_LOGI(TAG, "Create LCD panel");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

    // --- 5. Initialize and configure the display panel ---
    ESP_LOGI(TAG, "Configure display");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    // Flip display horizontally only
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));  // mirror_x=true, mirror_y=false
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // --- 6. Initialize backlight ---
    ESP_LOGI(TAG, "Initialize backlight");
    gpio_set_direction(EXAMPLE_PIN_NUM_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(EXAMPLE_PIN_NUM_BACKLIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    // --- 7. Create touch panel IO (using same SPI bus) ---
    ESP_LOGI(TAG, "Initialize touch controller XPT2046");
    
    // Configure SPI device for touch
    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t touch_io_config = {
        .dc_gpio_num = GPIO_NUM_NC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_TOUCH_CS,
        .pclk_hz = 2 * 1000 * 1000,  // 2MHz for touch
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &touch_io_config, &touch_io_handle));

    // Configure touch parameters
    esp_lcd_touch_config_t touch_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_IRQ,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    // Create touch handle
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(touch_io_handle, &touch_cfg, &touch_handle));

    // --- 8. Initialize LVGL port ---
    ESP_LOGI(TAG, "Initialize LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // --- 9. Add display to LVGL ---
    ESP_LOGI(TAG, "Add display to LVGL");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = EXAMPLE_LVGL_BUF_SIZE,
        .double_buffer = true,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,   // Mirror horizontally
            .mirror_y = false,   // No vertical mirror
        }
    };
        
    lvgl_port_add_disp(&disp_cfg);

    // --- 10. Register touch input device with LVGL ---
    ESP_LOGI(TAG, "Register touch input device");
    
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_driver_read;
    lv_indev_drv_register(&indev_drv);

    // Wait for LVGL to be ready
    vTaskDelay(pdMS_TO_TICKS(100));

    // --- 11. Create UI with touch-enabled widgets ---
    ESP_LOGI(TAG, "Creating UI");
    
    lvgl_port_lock(0);
    
    // Get active screen and set background
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x2F4F4F), LV_STATE_DEFAULT);

    // Create a title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-S3 + ST7796S + Touch");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Create a touch button
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 150, 60);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2196F3), 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -30);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch Me");
    lv_obj_center(btn_label);
    
    // Add counter display
    lv_obj_t *counter_label = lv_label_create(scr);
    lv_label_set_text(counter_label, "Button pressed: 0 times");
    lv_obj_align(counter_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    
    // Add button click event
    lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_CLICKED, counter_label);
    
    // Create a slider
    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 280);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 50);
    
    // Value display for slider
    lv_obj_t *slider_value = lv_label_create(scr);
    lv_label_set_text(slider_value, "Slider: 0");
    lv_obj_align_to(slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    
    // Update slider value on change
    lv_obj_add_event_cb(slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, slider_value);
    
    // Add a simple touch area indicator
    lv_obj_t *touch_indicator = lv_label_create(scr);
    lv_label_set_text(touch_indicator, "Touch anywhere to test");
    lv_obj_set_style_text_color(touch_indicator, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(touch_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL with touch initialized successfully!");
    
    // Main loop does nothing - LVGL runs in its own task
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}