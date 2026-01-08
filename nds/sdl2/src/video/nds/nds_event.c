// LGPL-2.1 License
// (C) 2025 Steward Fu <steward.fu@gmail.com>

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <linux/input.h>
#include <dlfcn.h>

#if defined(UT)
#include "unity_fixture.h"
#endif

#include "../../SDL_internal.h"
#include "../../events/SDL_events_c.h"
#include "../../core/linux/SDL_evdev.h"
#include "../../thread/SDL_systhread.h"
#include "../../joystick/nds/nds_joy.h"

#include "snd.h"
#include "common.h"
#include "nds_video.h"
#include "nds_event.h"

#include <dlfcn.h>

nds_event myevent = { 0 };

#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(FXTEC_QX1000) || defined(FXTEC_QX1050) || defined(UT)
static touch_data_t tp[10] = { 0 };
#endif

extern nds_joy myjoy;
extern nds_hook myhook;
extern nds_video myvideo;
extern nds_config myconfig;

static const SDL_Scancode nds_key_code[] = {
    SDLK_UP,            // UP
    SDLK_DOWN,          // DOWN
    SDLK_LEFT,          // LEFT
    SDLK_RIGHT,         // RIGHT
    SDLK_RETURN,        // A
    SDLK_ESCAPE,        // B
    SDLK_LSHIFT,        // X
    SDLK_LALT,          // Y
    SDLK_PAGEUP,        // L1
    SDLK_PAGEDOWN,      // R1
    SDLK_TAB,           // L2
    SDLK_BACKSPACE,     // R2
    SDLK_RCTRL,         // SELECT
    SDLK_RETURN,        // START
    SDLK_0,             // SAVE
    SDLK_1,             // LOAD
    SDLK_2,             // FAST
    SDLK_3,             // EXIT
    SDLK_4,             // SWAP
    SDLK_5,             // MENU
    SDLK_HOME,          // ONION
};

#if defined(UT)
TEST_GROUP(sdl2_event);

TEST_SETUP(sdl2_event)
{
}

TEST_TEAR_DOWN(sdl2_event)
{
}
#endif

static int limit_touch_axis(void)
{
    int r = 0;

    trace_throttled("call %s()\n", __func__);

    if (myevent.touch.x < 0) {
        r = 1;
        myevent.touch.x = 0;
    }

    if (myevent.touch.x >= myevent.touch.max_x) {
        r = 1;
        myevent.touch.x = myevent.touch.max_x;
    }

    if (myevent.touch.y < 0) {
        r = 1;
        myevent.touch.y = 0;
    }

    if (myevent.touch.y > myevent.touch.max_y) {
        r = 1;
        myevent.touch.y = myevent.touch.max_y;
    }

    return r;
}

#if defined(UT)
TEST(sdl2_event, limit_touch_axis)
{
    myevent.touch.x = -10;
    TEST_ASSERT_EQUAL_INT(1, limit_touch_axis());
    TEST_ASSERT_EQUAL_INT(0, myevent.touch.x);

    myevent.touch.x = 10000;
    myevent.touch.max_x = 256;
    TEST_ASSERT_EQUAL_INT(1, limit_touch_axis());
    TEST_ASSERT_EQUAL_INT(myevent.touch.max_x, myevent.touch.x);
}
#endif

