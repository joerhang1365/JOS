// skyline.h
//

#ifndef _SKYLINE_H_
#define _SKYLINE_H_

#include <stdint.h>

#define SKYLINE_WIDTH 640
#define SKYLINE_HEIGHT 480
#define SKYLINE_WIN_MAX 4000

struct skyline_star {
    struct skyline_star * next;
    uint16_t x;
    uint16_t y;
    uint32_t color;
};

struct skyline_window {
    uint16_t x;
    uint16_t y;
    uint8_t w;
    uint8_t h;
    uint32_t color;
};

struct skyline_beacon {
    const uint32_t * img;
    uint16_t x;
    uint16_t y;
    uint8_t dia;
    uint16_t period;
    uint16_t ontime;
};

// The following global variables are defined elsewhere. The skyline module
// (what you write) should not define these itself, but should access them.

extern struct skyline_star * skyline_star_list;

extern struct skyline_window skyline_windows[SKYLINE_WIN_MAX];
extern uint16_t skyline_win_cnt;

extern struct skyline_beacon skyline_beacon;

// skyline_init() is called before any of the functions bellow. You can assume
// that all of the global varaibles aboe have been zero-initialized.

extern void skyline_init(void);

// add_star() adds a start at position (x,y) to skyline_stars_list with the
// specified color.

extern void add_star(uint16_t x, uint16_t y, uint32_t color);

// remove_star() removes the star at location (x,y) from the star list, if such
// a star exists. You may assume that at most one star will be added at each location.

extern void remove_star(uint16_t x, uint16_t y);

// draw_star() draws a star to the frame buffer /fbuf/. Do not assume that the
// pointer is to a star in skyline_star_list. (During testing, the function may be
// called with a pointer to a star struct that is not in the list.) You should only
// draw stars that are inside the screen area. That is, 0 <= x < SKYLINE_WIDTH and
// 0 <= y < SKYLINE_HEIGHT.

extern void draw_star(uint16_t * fbuf, const struct skyline_star * star);

// add_window() adds a window of the specified color and size at the specified
// position to the skyline_windows array and updates skyline_win_cnt. If there
// is no room for the window in the array, the request should be ignored.
// The upper left corner of the window is at the (x,y) coordinate. The width and height
// of the window are given by /w/ and /h/. The window color is given by /color/.

extern void add_window (
    uint16_t x, uint16_t y,
    uint8_t w, uint8_t h,
    uint32_t color);

// remove_window() removes a window the upper left corner of which is at
// position (x,y), if such a window exists and updates the window count.
// You may assume that there is at most one window at those coordinates.
// Remember that the windows must be contiguous in the window array.

extern void remove_window(uint16_t x, uint16_t y);

// draw_window() draws a window to the frame buffer /fbuf/. Do not assume that the
// pointer is to a window in skyline_windows. (During testing, the function may be
// called with a pointer to a window struct that is not in the list.) You should
// only draw the portion of the window that is within the screen area. However, some
// or all of the window may be outside the screen area.

extern void draw_window(uint16_t * fbuf, const struct skyline_window * win);

// start_beacon() initializes all fields of the /skyline_beacon/ struct with the
// given values.

extern void start_beacon (
    const uint32_t * img,
    uint16_t x,
    uint16_t y,
    uint8_t dia,
    uint16_t period,
    uint16_t ontime);

// draw_beacon() draws the beacon to the frame buffer /fbuf/. See MP1 documentation for
// more details. Do not assume that /bcn/ is the same as /skyline_beacon/, for testing
// purposes we may call this function with a different beacon struct.

extern void draw_beacon (
    uint16_t * fbuf,
    uint64_t t, // current time
    const struct skyline_beacon * bcn);

#endif // _SKYLINE_H_
