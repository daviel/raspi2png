//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2014 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#define _GNU_SOURCE

#include <math.h>
#include <stdbool.h>
#include <zlib.h>

#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>

#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"
#include "ws2811.h"

#include "bcm_host.h"

typedef struct {
  char red;
  char green;
  char blue;
} Color;

//-----------------------------------------------------------------------

#ifndef ALIGN_TO_16
#define ALIGN_TO_16(x)  ((x + 15) & ~15)
#endif

//-----------------------------------------------------------------------

#define DEFAULT_DISPLAY_NUMBER 0

//-----------------------------------------------------------------------

const char* program = NULL;

//-----------------------------------------------------------------------

void calculate_colors(void);
void calculate_top(void);
void calculate_left(void);
void calculate_right(void);
void calculate_bottom(void);
void screenshot(void);
void image_get_average_color(int xOffset, int yOffset, int xWidth, int yHeight);
void init(void);

int BLOCKSIZE_DIVISION_OF_RESOLUTION = 20; // means resolution divided by

int RESOLUTION_X = 1920;
int RESOLUTION_Y = 1080;
int TARGET_FPS = 24;

int RIGHT_START_LED = 95;
int RIGHT_STOP_LED = 123;

int TOP_START_LED = 123;
int TOP_STOP_LED = 174;

int LEFT_START_LED = 174;
int LEFT_STOP_LED = 204;

int BOTTOM_START_LED = 204;
int BOTTOM_STOP_LED = 255;


int RIGHT_LENGTH;
int TOP_LENGTH;
int LEFT_LENGTH;
int BOTTOM_LENGTH;

int TOP_BLOCKSIZE_X;
int TOP_BLOCKSIZE_Y;

int BOTTOM_BLOCKSIZE_X;
int BOTTOM_BLOCKSIZE_Y;

int LEFT_BLOCKSIZE_X;
int LEFT_BLOCKSIZE_Y;

int RIGHT_BLOCKSIZE_X;
int RIGHT_BLOCKSIZE_Y;


// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10
//#define STRIP_TYPE            WS2811_STRIP_RGB                // WS2812/SK6812RGB integrated chip+leds
#define STRIP_TYPE              WS2811_STRIP_GBR                // WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            SK6812_STRIP_RGBW               // SK6812RGBW (NOT SK6812RGB)
#define LED_COUNT               300


ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum = GPIO_PIN,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = 255,
            .strip_type = STRIP_TYPE,
        },
        [1] =
        {
            .gpionum = 0,
            .count = 0,
            .invert = 0,
            .brightness = 0,
        },
    },
};

Color *color;
void *dmxImagePtr;
VC_RECT_T rect;
DISPMANX_DISPLAY_HANDLE_T displayHandle;
DISPMANX_RESOURCE_HANDLE_T resourceHandle;
int32_t dmxPitch;

//-----------------------------------------------------------------------

int
main(
    int argc,
    char *argv[])
{
    printf("Started!\n");
    int result = 0;
    uint32_t displayNumber = DEFAULT_DISPLAY_NUMBER;
    VC_IMAGE_TYPE_T imageType = VC_IMAGE_RGBA32;
    program = basename(argv[0]);
    bcm_host_init();
    init();

    ws2811_return_t ret;

    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
      fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
      return ret;
    }

    displayHandle = vc_dispmanx_display_open(displayNumber);

    if (displayHandle == 0)
    {
        fprintf(stderr,
                "%s: unable to open display %d\n",
                program,
                displayNumber);

        exit(EXIT_FAILURE);
    }

    DISPMANX_MODEINFO_T modeInfo;
    result = vc_dispmanx_display_get_info(displayHandle, &modeInfo);

    if (result != 0)
    {
        fprintf(stderr, "%s: unable to get display information\n", program);
        exit(EXIT_FAILURE);
    }

    //-------------------------------------------------------------------

    dmxPitch = sizeof(int) * ALIGN_TO_16(RESOLUTION_X);
    dmxImagePtr = malloc(TOP_BLOCKSIZE_Y * dmxPitch);

    //-------------------------------------------------------------------

    uint32_t vcImagePtr = 0;
    resourceHandle = vc_dispmanx_resource_create(imageType,
                                                 RESOLUTION_X,
                                                 RESOLUTION_Y,
                                                 &vcImagePtr);

    //-------------------------------------------------------------------


    struct timespec ts;
    float frame_time = 1/(float) TARGET_FPS;
    ts.tv_sec  = 0;
    ts.tv_nsec = 500000000L;
    double render_time = 0;

    for(int led_num = 0; led_num < 300; led_num++)
      ledstring.channel[0].leds[led_num] = 0;

    while(1){
      clock_t start = clock();
      screenshot();
      calculate_colors();
      clock_t end = clock();
      render_time = (double)(end-start) / CLOCKS_PER_SEC;

      if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
      {
        fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
        break;
      }

      //printf("Elapsed time: %.6f seconds\n", render_time);

      if(render_time >= frame_time){

      }else{
        ts.tv_nsec = (frame_time - render_time) * 1000000000;
        nanosleep(&ts, NULL);
      }
    }

    //-------------------------------------------------------------------

    vc_dispmanx_resource_delete(resourceHandle);
    vc_dispmanx_display_close(displayHandle);
    return 0;
}

