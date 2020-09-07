#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/rtc_gpio_channel.h"
#include "soc/soc_ulp.h"

#define BOUNCE_COUNT 3
// number of cycles to wait between button scan steps
#define STEP_WAIT 2000

    .bss

    .text

    // Bitmask index of the button that we woke up for.
    // The index of the GPIO pins are GPIO_12, GPIO_14, GPIO_27, GPIO_26
    .global wake_gpio_bit
wake_gpio_bit:
    .long 0

btnbounce_12:
    .long BOUNCE_COUNT
btnbounce_14:
    .long BOUNCE_COUNT
btnbounce_27:
    .long BOUNCE_COUNT
btnbounce_26:
    .long BOUNCE_COUNT 

    // Gets assigned to wake_gpio_bit on wakeup
current_button:
    .long 0

    .global scan_btns
scan_btns:
    // Clear the wake_gpio_bit variable
    move r3, wake_gpio_bit
    move r2, 0x0000
    st r2, r3, 0
    // Clear the bounce counters
    move r3, btnbounce_12
    move r2, BOUNCE_COUNT
    st r2, r3, 0
    st r2, r3, 4
    st r2, r3, 8
    st r2, r3, 12

scan_btn_init:
    /* Start with all of the COL gpio pins in HIGH */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO12_CHANNEL, 1, 1)
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO14_CHANNEL, 1, 1)
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO27_CHANNEL, 1, 1)
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO26_CHANNEL, 1, 1)

scan_btn_check:

handle_gpio_12: 
    /* Track that we're working on button 12 */
    move r3, current_button
    move r2, 1 << 0
    st r2, r3, 0 
    /* Set GPIO 12 to LOW */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + RTCIO_GPIO12_CHANNEL, 1, 1)
    wait STEP_WAIT
    /* Read gpio 13 state into R0 */
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + RTCIO_GPIO13_CHANNEL, 1)
    jumpr notpressed_12, 1, GE        /* if we read high, then button wasn't pressed, move past the incrementing */
    /* Button was pressed ... */
    move r3, btnbounce_12   /* decrement the bounce counter and check if we need to wake_up */
    ld r2, r3, 0
    sub r0, r2, 1
    jump wake_up, EQ         /* if the subtraction result was 0, then jump to wake_up */
    st r0, r3, 0
    wait STEP_WAIT                /* we got a button press, so wait a bit and restart the loop */
    jump scan_btn_init

notpressed_12:
    /* Button was not pressed reset the bounce counter */
    move r1, btnbounce_12
    move r0, BOUNCE_COUNT
    st r0, r1, 0
    /* Reset gpio to high */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO12_CHANNEL, 1, 1)

    wait 8000

handle_gpio_14:
    /* Track that we're working on button 14 */
    move r3, current_button
    move r2, 1 << 1
    st r2, r3, 0 
    /* Set GPIO 14 to LOW */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + RTCIO_GPIO14_CHANNEL, 1, 1)
    wait STEP_WAIT
    /* Read gpio 13 state into R0 */
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + RTCIO_GPIO13_CHANNEL, 1)
    jumpr notpressed_14, 1, GE        /* if we read high, then button wasn't pressed, move past the incrementing */
    /* Button was pressed ... */
    move r3, btnbounce_14   /* decrement the bounce counter and check if we need to wake_up */
    ld r2, r3, 0
    sub r0, r2, 1
    jump wake_up, EQ         /* if the subtraction result was 0, then jump to wake_up */
    st r0, r3, 0
    wait STEP_WAIT                /* we got a button press, so wait a bit and restart the loop */
    jump scan_btn_init

notpressed_14:
    /* Button was not pressed reset the bounce counter */
    move r1, btnbounce_14
    move r0, BOUNCE_COUNT
    st r0, r1, 0
    /* Reset gpio to high */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO14_CHANNEL, 1, 1)

    wait 8000

handle_gpio_27:
    /* Track that we're working on button 27 */
    move r3, current_button
    move r2, 1 << 2
    st r2, r3, 0 
    /* Set GPIO 27 to LOW */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + RTCIO_GPIO27_CHANNEL, 1, 1)
    wait STEP_WAIT
    /* Read gpio 13 state into R0 */
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + RTCIO_GPIO13_CHANNEL, 1)
    jumpr notpressed_27, 1, GE        /* if we read high, then button wasn't pressed, move past the incrementing */
    /* Button was pressed ... */
    move r3, btnbounce_27   /* decrement the bounce counter and check if we need to wake_up */
    ld r2, r3, 0
    sub r0, r2, 1
    jump wake_up, EQ         /* if the subtraction result was 0, then jump to wake_up */
    st r0, r3, 0
    wait STEP_WAIT                /* we got a button press, so wait a bit and restart the loop */
    jump scan_btn_init

notpressed_27:
    /* Button was not pressed reset the bounce counter */
    move r1, btnbounce_27
    move r0, BOUNCE_COUNT
    st r0, r1, 0
    /* Reset gpio to high */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO27_CHANNEL, 1, 1)

    wait 8000

handle_gpio_26:
    /* Track that we're working on button 26 */
    move r3, current_button
    move r2, 1 << 3
    st r2, r3, 0 
    /* Set GPIO 26 to LOW */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + RTCIO_GPIO26_CHANNEL, 1, 1)
    wait STEP_WAIT
    /* Read gpio 13 state into R0 */
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + RTCIO_GPIO13_CHANNEL, 1)
    jumpr notpressed_26, 1, GE        /* if we read high, then button wasn't pressed, move past the incrementing */
    /* Button was pressed ... */
    move r3, btnbounce_26   /* decrement the bounce counter and check if we need to wake_up */
    ld r2, r3, 0
    sub r0, r2, 1
    jump wake_up, EQ         /* if the subtraction result was 0, then jump to wake_up */
    st r0, r3, 0
    wait STEP_WAIT                /* we got a button press, so wait a bit and restart the loop */
    jump scan_btn_init

notpressed_26:
    /* Button was not pressed reset the bounce counter */
    move r1, btnbounce_26
    move r0, BOUNCE_COUNT
    st r0, r1, 0
    /* Reset gpio to high */
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + RTCIO_GPIO26_CHANNEL, 1, 1)

    /* we didn't find any buttons pressed. HALT and let the timer wake us up again. */
    halt

/* reset all of the bounce counters, set the wake_gpio_bit variable, wake_up and halt */
    .global wake_up
wake_up:
    move r1, btnbounce_12
    move r0, BOUNCE_COUNT
    st r0, r1, 0
    st r0, r1, 4
    st r0, r1, 8
    st r0, r1, 12
    move r1, current_button
    move r2, wake_gpio_bit
    ld r0, r1, 0
    st r0, r2, 0 /* store the current button value into wake gpio */
    wake
    /* Disable the sleep wake_up timer so we don't start running again until the main CPU starts us */ 
    WRITE_RTC_FIELD(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
    halt