static int is_book_mode(void)
{
    trace_throttled("call %s()\n", __func__);

    if ((myconfig.layout.mode.sel == LAYOUT_MODE_B0) ||
        (myconfig.layout.mode.sel == LAYOUT_MODE_B1) ||
        (myconfig.layout.mode.sel == LAYOUT_MODE_B2) ||
        (myconfig.layout.mode.sel == LAYOUT_MODE_B3))
    {
        return 1;
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, is_book_mode)
{
    myconfig.layout.mode.sel = LAYOUT_MODE_B0;
    TEST_ASSERT_EQUAL_INT(1, is_book_mode());

    myconfig.layout.mode.sel = LAYOUT_MODE_N0;
    TEST_ASSERT_EQUAL_INT(0, is_book_mode());
}
#endif

static int inc_touch_axis(int type)
{
    float move = 0.0;
    float v = 100000.0 / ((float)myconfig.pen.speed / 10.0);

    trace_throttled("call %s(type=%d)\n", __func__, type);

    if (myevent.touch.slow_down) {
        v *= 2;
    }

    move = 250000.0 / v;
    if (move <= 0.0) {
        move = 1.0;
    }
    return (int)(1.0 * move);
}

#if defined(UT)
TEST(sdl2_event, inc_touch_axis)
{
    TEST_ASSERT_EQUAL_INT(1, !!inc_touch_axis(0));
}
#endif

static int release_key(void)
{
    int cc = 0;

    trace_throttled("call %s()\n", __func__);

    for (cc = 0; cc <= KEY_BIT_LAST; cc++) {
        if (myevent.keypad.cur_bits & (1 << cc)) {
#if !defined(UT)
            SDL_SendKeyboardKey(SDL_RELEASED, SDL_GetScancodeFromKey(nds_key_code[cc]));
#endif
        }
    }
    myevent.keypad.cur_bits = 0;
    myevent.keypad.pre_bits = 0;
    myevent.input.touch_status = 0;
    myevent.input.button_status = 0;

    return 0;
}

#if defined(UT)
TEST(sdl2_event, release_key)
{
    myevent.keypad.cur_bits = (1 << KEY_BIT_A);
    TEST_ASSERT_EQUAL_INT(0, release_key());
    TEST_ASSERT_EQUAL_INT(0, myevent.keypad.cur_bits);
}
#endif


#if defined(UT)
TEST(sdl2_event, hit_hotkey)
{
    myconfig.hotkey = HOTKEY_BIND_SELECT;
    myevent.keypad.cur_bits = (1 << KEY_BIT_SELECT) | (1 << KEY_BIT_A);
    TEST_ASSERT_EQUAL_INT(1, hit_hotkey(KEY_BIT_A));
}
#endif

static int set_key_bit(uint32_t bit, int val)
{
    if (val) {
        if (myconfig.hotkey == HOTKEY_BIND_SELECT) {
            if (bit == KEY_BIT_SELECT) {
                myevent.keypad.cur_bits = 0;
            }
        }
        else {
            if (bit == KEY_BIT_MENU) {
                myevent.keypad.cur_bits = 0;
            }
        }

        myevent.keypad.cur_bits |= (1 << bit);
    }
    else {
        myevent.keypad.cur_bits &= ~(1 << bit);
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, set_key_bit)
{
    myevent.keypad.cur_bits = 0;
    TEST_ASSERT_EQUAL_INT(0, set_key_bit(KEY_BIT_UP, 1));
    TEST_ASSERT_EQUAL_INT((1 << KEY_BIT_UP), myevent.keypad.cur_bits);

    TEST_ASSERT_EQUAL_INT(0, set_key_bit(KEY_BIT_UP, 0));
    TEST_ASSERT_EQUAL_INT((0 << KEY_BIT_UP), myevent.keypad.cur_bits);
}
#endif

#if defined(MIYOO_FLIP) || defined(R36S) || defined(UT)
static int remap_keypad(jval_t *j, int idx)
{
    int r = 0;
    static int pre_x[2] = { -1, -1 };
    static int pre_y[2] = { -1, -1 };

    static int pre_up[2] = { 0 };
    static int pre_down[2] = { 0 };
    static int pre_left[2] = { 0 };
    static int pre_right[2] = { 0 };

    uint32_t u_key = KEY_BIT_UP;
    uint32_t d_key = KEY_BIT_DOWN;
    uint32_t l_key = KEY_BIT_LEFT;
    uint32_t r_key = KEY_BIT_RIGHT;

    int UP_TH = -1 * myconfig.joy.dzone;
    int DOWN_TH = myconfig.joy.dzone;
    int LEFT_TH = -1 * myconfig.joy.dzone;
    int RIGHT_TH = myconfig.joy.dzone;

    if (idx) {
        u_key = KEY_BIT_X;
        d_key = KEY_BIT_B;
        l_key = KEY_BIT_Y;
        r_key = KEY_BIT_A;

        UP_TH = -1 * myconfig.rjoy.dzone;
        DOWN_TH = myconfig.rjoy.dzone;
        LEFT_TH = -1 * myconfig.rjoy.dzone;
        RIGHT_TH = myconfig.rjoy.dzone;
    }

    if (j->x != pre_x[idx]) {
        pre_x[idx] = j->x;
        if (pre_x[idx] < LEFT_TH) {
            if (pre_left[idx] == 0) {
                r = 1;
                pre_left[idx] = 1;
                set_key_bit(l_key, 1);
            }

            if (pre_right[idx]) {
                r = 1;
                pre_right[idx] = 0;
                set_key_bit(r_key, 0);
            }
        }
        else if (pre_x[idx] > RIGHT_TH){
            if (pre_right[idx] == 0) {
                r = 1;
                pre_right[idx] = 1;
                set_key_bit(r_key, 1);
            }

            if (pre_left[idx]) {
                r = 1;
                pre_left[idx] = 0;
                set_key_bit(l_key, 0);
            }
        }
        else {
            if (pre_left[idx]) {
                r = 1;
                pre_left[idx] = 0;
                set_key_bit(l_key, 0);
            }

            if (pre_right[idx]) {
                r = 1;
                pre_right[idx] = 0;
                set_key_bit(r_key, 0);
            }
        }
    }

    if (j->y != pre_y[idx]) {
        pre_y[idx] = j->y;
        if (pre_y[idx] < UP_TH) {
            if (pre_up[idx] == 0) {
                r = 1;
                pre_up[idx] = 1;
                set_key_bit(u_key, 1);
            }

            if (pre_down[idx]) {
                r = 1;
                pre_down[idx] = 0;
                set_key_bit(d_key, 0);
            }
        }
        else if (pre_y[idx] > DOWN_TH){
            if (pre_down[idx] == 0) {
                r = 1;
                pre_down[idx] = 1;
                set_key_bit(d_key, 1);
            }

            if (pre_up[idx]) {
                r = 1;
                pre_up[idx] = 0;
                set_key_bit(u_key, 0);
            }
        }
        else {
            if (pre_up[idx]) {
                r = 1;
                pre_up[idx] = 0;
                set_key_bit(u_key, 0);
            }
            if (pre_down[idx]) {
                r = 1;
                pre_down[idx] = 0;
                set_key_bit(d_key, 0);
            }
        }
    }

    return r;
}

#if defined(UT)
TEST(sdl2_event, remap_keypad)
{
    jval_t j = { 0 };

    TEST_ASSERT_EQUAL_INT(0, remap_keypad(&j, 0));
}
#endif

static int remap_hotkey_directions(jval_t *j, int idx)
{
    int r = 0;
    static int pre_x[2] = { -1, -1 };
    static int pre_y[2] = { -1, -1 };
    static int pre_up[2] = { 0 };
    static int pre_down[2] = { 0 };
    static int pre_left[2] = { 0 };
    static int pre_right[2] = { 0 };

    uint32_t u_key = KEY_BIT_RSTICK_UP;
    uint32_t d_key = KEY_BIT_RSTICK_DOWN;
    uint32_t l_key = KEY_BIT_RSTICK_LEFT;
    uint32_t r_key = KEY_BIT_RSTICK_RIGHT;

    int UP_TH = -10000;
    int DOWN_TH = 10000;
    int LEFT_TH = -10000;
    int RIGHT_TH = 10000;

    if (j->x != pre_x[idx]) {
        pre_x[idx] = j->x;
        if (pre_x[idx] < LEFT_TH) {
            if (!pre_left[idx]) { r = 1; pre_left[idx] = 1; set_key_bit(l_key, 1); }
            if (pre_right[idx]) { r = 1; pre_right[idx] = 0; set_key_bit(r_key, 0); }
        } else if (pre_x[idx] > RIGHT_TH) {
            if (!pre_right[idx]) { r = 1; pre_right[idx] = 1; set_key_bit(r_key, 1); }
            if (pre_left[idx]) { r = 1; pre_left[idx] = 0; set_key_bit(l_key, 0); }
        } else {
            if (pre_left[idx]) { r = 1; pre_left[idx] = 0; set_key_bit(l_key, 0); }
            if (pre_right[idx]) { r = 1; pre_right[idx] = 0; set_key_bit(r_key, 0); }
        }
    }

    if (j->y != pre_y[idx]) {
        pre_y[idx] = j->y;
        if (pre_y[idx] < UP_TH) {
            if (!pre_up[idx]) { r = 1; pre_up[idx] = 1; set_key_bit(u_key, 1); }
            if (pre_down[idx]) { r = 1; pre_down[idx] = 0; set_key_bit(d_key, 0); }
        } else if (pre_y[idx] > DOWN_TH) {
            if (!pre_down[idx]) { r = 1; pre_down[idx] = 1; set_key_bit(d_key, 1); }
            if (pre_up[idx]) { r = 1; pre_up[idx] = 0; set_key_bit(u_key, 0); }
        } else {
            if (pre_up[idx]) { r = 1; pre_up[idx] = 0; set_key_bit(u_key, 0); }
            if (pre_down[idx]) { r = 1; pre_down[idx] = 0; set_key_bit(d_key, 0); }
        }
    }
    return r;
}

static int remap_touch(jval_t *j, int idx)
{
    int r = 0;
    static int pre_x[2] = { -1, -1 };
    static int pre_y[2] = { -1, -1 };

    static int pre_up[2] = { 0 };
    static int pre_down[2] = { 0 };
    static int pre_left[2] = { 0 };
    static int pre_right[2] = { 0 };

    int UP_TH = -1 * myconfig.joy.dzone;
    int DOWN_TH = myconfig.joy.dzone;
    int LEFT_TH = -1 * myconfig.joy.dzone;
    int RIGHT_TH = myconfig.joy.dzone;

#if defined(R36S)
    int R36S_TH = 500;
    UP_TH = -R36S_TH;
    DOWN_TH = R36S_TH;
    LEFT_TH = -R36S_TH;
    RIGHT_TH = R36S_TH;
#endif

    if (idx) {
#if defined(R36S)
        UP_TH = -R36S_TH;
        DOWN_TH = R36S_TH;
        LEFT_TH = -R36S_TH;
        RIGHT_TH = R36S_TH;
#else
        UP_TH = -1 * myconfig.rjoy.dzone;
        DOWN_TH = myconfig.rjoy.dzone;
        LEFT_TH = -1 * myconfig.rjoy.dzone;
        RIGHT_TH = myconfig.rjoy.dzone;
#endif
    }

    if (j->x != pre_x[idx]) {
        pre_x[idx] = j->x;
        if (pre_x[idx] < LEFT_TH) {
            if (pre_left[idx] == 0) {
                pre_left[idx] = 1;
            }

            if (pre_right[idx]) {
                pre_right[idx] = 0;
            }
        }
        else if (pre_x[idx] > RIGHT_TH){
            if (pre_right[idx] == 0) {
                pre_right[idx] = 1;
            }

            if (pre_left[idx]) {
                pre_left[idx] = 0;
            }
        }
        else {
            if (pre_left[idx]) {
                pre_left[idx] = 0;
            }
            if (pre_right[idx]) {
                pre_right[idx] = 0;
            }
        }
    }

    if (j->y != pre_y[idx]) {
        pre_y[idx] = j->y;
        if (pre_y[idx] < UP_TH) {
            if (pre_up[idx] == 0) {
                pre_up[idx] = 1;
            }

            if (pre_down[idx]) {
                pre_down[idx] = 0;
            }
        }
        else if (pre_y[idx] > DOWN_TH){
            if (pre_down[idx] == 0) {
                pre_down[idx] = 1;
            }

            if (pre_up[idx]) {
                pre_up[idx] = 0;
            }
        }
        else {
            if (pre_up[idx]) {
                pre_up[idx] = 0;
            }
            if (pre_down[idx]) {
                pre_down[idx] = 0;
            }
        }
    }

    if (pre_up[idx] || pre_down[idx] || pre_left[idx] || pre_right[idx]) {
        r = 1;
        if (myevent.keypad.cur_bits &  (1 << KEY_BIT_Y)) {
            if (pre_right[idx]) {
                static int cc = 0;

                if (cc == 0) {
                    myconfig.pen.sel+= 1;
                    if (myconfig.pen.sel >= myconfig.pen.max) {
                        myconfig.pen.sel = 0;
                    }
                    load_touch_pen();
                    cc = 30;
                }
                else {
                    cc -= 1;
                }
            }
        }
        else {
            int x = 0;
            int y = 0;
            int dx = 0;
            int dy = 0;
            float move_x = 0;
            float move_y = 0;

            // Calculate displacement from threshold
            if (j->x < LEFT_TH) dx = j->x - LEFT_TH;
            else if (j->x > RIGHT_TH) dx = j->x - RIGHT_TH;

            if (j->y < UP_TH) dy = j->y - UP_TH;
            else if (j->y > DOWN_TH) dy = j->y - DOWN_TH;

            // Scale movement (ArkOS analog values range roughly +/- 32767)
            // Displacement is up to ~32000. 
            // Scale by 1000 to get a responsive range, ensuring at least 1px move if pushed.
            if (dx > 0) move_x = (float)dx / 1000.0f + 1.0f;
            else if (dx < 0) move_x = (float)dx / 1000.0f - 1.0f;

            if (dy > 0) move_y = (float)dy / 1000.0f + 1.0f;
            else if (dy < 0) move_y = (float)dy / 1000.0f - 1.0f;

            if (is_book_mode() && (myconfig.key_rotate == 0)) {
                myevent.touch.x += (int)move_y;
                myevent.touch.y -= (int)move_x;
            }
            else {
                myevent.touch.x += (int)move_x;
                myevent.touch.y += (int)move_y;
            }
            limit_touch_axis();

            x = (myevent.touch.x * 160) / myevent.touch.max_x;
            y = (myevent.touch.y * 120) / myevent.touch.max_y;
            if (myvideo.win) {
                SDL_SendMouseMotion(myvideo.win, 0, 0, x + 80, y + (*myhook.var.sdl.swap_screens ? 120 : 0));
            }
        }
        myconfig.joy.show_cnt = MYJOY_SHOW_CNT;
    }

    return r;
}

#if defined(UT)
TEST(sdl2_event, remap_touch)
{
    jval_t j = { 0 };

    TEST_ASSERT_EQUAL_INT(0, remap_touch(&j, 0));
}
#endif

static int remap_custkey(jval_t *j, int idx)
{
    int r = 0;
    static int pre_x[2] = { -1, -1 };
    static int pre_y[2] = { -1, -1 };

    static int pre_up[2] = { 0 };
    static int pre_down[2] = { 0 };
    static int pre_left[2] = { 0 };
    static int pre_right[2] = { 0 };

    uint32_t u_key = myconfig.joy.cust_key[0];
    uint32_t d_key = myconfig.joy.cust_key[1];
    uint32_t l_key = myconfig.joy.cust_key[2];
    uint32_t r_key = myconfig.joy.cust_key[3];

    int UP_TH = -1 * myconfig.joy.dzone;
    int DOWN_TH = myconfig.joy.dzone;
    int LEFT_TH = -1 * myconfig.joy.dzone;
    int RIGHT_TH = myconfig.joy.dzone;

    trace_throttled("call %s(joy=%p, idx=%d)\n", __func__, j, idx);

    if (idx) {
        UP_TH = -1 * myconfig.rjoy.dzone;
        DOWN_TH = myconfig.rjoy.dzone;
        LEFT_TH = -1 * myconfig.rjoy.dzone;
        RIGHT_TH = myconfig.rjoy.dzone;

        u_key = myconfig.rjoy.cust_key[0];
        d_key = myconfig.rjoy.cust_key[1];
        l_key = myconfig.rjoy.cust_key[2];
        r_key = myconfig.rjoy.cust_key[3];
    }

    if (j->x != pre_x[idx]) {
        pre_x[idx] = j->x;
        if (pre_x[idx] < LEFT_TH) {
            if (pre_left[idx] == 0) {
                r = 1;
                pre_left[idx] = 1;
                set_key_bit(l_key, 1);
            }

            if (pre_right[idx]) {
                r = 1;
                pre_right[idx] = 0;
                set_key_bit(r_key, 0);
            }
        }
        else if (pre_x[idx] > RIGHT_TH){
            if (pre_right[idx] == 0) {
                r = 1;
                pre_right[idx] = 1;
                set_key_bit(r_key, 1);
            }

            if (pre_left[idx]) {
                r = 1;
                pre_left[idx] = 0;
                set_key_bit(l_key, 0);
            }
        }
        else {
            if (pre_left[idx]) {
                r = 1;
                pre_left[idx] = 0;
                set_key_bit(l_key, 0);
            }

            if (pre_right[idx]) {
                r = 1;
                pre_right[idx] = 0;
                set_key_bit(r_key, 0);
            }
        }
    }

    if (j->y != pre_y[idx]) {
        pre_y[idx] = j->y;
        if (pre_y[idx] < UP_TH) {
            if (pre_up[idx] == 0) {
                r = 1;
                pre_up[idx] = 1;
                set_key_bit(u_key, 1);
            }

            if (pre_down[idx]) {
                r = 1;
                pre_down[idx] = 0;
                set_key_bit(d_key, 0);
            }
        }
        else if (pre_y[idx] > DOWN_TH){
            if (pre_down[idx] == 0) {
                r = 1;
                pre_down[idx] = 1;
                set_key_bit(d_key, 1);
            }

            if (pre_up[idx]) {
                r = 1;
                pre_up[idx] = 0;
                set_key_bit(u_key, 0);
            }
        }
        else {
            if (pre_up[idx]) {
                r = 1;
                pre_up[idx] = 0;
                set_key_bit(u_key, 0);
            }

            if (pre_down[idx]) {
                r = 1;
                pre_down[idx] = 0;
                set_key_bit(d_key, 0);
            }
        }
    }

    return r;
}

#if defined(UT)
TEST(sdl2_event, remap_custkey)
{
    jval_t j = { 0 };

    TEST_ASSERT_EQUAL_INT(0, remap_custkey(&j, 0));
}
#endif

static int update_joy_state(void)
{
    int r = 0;
    int l3_is_held = 0;
    static int l3_was_held = 0;

    if (myconfig.joy.mode == MYJOY_MODE_KEY) {
        r |= remap_keypad(&myjoy.left.last, 0);
    }
    else if (myconfig.joy.mode == MYJOY_MODE_TOUCH) {
        r |= remap_touch(&myjoy.left.last, 0);
    }
    else if (myconfig.joy.mode == MYJOY_MODE_CUST_KEY) {
        r |= remap_custkey(&myjoy.left.last, 0);
    }

#if defined(MIYOO_FLIP) || defined(R36S)
    l3_is_held = (myevent.keypad.cur_bits & (1 << KEY_BIT_L3));
    
    if (l3_is_held) {
        // While L3 is held, Right Stick acts as discrete hotkey directions
        r |= remap_hotkey_directions(&myjoy.right.last, 1);
        l3_was_held = 1;
    } else {
        if (l3_was_held) {
            // Clean up: Reset Right Stick bits when L3 is released
            set_key_bit(KEY_BIT_RSTICK_UP, 0);
            set_key_bit(KEY_BIT_RSTICK_DOWN, 0);
            set_key_bit(KEY_BIT_RSTICK_LEFT, 0);
            set_key_bit(KEY_BIT_RSTICK_RIGHT, 0);
            l3_was_held = 0;
            r = 1;
        }
        
        if (myconfig.rjoy.mode == MYJOY_MODE_KEY) {
            r |= remap_keypad(&myjoy.right.last, 1);
        }
        else if (myconfig.rjoy.mode == MYJOY_MODE_TOUCH) {
            r |= remap_touch(&myjoy.right.last, 1);
        }
        else if (myconfig.rjoy.mode == MYJOY_MODE_CUST_KEY) {
            r |= remap_custkey(&myjoy.right.last, 1);
        }
    }
#endif

    return r;
}
#endif

#if defined(UT)
TEST(sdl2_event, update_joy_state)
{
    TEST_ASSERT_EQUAL_INT(0, update_joy_state());
}
#endif

static int enter_sdl2_menu(sdl2_menu_type_t t)
{
    trace_throttled("call %s(t=%d)\n", __func__, t);

    if (myvideo.menu.sdl2.enable == 0) {
        myvideo.menu.sdl2.type = t;
        myvideo.menu.sdl2.enable = 1;
        usleep(100000);

#if !defined(UT)
        handle_sdl2_menu(-1);
#endif

        myevent.keypad.pre_bits = myevent.keypad.cur_bits = 0;
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, enter_sdl2_menu)
{
    myevent.keypad.pre_bits = 1;
    myevent.keypad.cur_bits = 1;
    myvideo.menu.sdl2.enable = 0;

    TEST_ASSERT_EQUAL_INT(0, enter_sdl2_menu(0));
    TEST_ASSERT_EQUAL_INT(1, myvideo.menu.sdl2.enable);
    TEST_ASSERT_EQUAL_INT(0, myevent.keypad.pre_bits);
    TEST_ASSERT_EQUAL_INT(0, myevent.keypad.cur_bits);
}
#endif

static int find_next_available_bg(void)
{
    int cc = 0;
    int mode = myconfig.layout.mode.sel;
    int next = (myconfig.layout.bg.sel + 1) % MAX_LAYOUT_BG_FILE;

    trace_throttled("call %s()\n", __func__);

    trace_throttled("next=%d\n", next);
    for (cc = next; cc < MAX_LAYOUT_BG_FILE; cc++) {
        if (myvideo.layout.mode[mode].bg[cc].path[0]) {
            trace_throttled("next available=%d\n", cc);
            return cc;
        }
    }

    trace_throttled("used black bg\n");
    return MAX_LAYOUT_BG_FILE - 1;
}

#if defined(UT)
TEST(sdl2_event, find_next_available_bg)
{
    TEST_ASSERT_EQUAL_INT(MAX_LAYOUT_BG_FILE - 1, find_next_available_bg());
}
#endif

static int hit_hotkey(uint32_t bit)
{
    uint32_t l3_bit = (1 << KEY_BIT_L3);
    uint32_t target_bit = (1 << bit);
    return ((myevent.keypad.cur_bits & l3_bit) && (myevent.keypad.cur_bits & target_bit));
}

static int handle_hotkey(void)
{
    int check_hotkey = 0;
    static int l2_pressed = 0;

#if defined(R36S)
    // L2 PiP Toggle (Single press)
    if (myevent.keypad.cur_bits & (1 << KEY_BIT_L2)) {
        if (!l2_pressed) {
            l2_pressed = 1;
            myconfig.layout.mode.sel = (myconfig.layout.mode.sel == LAYOUT_MODE_N0) ? LAYOUT_MODE_N1 : LAYOUT_MODE_N0;
            trace_throttled("Toggle PiP via L2: mode=%d\n", myconfig.layout.mode.sel);
        }
    } else {
        l2_pressed = 0;
    }

    // Restore Fn/Menu button logic
    if (myevent.keypad.cur_bits & (1 << KEY_BIT_MENU)) {
        set_key_bit(KEY_BIT_DRASTIC, 1);
        set_key_bit(KEY_BIT_MENU, 0);
    }
#endif

    // Chorded hotkeys disabled for now.
    // Preserved in hotkey-preserved.md for future reintegration.

    return 0;
}

#if defined(UT)
TEST(sdl2_event, handle_hotkey)
{
    TEST_ASSERT_EQUAL_INT(0, handle_hotkey());
}
#endif

static int update_key_bit(uint32_t c, uint32_t v)
{
    if (c == myevent.keypad.up) {
        trace_throttled("set KEY_BIT_UP\n");
        set_key_bit(KEY_BIT_UP, v);
    }
    if (c == myevent.keypad.down) {
        trace_throttled("set KEY_BIT_DOWN\n");
        set_key_bit(KEY_BIT_DOWN, v);
    }
    if (c == myevent.keypad.left) {
        trace_throttled("set KEY_BIT_LEFT\n");
        set_key_bit(KEY_BIT_LEFT, v);
    }
    if (c == myevent.keypad.right) {
        trace_throttled("set KEY_BIT_RIGHT\n");
        set_key_bit(KEY_BIT_RIGHT, v);
    }
    if (c == myevent.keypad.a) {
        trace_throttled("set KEY_BIT_A\n");
        set_key_bit(KEY_BIT_A, v);
    }
    if (c == myevent.keypad.b) {
        trace_throttled("set KEY_BIT_B\n");
        set_key_bit(KEY_BIT_B, v);
    }
    if (c == myevent.keypad.x) {
        trace_throttled("set KEY_BIT_X\n");
        set_key_bit(KEY_BIT_X, v);
    }
    if (c == myevent.keypad.y) {
        trace_throttled("set KEY_BIT_Y\n");
        set_key_bit(KEY_BIT_Y, v);
    }
    if (c == myevent.keypad.l1) {
        trace_throttled("set KEY_BIT_L1\n");
        set_key_bit(KEY_BIT_L1, v);
    }
    if (c == myevent.keypad.r1) {
        trace_throttled("set KEY_BIT_R1\n");
        set_key_bit(KEY_BIT_R1, v);
    }
    if (c == myevent.keypad.l2) {
#if defined(MIYOO_FLIP)
        if (myconfig.joy.mode == MYJOY_MODE_TOUCH) {
            myconfig.joy.show_cnt = MYJOY_SHOW_CNT;
            myevent.input.touch_status = !!v;
            SDL_SendMouseButton(myvideo.win, 0, v ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
        }
#endif
        trace_throttled("set KEY_BIT_L2\n");
        set_key_bit(KEY_BIT_L2, v);
    }
    if (c == myevent.keypad.r2) {
        trace_throttled("set KEY_BIT_R2\n");
        set_key_bit(KEY_BIT_R2, v);
    }
    if (c == myevent.keypad.select) {
        trace_throttled("set KEY_BIT_SELECT\n");
        set_key_bit(KEY_BIT_SELECT, v);
    }
    if (c == myevent.keypad.start) {
        trace_throttled("set KEY_BIT_START\n");
        set_key_bit(KEY_BIT_START, v);
    }
    if (c == myevent.keypad.menu) {
        trace_throttled("set KEY_BIT_MENU\n");
        set_key_bit(KEY_BIT_MENU, v);
    }
    if (c == myevent.keypad.r3) {
#if defined(R36S)
        myevent.input.touch_status = !!v;
        if (myvideo.win) {
            SDL_SendMouseButton(myvideo.win, 0, v ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
        }
#endif
        trace_throttled("set KEY_BIT_R3\n");
        set_key_bit(KEY_BIT_R3, v);
    }
    if (c == myevent.keypad.save) {
        trace_throttled("set KEY_BIT_SAVE\n");
        set_key_bit(KEY_BIT_SAVE, v);
    }
    if (c == myevent.keypad.load) {
        trace_throttled("set KEY_BIT_LOAD\n");
        set_key_bit(KEY_BIT_LOAD, v);
    }
    if (c == myevent.keypad.fast) {
        trace_throttled("set KEY_BIT_FAST\n");
        set_key_bit(KEY_BIT_FAST, v);
    }
    if (c == myevent.keypad.exit) {
        trace_throttled("set KEY_BIT_QUIT\n");
        set_key_bit(KEY_BIT_QUIT, v);
    }
    if (c == myevent.keypad.l3) {
        trace_throttled("set KEY_BIT_L3\n");
        set_key_bit(KEY_BIT_L3, v);
    }

#if defined(MIYOO_MINI) || defined(MOTO_XT894) || defined(MOTO_XT897) || defined(UT)
    if (c == myevent.keypad.power) {
        trace_throttled("set KEY_BIT_QUIT\n");
        set_key_bit(KEY_BIT_QUIT, v);
    }
#endif

    return 0;
}

#if defined(UT)
TEST(sdl2_event, update_key_bit)
{
    myevent.keypad.cur_bits = 0;
    myevent.keypad.up = DEV_KEY_CODE_UP;
    TEST_ASSERT_EQUAL_INT(0, update_key_bit(DEV_KEY_CODE_UP, 1));
    TEST_ASSERT_EQUAL_INT((1 << KEY_BIT_UP), myevent.keypad.cur_bits);
    TEST_ASSERT_EQUAL_INT(0, update_key_bit(DEV_KEY_CODE_UP, 0));
    TEST_ASSERT_EQUAL_INT((0 << KEY_BIT_UP), myevent.keypad.cur_bits);
}
#endif

#if defined(MIYOO_FLIP) || defined(UT)
static int get_flip_key_code(struct input_event *e)
{
    static uint32_t pre_bits = 0;

    int r = 0;
    int cc = 0;
    int buf[DEV_KEY_BUF_MAX] = { 0 };
    const int kval[DEV_KEY_BUF_MAX] = {
        /* 0  */ DEV_KEY_CODE_B,
        /* 1  */ DEV_KEY_CODE_Y,
        /* 2  */ DEV_KEY_CODE_SELECT,
        /* 3  */ DEV_KEY_CODE_START,
        /* 4  */ DEV_KEY_CODE_UP,
        /* 5  */ DEV_KEY_CODE_DOWN,
        /* 6  */ DEV_KEY_CODE_LEFT,
        /* 7  */ DEV_KEY_CODE_RIGHT,
        /* 8  */ DEV_KEY_CODE_A,
        /* 9  */ DEV_KEY_CODE_X,
        /* 10 */ DEV_KEY_CODE_L1,
        /* 11 */ DEV_KEY_CODE_R1,
        /* 12 */ DEV_KEY_CODE_L2,
        /* 13 */ DEV_KEY_CODE_R2,
        /* 14 */ -1, 
        /* 15 */ -1, 
        /* 16 */ DEV_KEY_CODE_MENU,
        /* 17 */ -1,
        /* 18 */ -1,
        /* 19 */ -1
    }; 

    trace_throttled("call %s(e=%p)\n", __func__, e);

    if (myevent.fd < 0) {
        error("invalid input handle\n");
        return r;
    }

    if (!e) {
        error("e is null\n");
        return -1;
    }

#if !defined(UT)
    if (read(myevent.fd, buf, sizeof(buf)) == 0) {
        return r;
    }
#endif

    for (cc = 0; cc < DEV_KEY_IDX_MAX; cc++) {
        if (kval[cc] < 0) {
            continue;
        }

        if ((!!buf[cc]) == (!!(pre_bits & (1 << cc)))) {
            continue;
        }

        r = 1;
        if (!!buf[cc]) {
            pre_bits |= (1 << cc);
        }
        else {
            pre_bits &= ~(1 << cc);
        }
        update_key_bit(kval[cc], !!buf[cc]);
    }

    e->code = 0;
    e->value = 0;
    return r;
}
#endif

#if defined(UT)
TEST(sdl2_event, get_flip_key_code)
{
    struct input_event e = {{ 0 }};

    TEST_ASSERT_EQUAL_INT(0, get_flip_key_code(&e));
}
#endif

#if defined(TRIMUI_BRICK) || defined(UT)
static int get_brick_key_code(struct input_event *e)
{
    int r = 0;
    static uint32_t pre_up_down = 0;
    static uint32_t pre_left_right = 0;

    trace_throttled("call %s(e=%p)\n", __func__, e);

    if (myevent.fd < 0) {
        error("invalid input handle\n");
        return -1;
    }

    if (!e) {
        error("e is null\n");
        return -1;
    }

#if !defined(UT)
    if (read(myevent.fd, e, sizeof(struct input_event)) == 0) {
        return 0;
    }
#endif

    r = 1;
    switch (e->code) {
    case 17:
        if (e->value == -1) {
            e->code = DEV_KEY_CODE_UP;
            e->value = 1;
            pre_up_down = DEV_KEY_CODE_UP;
        }
        else if (e->value == 1) {
            e->code = DEV_KEY_CODE_DOWN;
            e->value = 1;
            pre_up_down = DEV_KEY_CODE_DOWN;
        }
        else {
            e->code = pre_up_down;
            e->value = 0;
        }
        break;
    case 16:
        if (e->value == -1) {
            e->code = DEV_KEY_CODE_LEFT;
            e->value = 1;
            pre_left_right = DEV_KEY_CODE_LEFT;
        }
        else if (e->value == 1) {
            e->code = DEV_KEY_CODE_RIGHT;
            e->value = 1;
            pre_left_right = DEV_KEY_CODE_RIGHT;
        }
        else {
            e->code = pre_left_right;
            e->value = 0;
        }
        break;
    case 2:
        e->code = DEV_KEY_CODE_L2;
        e->value = !!e->value;
        break;
    case 5:
        e->code = DEV_KEY_CODE_R2;
        e->value = !!e->value;
        break;
    }

    return r;
}
#endif

#if defined(UT)
TEST(sdl2_event, get_brick_key_code)
{
    struct input_event e = { 0 };

    e.code = 17;
    e.value = 1;
    TEST_ASSERT_EQUAL_INT(1, get_brick_key_code(&e));
    TEST_ASSERT_EQUAL_INT(DEV_KEY_CODE_DOWN, e.code);
}
#endif

#if defined(PANDORA) || defined(UT)
static int get_pandora_key_code(struct input_event *e)
{
    trace_throttled("call %s(e=%p)\n", __func__, e);

    if ((myevent.fd < 0) || (myevent.kb_fd < 0)) {
        error("invalid input handle\n");
        return -1;
    }

    if (!e) {
        error("e is null\n");
        return -1;
    }

#if !defined(UT)
    if (read(myevent.fd, e, sizeof(struct input_event))) {
        if ((e->type == EV_KEY) && (e->value != 2)) {
            return 1;
        }
    }

    if (read(myevent.kb_fd, e, sizeof(struct input_event))) {
        if ((e->type == EV_KEY) && (e->value != 2)) {
            return 1;
        }
    }
#endif

    return 0;
}

#if defined(UT)
TEST(sdl2_event, get_pandora_key_code)
{
    struct input_event e = {{ 0 }};

    TEST_ASSERT_EQUAL_INT(0, get_pandora_key_code(&e));
}
#endif
#endif

static int get_input_key_code(int fd, struct input_event *e)
{

    if (fd < 0) {
        error("invalid handle\n");
        return -1;
    }

    if (!e) {
        error("invalid parameter\n");
        return -1;
    }

#if defined(UT)
    return 0;
#endif

    if (read(fd, e, sizeof(struct input_event)) > 0) {
        if ((e->type == EV_KEY) && (e->value != 2)) {
            trace_throttled("got input event\n");
            return 1;
        }
        if (e->type == EV_ABS) {
#if defined(R36S)
            if (e->code == 0) { // Left Stick X
                myjoy.left.last.x = e->value;
                return 1;
            }
            if (e->code == 1) { // Left Stick Y
                myjoy.left.last.y = e->value;
                return 1;
            }
            if (e->code == 3) { // Right Stick X
                myjoy.right.last.x = e->value;
                return 1;
            }
            if (e->code == 4) { // Right Stick Y
                myjoy.right.last.y = e->value;
                return 1;
            }
#endif
            if (e->code == 16) { // Hat 0X
                e->type = EV_KEY;
                if (e->value == -1) e->code = 546; // Left
                else if (e->value == 1) e->code = 547; // Right
                else { e->value = 0; e->code = 546; } // Release (code doesn't matter much for 0, but keep consistent)
                return 1;
            }
            if (e->code == 17) { // Hat 0Y
                e->type = EV_KEY;
                if (e->value == -1) e->code = 544; // Up
                else if (e->value == 1) e->code = 545; // Down
                else { e->value = 0; e->code = 544; } // Release
                return 1;
            }
        }
        return 1; // Read something, keep draining
    }

    // trace_throttled("ignore input event\n");
    return 0;
}

#if defined(UT)
TEST(sdl2_event, get_input_key_code)
{
    TEST_ASSERT_EQUAL_INT(-1, get_input_key_code(-1, NULL));
    TEST_ASSERT_EQUAL_INT(0, get_input_key_code(0xdead, (void *)0xdead));
}
#endif

static int update_latest_keypad_value(void)
{
    if (myconfig.key_rotate == 1) {
        myevent.keypad.up = DEV_KEY_CODE_LEFT;
        myevent.keypad.down = DEV_KEY_CODE_RIGHT;
        myevent.keypad.left = DEV_KEY_CODE_DOWN;
        myevent.keypad.right = DEV_KEY_CODE_UP;

        myevent.keypad.a = DEV_KEY_CODE_X;
        myevent.keypad.b = DEV_KEY_CODE_A;
        myevent.keypad.x = DEV_KEY_CODE_Y;
        myevent.keypad.y = DEV_KEY_CODE_B;
    }
    else {
        myevent.keypad.up = DEV_KEY_CODE_UP;
        myevent.keypad.down = DEV_KEY_CODE_DOWN;
        myevent.keypad.left = DEV_KEY_CODE_LEFT;
        myevent.keypad.right = DEV_KEY_CODE_RIGHT;

        myevent.keypad.a = DEV_KEY_CODE_A;
        myevent.keypad.b = DEV_KEY_CODE_B;
        myevent.keypad.x = DEV_KEY_CODE_X;
        myevent.keypad.y = DEV_KEY_CODE_Y;
    }

    if (myconfig.swap_l1_l2) {
        myevent.keypad.l1 = DEV_KEY_CODE_L2;
        myevent.keypad.l2 = DEV_KEY_CODE_L1;
    }
    else {
        myevent.keypad.l1 = DEV_KEY_CODE_L1;
        myevent.keypad.l2 = DEV_KEY_CODE_L2;
    }

    if (myconfig.swap_r1_r2) {
        myevent.keypad.r1 = DEV_KEY_CODE_R2;
        myevent.keypad.r2 = DEV_KEY_CODE_R1;
    }
    else {
        myevent.keypad.r1 = DEV_KEY_CODE_R1;
        myevent.keypad.r2 = DEV_KEY_CODE_R2;
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, update_latest_keypad_value)
{
    myevent.keypad.up = 0;
    TEST_ASSERT_EQUAL_INT(0, update_latest_keypad_value());
    TEST_ASSERT_EQUAL_INT(DEV_KEY_CODE_UP, myevent.keypad.up);

    myconfig.swap_l1_l2 = 1;
    TEST_ASSERT_EQUAL_INT(0, update_latest_keypad_value());
    TEST_ASSERT_EQUAL_INT(DEV_KEY_CODE_L2, myevent.keypad.l1);

    myconfig.key_rotate = 1;
    myvideo.menu.sdl2.enable = 0;
    myvideo.menu.drastic.enable = 0;
    TEST_ASSERT_EQUAL_INT(0, update_latest_keypad_value());
    TEST_ASSERT_EQUAL_INT(DEV_KEY_CODE_LEFT, myevent.keypad.up);
}
#endif

#if defined(TRIMUI_SMART) || defined(UT)
static int handle_trimui_special_key(void)
{
    int r = 0;
    static uint32_t pre_value = 0;

    trace_throttled("call %s()\n", __func__);

    if (myevent.cust_key.gpio != NULL) {
        uint32_t v = *myevent.cust_key.gpio & 0x800;

        if (v != pre_value) {
            r = 1;
            pre_value = v;
            trace_throttled("set r2=%d\n", !v);
            set_key_bit(KEY_BIT_R2, !v);
        }
    }

    return r;
}
#endif

#if defined(UT)
TEST(sdl2_event, handle_trimui_special_key)
{
    uint32_t t = 0;

    myevent.cust_key.gpio = NULL;
    TEST_ASSERT_EQUAL_INT(0, handle_trimui_special_key());

    t = 0x800;
    myevent.cust_key.gpio = &t;
    myevent.keypad.cur_bits = 0;
    TEST_ASSERT_EQUAL_INT(1, handle_trimui_special_key());
    TEST_ASSERT_EQUAL_INT((0 << KEY_BIT_R2), myevent.keypad.cur_bits);

    t = 0;
    myevent.cust_key.gpio = &t;
    myevent.keypad.cur_bits = 0;
    TEST_ASSERT_EQUAL_INT(1, handle_trimui_special_key());
    TEST_ASSERT_EQUAL_INT((1 << KEY_BIT_R2), myevent.keypad.cur_bits);

    t = 0;
    myevent.cust_key.gpio = &t;
    myevent.keypad.cur_bits = 0;
    TEST_ASSERT_EQUAL_INT(0, handle_trimui_special_key());
    TEST_ASSERT_EQUAL_INT(0, myevent.keypad.cur_bits);
}
#endif

static int send_touch_axis(void)
{
#if !defined(UT)
    int x = 0;
    int y = 0;

#endif

    trace_throttled("call %s()\n", __func__);

#if !defined(UT)
    x = (myevent.touch.x * 160) / myevent.touch.max_x;
    y = (myevent.touch.y * 120) / myevent.touch.max_y;

    if (myvideo.win) {
        SDL_SendMouseMotion(myvideo.win, 0, 0, x + 80, y + (*myhook.var.sdl.swap_screens ? 120 : 0));
    }
#endif

    return 0;
}

#if defined(UT)
TEST(sdl2_event, send_touch_axis)
{
    TEST_ASSERT_EQUAL_INT(0, send_touch_axis());
}
#endif

#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(FXTEC_QX1000) || defined(FXTEC_QX1050) || defined(UT)
int handle_touch_event(int fd)
{
    static int tp_id = 0;
    static int tp_valid = 0;
    struct input_event ev = { 0 };

    const int screen_w = WL_WIN_H;
    const int screen_h = WL_WIN_W;

#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(UT)
    float tp_max_x = 1000.0;
    float tp_max_y = 1000.0;
#endif

#if defined(FXTEC_QX1000)
    float tp_max_x = 2160.0;
    float tp_max_y = 1080.0;
#endif

    trace_throttled("call %s(fd=%d)\n", __func__, fd);

    if (fd < 0) {
        error("invalid parameter\n");
        return -1;
    }

    if (read(fd, &ev, sizeof(struct input_event)) <= 0) {
        return 0;
    }

    trace_throttled("touch, type:%d, code:0x%x, value:%d\n", ev.type, ev.code, ev.value);
    if (ev.type == EV_ABS) {
        if (ev.code == ABS_MT_TRACKING_ID) {
#if defined(MOTO_XT894) || defined(MOTO_XT897)
            tp_valid = 1;
            tp_id = ev.value;
#endif

#if defined(FXTEC_QX1000)
            if (ev.value >= 0) {
                tp_valid = 1;
                tp_id = 0;
            }
            else {
                tp_valid = 0;
            }
#endif
        }
        else if (ev.code == ABS_MT_POSITION_X) {
            tp_valid = 1;
            tp[tp_id].y = screen_h - (((float)ev.value / tp_max_y) * screen_h);
        }
        else if (ev.code == ABS_MT_POSITION_Y) {
            tp_valid = 1;
            tp[tp_id].x = ((float)ev.value / tp_max_x) * screen_w;
        }
        else if (ev.code == ABS_MT_PRESSURE) {
            tp_valid = 1;
            tp[tp_id].pressure = ev.value;
        }
    }
    else if (ev.type == EV_SYN) {
#if defined(MOTO_XT894) || defined(MOTO_XT897)
        if ((ev.code == ABS_Z) && (ev.value == 0)) {
#endif

#if defined(FXTEC_QX1000) || defined(FXTEC_QX1050)
        if ((ev.code == 0) && (ev.value == 0)) {
#endif
            if (tp_valid) {
                int x = 0;
                int y = 0;
                int lcd = 0;
                int update = 0;

                tp_valid = 0;
                trace_throttled(
                    "touch id=%d, x=%d, y=%d, pressure=%d\n",
                    tp_id,
                    tp[tp_id].x,
                    tp[tp_id].y,
                    tp[tp_id].pressure
                );

                switch (myconfig.layout.mode.sel) {
                case LAYOUT_MODE_N0:
                case LAYOUT_MODE_N1:
                case LAYOUT_MODE_N2:
                case LAYOUT_MODE_N3:
                case LAYOUT_MODE_N4:
                    if (*myhook.var.sdl.swap_screens != 0) {
                        break;
                    }

                    lcd = 1;
                    update = 1;
                    break;
                default:
                    update = 1;
                    lcd = !(*myhook.var.sdl.swap_screens);
                    break;
                }

                if (update) {

                    switch (myconfig.layout.mode.sel) {
                    case LAYOUT_MODE_B0:
                    case LAYOUT_MODE_B2:
                        x = tp[tp_id].x -
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].x;
                        x = ((float)x /
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].h) * NDS_H;

                        y = tp[tp_id].y -
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].y;
                        y = ((float)y /
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].w) * NDS_W;

                        myevent.touch.x = (NDS_H - y) + 60;
                        myevent.touch.y = x;
                        break;
                    case LAYOUT_MODE_B1:
                    case LAYOUT_MODE_B3:
                        x = tp[tp_id].x -
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].x;
                        x = ((float)x /
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].h) * NDS_H;

                        y = tp[tp_id].y -
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].y;
                        y = ((float)y /
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].w) * NDS_W;

                        myevent.touch.x = y;
                        myevent.touch.y = (NDS_W - x) - 60;
                        break;
                    default:
                        x = tp[tp_id].x -
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].x;
                        x = ((float)x /
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].w) * NDS_W;

                        y = tp[tp_id].y -
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].y;
                        y = ((float)y /
                            myvideo.layout.mode[myconfig.layout.mode.sel].screen[lcd].h) * NDS_H;

                        myevent.touch.x = x;
                        myevent.touch.y = y;
                        break;
                    }
                    myevent.input.touch_status = tp[tp_id].pressure * 100;
                    limit_touch_axis();

                    trace_throttled("send touch event, x=%d, y=%d, pressure=%d\n",
                        myevent.touch.x,
                        myevent.touch.y,
                        myevent.input.touch_status
                    );
                }
            }
            else {
                myevent.input.touch_status = 0;
            }
