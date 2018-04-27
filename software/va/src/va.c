// c / cpp
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// hwlib
#include <alt_16550_uart.h>
#include <alt_address_space.h>
#include <alt_bridge_manager.h>
#include <alt_cache.h>
#include <alt_clock_manager.h>
#include <alt_fpga_manager.h>
#include <alt_generalpurpose_io.h>
#include <alt_globaltmr.h>
#include <alt_int_device.h>
#include <alt_interrupt.h>
#include <alt_interrupt_common.h>
#include <alt_mmu.h>
#include <alt_printf.h>
#include <alt_watchdog.h>
#include <hwlib.h>
// socal
#include <alt_gpio.h>
#include <alt_sdmmc.h>
#include <hps.h>
#include <socal.h>
// project
#include "../include/alt_pt.h"
#include "../include/diskio.h"
#include "../include/ff.h"
#include "../include/pio.h"
#include "../include/system.h"
#include "../include/va_sm.h"
#include "../include/video.h"

// int __auto_semihosting;

#define FREQ 950000
#define XSIZE 256
#define YSIZE 192
#define PI 3.141596

#define BUTTON_IRQ ALT_INT_INTERRUPT_F2S_FPGA_IRQ0
#define fpga_leds (void*)((uint32_t)ALT_LWFPGASLVS_ADDR + (uint32_t)LED_PIO_BASE)

ALT_16550_HANDLE_t uart;

#define MARGX 40
#define MARGBOT 40
#define MARGTOP 20

#define MINX1 MARGX
#define MAXX1 WIDTH-MARGX
#define MINY1 HEIGHT-MARGBOT
#define MAXY1 MARGTOP

#define HORLINES 3
#define VERLINES 5

extern volatile uint16_t screen[1024 * 768 * 4 + 4];
int speed;

//extern volatile uint8_t font[256][64];

ALT_STATUS_CODE delay_us(uint32_t us) {
	ALT_STATUS_CODE status = ALT_E_SUCCESS;

	uint64_t start_time = alt_globaltmr_get64();
	uint32_t timer_prescaler = alt_globaltmr_prescaler_get() + 1;
	uint64_t end_time;
	alt_freq_t timer_clock;

	status = alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock);
	end_time = start_time + us * ((timer_clock / timer_prescaler) / 1000000);

	while (alt_globaltmr_get64() < end_time) {
	}

	return status;
}

void fpgaprepare() {
	uintptr_t pa;

	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_READY_BASE, 0x0000);
	delay_us(1);

	pa = alt_mmu_va_to_pa((void*) screen, NULL, NULL);

	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_BASE, pa / 8);
	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_READY_BASE, 0x0001);

}

/* Interrupt service routine for the buttons */
void fpga_pb_isr_callback(uint32_t icciar, void *context) {
	//int ALT_RESULT;

	/* Read the captured edges */
	uint32_t edges = pio_get_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE);

	/* Clear the captured edges */
	pio_set_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, edges);

	/* Increase blinking speed if requested */
	if (edges & 0x1) {
		alt_16550_fifo_write_safe(&uart, "INTERRUPT!\n\r", 12, true);
		speed = speed * -1;
	}

}

