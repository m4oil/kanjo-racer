/*
 * KANJO RACER: BEAR PRODUCTS - starter project (Butano)
 * Dedicated to Collin.
 *
 * Controls:  A = gas   B = brake   LEFT/RIGHT = steer   START = tune menu
 * Tune menu: UP/DOWN = pick part, LEFT/RIGHT = change level, START = back
 *            After a race: A = restart, B = back to the title screen
 *
 * Track geometry constants MUST match tools/make_assets.py
 */

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_math.h"
#include "bn_fixed.h"
#include "bn_fixed_point.h"
#include "bn_algorithm.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_item.h"
#include "bn_sprite_text_generator.h"

#include "bn_regular_bg_items_track.h"
#include "bn_regular_bg_items_title.h"
#include "bn_sprite_items_civic.h"
#include "bn_sprite_items_rival.h"

#include "common_variable_8x16_sprite_font.h"

namespace
{
    // ------------------------------------------------------------------ track geometry
    constexpr int CX = 256, CY = 256;
    constexpr int A_O = 212, B_O = 180;   // outer edge
    constexpr int A_I = 156, B_I = 124;   // inner edge
    constexpr int A_C = 184, B_C = 152;   // center line (AI follows this)
    constexpr int TOTAL_LAPS = 3;

    bool inside_ellipse(int dx, int dy, int a, int b)
    {
        long long l = (long long)dx * dx * b * b + (long long)dy * dy * a * a;
        return l <= (long long)a * a * b * b;
    }

    bool on_road(const bn::fixed_point& p)
    {
        int dx = p.x().right_shift_integer() - CX;
        int dy = p.y().right_shift_integer() - CY;
        return inside_ellipse(dx, dy, A_O, B_O) && ! inside_ellipse(dx, dy, A_I, B_I);
    }

    // ------------------------------------------------------------------ tuning
    // Each level is 0..4. Later: tie levels to cash, add more parts, save to SRAM.
    struct tune
    {
        int engine = 0;    // intake/exhaust/cams: more power
        int grip = 0;      // tires/suspension: less sliding
        int gearing = 0;   // final drive: lower accel, higher top speed
        int weight = 0;    // weight reduction: quicker everything
    };

    struct stats
    {
        bn::fixed power;   // acceleration per frame
        bn::fixed drag;    // fraction of speed lost per frame (sets top speed)
        bn::fixed grip;    // fraction of sideways speed removed per frame
        bn::fixed turn;    // max degrees of steering per frame
    };

    stats compute_stats(const tune& t)
    {
        stats s;
        s.power = (bn::fixed(0.034) + bn::fixed(0.0025) * t.engine)
                * (1 - bn::fixed(0.035) * t.gearing)
                * (1 + bn::fixed(0.02) * t.weight);
        s.drag = bn::fixed(0.020) - bn::fixed(0.001) * t.gearing;
        s.grip = bn::fixed(0.14) + bn::fixed(0.04) * t.grip;
        s.turn = bn::fixed(1.9) + bn::fixed(0.05) * t.weight;
        return s;
    }

    // ------------------------------------------------------------------ cars
    struct controls
    {
        bn::fixed throttle = 0;   // 0..1
        bn::fixed brake = 0;      // 0..1
        bn::fixed steer = 0;      // -1..1
    };

    struct car
    {
        const bn::sprite_item* item;
        bn::sprite_ptr sprite;
        bn::fixed_point pos;
        bn::fixed_point vel;
        bn::fixed heading = 0;    // degrees, 0 = up, clockwise (matches the sprite sheet)
        int frame = -1;
        int lap = 0;
        bool half = false;        // passed the bottom half this lap (stops lap cheating)
        tune tn;

        explicit car(const bn::sprite_item& it) :
            item(&it),
            sprite(it.create_sprite(0, 0))
        {
        }
    };

    void place_car(car& c, bn::fixed x, bn::fixed y)
    {
        c.pos = bn::fixed_point(x, y);
        c.vel = bn::fixed_point(0, 0);
        c.heading = 90;           // facing right: the loop runs clockwise
        c.frame = -1;
        c.lap = 0;
        c.half = false;
    }