#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(FXTEC_QX1000) || defined(FXTEC_QX1050)
        }
#endif
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, handle_touch_event)
{
}
#endif
#endif

int input_handler(void *data)
{
    int rk = 0;
    int rj = 0;
    struct input_event ev = {{ 0 }};

    trace_throttled("call %s()\n", __func__);

#if !defined(UT)
    myevent.fd = open(INPUT_DEV, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (myevent.fd < 0) {
        error("failed to open \"%s\"\n", INPUT_DEV);
        exit(-1);
    }
#endif

#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(FXTEC_QX1000)
#if defined(MOTO_XT894) || defined(MOTO_XT897)
    sleep_us = 100;
#else
    sleep_us = 1000;
#endif
    myevent.tp_fd = open(TOUCH_DEV, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    myevent.pwr_fd = open(POWER_DEV, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
#endif

#if defined(PANDORA)
    myevent.kb_fd = open(KEYPAD_DEV, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (myevent.kb_fd < 0) {
        error("failed to open \"%s\"\n", KEYPAD_DEV);
        exit(-1);
    }
#endif

#if defined(UT)
    myevent.thread.running = 0;
#else
    myevent.thread.running = 1;

    myevent.sem = SDL_CreateSemaphore(1);
    if(myevent.sem == NULL) {
        error("failed to create semaphore\n");
        exit(-1);
    }
#endif

    while (myevent.thread.running) {
        update_latest_keypad_value();

        SDL_SemWait(myevent.sem);

        rk = 0;
        rj = 0;

        while (get_input_key_code(myevent.fd, &ev) > 0) {
            update_key_bit(ev.code, ev.value);
            rk = 1;
        }

#if defined(MIYOO_FLIP) || defined(R36S)
        rj = update_joy_state();
#endif

        if ((rk > 0) || (rj > 0)) {
            handle_hotkey();
        }

#if defined(TRIMUI_SMART) || defined(UT)
        handle_trimui_special_key();
#endif

#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(FXTEC_QX1000)
        while (get_input_key_code(myevent.pwr_fd, &ev) > 0) {
            update_key_bit(ev.code, ev.value);
        }

        handle_touch_event(myevent.tp_fd);
#endif

        SDL_SemPost(myevent.sem);

        usleep(2000);
    }
    
    return 0;
}

#if defined(UT)
TEST(sdl2_event, input_handler)
{
    TEST_ASSERT_EQUAL_INT(0, input_handler(NULL));
}
#endif

void init_event(void)
{
    trace_throttled("call %s()\n", __func__);

    memset(&myevent, 0, sizeof(myevent));

#if defined(R36S)
    myevent.mode = NDS_TOUCH_MODE;
#else
    myevent.mode = NDS_KEY_MODE;
#endif
    myevent.touch.max_x = NDS_W;
    myevent.touch.max_y = NDS_H;
    myevent.touch.x = myevent.touch.max_x >> 1;
    myevent.touch.y = myevent.touch.max_y >> 1;

    myevent.keypad.up = DEV_KEY_CODE_UP;
    myevent.keypad.down = DEV_KEY_CODE_DOWN;
    myevent.keypad.left = DEV_KEY_CODE_LEFT;
    myevent.keypad.right = DEV_KEY_CODE_RIGHT;
    myevent.keypad.a = DEV_KEY_CODE_A;
    myevent.keypad.b = DEV_KEY_CODE_B;
    myevent.keypad.x = DEV_KEY_CODE_X;
    myevent.keypad.y = DEV_KEY_CODE_Y;
    myevent.keypad.l1 = DEV_KEY_CODE_L1;
    myevent.keypad.r1 = DEV_KEY_CODE_R1;
    myevent.keypad.l2 = DEV_KEY_CODE_L2;
    myevent.keypad.r2 = DEV_KEY_CODE_R2;
    myevent.keypad.select = DEV_KEY_CODE_SELECT;
    myevent.keypad.start = DEV_KEY_CODE_START;
    myevent.keypad.menu = DEV_KEY_CODE_MENU;
    myevent.keypad.power = DEV_KEY_CODE_POWER;
#if defined(R36S)
    myevent.keypad.save = -1;
    myevent.keypad.r3 = DEV_KEY_CODE_R3;
    myevent.keypad.l3 = DEV_KEY_CODE_L3;
#else
    myevent.keypad.save = -1;
    myevent.keypad.l3 = -1;
    myevent.keypad.r3 = -1;
#endif
    myevent.keypad.load = -1;
    myevent.keypad.fast = -1;
    myevent.keypad.exit = -1;

#if defined(R36S) || defined(FXTEC_QX1050) || defined(FXTEC_QX1000) || defined(MOTO_XT894) || defined(MOTO_XT897) || defined(TRIMUI_BRICK) || defined(GKD_MINIPLUS) || defined(UT)
    myevent.keypad.save = DEV_KEY_CODE_SAVE;
    myevent.keypad.load = DEV_KEY_CODE_LOAD;
#if defined(R36S) || defined(FXTEC_QX1050) || defined(FXTEC_QX1000) || defined(MOTO_XT894) || defined(MOTO_XT897) || defined(GKD_MINIPLUS) || defined(UT)
    myevent.keypad.fast = DEV_KEY_CODE_FAST;
    myevent.keypad.exit = DEV_KEY_CODE_EXIT;
#endif
#endif

#if defined(TRIMUI_SMART) || defined(UT)
    myevent.cust_key.gpio = NULL;
    myevent.cust_key.fd = open("/dev/mem", O_RDWR);
    if (myevent.cust_key.fd > 0) {
        myevent.cust_key.mem = mmap(
            0,
            4096,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            myevent.cust_key.fd,
            0x01c20000
        );

        if (myevent.cust_key.mem != MAP_FAILED) {
            uint32_t *p = NULL;

            p = (uint32_t *)(myevent.cust_key.mem + 0x800 + (0x24 * 6) + 0x04);
            myevent.cust_key.pre_cfg = *p;
            *p &= 0xfff000ff;

            p = (uint32_t *)(myevent.cust_key.mem + 0x800 + (0x24 * 6) + 0x1c);
            *p |= 0x01500000;

            myevent.cust_key.gpio = (uint32_t *)(myevent.cust_key.mem + 0x800 + (0x24 * 6) + 0x10);
        }
    }
#endif

    myevent.thread.id = SDL_CreateThreadInternal(input_handler, "NDS Input Handler", 4096, NULL);
    if (myevent.thread.id == NULL) {
        error("failed to create thread for input handler\n");
        exit(-1);
    }
}

#if defined(UT)
TEST(sdl2_event, init_event)
{
    init_event();
    TEST_ASSERT_EQUAL_INT(NDS_KEY_MODE, myevent.mode);
    TEST_ASSERT_NOT_NULL(myevent.thread.id);
    TEST_ASSERT_EQUAL_INT(DEV_KEY_CODE_SAVE, myevent.keypad.save);
}
#endif

void quit_event(void)
{
    trace_throttled("call %s()\n", __func__);

    myevent.thread.running = 0;
    trace_throttled("wait for input handler complete...\n");
    if (myevent.thread.id) {
        SDL_WaitThread(myevent.thread.id, NULL);
    }
    trace_throttled("completed\n");

    if (myevent.sem) {
        SDL_DestroySemaphore(myevent.sem);
    }

    if(myevent.fd > 0) {
        close(myevent.fd);
        myevent.fd = -1;
    }

#if defined(MOTO_XT894) || defined(MOTO_XT897) || defined(FXTEC_QX1000)
    if (myevent.tp_fd > 0) {
        close(myevent.tp_fd);
        myevent.tp_fd = -1;
    }

    if (myevent.pwr_fd > 0) {
        close(myevent.pwr_fd);
        myevent.pwr_fd = -1;
    }
#endif

#if defined(PANDORA)
    if(myevent.kb_fd > 0) {
        close(myevent.kb_fd);
        myevent.kb_fd = -1;
    }
#endif

#if defined(TRIMUI_SMART) || defined(UT)
    if (myevent.cust_key.fd > 0) {
        uint32_t *p = (uint32_t *)(myevent.cust_key.mem + 0x800 + (0x24 * 6) + 0x04);

        *p = myevent.cust_key.pre_cfg;
        munmap(myevent.cust_key.mem, 4096);
        close(myevent.cust_key.fd);

        myevent.cust_key.gpio = NULL;
        myevent.cust_key.fd = -1;
    }
#endif
}

#if defined(UT)
TEST(sdl2_event, quit_event)
{
    myevent.thread.running = 1;
    quit_event();
    TEST_ASSERT_EQUAL_INT(0, myevent.thread.running);
}
#endif

static int send_key_to_menu(void)
{
    int cc = 0;
    uint32_t bit = 0;
    uint32_t changed = myevent.keypad.pre_bits ^ myevent.keypad.cur_bits;

    trace_throttled("call %s()\n", __func__);

    for (cc = 0; cc <= KEY_BIT_LAST; cc++) {
        bit = 1 << cc;

        if (changed & bit) {
            if ((myevent.keypad.cur_bits & bit) == 0) {
#if !defined(UT)
                handle_sdl2_menu(cc);
#endif
            }
        }
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, send_key_to_menu)
{
    myevent.keypad.pre_bits = 0;
    myevent.keypad.cur_bits = (1 << KEY_BIT_A);
    TEST_ASSERT_EQUAL_INT(0, send_key_to_menu());
    TEST_ASSERT_EQUAL_INT(myevent.keypad.pre_bits, myevent.keypad.cur_bits);
}
#endif

static int update_raw_input_statue(uint32_t kbit, uint32_t val)
{
    uint32_t b = 0;

    trace_throttled("call %s(kbit=%d, val=%d)\n", __func__, kbit, val);

    switch (kbit) {
    case KEY_BIT_UP:        b = NDS_KEY_BIT_UP;     break;
    case KEY_BIT_DOWN:      b = NDS_KEY_BIT_DOWN;   break;
    case KEY_BIT_LEFT:      b = NDS_KEY_BIT_LEFT;   break;
    case KEY_BIT_RIGHT:     b = NDS_KEY_BIT_RIGHT;  break;
    case KEY_BIT_A:         b = NDS_KEY_BIT_A;      break;
    case KEY_BIT_B:         b = NDS_KEY_BIT_B;      break;
    case KEY_BIT_X:         b = NDS_KEY_BIT_X;      break;
    case KEY_BIT_Y:         b = NDS_KEY_BIT_Y;      break;
    case KEY_BIT_L1:        b = NDS_KEY_BIT_L;      break;
    case KEY_BIT_R1:        b = NDS_KEY_BIT_R;      break;
    case KEY_BIT_R2:        b = NDS_KEY_BIT_SWAP;   break;
    case KEY_BIT_SELECT:    b = NDS_KEY_BIT_SELECT; break;
    case KEY_BIT_START:     b = NDS_KEY_BIT_START;  break;
    case KEY_BIT_SWAP:      b = NDS_KEY_BIT_SWAP;   break;
    case KEY_BIT_DRASTIC:   b = NDS_KEY_BIT_MENU;   break;
    case KEY_BIT_QUIT:      b = NDS_KEY_BIT_QUIT;   break;
    case KEY_BIT_SAVE:      b = NDS_KEY_BIT_SAVE;   break;
    case KEY_BIT_LOAD:      b = NDS_KEY_BIT_LOAD;   break;
    case KEY_BIT_FAST:      b = NDS_KEY_BIT_FAST;   break;
    case KEY_BIT_HINGE:     b = NDS_KEY_BIT_HINGE;  break;
    default:                                        return 0;
    }

    if (val) {
        myevent.input.button_status |= b;
    }
    else {
        myevent.input.button_status &= ~b;
    }

    return 0;
}
#if defined(UT)
TEST(sdl2_event, update_raw_input_statue)
{
    myevent.input.button_status = 0;
    TEST_ASSERT_EQUAL_INT(0, update_raw_input_statue(KEY_BIT_UP, 1));
    TEST_ASSERT_EQUAL_INT(NDS_KEY_BIT_UP, myevent.input.button_status);
    TEST_ASSERT_EQUAL_INT(0, update_raw_input_statue(KEY_BIT_UP, 0));
    TEST_ASSERT_EQUAL_INT(0, myevent.input.button_status);
}
#endif

static int send_key_event(int raw_event)
{
    int cc = 0;
    int pressed = 0;
    uint32_t bit = 0;
    uint32_t changed = myevent.keypad.pre_bits ^ myevent.keypad.cur_bits;

    trace_throttled("call %s(raw_event=%d)\n", __func__, raw_event);

    for (cc=0; cc<=KEY_BIT_LAST; cc++) {
        bit = 1 << cc;
        pressed = !!(myevent.keypad.cur_bits & bit);

#if !defined(TRIMUI_SMART)
        if ((myconfig.hotkey == HOTKEY_BIND_MENU) && (cc == KEY_BIT_MENU)) {
            continue;
        }
#endif

#if defined(TRIMUI_SMART)
        if (cc == KEY_BIT_MENU) {
            continue;
        }
#endif

        if (changed & bit) {
            if (raw_event) {
                trace_throttled("input bit=0x%x, pressed=%d\n", cc, pressed);
                update_raw_input_statue(cc, pressed);
            }
            else {
                trace_throttled("send code[%d]=0x%04x, pressed=%d\n", cc, nds_key_code[cc], pressed);

#if !defined(UT)
                SDL_SendKeyboardKey(
                    pressed ? SDL_PRESSED : SDL_RELEASED,
                    SDL_GetScancodeFromKey(nds_key_code[cc])
                );
#endif
            }
        }
    }

#if defined(TRIMUI_SMART)
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_R2)) {
        set_key_bit(KEY_BIT_R2, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_L2)) {
        set_key_bit(KEY_BIT_L2, 0);
    }
#endif
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_SAVE)) {
        myvideo.lcd.status |= NDS_STATE_SAVE;
        set_key_bit(KEY_BIT_SAVE, 0);
        update_raw_input_statue(KEY_BIT_SAVE, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_LOAD)) {
        myvideo.lcd.status |= NDS_STATE_LOAD;
        set_key_bit(KEY_BIT_LOAD, 0);
        update_raw_input_statue(KEY_BIT_LOAD, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_FAST)) {
        set_key_bit(KEY_BIT_FAST, 0);
        update_raw_input_statue(KEY_BIT_FAST, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_R3)) {
        set_key_bit(KEY_BIT_R3, 0);
        update_raw_input_statue(KEY_BIT_R3, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_DRASTIC)) {
        set_key_bit(KEY_BIT_DRASTIC, 0);
        update_raw_input_statue(KEY_BIT_DRASTIC, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_SWAP)) {
        set_key_bit(KEY_BIT_SWAP, 0);
        update_raw_input_statue(KEY_BIT_SWAP, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_QUIT)) {
        release_key();
        update_raw_input_statue(KEY_BIT_QUIT, 0);
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, send_key_event)
{
    myevent.keypad.pre_bits = 0;
    myevent.keypad.cur_bits = (1 << KEY_BIT_QUIT);
    TEST_ASSERT_EQUAL_INT(0, send_key_event(0));
    TEST_ASSERT_EQUAL_INT((1 << KEY_BIT_QUIT), myevent.keypad.cur_bits);
    TEST_ASSERT_EQUAL_INT((1 << KEY_BIT_QUIT), myevent.keypad.pre_bits);
}
#endif

static int update_touch_axis(void)
{
    int r = 0;

    trace_throttled("call %s()\n", __func__);

#if defined(R36S)
    return 0;
#endif

    if (is_book_mode() && (myconfig.key_rotate == 0)) {
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_UP)) {
            r = 1;
            myevent.touch.x+= inc_touch_axis(1);
        }
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_DOWN)) {
            r = 1;
            myevent.touch.x-= inc_touch_axis(1);
        }
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_LEFT)) {
            r = 1;
            myevent.touch.y-= inc_touch_axis(0);
        }
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_RIGHT)) {
            r = 1;
            myevent.touch.y+= inc_touch_axis(0);
        }
    }
    else {
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_UP)) {
            r = 1;
            myevent.touch.y-= inc_touch_axis(1);
        }
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_DOWN)) {
            r = 1;
            myevent.touch.y+= inc_touch_axis(1);
        }
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_LEFT)) {
            r = 1;
            myevent.touch.x-= inc_touch_axis(0);
        }
        if (myevent.keypad.cur_bits & (1 << KEY_BIT_RIGHT)) {
            r = 1;
            myevent.touch.x+= inc_touch_axis(0);
        }
    }

    return r;
}

