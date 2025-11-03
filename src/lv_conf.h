/**
 * @file lv_conf.h
 * Configuration file for LVGL.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/*Color depth: 1, 8, 16, 32*/
#define LV_COLOR_DEPTH     16

/*Swap the 2 bytes of RGB565 color. Useful if the display has a different byte order.*/
#define LV_COLOR_16_SWAP   0

/*Enable features to draw on transparent background.
 *It's required if you need transparent charts, labels, etc.
 *Requires `LV_COLOR_DEPTH = 32`*/
#define LV_COLOR_SCREEN_TRANSP    0

/* Adjust color mix functions rounding.
 * 0: round down, 64: round to nearest, 128: round up
 */
#define LV_COLOR_MIX_ROUND_OFS  0

/*Images pixels with this color will not be drawn if they are  chroma keyed)*/
#define LV_COLOR_CHROMA_KEY    lv_color_hex(0x00ff00)         /*Greenscreen*/

/*=========================
   MEMORY SETTINGS
 *=========================*/

/*1: use custom malloc/free, 0: use the built-in `lv_mem_alloc()` and `lv_mem_free()`*/
#define LV_MEM_CUSTOM      0
#if LV_MEM_CUSTOM == 0
/*Size of the memory available for `lv_mem_alloc()` in bytes (>= 2kB)*/
#  define LV_MEM_SIZE    (32U * 1024U)          /*[bytes]*/

/*Set an address for the memory pool instead of allocating it as a normal array. Can be in external SRAM too.*/
#  define LV_MEM_ADR          0

/*0: Default internal malloc header, 1: Internal malloc header is distributed in 2 locations*/
#  define LV_MEM_POOL_INCLUDE_POINTER_SIZE 0

#endif  /*LV_MEM_CUSTOM*/

/*Number of the intermediate memory buffer used during rendering and other internal processing mechanisms.
 *You will see an error log message if there wasn't enough buffers. */
#define LV_MEM_BUF_MAX_NUM 16

/*Use the standard `memcpy` and `memset` instead of LVGL's own functions. (Might be faster)*/
#define LV_MEMCPY_MEMSET_STD    1

/*====================
   HAL SETTINGS
 *====================*/

/*Default display refresh period.
 *Can be changed in the display driver (`lv_disp_drv_t`).*/
#define LV_DISP_DEF_REFR_PERIOD     30      /*[ms]*/

/*Input device read period in milliseconds*/
#define LV_INDEV_DEF_READ_PERIOD    30      /*[ms]*/

/*Use a custom tick source that tells the elapsed time in milliseconds.
 *It removes the need to call `lv_tick_inc()` in every ms.*/
#define LV_TICK_CUSTOM     1
#if LV_TICK_CUSTOM
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"         /*Header for the system time function*/
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())     /*Expression evaluating to current system time in ms*/
#endif   /*LV_TICK_CUSTOM*/

/*Default Dot Per Inch*/
#define LV_DPI_DEF                  130

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/*-------------
 * Drawing
 *-----------*/

/*Enable complex draw engine.
 *Required to draw shadow, gradient, rounded corners, circles, arc, skew lines, image transformations or any masks*/
#define LV_DRAW_COMPLEX 1
#if LV_DRAW_COMPLEX != 0

/*Allow buffering some drawn objects.
 *It might render the screen faster in some cases.*/
#define LV_DRAW_BUF_MAX_NUM 16

/*If it's enabled, the rendered data is stored in `lv_mem` and can be automatically cached.*/
#define LV_DRAW_CACHE_ENABLED 0

#endif /*LV_DRAW_COMPLEX*/

/*Set the number of draw units.
 *A draw unit is a piece of drawing work that can be rendered in parallel.
 *E.g. there is a screen with buttons, labels, charts.
 *The buttons can be a draw unit, the labels can be an other unit, and the chart is a third one.
 *That is, they can be rendered in parallel if the GPU supports it.*/
#define LV_DRAW_UNIT_MAX_COUNT 1

/*Default image cache size. Image caching keeps the images opened.
 *If running out of memory try to reduce it. [bytes]*/
#define LV_IMG_CACHE_DEF_SIZE       0

/*Number of stops allowed per gradient. Increase this to allow more stops.
 *This adds memory consumption per gradient including rounded rectangles.*/
#define LV_GRADIENT_MAX_STOPS       2

/*Default gradient buffer size.
 *When LVGL calculates the gradient, it can save the result into a buffer to avoid recalculating it.*/
#define LV_GRAD_CACHE_DEF_SIZE      0

/*Allow dithering that looks good on lower color depth display.
 *Dithering is a graphics technique that creates the illusion of new colors and shades
 *by varying the pattern of dots that are displayed.
 *This is a post processing effect so it requires `LV_USE_DRAW_BUF` and `lv_draw_buf_init()`.*/
