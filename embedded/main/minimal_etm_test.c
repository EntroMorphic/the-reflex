/* Minimal serial test */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "hal/usb_serial_jtag_ll.h"

// Direct write to USB Serial JTAG bypassing all drivers
static void direct_usj_write(const char *str) {
    while (*str) {
        int timeout = 10000;
        while (!usb_serial_jtag_ll_txfifo_writable() && timeout > 0) {
            esp_rom_delay_us(10);
            timeout--;
        }
        usb_serial_jtag_ll_write_txfifo((const uint8_t *)str, 1);
        str++;
    }
    usb_serial_jtag_ll_txfifo_flush();
}

// Constructor runs early during C runtime init
__attribute__((constructor(101))) void early_startup_hook(void) {
    direct_usj_write("### CONSTRUCTOR ###\r\n");
}

void app_main(void)
{
    direct_usj_write("### APP_MAIN START ###\r\n");
    esp_rom_printf("ROM printf from app_main\r\n");
    printf("stdio printf from app_main\n");
    fflush(stdout);
    
    int i = 0;
    while (1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Count: %d\r\n", i++);
        direct_usj_write(buf);
        usb_serial_jtag_ll_txfifo_flush();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
