# name: Joseph Hanger
# netID: jhang5
# title: mp1
# date: 02/07/2025

.data

.extern skyline_star_list     # head of linked list of stars
.extern skyline_windows       # head of windows array
.extern skyline_win_cnt       # number of windows in array
.extern skyline_beacon        # beacon struct

.equ SKYLINE_WIN_MAX, 4000
.equ SKYLINE_SCREEN_WIDTH, 640
.equ SKYLINE_SCREEN_HEIGHT, 480
.equ SKYLINE_STAR_BYTES, 16
.equ SKYLINE_WIN_BYTES, 16
.equ SKYLINE_FBUF_BYTES, 4
.equ SKYLINE_IMG_BYTES, 4

.text

.global   skyline_init
.type     skyline_init, @function
.global   add_star
.type     add_star, @function
.global   remove_star
.type     remove_star, @function
.global   draw_star
.type     draw_star, @function
.global   add_window
.type     add_window, @function
.global   remove_window
.type     remove_window, @function
.global   draw_window
.type     draw_window, @function
.global   start_beacon
.type     start_beacon, @function
.global   draw_beacon
.type     draw_beacon, @function

# initializes global variables
# args: void
# return void
skyline_init:
     la   t0, skyline_star_list
     sd   x0, 0(t0)                # initialize head to NULL
     ret

# struct skyline_star
# datatype var   offset
# =======================
# struct * next  0 bytes
# uint16_t x     8 bytes
# uint16_t y     10 bytes
# uint32_t color 12 bytes

# adds a new star to a linked list
# allocates memory and adds star to head of linked list
# void add_star(uint16_t x, uint16_t y, uint16_t color)
# args: uint16_t x, uint16_t y, uint32_t color
# return: void
add_star:
     addi sp, sp, -32                   # caller-save

     sd   ra, 24(sp)
     sd   s1, 16(sp)
     sd   s2, 8(sp)
     sd   s3, 0(sp)

     mv   s1, a0
     mv   s2, a1
     mv   s3, a2

     addi a0, x0, SKYLINE_STAR_BYTES    # size of bytes to allocate from memory
     call malloc                        # return pointer to new address
     mv   t0, a0                        # t0 holds new node pointer

     mv   a0, s1                        # caller-restore
     mv   a1, s2
     mv   a2, s3

     ld   ra, 24(sp)
     ld   s1, 16(sp)
     ld   s2, 8(sp)
     ld   s3, 0(sp)

     addi sp, sp, 32

     beq  t0, x0, return                # check if malloc failed to allocate memory

# t0 - new node pointer
# t1 - new node next pointer
# t2 - skyline_star_list head address
# t3 - skyline_star_list pointer
     sh   a0, 8(t0)
     sh   a1, 10(t0)
     sw   a2, 12(t0)

     la   t2, skyline_star_list    # get head address
     ld   t3, 0(t2)                # get head pointer
     ld   t1, 0(t0)                # get new node next pointer
     sd   t3, 0(t0)
     sd   t0, 0(t2)                # make head the new node

     ret

# removes a star from linked list
# traverse through linked list and free memory of star at specified position
# handle edge cases like removing head and tail
# void remove_star(uint16_t x, uint16_t y)
# args: uint16_t x, uint16_t
# return: void
remove_star:
# t0 - current pointer
# t1 - previous pointer
# t2 - head address
# t3 - node x
# t4 - node y
     la   t2, skyline_star_list
     ld   t0, 0(t2)
     li   t1, 0

rs_search:
     beq  t0, x0, return           # check if current pointer is at end of list
     lhu  t3, 8(t0)
     lhu  t4, 10(t0)
     bne  t3, a0, rs_next          # check if current pointer x is equal to search x
     bne  t4, a1, rs_next          # check if current pointer y is equal to search y
     j    rs_found

rs_next:
     mv   t1, t0                   # save previous pointer
     ld   t0, 0(t0)                # get the next pointer
     j    rs_search

rs_found:
# t4 - head pointer
# t3 - previous next pointer
     ld   t4, 0(t2)
     beq  t0, t4, rs_head          # check if trying to delete head

     ld   t3, 0(t0)
     sd   t3, 0(t1)                # set previous next pointer to current next pointer
     j    rs_free

rs_head:
# t3 - current next pointer
     ld   t3, 0(t0)
     sd   t3, 0(t2)                # set head to current next pointer
     j    rs_free

rs_free:
     addi sp, sp, -16              # IDK does stack need to be 16-byte aligned?
     sd   ra, 8(sp)

     mv   a0, t0                   # set input to current pointer
     call free

     # implement caller-restore
     ld   ra, 8(sp)
     addi sp, sp, 16

     ret

