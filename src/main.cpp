#include <stdio.h>
#include "pico/binary_info.h"
#include "pico/stdlib.h"
#include "seesaw.h"
#include "usb_cdc.h"

#define USE_I2C_DMA 1

#if USE_I2C_DMA
    #include "i2c_dma.h"
    static i2c_dma_t *i2c0_dma;
#else
    #include "hardware/i2c.h"
#endif

void ss_read(uint8_t regHigh, uint8_t regLow, uint8_t *buf, uint8_t num)
{
    uint8_t pos = 0;
    uint8_t prefix[2];
    prefix[0] = (uint8_t)regHigh;
    prefix[1] = (uint8_t)regLow;
    while (pos < num)
    {
        uint8_t read_now = [num, pos]
        {
            const uint8_t a = num - pos;
            return 32 < a ? 32 : a;
        }();
        i2c_write_blocking(i2c_default, SEESAW_ADDRESS, prefix, 2, true);
        i2c_read_blocking(i2c_default, SEESAW_ADDRESS, buf + pos, read_now, false);
        pos += read_now;
    }
}

void ss_getID()
{
    uint8_t c = 0;
    ss_read(SEESAW_STATUS_BASE, SEESAW_STATUS_HW_ID, &c, 1);
}

void ss_pinMode(int pins)
{
    uint8_t cmd[] = {SEESAW_GPIO_BASE,
                     SEESAW_GPIO_DIRSET_BULK,
                     (uint8_t)(pins >> 24), (uint8_t)(pins >> 16),
                     (uint8_t)(pins >> 8), (uint8_t)pins};

#if USE_I2C_DMA
    i2c_dma_write(i2c0_dma, SEESAW_ADDRESS, cmd, sizeof(cmd));
#else
    i2c_write_blocking(i2c_default, SEESAW_ADDRESS, cmd, sizeof(cmd), false);
#endif
}

void ss_digitalWrite(uint32_t pins, uint8_t value)
{
    uint8_t cmd[] = {SEESAW_GPIO_BASE,
                     value ? SEESAW_GPIO_BULK_SET : SEESAW_GPIO_BULK_CLR,
                     (uint8_t)(pins >> 24), (uint8_t)(pins >> 16),
                     (uint8_t)(pins >> 8), (uint8_t)pins};

#if USE_I2C_DMA
    i2c_dma_write(i2c0_dma, SEESAW_ADDRESS, cmd, sizeof(cmd));
#else
    i2c_write_blocking(i2c_default, SEESAW_ADDRESS, cmd, sizeof(cmd), false);
#endif
}

bool reserved_addr(uint8_t addr)
{
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

void i2c_scan()
{
    sleep_ms(5000);

    printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr)
    {
        if (addr % 16 == 0)
        {
            printf("%02x ", addr);
        }

        // Perform a 1-byte dummy read from the probe address. If a slave
        // acknowledges this address, the function returns the number of bytes
        // transferred. If the address byte is ignored, the function returns
        // -1.

        // Skip over any reserved addresses.
        int ret;
        uint8_t rxdata;
        if (reserved_addr(addr))
            ret = PICO_ERROR_GENERIC;
        else
            ret = i2c_read_blocking(i2c_default, addr, &rxdata, 1, false);

        printf(ret < 0 ? "." : "@");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
    printf("Done.\n");

    for (;;)
    {
        tight_loop_contents();
    }
}

int main()
{
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    uart_init(uart0, 115200);
    gpio_set_function(PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(PICO_DEFAULT_UART_RX_PIN, GPIO_FUNC_UART);
    // Make the UART pins available to picotool
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_UART_TX_PIN, PICO_DEFAULT_UART_RX_PIN, GPIO_FUNC_UART));
    usb_link_uart(uart0);

#if USE_I2C_DMA
    i2c_dma_init(&i2c0_dma, i2c_default, 100*1000, PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN);
#else
    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
#endif

    // Make the I2C pins available to picotool
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));

    ss_pinMode((1 << 5));

    absolute_time_t next_toggle = make_timeout_time_ms(500);
    absolute_time_t uart_timeout = make_timeout_time_ms(0);
    bool state = false;
    for (;;)
    {
        if (time_reached(next_toggle) && time_reached(uart_timeout))
        {
            next_toggle = make_timeout_time_ms(500);
            ss_digitalWrite((1 << 5), state);
            state = !state;
        }

        const int char_usb = getchar_timeout_us(1);
        if (char_usb != PICO_ERROR_TIMEOUT)
        {
            uart_timeout = make_timeout_time_ms(1000);
            gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
            uart_putc_raw(uart0, char_usb);
        }
        if (uart_is_readable(uart0))
        {
            uart_timeout = make_timeout_time_ms(1000);
            putchar_raw(uart_getc(uart0));
        }
    }
    return 0;
}