#define LV_DITHER_GRADIENT 0

/*Maximum buffer size to allocate for rotation.
 *Only used if software rotation is enabled in the display driver.*/
#define LV_DISP_ROT_MAX_BUF         (10*1024)

/*-------------
 * GPU
 *-----------*/

/*Use STM32's DMA2D (aka Chrom ART) GPU*/
#define LV_USE_GPU_STM32_DMA2D  0

/*Use NXP's PXP GPU i.MX RTxxx platforms*/
#define LV_USE_GPU_NXP_PXP      0

/*Use NXP's VG-Lite GPU i.MX RTxxx platforms*/
#define LV_USE_GPU_NXP_VG_LITE  0

/*Use SWM341's DMA2D GPU*/
#define LV_USE_GPU_SWM341_DMA2D 0

/*Use ESP32's DMA.
 *A transfer is started in `lv_draw_map()` and `lv_draw_fill()` and
 *`lv_draw_wait_for_finish()` waits for the transfer to be finished.
 *Requires `LV_USE_DRAW_BUF` and `lv_draw_buf_init()`*/
#define LV_USE_DMA_ESP32 0

/*-------------
 * Logging
 *-----------*/

/*Enable the log module*/
#define LV_USE_LOG      1
#if LV_USE_LOG

/*How important log should be added:
 *LV_LOG_LEVEL_TRACE       A lot of logs to give detailed information
 *LV_LOG_LEVEL_INFO        Log important events
 *LV_LOG_LEVEL_WARN        Log if something unwanted happened but didn't cause a problem
 *LV_LOG_LEVEL_ERROR       Only critical issue, when the system may fail
 *LV_LOG_LEVEL_USER        Only logs added by the user
 *LV_LOG_LEVEL_NONE        Do not log anything*/
#  define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN

/*1: Print the log with 'printf'; 0: User need to register a callback*/
#  define LV_LOG_PRINTF   0

/*Enable/disable LV_LOG_TRACE in modules that produces a lot of logs*/
#  define LV_LOG_TRACE_MEM            1
#  define LV_LOG_TRACE_TIMER          1
#  define LV_LOG_TRACE_INDEV          1
#  define LV_LOG_TRACE_DISP_REFR      1
#  define LV_LOG_TRACE_EVENT          1
#  define LV_LOG_TRACE_OBJ_CREATE     1
#  define LV_LOG_TRACE_LAYOUT         1
#  define LV_LOG_TRACE_ANIM           1

#endif  /*LV_USE_LOG*/

/*-------------
 * Asserts
 *-----------*/

/*Enable asserts if an argument is incorrect. E.g. an invalid object type.*/
#define LV_USE_ASSERT_NULL          1   /*Check if pointers are NULL*/
#define LV_USE_ASSERT_OBJ           1   /*Check the object's type and existence*/
#define LV_USE_ASSERT_STYLE         0   /*Check if the styles are properly initialized*/

/*Add a custom handler when assert happens e.g. to print where it happened*/
#define LV_ASSERT_HANDLER_INCLUDE   <stdio.h>
#define LV_ASSERT_HANDLER   {printf("LVGL Assert: %s, %d\n", __FILE__, __LINE__); while(1);}

/*-------------
 * Others
 *-----------*/

/*1: Show CPU usage and FPS count*/
#define LV_USE_PERF_MONITOR     0

/*1: Show the used memory and the memory fragmentation
 * Requires LV_MEM_CUSTOM = 0*/
#define LV_USE_MEM_MONITOR      0

/*1: Draw random colored rectangles over the redrawn areas*/
#define LV_USE_REFR_DEBUG       0

/*Change the built-in hit-test algorithm.
 * `LV_USE_HIT_TEST_SIMPLE` is faster but considers the object's bounding box only.
 * `LV_USE_HIT_TEST_PRECISE` is slower but considers the object's shape too.*/
#define LV_USE_HIT_TEST_SIMPLE 0

/*==================
 *    FONT USAGE
 *===================*/

/*Montserrat fonts with ASCII range and some symbols using bpp = 4
 *https://fonts.google.com/specimen/Montserrat*/
#define LV_FONT_MONTSERRAT_8     0
#define LV_FONT_MONTSERRAT_10    0
#define LV_FONT_MONTSERRAT_12    1
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_18    0
#define LV_FONT_MONTSERRAT_20    0
#define LV_FONT_MONTSERRAT_22    0
#define LV_FONT_MONTSERRAT_24    1
#define LV_FONT_MONTSERRAT_26    0
#define LV_FONT_MONTSERRAT_28    1
#define LV_FONT_MONTSERRAT_30    0
#define LV_FONT_MONTSERRAT_32    0
#define LV_FONT_MONTSERRAT_34    0
#define LV_FONT_MONTSERRAT_36    0
#define LV_FONT_MONTSERRAT_38    0
#define LV_FONT_MONTSERRAT_40    0
#define LV_FONT_MONTSERRAT_42    0
#define LV_FONT_MONTSERRAT_44    0
#define LV_FONT_MONTSERRAT_46    0
#define LV_FONT_MONTSERRAT_48    0