    void update_car(car& c, const controls& in)
    {
        stats st = compute_stats(c.tn);

        bn::fixed s = bn::degrees_lut_sin(c.heading);
        bn::fixed co = bn::degrees_lut_cos(c.heading);
        bn::fixed fx = s, fy = -co;       // forward vector (screen y points down)
        bn::fixed rx = co, ry = s;        // right vector

        bn::fixed vx = c.vel.x();
        bn::fixed vy = c.vel.y();

        // engine
        vx += fx * st.power * in.throttle;
        vy += fy * st.power * in.throttle;

        // tires: bleed off sideways velocity (low grip = drifty)
        bn::fixed side = vx * rx + vy * ry;
        vx -= rx * side * st.grip;
        vy -= ry * side * st.grip;

        // drag + brakes
        bn::fixed keep = 1 - st.drag - bn::fixed(0.05) * in.brake;
        vx *= keep;
        vy *= keep;

        // grass / outside the loop
        if(! on_road(c.pos))
        {
            vx *= bn::fixed(0.94);
            vy *= bn::fixed(0.94);
        }

        // steering only bites when rolling
        bn::fixed fwd = vx * fx + vy * fy;
        bn::fixed authority = bn::min(bn::abs(fwd) / bn::fixed(0.8), bn::fixed(1));
        c.heading += in.steer * st.turn * authority;
        while(c.heading >= 360) { c.heading -= 360; }
        while(c.heading < 0) { c.heading += 360; }

        c.vel = bn::fixed_point(vx, vy);
        bn::fixed old_x = c.pos.x();
        c.pos += c.vel;

        // lap counting: cross the start line at the top, left -> right, after the bottom half
        if(c.pos.y() > CY + 60)
        {
            c.half = true;
        }
        if(c.half && c.pos.y() < CY && old_x < CX && c.pos.x() >= CX)
        {
            c.half = false;
            ++c.lap;
        }
    }

    void draw_car(car& c, const bn::fixed_point& cam)
    {
        int f = ((c.heading * 32 + 180).right_shift_integer() / 360) % 32;
        if(f != c.frame)
        {
            c.frame = f;
            c.sprite.set_tiles(c.item->tiles_item(), f);
        }
        c.sprite.set_position(c.pos - cam);
    }

    int speed_kmh(const car& c)
    {
        bn::fixed ax = bn::abs(c.vel.x());
        bn::fixed ay = bn::abs(c.vel.y());
        bn::fixed speed = bn::max(ax, ay) + bn::min(ax, ay) * bn::fixed(0.4);   // cheap sqrt(x^2+y^2)
        return (speed * 100).right_shift_integer();
    }

    // Soft bump between two cars.
    void bump(car& a, car& b)
    {
        bn::fixed dx = a.pos.x() - b.pos.x();
        bn::fixed dy = a.pos.y() - b.pos.y();
        if(dx * dx + dy * dy < 150)
        {
            a.pos += bn::fixed_point(dx * bn::fixed(0.12), dy * bn::fixed(0.12));
            b.pos -= bn::fixed_point(dx * bn::fixed(0.12), dy * bn::fixed(0.12));
            a.vel *= bn::fixed(0.97);
            b.vel *= bn::fixed(0.97);
        }
    }

    // ------------------------------------------------------------------ AI rival
    // Chases a target point that slides around the center line ahead of the car.
    controls ai_controls(const car& c, bn::fixed& theta)
    {
        bn::fixed tx = 0, ty = 0;
        for(int i = 0; i < 6; ++i)
        {
            tx = CX + bn::fixed(A_C) * bn::degrees_lut_sin(theta);
            ty = CY - bn::fixed(B_C) * bn::degrees_lut_cos(theta);
            bn::fixed dx = tx - c.pos.x();
            bn::fixed dy = ty - c.pos.y();
            if(dx * dx + dy * dy < 45 * 45)
            {
                theta += 3;
                if(theta >= 360) { theta -= 360; }
            }
            else
            {
                break;
            }
        }

        bn::fixed dx = tx - c.pos.x();
        bn::fixed dy = ty - c.pos.y();
        bn::fixed dot_right = dx * bn::degrees_lut_cos(c.heading) + dy * bn::degrees_lut_sin(c.heading);

        controls in;
        in.steer = bn::clamp(dot_right / 12, bn::fixed(-1), bn::fixed(1));
        in.throttle = bn::abs(in.steer) > bn::fixed(0.7) ? bn::fixed(0.6) : bn::fixed(0.92);
        return in;
    }