#if defined(UT)
TEST(sdl2_event, update_touch_axis)
{
    myevent.touch.x = 0;
    myevent.touch.y = 0;
    myevent.keypad.cur_bits = (1 << KEY_BIT_RIGHT);
    TEST_ASSERT_EQUAL_INT(1, update_touch_axis());
    TEST_ASSERT_EQUAL_INT(1, !!myevent.touch.x);
}
#endif

static int send_touch_key(int raw_event)
{
    uint32_t cc = 0;
    uint32_t bit = 0;
    uint32_t pressed = 0;
    uint32_t changed = myevent.keypad.pre_bits ^ myevent.keypad.cur_bits;

    trace_throttled("call %s(changed=0x%x)\n", __func__, changed);

#if !defined(R36S)
    if (changed & (1 << KEY_BIT_A)) {
        pressed = !!(myevent.keypad.cur_bits & (1 << KEY_BIT_A));
        trace_throttled("send touch key (pressed=%d)\n", pressed);

        if (raw_event) {
            myevent.input.touch_status = pressed;
        }
        else {
#if !defined(UT)
            SDL_SendMouseButton(
                myvideo.win,
                0,
                pressed ? SDL_PRESSED : SDL_RELEASED,
                SDL_BUTTON_LEFT
            );
#endif
        }
    }
#endif

    for (cc = 0; cc <= KEY_BIT_LAST; cc++) {
        bit = 1 << cc;

        if ((cc == KEY_BIT_FAST) ||
            (cc == KEY_BIT_SAVE) ||
            (cc == KEY_BIT_LOAD) ||
            (cc == KEY_BIT_QUIT) ||
            (cc == KEY_BIT_HINGE))
        {
            if (changed & bit) {
                pressed = myevent.keypad.cur_bits & bit;

#if !defined(UT)
                SDL_SendKeyboardKey(
                    pressed ? SDL_PRESSED : SDL_RELEASED,
                    SDL_GetScancodeFromKey(nds_key_code[cc])
                );
#endif
            }
        }
        if (cc == KEY_BIT_R1) {
            if (changed & bit) {
                myevent.touch.slow_down = (myevent.keypad.cur_bits & bit) ? 1 : 0;
            }
        }
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, send_touch_key)
{
    myevent.input.touch_status = 0;
    myevent.keypad.pre_bits = 0;
    myevent.keypad.cur_bits = (1 << KEY_BIT_A);
    TEST_ASSERT_EQUAL_INT(0, send_touch_key(1));
    TEST_ASSERT_EQUAL_INT(1, myevent.input.touch_status);
}
#endif

static int send_touch_event(int raw_event)
{
    int r = 0;
    static int pre_x = -1;
    static int pre_y = -1;

    trace_throttled("call %s()\n", __func__);

    if (myevent.keypad.pre_bits != myevent.keypad.cur_bits) {
        send_touch_key(raw_event);
    }

    r |= update_touch_axis();
    r |= limit_touch_axis();

    if (pre_x != myevent.touch.x || pre_y != myevent.touch.y) {
        r = 1;
        pre_x = myevent.touch.x;
        pre_y = myevent.touch.y;
    }

    if (r) {
        send_touch_axis();
    }

#if defined(TRIMUI_SMART)
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_R2)) {
        set_key_bit(KEY_BIT_R2, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_L2)) {
        set_key_bit(KEY_BIT_L2, 0);
    }