/*Demonstrate special features*/
#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0  /*bpp = 3*/

/*Pixel perfect monospace font
 *http://pelulamu.net/unscii/*/
#define LV_FONT_UNSCII_8     0
#define LV_FONT_UNSCII_16    0

/*Optionally declare custom fonts here.
 *You can use these fonts as default font too and they will be available globally.
 *E.g. #define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(my_font_1) LV_FONT_DECLARE(my_font_2)*/
#define LV_FONT_CUSTOM_DECLARE

/*Always set a default font from the built-in fonts*/
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/*Enable handling large fonts e.g. 100+ px height*/
#define LV_FONT_FMT_TXT_LARGE   0

/*Enables/disables support for compressed fonts.*/
#define LV_USE_FONT_COMPRESSED  0

/*Enable subpixel rendering*/
#define LV_USE_FONT_SUBPX       0
#if LV_USE_FONT_SUBPX
/*Set the pixel order of the display.
 *Important only if LV_USE_FONT_SUBPX is enabled.
 *LV_SUBPX_ORDER_RGB or LV_SUBPX_ORDER_BGR*/
#define LV_SUBPX_ORDER    LV_SUBPX_ORDER_RGB
#endif

/*=================
 *  TEXT SETTINGS
 *=================*/

/**
 * Select a character encoding for strings.
 * Your IDE or editor should have the same character encoding
 * - LV_TXT_ENC_UTF8
 * - LV_TXT_ENC_ASCII
 */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/*Can break (wrap) texts on these chars*/
#define LV_TXT_BREAK_CHARS                  " ,.;:-_"

/*If a word is wider than the available space, cut it or scroll it*/
#define LV_TXT_LINE_BREAK_LONG_LEN          0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN  0
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 0

/*The space between characters. If you find issues with kerning, you can reduce it.*/
#define LV_TXT_LETTER_SPACE 0

/*The space between lines in case of multi-line texts.*/
#define LV_TXT_LINE_SPACE 0

/*Enable bidi (bidirectional) support*/
#define LV_USE_BIDI     0

/*Enable Arabic/Persian processing
 *In this case LV_USE_BIDI and LV_TXT_ENC must be LV_TXT_ENC_UTF8*/
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 *  WIDGET USAGE
 *================*/

/*Documentation of the widgets: https://docs.lvgl.io/latest/en/html/widgets/index.html*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1

/*==================
 * EXTRA THEMES
 *==================*/

/*A simple, impressive and very complete theme*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT

/*0: Light mode; 1: Dark mode*/
#  define LV_THEME_DEFAULT_DARK          1

/*1: Enable grow on press*/
#  define LV_THEME_DEFAULT_GROW          1

/*Default transition time in [ms]*/
#  define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif /*LV_USE_THEME_DEFAULT*/

/*A theme designed for monochrome displays*/
#define LV_USE_THEME_MONO   0

/*==================
 *    OTHERS
 *==================*/

/*1: Enable API to take snapshot for object*/
#define LV_USE_SNAPSHOT   0

/*1: Enable monkey-like testing*/
#define LV_USE_MONKEY     0

/*1: Enable grid navigation*/
#define LV_USE_GRIDNAV    0

/*1: Enable flexbox-like layout*/
#define LV_USE_FLEX       1

/*1: Enable assets loading from fs*/
#define LV_USE_FS_STDIO   0

/*1: Enable reading assets from a POSIX-like file system*/
#define LV_USE_FS_POSIX   0

/*1: Enable reading assets from a Win32-like file system*/
#define LV_USE_FS_WIN32   0

/*1: Enable reading assets from a FATFS file system*/
#define LV_USE_FS_FATFS   0

/*1: Enable an API to spilt a string by a delimiter*/
#define LV_USE_STDLIB_STRING  0

/*1: Enable an API to format a string with printf-like syntax*/
#define LV_USE_STDLIB_SPRINTF 0

/*1: Enable functions to convert numbers to string*/
#define LV_USE_STDLIB_STRTO   0

/*==================
* EXAMPLES
*==================*/

/*Enable the examples*/
#define LV_BUILD_EXAMPLES        0

/*===================
 * DEMO USAGE
 ====================*/

/*Enable the demos*/
#define LV_BUILD_DEMO       0

#endif /*LV_CONF_H*/