    // ------------------------------------------------------------------ HUD helpers
    using text_vec = bn::vector<bn::sprite_ptr, 40>;

    void write_text(bn::sprite_text_generator& gen, text_vec& out, int x, int y, const bn::string_view& s)
    {
        gen.generate(bn::fixed(x), bn::fixed(y), s, out);
    }

    enum class state { countdown, racing, finished };

    // ------------------------------------------------------------------ title screen
    // Returns when the player presses START or A.
    void title_screen(bn::sprite_text_generator& gen)
    {
        bn::regular_bg_ptr title_bg = bn::regular_bg_items::title.create_bg(0, 0);
        bn::sprite_ptr demo_car = bn::sprite_items::civic.create_sprite(-140, 70, 8);   // frame 8 = facing right

        bn::vector<bn::sprite_ptr, 64> texts;
        bn::vector<bn::sprite_ptr, 16> prompt;

        gen.set_center_alignment();
        gen.generate(bn::fixed(0), bn::fixed(-32), "KANJO RACER:", texts);
        gen.generate(bn::fixed(0), bn::fixed(-14), "BEAR PRODUCTS", texts);
        gen.generate(bn::fixed(0), bn::fixed(50), "DEDICATED TO COLLIN", texts);

        bn::fixed car_x = -140;
        int t = 0;

        while(! bn::keypad::start_pressed() && ! bn::keypad::a_pressed())
        {
            car_x += 2;
            if(car_x > 140) { car_x = -140; }
            demo_car.set_x(car_x);

            if(t % 60 == 0)
            {
                gen.set_center_alignment();
                gen.generate(bn::fixed(0), bn::fixed(34), "PRESS START", prompt);
            }
            else if(t % 60 == 40)
            {
                prompt.clear();
            }

            ++t;
            bn::core::update();
        }

        bn::core::update();   // consume the START/A press so the game loop doesn't see it again
    }
}