# draws star to frame buffer
# finds frame buffer pixel for x and y pixel on screen
# stores star color at frame buffer pixel
# void draw_star(uint16_t * fbuf, const struct skyline_star * star)
# args: uint16_t * fbuf, const struct skyline_star *star
# return: void
draw_star:
# t0 - x
# t1 - y
# t2 - color
# t3 - fbuf address
# t4 - SCREEN_WIDTH
# t5 - SCREEN_HEIGHT
# t6 - temp for multiplying
     lhu  t0, 8(a1)
     lhu  t1, 10(a1)
     lw   t2, 12(a1)
     li   t4, SKYLINE_SCREEN_WIDTH
     li   t5, SKYLINE_SCREEN_HEIGHT

     bltz t0, return     # check x out of lower bound SCREEN_WIDTH
     bgeu t0, t4, return # check x out of upper bound 0
     bltz t1, return     # check y out of lower bound SCREEN_HEIGHT
     bgeu t1, t5, return # check y out of upper bound 0

     mv   t3, t1         # fbuf address = (x + y * 640) * 4 + fbuf
     mul  t3, t3, t4
     add  t3, t3, t0
     li   t6, 4
     mul  t3, t3, t6
     add  t3, t3, a0

     sw   t2, 0(t3)      # store color at fbuf pixel address

     ret

# struct skyline_window
# datatype  var   offset
# =======================
# uint16_t  x     0 bytes
# uint16_t  y     2 bytes
# uint8_t   w     4 bytes
# uint8_t   h     5 bytes
# uint32_t  color 6 bytes

# add window to windows array
# find next available address in array and add new window content
# void add_window(uint16_t x, uint16_t y, uint8_t h, uint8_t w, uint16_t color)
# args: uint16_t x, uint16_t y, uint8_t h, uint8_t w, uint16_t color
# return: void
add_window:
# t0 - skyline_win_cnt address
# t1 - skyline_win_cnt data
# t2 - skyline_windows array address
# t3 - max number of skyline windows
# t4 - skyline struct number of bytes
# t5 - temp for algebra
     la   t0, skyline_win_cnt # check if reached window cap
     lhu  t1, 0(t0)
     li   t3, SKYLINE_WIN_MAX
     bgeu t1, t3, return

     la   t2, skyline_windows      # next window address in array equal to
     li   t4, SKYLINE_WIN_BYTES
     mul  t5, t1, t4               # array address = SKYLINE_WIN_NUM_BYTES *
     add  t2, t2, t5               # skyline_win_cnt + skyline_windows

     sh   a0, 0(t2)
     sh   a1, 2(t2)
     sb   a2, 4(t2)
     sb   a3, 5(t2)
     sw   a4, 6(t2)

     addi t1, t1, 1                # increment skyline_win_cnt
     sh   t1, 0(t0)

     ret

# remove window from windows array
# traverse through array until find window at specified position
# shift higher addresses down to keep memory contiguous
# delete the last windows contents
# void remove_window(uint16_t x, uint16_t y)
# args: uint16_t x, uint16_t y
# return: void
remove_window:
# t0 - skyline_win_cnt address
# t1 - skyline_win_cnt data
# t2 - skyline_windows array address
# t3 - counter
     la   t0, skyline_win_cnt
     lhu  t1, 0(t0)
     beqz t1, return          # check windows array is empty

     la   t2, skyline_windows
     li   t3, 0

rw_search:         # traverse array till find matching x and y
# t5 - temp x
# t6 - temp y
     lhu  t5, 0(t2)
     lhu  t6, 2(t2)
     addi t3, t3, 1
     bne  a0, t5, rw_next               # check if x and y match
     bne  a1, t6, rw_next
     j    rw_found

rw_next:
     addi t2, t2, SKYLINE_WIN_BYTES     # advance address to next window in array
     bltu t3, t1, rw_search
     j return

rw_found:                # shift each element address -SKYLINE_WIN_BYTES
# t5 - element data
     lh   t5, 8(t2)
     sh   t5, 0(t2)
     lh   t5, 10(t2)
     sh   t5, 2(t2)
     lb   t5, 12(t2)
     sb   t5, 4(t2)
     lb   t5, 13(t2)
     sb   t5, 5(t2)
     lw   t5, 14(t2)
     sw   t5, 6(t2)

     addi t2, t2, SKYLINE_WIN_BYTES     # advance to next window in array
     addi t3, t3, 1
     bltu t3, t1, rw_found              # check if at the end of array

     sh   x0, 0(t2)                     # clear the last element
     sh   x0, 2(t2)
     sb   x0, 4(t2)
     sb   x0, 5(t2)
     sw   x0, 6(t2)

     addi t1, t1, -1
     sh   t1, 0(t0)

     ret

# draw window to screen
# find frame buffer pixels at window x + width and y + height
# loop over height and width
# check if pixels are out of bounds
# void draw_window(uint16_t *fbuf, const struct skyline_window *win)
# args: uint16_t *fbuf, const struct skyline_window *win
# return: void
draw_window:
# t0 - width
# t1 - height
# t2 - index
# t3 - jndex
     lb   t0, 4(a1)
     lb   t1, 5(a1)
     li   t2, 0

dw_outer:
# t4 - y + index
# t5 - screen height
     bgeu t2, t1, return                # check y index more than height

     lhu  t4, 2(a1)
     add  t4, t4, t2                    # check y + index out of lower bound
     bltz t4, dw_skip_outer

     li   t5, SKYLINE_SCREEN_HEIGHT
     bgeu t4, t5, dw_skip_outer         # check if y + index out of upper bound

     li   t3, 0