#endif
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_SAVE)) {
        set_key_bit(KEY_BIT_SAVE, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_LOAD)) {
        set_key_bit(KEY_BIT_LOAD, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_FAST)) {
        set_key_bit(KEY_BIT_FAST, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_SWAP)) {
        set_key_bit(KEY_BIT_SWAP, 0);
    }
    if (myevent.keypad.pre_bits & (1 << KEY_BIT_QUIT)) {
        release_key();
    }

    return 0;
}

#if defined(UT)
TEST(sdl2_event, send_touch_event)
{
    myevent.keypad.cur_bits = 0;
    myevent.keypad.pre_bits = (1 << KEY_BIT_QUIT);
    TEST_ASSERT_EQUAL_INT(0, send_touch_event(1));
    TEST_ASSERT_EQUAL_INT(0, myevent.keypad.pre_bits);
    TEST_ASSERT_EQUAL_INT(0, myevent.keypad.cur_bits);
}
#endif

void pump_event(_THIS)
{
    trace_throttled("call %s()\n", __func__);

#if !defined(UT)
    SDL_SemWait(myevent.sem);
#endif

    if (myvideo.menu.sdl2.enable) {
        send_key_to_menu();
    }
    else {
#if defined(R36S)
        send_key_event(0);
#endif
        if (myevent.mode == NDS_KEY_MODE) {
#if !defined(R36S)
            if (myevent.keypad.pre_bits != myevent.keypad.cur_bits) {
                send_key_event(0);
            }
#endif
        }
        else if (myevent.mode == NDS_TOUCH_MODE) {
            send_touch_event(0);
        }
    }

#if !defined(UT)
    SDL_SemPost(myevent.sem);
#endif
    myevent.keypad.pre_bits = myevent.keypad.cur_bits;
}