void init(void) {
	ALT_STATUS_CODE ALT_RESULT = ALT_E_SUCCESS;
	ALT_STATUS_CODE ALT_RESULT2 = ALT_E_SUCCESS;
	//ALT_STATUS_CODE status;

	ALT_RESULT = alt_globaltmr_init();
	ALT_RESULT2 = alt_bridge_init(ALT_BRIDGE_F2S, NULL, NULL);

	alt_fpga_init();
	if (alt_fpga_state_get() != ALT_FPGA_STATE_USER_MODE) {
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "FPGA ERROR\n\r", 12,
		true);
//		status = ALT_E_ERROR;
	}

	ALT_RESULT = alt_bridge_init(ALT_BRIDGE_LWH2F, NULL, NULL);
	alt_addr_space_remap(ALT_ADDR_SPACE_MPU_ZERO_AT_BOOTROM,
			ALT_ADDR_SPACE_NONMPU_ZERO_AT_OCRAM, ALT_ADDR_SPACE_H2F_ACCESSIBLE,
			ALT_ADDR_SPACE_LWH2F_ACCESSIBLE);

	alt_int_global_init();
	alt_int_cpu_init();
	alt_pt_init();
	alt_cache_system_enable();

	//	ALT_RESULT = alt_wdog_reset(ALT_WDOG0);
	//	ALT_RESULT = alt_wdog_reset(ALT_WDOG0_INIT);

	alt_int_dist_target_set(BUTTON_IRQ, 0x3);
	alt_int_dist_trigger_set(BUTTON_IRQ, ALT_INT_TRIGGER_EDGE);
	alt_int_dist_enable(BUTTON_IRQ);
	alt_int_isr_register(BUTTON_IRQ, fpga_pb_isr_callback, NULL);

	/* Clear button presses already detected */
	pio_set_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, 0x1);
	/* Enable the button interrupts */
	pio_set_intmask(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, 0x1);

	alt_int_cpu_enable();
	alt_int_global_enable();

	ALT_RESULT = alt_clk_is_enabled(ALT_CLK_L4_SP);
	if (ALT_RESULT == ALT_E_FALSE)
		ALT_RESULT = alt_clk_clock_enable(ALT_CLK_L4_SP);

	ALT_RESULT = alt_16550_init(ALT_16550_DEVICE_SOCFPGA_UART0, NULL, 0, &uart);
	ALT_RESULT = alt_16550_baudrate_set(&uart, 115200);
	ALT_RESULT = alt_16550_line_config_set(&uart, ALT_16550_DATABITS_8,
			ALT_16550_PARITY_DISABLE, ALT_16550_STOPBITS_1);
	ALT_RESULT = alt_16550_fifo_enable(&uart);
	ALT_RESULT = alt_16550_enable(&uart);
	//ALT_RESULT = alt_gpio_init();
	//ALT_RESULT = alt_gpio_group_config(led_gpio_init, 24);
	ALT_RESULT = alt_16550_fifo_write_safe(&uart, "Program START\n\r", 15,
	true);

	if (ALT_RESULT2 == ALT_E_SUCCESS)
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "F2S Bridge init!!\n\r",
				19, true);
}

void setup_fpga_leds() {
	alt_write_word(fpga_leds, 0x1);
}

void handle_fpga_leds() {
	uint32_t leds_mask = alt_read_word(fpga_leds);
	if (leds_mask != (0x01 << (LED_PIO_DATA_WIDTH - 1))) {
		leds_mask <<= 1;
	} else {
		leds_mask = 0x1;
	}
	alt_write_word(fpga_leds, leds_mask);
}