void screenshot(void){
  int result = vc_dispmanx_snapshot(displayHandle, resourceHandle, DISPMANX_NO_ROTATE);
  if (result != 0)
  {
      vc_dispmanx_resource_delete(resourceHandle);
      vc_dispmanx_display_close(displayHandle);

      fprintf(stderr, "%s: vc_dispmanx_snapshot() failed\n", program);
      exit(EXIT_FAILURE);
  }
  result = vc_dispmanx_rect_set(&rect, 0, 0, RESOLUTION_X, TOP_BLOCKSIZE_Y);
  result = vc_dispmanx_resource_read_data(resourceHandle, &rect, dmxImagePtr, dmxPitch);
}

void clean_up(void){
  for(int x = 0, l = LED_COUNT; x < l; x++){
    ledstring.channel[0].leds[x] = 0;
  }
  printf("Turning off LEDs.\n");
}

void init(void){
  RIGHT_LENGTH = RIGHT_STOP_LED - RIGHT_START_LED;
  TOP_LENGTH = TOP_STOP_LED - TOP_START_LED;
  LEFT_LENGTH = LEFT_STOP_LED - LEFT_START_LED;
  BOTTOM_LENGTH = BOTTOM_STOP_LED - BOTTOM_START_LED;

  TOP_BLOCKSIZE_X = RESOLUTION_X / TOP_LENGTH;
  TOP_BLOCKSIZE_Y = RESOLUTION_Y / BLOCKSIZE_DIVISION_OF_RESOLUTION;

  BOTTOM_BLOCKSIZE_X = RESOLUTION_X / BOTTOM_LENGTH;
  BOTTOM_BLOCKSIZE_Y = RESOLUTION_Y / BLOCKSIZE_DIVISION_OF_RESOLUTION;

  LEFT_BLOCKSIZE_X = RESOLUTION_X / BLOCKSIZE_DIVISION_OF_RESOLUTION;
  LEFT_BLOCKSIZE_Y = RESOLUTION_Y / LEFT_LENGTH;

  RIGHT_BLOCKSIZE_X = RESOLUTION_X / BLOCKSIZE_DIVISION_OF_RESOLUTION;
  RIGHT_BLOCKSIZE_Y = RESOLUTION_Y / RIGHT_LENGTH;

  color = (Color*) malloc(sizeof(Color));
  on_exit(clean_up, NULL);
}

void image_get_average_color(int xOffset, int yOffset, int xWidth, int yHeight){
  int red = 0;
  int green = 0;
  int blue = 0;

  char *colorPtr = dmxImagePtr;
  colorPtr += xOffset;
  colorPtr += yOffset * RESOLUTION_X;

  char *colorPtrX = colorPtr;
  for (int x = 0, l = xWidth; x < l; x++)
  {
    char *colorPtrY = colorPtrX;
    for (int y = 0, k = yHeight; y < k; y++)
    {
      red += *colorPtrY;
      colorPtrY++;
      green += *colorPtrY;
      colorPtrY++;
      blue += *colorPtrY;
      colorPtrY++;
      colorPtrY++;

      colorPtrY += RESOLUTION_X * sizeof(int);
    }
    colorPtrX += sizeof(int);
  }
  int factor = xWidth * yHeight;
  color->red = red / factor;
  color->green = green / factor;
  color->blue = blue / factor;
}

void calculate_colors(void){
  calculate_top();
  calculate_left();
  calculate_right();
  calculate_bottom();
}

void calculate_top(){
  for(int x = 0, l = TOP_LENGTH; x < l; x++){
    image_get_average_color(x * TOP_BLOCKSIZE_X, 0, TOP_BLOCKSIZE_X, TOP_BLOCKSIZE_Y);
    ledstring.channel[0].leds[TOP_STOP_LED - x] = ((color.blue << 16) + (color.green << 8) + color.red);
    //printf("Pixel %d: rgb: %d %d %d\n", x, color.red, color.green, color.blue);
  }
}

void calculate_left(){
  for(int x = 0, l = LEFT_LENGTH; x < l; x++){
    image_get_average_color(0, x * LEFT_BLOCKSIZE_Y, LEFT_BLOCKSIZE_X, LEFT_BLOCKSIZE_Y);
    ledstring.channel[0].leds[LEFT_START_LED + x] = (color.blue << 16) + (color.green << 8) + color.red;
    //printf("Pixel %d: rgb: %d %d %d\n", x, color.red, color.green, color.blue);
  }
}

void calculate_bottom(){
  for(int x = 0, l = BOTTOM_LENGTH; x < l; x++){
    image_get_average_color(x * BOTTOM_BLOCKSIZE_X, 0, BOTTOM_BLOCKSIZE_X, BOTTOM_BLOCKSIZE_Y);
    ledstring.channel[0].leds[BOTTOM_START_LED + x] = (color.blue << 16) + (color.green << 8) + color.red;
    //printf("Pixel %d: rgb: %d %d %d\n", x, color.red, color.green, color.blue);
  }
}

void calculate_right(){
  for(int x = 0, l = RIGHT_LENGTH; x < l; x++){
    image_get_average_color(0, x * RIGHT_BLOCKSIZE_Y, RIGHT_BLOCKSIZE_X, RIGHT_BLOCKSIZE_Y);
    ledstring.channel[0].leds[RIGHT_STOP_LED - x] = (color.blue << 16) + (color.green << 8) + color.red;
    //printf("Pixel %d: rgb: %d %d %d\n", x, color.red, color.green, color.blue);
  }
}