#if defined(UT)
TEST(sdl2_event, pump_event)
{
    pump_event(NULL);
    TEST_PASS();
}
#endif

void prehook_platform_get_input(uintptr_t p)
{
    static int pre_tp_x = 0;
    static int pre_tp_y = 0;
    static uint32_t pre_tp_bits = 0;
    static uint32_t pre_key_bits = 0;

    input_struct *input = (input_struct *)(((uint8_t *)p) + NDS_INPUT_OFFSET);

    trace_throttled("call %s(p=%p)\n", __func__, input);

    if (myvideo.menu.drastic.enable) {
        release_key();
    }

    if (myvideo.menu.sdl2.enable) {
        send_key_to_menu();
    }
    else {
#if defined(R36S)
        send_key_event(1);
#endif
        if (myevent.mode == NDS_KEY_MODE) {
#if !defined(R36S)
            if (myevent.keypad.pre_bits != myevent.keypad.cur_bits) {
                send_key_event(1);
            }
#endif
        }
        else if (myevent.mode == NDS_TOUCH_MODE) {
            send_touch_event(1);
        }
    }

    if (pre_key_bits != myevent.input.button_status) {
        pre_key_bits = myevent.input.button_status;
        if (p) {
            input->button_status = myevent.input.button_status;
            trace_throttled("button_status=0x%x\n", input->button_status);
        }
        else {
            error("p is null\n");
        }
    }

    if ((pre_tp_bits != myevent.input.touch_status)  ||
        (pre_tp_x != myevent.touch.x) ||
        (pre_tp_y != myevent.touch.y))
    {
        pre_tp_x = myevent.touch.x;
        pre_tp_y = myevent.touch.y;
        pre_tp_bits = myevent.input.touch_status;

        if (p) {
            input->touch_x = myevent.touch.x;
            input->touch_y = myevent.touch.y;
            input->touch_status = myevent.input.touch_status;
            trace_throttled(
                "x=%d, y=%d, pressed%d\n",
                input->touch_x,
                input->touch_y,
                !!input->touch_status
            );
        }
        else {
            error("p is null\n");
        }
    }
    myevent.keypad.pre_bits = myevent.keypad.cur_bits;
}

#if defined(UT)
TEST(sdl2_event, prehook_platform_get_input)
{
    input_struct *in = malloc(sizeof(input_struct));

    TEST_ASSERT_NOT_NULL(in);
    prehook_platform_get_input(0);
    myevent.keypad.cur_bits = (1 << KEY_BIT_A);
    prehook_platform_get_input((uintptr_t)((uint8_t *)in - NDS_INPUT_OFFSET));
    TEST_ASSERT_EQUAL_INT(in->button_status, NDS_KEY_BIT_A);
    free(in);
}
#endif