int main()
{
    bn::core::init();

    bn::sprite_text_generator gen(common::variable_8x16_sprite_font);
    title_screen(gen);

    bn::regular_bg_ptr bg = bn::regular_bg_items::track.create_bg(0, 0);

    car rival(bn::sprite_items::rival);
    car player(bn::sprite_items::civic);
    rival.tn.engine = 1;
    rival.tn.grip = 2;

    bn::fixed ai_theta = 0;
    bn::fixed steer_smooth = 0;
    state st = state::countdown;
    int state_frames = 0;
    int race_frames = 0;
    bool player_won = false;
    bool menu_open = false;
    int menu_row = 0;
    int frame_counter = 0;

    text_vec hud, banner, menu_text;
    bn::fixed_point cam(CX, CY);

    auto reset_race = [&]()
    {
        place_car(player, CX - 30, 116);
        place_car(rival, CX - 30, 90);
        ai_theta = 355;
        steer_smooth = 0;
        st = state::countdown;
        state_frames = 0;
        race_frames = 0;
        banner.clear();
    };
    reset_race();

    while(true)
    {
        ++frame_counter;

        // ---------------------------------------------------------- tune menu (pauses the game)
        if(bn::keypad::start_pressed())
        {
            menu_open = ! menu_open;
            menu_text.clear();
            if(! menu_open) { reset_race(); }   // re-grid so new parts apply cleanly
        }

        if(menu_open)
        {
            int* levels[4] = { &player.tn.engine, &player.tn.grip, &player.tn.gearing, &player.tn.weight };
            const char* names[4] = { "ENGINE ", "GRIP   ", "GEARING", "WEIGHT " };

            if(bn::keypad::up_pressed())   { menu_row = (menu_row + 3) % 4; }
            if(bn::keypad::down_pressed()) { menu_row = (menu_row + 1) % 4; }
            if(bn::keypad::left_pressed()) { *levels[menu_row] = bn::max(0, *levels[menu_row] - 1); }
            if(bn::keypad::right_pressed()){ *levels[menu_row] = bn::min(4, *levels[menu_row] + 1); }

            menu_text.clear();
            gen.set_left_alignment();
            write_text(gen, menu_text, -80, -52, "- TUNE SHOP -");
            for(int i = 0; i < 4; ++i)
            {
                bn::string<32> line;
                line += (i == menu_row) ? "> " : "  ";
                line += names[i];
                line += " ";
                line += bn::to_string<4>(*levels[i]);
                write_text(gen, menu_text, -80, -28 + i * 18, line);
            }
            write_text(gen, menu_text, -80, 54, "START: BACK");
            bn::core::update();
            continue;
        }

        // ---------------------------------------------------------- state machine
        controls pin;
        controls ain;

        if(st == state::countdown)
        {
            if(++state_frames >= 240) { st = state::racing; state_frames = 0; }
        }
        else if(st == state::racing)
        {
            ++race_frames;

            bn::fixed target_steer = 0;
            if(bn::keypad::left_held())  { target_steer = -1; }
            if(bn::keypad::right_held()) { target_steer = 1; }
            steer_smooth += (target_steer - steer_smooth) * bn::fixed(0.3);
            pin.steer = steer_smooth;
            pin.throttle = bn::keypad::a_held() ? 1 : 0;
            pin.brake = bn::keypad::b_held() ? 1 : 0;

            ain = ai_controls(rival, ai_theta);
        }
        else if(bn::keypad::a_pressed())
        {
            reset_race();
        }
        else if(bn::keypad::b_pressed())
        {
            bg.set_visible(false);
            rival.sprite.set_visible(false);
            player.sprite.set_visible(false);
            hud.clear();
            title_screen(gen);
            bg.set_visible(true);
            rival.sprite.set_visible(true);
            player.sprite.set_visible(true);
            reset_race();
        }

        if(st != state::countdown)
        {
            update_car(player, pin);
            update_car(rival, ain);
            bump(player, rival);
        }

        if(st == state::racing)
        {
            if(player.lap >= TOTAL_LAPS || rival.lap >= TOTAL_LAPS)
            {
                player_won = player.lap >= TOTAL_LAPS;
                st = state::finished;
                banner.clear();
            }
        }

        // ---------------------------------------------------------- camera + draw
        bn::fixed cam_x = bn::clamp(player.pos.x() + player.vel.x() * 10, bn::fixed(120), bn::fixed(392));
        bn::fixed cam_y = bn::clamp(player.pos.y() + player.vel.y() * 10, bn::fixed(80), bn::fixed(432));
        cam = bn::fixed_point(cam_x, cam_y);

        bg.set_position(256 - cam.x(), 256 - cam.y());
        draw_car(rival, cam);
        draw_car(player, cam);

        // ---------------------------------------------------------- HUD
        if(frame_counter % 6 == 0)
        {
            hud.clear();
            gen.set_left_alignment();

            int lap_shown = bn::min(player.lap + 1, TOTAL_LAPS);
            int secs = race_frames / 60;
            int tenths = (race_frames % 60) * 10 / 60;

            bn::string<32> l1;
            l1 += "LAP ";
            l1 += bn::to_string<4>(lap_shown);
            l1 += "/";
            l1 += bn::to_string<4>(TOTAL_LAPS);
            l1 += "  ";
            l1 += bn::to_string<6>(secs);
            l1 += ".";
            l1 += bn::to_string<4>(tenths);
            write_text(gen, hud, -114, -72, l1);

            bn::string<32> l2;
            l2 += bn::to_string<6>(speed_kmh(player));
            l2 += " KM/H";
            write_text(gen, hud, -114, 66, l2);
        }

        // ---------------------------------------------------------- banner
        if(st == state::countdown)
        {
            int count = 3 - state_frames / 60;       // 3,2,1,GO
            if(state_frames % 60 == 0 || banner.empty())
            {
                banner.clear();
                gen.set_center_alignment();
                bn::string<32> b;
                if(count > 0) { b += bn::to_string<4>(count); } else { b += "GO!"; }
                write_text(gen, banner, 0, -20, b);
            }
        }
        else if(st == state::racing && race_frames == 45)
        {
            banner.clear();
        }
        else if(st == state::racing && race_frames < 45 && banner.empty())
        {
            gen.set_center_alignment();
            write_text(gen, banner, 0, -20, "GO!");
        }
        else if(st == state::finished && banner.empty())
        {
            gen.set_center_alignment();
            write_text(gen, banner, 0, -24, player_won ? "YOU WIN!" : "RIVAL WINS");
            write_text(gen, banner, 0, 0, "A: RACE AGAIN");
            write_text(gen, banner, 0, 18, "B: TITLE");
        }

        bn::core::update();
    }
}