dw_inner:
# t4 - doin all kinds of stuff
# t5 - same
     bgeu t3, t0, dw_skip_outer         # check x index more than width

     lhu  t4, 0(a1)
     add  t4, t4, t3                    # check x + jndex out of lower bounds
     bltz t4, dw_skip_inner

     li   t5, SKYLINE_SCREEN_WIDTH
     bgeu t4, t5, dw_skip_inner         # check x + jndex out of upper bounds

     lhu  t4, 2(a1)
     add  t4, t4, t2                    # fbuf address = (x + j + (y + i) * 640) * 2 + fbuf
     li   t5, SKYLINE_SCREEN_WIDTH
     mul  t4, t4, t5
     add  t4, t4, t3
     lhu  t5, 0(a1)
     add  t4, t4, t5
     li   t5, SKYLINE_FBUF_BYTES
     mul  t4, t4, t5
     add  t4, t4, a0

     lw   t5, 6(a1)
     sw   t5, 0(t4)                     # store color at pixel_addr

dw_skip_inner:
     addi t3, t3, 1                     # increment jndex
     j    dw_inner

dw_skip_outer:
     addi t2, t2, 1                     # increment index
     j    dw_outer

# struct skyline_beacon
# datatype  var    offset
# =========================
# uint16_t* img    0 bytes
# uint16_t  x      8 bytes
# uint16_t  y      10 bytes
# uint8_t   dia    12 bytes
# padding //////// 13 bytes
# uint16_t  period 14 bytes
# uint16_t  ontime 16 bytes

# initilize the beacon struct
# void start_beacon(const uint16_t *img, uint16_t x, uint16_t y,
#                   uint8_t dia, uint16_t period, uint16_t ontime)
# args: const uint16_t *img, uint16_t x, uint16_t y,
#       uint8_t dia, uint16_t period, uint16_t ontime
# return: void
start_beacon:
# t0 - beacon struct address
     la   t0, skyline_beacon
     sd   a0, 0(t0)
     sh   a1, 8(t0)
     sh   a2, 10(t0)
     sb   a3, 12(t0)
     sh   a4, 14(t0)
     sh   a5, 16(t0)
     ret

# draw beacon to screen
# make beacon flash
# find frame buffer pixel and beacon image pixel
# loop over beacon diameter
# use x and y offset for frame buffer
# check if pixels are out of bounds
# void draw_beacon(uint16_t *fbuf, uint64_t t, const struct skyline_beacon *bcn)
# args: uint16_t *fbuf, uint64_t t, const struct skyline_beacon *bcn
# return: void
draw_beacon:
# t0 - beacon period
# t1 - beacon ontime
# t2 - t % period
     lhu  t0, 14(a2)     # the beacon should only draw for value of tick period
     lhu  t1, 16(a2)
     rem  t2, a1, t0
     bgeu t2, t1, return # check t % period greater than or equal ontime

# t0 - index
# t1 - jndex
# t2 - dia
     lbu  t2, 12(a2)
     li   t0, 0
db_outer:
# t3 - y + index
# t4 - screen height
     bgeu t0, t2, return                # check y index more than diameter

     lhu  t3, 10(a2)
     add  t3, t3, t0
     bltz t3, db_skip_outer             # check y + index out of lower bounds

     li   t4, SKYLINE_SCREEN_HEIGHT
     bgeu t3, t4, db_skip_outer         # check y + index out of upper bounds

     li   t1, 0
db_inner:
# t3 - x + jndex then fbuf address
# t4 - constants then img address
# t5 - constants and inputs

     bgeu t1, t2, db_skip_outer         # check x index more than diameter

     lhu  t3, 8(a2)
     add t3, t3, t1
     bltz t3, db_skip_inner             # check x + jndex out of upper bounds

     li   t4, SKYLINE_SCREEN_WIDTH
     bgeu t3, t4, db_skip_inner         # check x + jndex out of lower bounds

     lhu  t3, 10(a2)
     add  t3, t3, t0                    # fbuf address = (x + j + (y + i) * 640) * 4 + fbuf
     li   t4, SKYLINE_SCREEN_WIDTH
     mul  t3, t3, t4
     add  t3, t3, t1
     lhu  t4, 8(a2)
     add  t3, t3, t4
     li   t4, SKYLINE_FBUF_BYTES
     mul  t3, t3, t4
     add  t3, t3, a0               # fbuf address

     mul  t4, t0, t2               # img address = (j + i * dia) * 2 + img
     add  t4, t4, t1
     li   t5, SKYLINE_IMG_BYTES
     mul  t4, t4, t5
     ld   t5, 0(a2)
     add  t4, t4, t5               # img pixel addr

     lw   t5, 0(t4)                # get color data
     sw   t5, 0(t3)

db_skip_inner:
     addi t1, t1, 1                # increment jndex
     j    db_inner

db_skip_outer:
     addi t0, t0, 1                # increment index
     j    db_outer

     ret

# the most important label
return:
     ret

# ###################
# .........
#
#     |\__/,|   (`\
#   _.|o o  |_   ) )
# -(((---(((--------

.end