int main(void) {

	// Calculations
	int32_t *data_arr;
	int32_t data_len;
	int32_t data_cnt;
	uint32_t freq_low = 800000;
	uint32_t freq_high = 923000;
	uint32_t freq_step = 1000;

	// ???

	double hue = 0;
	FATFS *fs;

	int16_t x1 = 0;
	int16_t y1 = 0;
	int16_t x2 = 0;
	int16_t y2 = 0;
	//int16_t x3 = 0;
	//int16_t y3 = 0;
	//int16_t radius = 100;
	//int16_t centerx = 1024 / 2;
	//int16_t centery = 768 / 2;
	char fpsstring[5];

	char string1[255];
	//int sinlut[360];
	//int coslut[360];
	int i = 0;
	int angle = 0;
	double MAXYVAL1, MINYVAL1;
	double MAXYVAL2, MINYVAL2;
	double khz;

	uint32_t freq;
	uint64_t secstart;
	uint64_t secend;
	uint16_t frames;
	uint16_t fps;
	uint32_t timer_prescaler;
	alt_freq_t timer_clock;

	init();

	fs = malloc(sizeof(FATFS));
	f_mount(fs, "0:", 0);

	videoinit();
	fpgaprepare();

	//alt_write_word(ALT_LWFPGASLVS_OFST+LED_PIO_BASE,0xAA);

//	for (i = 0; i < 360; i++) {
//		sinlut[i] = floor(radius * sin(PI * i / 180.0));
//		coslut[i] = floor(radius * cos(PI * i / 180.0));
//	}

	timer_prescaler = alt_globaltmr_prescaler_get() + 1;
	alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock);

	alt_16550_fifo_write_safe(&uart, "Ready to go!\n\r", 14, true);

	speed = 1;
	i = 0;
	angle = 0;
	frames = 0;
	fps = 0;

	secstart = alt_globaltmr_get64();

	setup_fpga_leds();
	va_sm_init();

	for (;;) {
		//main loop

		// -----------------------------------------------------------------------
		// Start calculations

		angle = (angle + speed + 360) % 360;
		data_len = ((freq_high - freq_low) / freq_step);
		data_len = 2 * (data_len + 1);
		data_cnt = 0;

		data_arr = (int32_t*) malloc(data_len * sizeof(int32_t));

		for (freq = freq_low; freq <= freq_high; freq = freq + freq_step) {
			//va_nco_meas(data_arr + data_cnt, freq, 30000);
			data_cnt = data_cnt + 2;
		}
		data_cnt--;
		// End calculations

		// Delay
		// for (int j = 0; j < 950000; j++) {}

		// -----------------------------------------------------------------------

		for (i = 0; i <= data_cnt; i = i + 2) {
			data_arr[i] = floor(
					200
							* cos( angle*1.0/360.0* 8*PI)* sin(
									(i * 0.5 - 0.0) / (0.5 * data_cnt - 0) * 4
											* PI + angle * 1.0 / 360 * 2 * PI)) + 305*sin ( (i * 0.5 - 0.0) / (0.5 * data_cnt - 0) * 8
													* PI + angle * 1.0 / 360 * 12*PI  ) ;
			data_arr[i + 1] = floor(
					150 * cos((i * 0.5 - 0.0) / (0.5 * data_cnt - 0) * 4 * PI)
							+ 50);
		}
		//do something
		//delay_us(25);

//		MAXYVAL1 = data_arr[0];
//		MINYVAL1 = data_arr[0];
//		MAXYVAL2 = data_arr[1];
//		MINYVAL2 = data_arr[1];
//
//		for (i = 2; i < data_cnt; i = i + 2) {
//			if (data_arr[i] > MAXYVAL1)
//				MAXYVAL1 = data_arr[i];
//			else if (data_arr[i] < MINYVAL1)
//				MINYVAL1 = data_arr[i];
//
//			if (data_arr[i + 1] > MAXYVAL2)
//				MAXYVAL2 = data_arr[i + 1];
//			else if (data_arr[i + 1] < MINYVAL2)
//				MINYVAL2 = data_arr[i + 1];
//
//		}

		MINYVAL1 = -1000;
		MINYVAL2 = -500;

		MAXYVAL1 = 1000;
		MAXYVAL2 = 500;
		clrscr();

		setcolor(220, 220, 200);
		// main border
		drawline(MARGX, MARGTOP, WIDTH - MARGX, MARGTOP);
		drawline(WIDTH - MARGX, MARGTOP, WIDTH - MARGX, HEIGHT - MARGBOT);
		drawline(WIDTH - MARGX, HEIGHT - MARGBOT, MARGX, HEIGHT - MARGBOT);
		drawline(MARGX, HEIGHT - MARGBOT, MARGX, MARGTOP);

		setcolor(110, 110, 100);
		//horizontal grid
		for (i = 1; i <= HORLINES; i++)
			drawline(MARGX,
			MARGTOP + i * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1),
			WIDTH - MARGX,
					20 + i * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1));

		//vertical grid
		for (i = 1; i <= VERLINES; i++)
			drawline(MARGX + i * (WIDTH - 2 * MARGX) / (VERLINES + 1), MARGTOP,
			MARGX + i * (WIDTH - 2 * MARGX) / (VERLINES + 1), HEIGHT - MARGBOT);

		//X-AXIS LEGEND
		setcolor(220, 220, 200);
		memset(string1, 0, 255);
		for (i = 0; i <= VERLINES + 1; i++) {
			khz = (freq_low + i * (freq_high - freq_low) / (VERLINES + 1))
					/ 1000.0;
			x1 = floor(khz);
			if (khz < 1000)
				alt_sprintf(string1, "%3d KHz", x1);
			else {
				x2 = (x1%1000)/10;
				x1 = floor(khz / 1000.0);
				//sprintf(string1,"%f Mhz",khz/1000);
				alt_sprintf(string1, "%3d.%0d MHz", x1, x2);
			}
			drawtext(string1, strlen(string1),
					MARGX + i * (WIDTH - 2 * MARGX) / (VERLINES + 1)
							- strlen(string1) / 2 * 8, HEIGHT - MARGBOT + 8);
		}

		//LEFT Y-AXIS LEGEND
		setcolor(255, 0, 0);
		drawtext("1", 1, MARGX - 2 - 8,
		MARGTOP + 0 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
		drawtext("0.75", 4, MARGX - 2 - 8 * 4,
		MARGTOP + 1 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
		drawtext("0.5", 3, MARGX - 2 - 8 * 3,
		MARGTOP + 2 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
		drawtext("0.25", 4, MARGX - 2 - 8 * 4,
		MARGTOP + 3 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
		drawtext("0", 1, MARGBOT - 2 - 8,
		MARGTOP + 4 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

		//RIGHT Y-AXIS LEGEND
		setcolor(0, 255, 0);
		alt_sprintf(string1, " 90");
		string1[3] = 248;
		drawtext(string1, 4, WIDTH - MARGX + 2,
		MARGTOP + 0 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

		alt_sprintf(string1, " 45");
		string1[3] = 248;
		drawtext(string1, 4, WIDTH - MARGX + 2,
		MARGTOP + 1 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

		alt_sprintf(string1, " 0");
		string1[2] = 248;
		drawtext(string1, 3, WIDTH - MARGX + 2,
		MARGTOP + 2 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

		alt_sprintf(string1, "-45");
		string1[3] = 248;
		drawtext(string1, 4, WIDTH - MARGX + 2,
		MARGTOP + 3 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

		alt_sprintf(string1, "-90");
		string1[3] = 248;
		drawtext(string1, 4, WIDTH - MARGX + 2,
		MARGTOP + 4 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

		// GRAPH 1
		setcolor(255, 0, 0);
		i = 0;
		x1 = floor(
		MINX1 + (MAXX1 - MINX1) * (i) / (data_cnt - 1));
		y1 = floor(
				MINY1
						- (MINY1 - MAXY1) * (data_arr[i] - MINYVAL1)
								/ (MAXYVAL1 - MINYVAL1));
		for (i = 2; i <= data_cnt; i = i + 2) {

			x2 = floor(
			MINX1 + (MAXX1 - MINX1) * (i) / (data_cnt - 1));
			y2 = floor(
					MINY1
							- (MINY1 - MAXY1) * (data_arr[i] - MINYVAL1)
									/ (MAXYVAL1 - MINYVAL1));
			drawline(x1, y1, x2, y2);

			x1 = x2;
			y1 = y2;
		}

		// GRAPH 2
		setcolor(0, 255, 0);
		i = 0;
		x1 = floor(
		MINX1 + (MAXX1 - MINX1) * i / (data_cnt - 1));
		y1 = floor(
				MINY1
						- (MINY1 - MAXY1) * (data_arr[i + 1] - MINYVAL2)
								/ (MAXYVAL2 - MINYVAL2));
		// Start printing
		for (i = 2; i <= data_cnt; i = i + 2) {

			x2 = floor(
			MINX1 + (MAXX1 - MINX1) * (i) / (data_cnt - 1));
			y2 = floor(
					MINY1
							- (MINY1 - MAXY1) * (data_arr[i + 1] - MINYVAL2)
									/ (MAXYVAL2 - MINYVAL2));
			drawline(x1, y1, x2, y2);

			x1 = x2;
			y1 = y2;
		}

		free(data_arr);
		// End printing

		// Indication
		handle_fpga_leds();

		setcolor(hslToR(hue, 0.6, 0.5), hslToG(hue, 0.6, 0.5),
				hslToB(hue, 0.6, 0.5));
		memset(fpsstring, 0, 5);
		alt_sprintf(fpsstring, "%dFPS", fps);
		drawtext(fpsstring, strlen(fpsstring), 2, 2);
		swapbuffers();

		secend = alt_globaltmr_get64();
		frames++;

		if ((secend - secstart) > (timer_clock / timer_prescaler)) {
			fps = frames;
			frames = 0;
			secstart = secend;
		}
	}

	// virtually never
	/*alt_16550_fifo_write_safe(&uart, "Program end. Why?\n\r", 3,
	 true);
	 //	ALT_RESULT = alt_int_global_uninit	();
	 alt_bridge_uninit(ALT_BRIDGE_F2S, NULL, NULL);
	 alt_bridge_uninit(ALT_BRIDGE_LWH2F, NULL, NULL);
	 alt_16550_uninit(&uart);

	 return ALT_RESULT;*/
}
