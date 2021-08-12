#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>

#include <mpg123.h>

#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "sprites.h"

#define FVec2_New(x, y) FVec3_New(x, y, 0)

struct MPGHandleDeleter {
    void operator()(mpg123_handle* p)
    {
        mpg123_delete(p);
    }
};
using MPGHandle = std::unique_ptr<mpg123_handle, MPGHandleDeleter>;
struct LinearDeleter {
    void operator()(void* p)
    {
        linearFree(p);
    }
};
using LinearBuffer = std::unique_ptr<void, LinearDeleter>;

class SFX {
    bool valid{false};
    int channel_id;
    ndspWaveBuf waveBuf;
    LinearBuffer audioBuffer;

public:
    SFX(const char* path, const int channel_id_arg)
        : channel_id(channel_id_arg)
    {
        MPGHandle mh_holder(mpg123_new(nullptr, nullptr));
        const auto mh = mh_holder.get();

        if(mpg123_format(mh, 48000, MPG123_STEREO, MPG123_ENC_SIGNED_16) != MPG123_OK) return;
        if(mpg123_open(mh, path) != MPG123_OK) return;

        class Closer {
            mpg123_handle* const mh;
        public:
            Closer(mpg123_handle* mh_arg)
                : mh(mh_arg)
            {

            }
            ~Closer()
            {
                mpg123_close(mh);
            }
        } closer(mh);

        mpg123_getformat(mh, nullptr, nullptr, nullptr);

        const int samplecount = mpg123_seek(mh, 0, SEEK_END);
        const int bufSize = samplecount * 2 * sizeof(s16);
        mpg123_seek(mh, 0, SEEK_SET);

        audioBuffer.reset(linearAlloc(bufSize));

        size_t unused = 0;
        mpg123_read(mh, (u8*)audioBuffer.get(), bufSize, &unused);

        ndspChnSetInterp(channel_id, NDSP_INTERP_LINEAR);
        ndspChnSetRate(channel_id, 48000);
        ndspChnSetFormat(channel_id, NDSP_FORMAT_STEREO_PCM16);

        waveBuf.data_vaddr = audioBuffer.get();
        waveBuf.nsamples = samplecount;
        DSP_FlushDataCache(audioBuffer.get(), bufSize);

        float mix[12] = {1.0f, 1.0f};
        ndspChnSetMix(channel_id, mix);

        valid = true;
    }
    ~SFX()
    {
        ndspChnReset(channel_id);
    }

    void play()
    {
        if(!valid) return;

        if(!ndspChnIsPlaying(channel_id))
            ndspChnWaveBufAdd(channel_id, &waveBuf);
    }
};

namespace {

bool have_audio{false};
std::optional<SFX> sfx_list[3];
std::optional<SFX>& swing_sfx = sfx_list[0];
std::optional<SFX>& charge_sfx = sfx_list[1];
std::optional<SFX>& hole_sfx = sfx_list[2];

}

struct InputState {
    u32 kDown, kHeld, kUp;
    circlePosition circle;
    touchPosition touch;
    double dt;
};

class Level {
    static inline constexpr int tile_size = 16;
    static inline constexpr float hole_offset = 2.0f/32.0f;
    static inline constexpr float friction = 0.001f;
    unsigned level_counter{0};
    unsigned total_hits{0};
    unsigned current_hits{0};

    C3D_FVec direction;
    bool directing{false};

    C3D_FVec first_touch;
    bool touched{false};

    static inline constexpr int meter_speed = 1;
    static inline constexpr int meter_max = 30;
    static inline constexpr int meter_min = 0;
    int meter_strength;
    int meter_direction;
    bool powering{false};

    C3D_FVec launch_vel;
    float launch_vel_1d;

    struct Ball {
        C3D_FVec pos;
        C3D_FVec scale{FVec2_New(1,1)};
        C3D_FVec velocity{FVec2_New(0,0)};
        float velocity1D{0};
    };

    struct Tile {
        C3D_FVec pos;
        bool big;

        static Tile BigTile(C3D_FVec pos)
        {
            return Tile{pos, true}; // 32x32
        }
        static Tile SmallTile(C3D_FVec pos)
        {
            return Tile{pos, false}; // 16x16
        }
    };

    struct Hole {
        C3D_FVec pos;
    };

    struct SubLevel {
        std::vector<Tile> tiles;
        C2D_Image big_tile, small_tile;
        Ball ball;
        Hole hole;
        bool done{false};
        int dirX, dirY;
    };

    C2D_Image ball_img, shadow_img, hole_img, powermeter_img, arrow_img;

    SubLevel top, bottom;
    C2D_Text level_text, current_strike_text;
    C2D_Text win_top_text, win_bottom_text, strike_text;
    float win_top_height, win_bottom_height;

    float num_width05x[10];
    C2D_Text num_text[10];

    int fade_out_color;
    bool finished{false};

    static C3D_FVec BALL_POS(const float x, const float y)
    {
        return FVec2_New(tile_size * x, tile_size * y);
    }
    static C3D_FVec TILE_POS(const float x, const float y)
    {
        return FVec2_New(tile_size * x, tile_size * y);
    }
    static C3D_FVec HOLE_POS(const float x, const float y)
    {
        return FVec2_New(tile_size * x, tile_size * y - hole_offset);
    }
    void load_next()
    {
        total_hits += current_hits;
        current_hits = 0;
        fade_out_color = 255;

        top.done = false;
        top.tiles.clear();

        bottom.done = false;
        bottom.tiles.clear();
        switch(level_counter++)
        {
        case 0:
            top.ball = Ball{BALL_POS(11.75f, 4.75f)};
            top.hole = Hole{HOLE_POS(2.75f, 4.75f)};
            bottom.ball = Ball{BALL_POS(11.75f, 4.75f)};
            bottom.hole = Hole{HOLE_POS(2.75f, 4.75f)};

            top.tiles = {
                Tile::BigTile(TILE_POS(6, 6)),
                Tile::BigTile(TILE_POS(6, 8)),
                Tile::BigTile(TILE_POS(6, 0)),
                Tile::BigTile(TILE_POS(6, 2)),
            };
            bottom.tiles = {
                Tile::BigTile(TILE_POS(6, 6)),
                Tile::BigTile(TILE_POS(6, 8)),
                Tile::BigTile(TILE_POS(6, 0)),
                Tile::BigTile(TILE_POS(6, 2)),
            };
            break;
            
        case 1:
            top.ball = Ball{BALL_POS(11.75f, 4.75f)};
            top.hole = Hole{HOLE_POS(2.75f, 4.75f)};
            bottom.ball = Ball{BALL_POS(11.75f, 4.75f)};
            bottom.hole = Hole{HOLE_POS(2.75f, 4.75f)};

            top.tiles = {
                Tile::BigTile(TILE_POS(6, 4)),
            };
            bottom.tiles = {
                Tile::BigTile(TILE_POS(6, 8)),
            };
            break;
            
        case 2:
            top.ball = Ball{BALL_POS(10.25f, 7.25f)};
            top.hole = Hole{HOLE_POS(5.25f, 2.25f)};
            bottom.ball = Ball{BALL_POS(10.25f, 7.25f)};
            bottom.hole = Hole{HOLE_POS(3.25f, 4.25)};
            
            top.tiles = {};
            bottom.tiles = {
                Tile::SmallTile(TILE_POS(5, 2)),
            };
            break;

        case 3:
            top.ball = Ball{BALL_POS(5.75f, 4.75f)};
            top.hole = Hole{HOLE_POS(1.75f, 4.75f)};
            bottom.ball = Ball{BALL_POS(4.75f, 4.75f)};
            bottom.hole = Hole{HOLE_POS(11.75f, 4.75)};

            top.tiles = {
                Tile::BigTile(TILE_POS(7, 4)),
                Tile::SmallTile(TILE_POS(5, 3)),
                Tile::SmallTile(TILE_POS(3, 6)),
            };
            bottom.tiles = {
                Tile::BigTile(TILE_POS(2, 4)),
                Tile::SmallTile(TILE_POS(6, 3)),
                Tile::SmallTile(TILE_POS(9, 6)),
            };
            break;

        // case 4:
            // top.ball = Ball{BALL_POS(12.75f, 2.75f)};
            // top.hole = Hole{HOLE_POS(1.75f, 1.75f)};
            // bottom.ball = Ball{BALL_POS(5.75f, 0.75f)};
            // bottom.hole = Hole{HOLE_POS(7.75f, 0.75f)};
            // break;

        default:
            finished = true;
            break;
        }
    }

    void draw_level(const SubLevel& sub, const int x_off) const
    {
        C2D_ImageTint fade_out_tint;
        C2D_AlphaImageTint(&fade_out_tint, std::max(fade_out_color, 0)/255.0f);

        C2D_DrawImageAt(hole_img, x_off + sub.hole.pos.x, sub.hole.pos.y, 0.1875f, &fade_out_tint);
        if(!sub.done)
        {
            C2D_DrawImageAt(shadow_img, x_off + sub.ball.pos.x, sub.ball.pos.y + 4, 0.125f);
            if(directing)
            {
                C2D_DrawImageAtRotated(arrow_img, x_off + sub.ball.pos.x + ball_img.subtex->width / 2, sub.ball.pos.y + ball_img.subtex->height / 2, 0.1875f, std::atan2(direction.y, direction.x));
            }
        }
        
        if(sub.ball.scale.x > 0 && sub.ball.scale.y > 0)
            C2D_DrawImageAt(ball_img, x_off + sub.ball.pos.x, sub.ball.pos.y, 0.25f, nullptr, sub.ball.scale.x, sub.ball.scale.y);
        
        for(const auto& tile : sub.tiles)
        {
            C2D_DrawImageAt(tile.big ? sub.big_tile : sub.small_tile, x_off + tile.pos.x, tile.pos.y, 0.25f, &fade_out_tint);
        }
    }

    void simulate_level(SubLevel& sub, const double dt)
    {
        if(!sub.done && FVec3_Distance(sub.ball.pos, sub.hole.pos) < 12.0f)
        {
            sub.done = true;
            sub.ball.velocity1D = 0;
            sub.ball.velocity = FVec2_New(0,0);
            if(have_audio) hole_sfx->play();
        }
        else if(sub.done && sub.ball.scale.x > 0 && sub.ball.scale.y > 0)
        {
            sub.ball.scale = FVec3_Subtract(sub.ball.scale, FVec2_New(dt/300.0f, dt/300.0f));
            sub.ball.pos = FVec3_Add(sub.ball.pos, FVec3_Scale(FVec3_Subtract(FVec2_New(sub.hole.pos.x + hole_img.subtex->width/2.0f, sub.hole.pos.y + hole_img.subtex->height/2.0f - hole_offset), sub.ball.pos), 0.01f * dt));
        }
        else if(!sub.done && sub.ball.velocity1D > friction)
        {
            sub.ball.pos = FVec3_Add(sub.ball.pos, FVec3_Scale(sub.ball.velocity, dt));
        
            sub.ball.velocity1D -= friction * dt;
            const auto tmp = FVec3_Scale(launch_vel, sub.ball.velocity1D/launch_vel_1d);

            sub.ball.velocity.x = std::fabs(tmp.x) * sub.dirX;
            sub.ball.velocity.y = std::fabs(tmp.y) * sub.dirY;

            const int ball_w = ball_img.subtex->width;
            const int ball_h = ball_img.subtex->height;
            
            if(sub.ball.pos.x < 0)
            {
                sub.ball.velocity.x = std::fabs(sub.ball.velocity.x);
                sub.dirX = 1;
            }
            else if(sub.ball.pos.x >= (320 - ball_w))
            {
                sub.ball.velocity.x = -std::fabs(sub.ball.velocity.x);
                sub.dirX = -1;
            }

            if(sub.ball.pos.y < 0)
            {
                sub.ball.velocity.y = std::fabs(sub.ball.velocity.y);
                sub.dirY = 1;
            }
            else if(sub.ball.pos.y >= (240 - ball_h))
            {
                sub.ball.velocity.y = -std::fabs(sub.ball.velocity.y);
                sub.dirY = -1;
            }

            for (Tile& t : sub.tiles)
            {
                const int tile_wh = tile_size * (t.big ? 2 : 1);

                const auto do_check = [&t, ball_w, ball_h, tile_wh](const float newX, const float newY) -> bool {
                    return (newX + ball_w) > t.pos.x && newX < (t.pos.x + tile_wh)
                        && (newY + ball_h) > t.pos.y && newY < (t.pos.y + tile_wh);
                };
                if (do_check(sub.ball.pos.x + sub.ball.velocity.x * dt, sub.ball.pos.y))
                {
                    sub.ball.velocity.x = -sub.ball.velocity.x;
                    sub.dirX = -sub.dirX;
                }

                if (do_check(sub.ball.pos.x, sub.ball.pos.y + sub.ball.velocity.y * dt))
                {
                    sub.ball.velocity.y = -sub.ball.velocity.y;
                    sub.dirY = -sub.dirY;
                }
            }
        }
        else
        {
            sub.ball.velocity = FVec2_New(0,0);
            sub.ball.velocity1D = 0;
        }
    }

    bool can_move() const
    {
        return top.ball.velocity.x == 0 && top.ball.velocity.y == 0 && bottom.ball.velocity.x == 0 && bottom.ball.velocity.y == 0 && (!top.done || !bottom.done);
    }

public:

    Level(C2D_SpriteSheet sprites, C2D_TextBuf textBuf, C2D_Font font)
    {
        C2D_TextFontParse(&level_text, font, textBuf, "Lvl");
        C2D_TextOptimize(&level_text);
        C2D_TextFontParse(&current_strike_text, font, textBuf, "Strk");
        C2D_TextOptimize(&current_strike_text);

        C2D_TextFontParse(&win_top_text, font, textBuf, "You completed\nthe course!\nPress any button\nto try again!");
        C2D_TextOptimize(&win_top_text);
        C2D_TextGetDimensions(&win_top_text, 1.0f, 1.0f, nullptr, &win_top_height);

        C2D_TextFontParse(&win_bottom_text, font, textBuf, "It took you:");
        C2D_TextOptimize(&win_bottom_text);
        C2D_TextGetDimensions(&win_bottom_text, 1.0f, 1.0f, nullptr, &win_bottom_height);

        C2D_TextFontParse(&strike_text, font, textBuf, "strokes");
        C2D_TextOptimize(&strike_text);

        for(int i = 0; i < 10; ++i)
        {
            char txt[] = {char('0' + i), 0};
            C2D_TextFontParse(&num_text[i], font, textBuf, txt);
            C2D_TextOptimize(&num_text[i]);
            C2D_TextGetDimensions(&num_text[i], 0.5f, 0.5f, nullptr, &num_width05x[i]);
        }

        ball_img = C2D_SpriteSheetGetImage(sprites, sprites_ball_idx);
        shadow_img = C2D_SpriteSheetGetImage(sprites, sprites_ball_shadow_idx);
        hole_img = C2D_SpriteSheetGetImage(sprites, sprites_hole_idx);
        powermeter_img = C2D_SpriteSheetGetImage(sprites, sprites_powermeter_idx);
        arrow_img = C2D_SpriteSheetGetImage(sprites, sprites_point_idx);

        top.big_tile = C2D_SpriteSheetGetImage(sprites, sprites_tile32_dark_idx);
        top.small_tile = C2D_SpriteSheetGetImage(sprites, sprites_tile16_dark_idx);

        bottom.big_tile = C2D_SpriteSheetGetImage(sprites, sprites_tile32_light_idx);
        bottom.small_tile = C2D_SpriteSheetGetImage(sprites, sprites_tile16_light_idx);

        load_next();
    }

    bool update(const InputState& input_state)
    {
        if(finished)
        {
            if(fade_out_color != 255)
                fade_out_color -= 5;

            if(fade_out_color == -30)
            {
                return true;
            }
            else if(input_state.kDown != 0)
            {
                if(have_audio) charge_sfx->play();
                fade_out_color -= 5;
                return false;
            }
        }

        if(top.done && bottom.done)
        {
            if(fade_out_color == -30)
            {
                load_next();
            }
            else
                fade_out_color -= 5;
        }

        simulate_level(top, input_state.dt);
        simulate_level(bottom, input_state.dt);

        if(!can_move()) return false;

        const C3D_FVec touch_pos = FVec2_New(input_state.touch.px, input_state.touch.py);

        if(!directing && !touched && input_state.kDown & KEY_TOUCH)
        {
            touched = true;
            first_touch = touch_pos;
        }

        if(!directing && touched && input_state.kUp & KEY_TOUCH)
        {
            touched = false;
        }

        const C3D_FVec circle_dir = FVec2_New(input_state.circle.dx, -input_state.circle.dy);

        if(directing)
        {
            const bool big_cond = (touched && (input_state.kHeld & KEY_TOUCH)) || (!touched && (input_state.kHeld & KEY_A));
            if(!powering && big_cond)
            {
                if(have_audio) charge_sfx->play();
                powering = true;
                meter_strength = 0;
                meter_direction = 1;
            }
            else if(powering && !big_cond)
            {
                if(have_audio) swing_sfx->play();
                powering = false;
                directing = false;
                touched = false;
                current_hits++;

                const auto ang = -std::atan2(direction.y, direction.x);
                launch_vel_1d = (meter_strength / float(meter_max));
                launch_vel = FVec3_Scale(FVec2_New(std::cos(ang), -std::sin(ang)), launch_vel_1d);
                const int dirX = std::signbit(launch_vel.x) ? -1 : 1;
                const int dirY = std::signbit(launch_vel.y) ? -1 : 1;
                if(!top.done)
                {
                    top.ball.velocity = launch_vel;
                    top.ball.velocity1D = launch_vel_1d;
                    top.dirX = dirX;
                    top.dirY = dirY;
                }
                if(!bottom.done)
                {
                    bottom.ball.velocity = launch_vel;
                    bottom.ball.velocity1D = launch_vel_1d;
                    bottom.dirX = dirX;
                    bottom.dirY = dirY;
                }

                return false;
            }
        }

        if(!touched && FVec3_Magnitude(circle_dir) > 36.0f)
        {
            directing = true;
            direction = FVec3_Normalize(circle_dir);
        }
        else if(touched && FVec3_Distance(first_touch, touch_pos) > 24.0f)
        {
            directing = true;
            direction = FVec3_Normalize(FVec3_Subtract(first_touch, touch_pos));
        }
        else
        {
            directing = false;
            powering = false;
        }

        if(powering)
        {
            meter_strength += meter_direction * meter_speed;
            if(meter_strength == meter_min)
            {
                meter_direction = 1;
            }
            else if(meter_strength == meter_max)
            {
                meter_direction = -1;
            }
        }
        return false;
    }

    void drawOdd()
    {
        if(!finished)
        {
            const auto draw_num = [&](const float y, const unsigned num) {
                const std::string val = std::to_string(num);
                float w = 0;
                for(const auto c : val)
                {
                    w += num_width05x[c - '0'];
                }
                float x = (40.0f - w) / 2.0f;
                for(const auto c : val)
                {
                    C2D_DrawText(&num_text[c - '0'], C2D_WithColor, x, y, 0.5f, 0.5f, 0.5f, C2D_Color32f(0,0,0,1));
                    x += num_width05x[c - '0'];
                }
            };
            C2D_DrawText(&level_text, C2D_AlignCenter | C2D_WithColor, 16, 30, 0.5f, 0.625f, 0.625f, C2D_Color32f(0,0,0,1));
            draw_num(80, level_counter);

            C2D_DrawText(&current_strike_text, C2D_AlignCenter | C2D_WithColor, 16, 130, 0.5f, 0.625f, 0.625f, C2D_Color32f(0,0,0,1));
            draw_num(180, current_hits);

            if(powering)
            {
                const int power_length = (powermeter_img.subtex->height - 10) * (meter_strength / float(meter_max));
                const int power_x = 400 - 40 + 10;
                const int power_y = (240 - powermeter_img.subtex->height) / 2;
                C2D_DrawImageAt(powermeter_img, power_x, power_y, 0.25f);
                if(power_length != 0)
                    C2D_DrawRectSolid(power_x + 3, power_y + powermeter_img.subtex->height - 5 - power_length, 0.3125f, powermeter_img.subtex->width - 6, power_length, C2D_Color32f(1,1,0,1));
            }

            draw_level(top, 40);
        }
        else
        {
            C2D_DrawRectSolid(80, 30, 0.25f, 240, 180, C2D_Color32f(0,0,0,std::max(fade_out_color, 0)/255.0f * 0.5f));
            C2D_DrawText(&win_top_text, C2D_AlignCenter | C2D_WithColor, 200, (240 - win_top_height) / 2.0f, 0.5f, 1.0f, 1.0f, C2D_Color32f(1,1,1,std::max(fade_out_color, 0)/255.0f));
        }
    }

    void drawEven()
    {
        if(!finished)
        {
            draw_level(bottom, 0);
        }
        else
        {
            C2D_DrawRectSolid(60, 30, 0.25f, 200, 180, C2D_Color32f(0,0,0,std::max(fade_out_color, 0)/255.0f * 0.5f));
            const u32 text_color = C2D_Color32f(1,1,1,std::max(fade_out_color, 0)/255.0f);
            const float y = (240 - win_bottom_height) / 2.0f - win_bottom_height;
            C2D_DrawText(&win_bottom_text, C2D_AlignCenter | C2D_WithColor, 160, y, 0.5f, 1.0f, 1.0f, text_color);
            
            const std::string val = std::to_string(total_hits);
            float w = 0;
            for(const auto c : val)
            {
                w += num_width05x[c - '0'];
            }
            w /= 2;
            float x = (320.0f - w) / 2.0f;
            for(const auto c : val)
            {
                C2D_DrawText(&num_text[c - '0'], C2D_WithColor, x, y + win_bottom_height, 0.5f, 1.0f, 1.0f, text_color);
                x += num_width05x[c - '0'];
            }

            C2D_DrawText(&strike_text, C2D_AlignCenter | C2D_WithColor, 160, y + win_bottom_height * 2, 0.5f, 1.0f, 1.0f, text_color);
        }
    }
};

int main()
{
    romfsInit();

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    if(R_SUCCEEDED(ndspInit()))
    {
        have_audio = true;
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);

        if(mpg123_init() == MPG123_OK)
        {
            auto sfx = sfx_list;
            #define SFX_NAME(n) ("romfs:/sfx/" n ".mp3")
            for(const char* filename : {SFX_NAME("swing"), SFX_NAME("charge"), SFX_NAME("hole")})
            {
                sfx->emplace(filename, sfx - sfx_list);
                sfx++;
            }
            mpg123_exit();
        }
    }

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    C2D_Font font = C2D_FontLoad("romfs:/gfx/font.bcfnt");
    C2D_TextBuf textBuf = C2D_TextBufNew(4096);
    C2D_SpriteSheet sprites = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");

    const auto bg_dark_img = C2D_SpriteSheetGetImage(sprites, sprites_bg_dark_idx);
    const auto bg_light_img = C2D_SpriteSheetGetImage(sprites, sprites_bg_light_idx);
    const auto title_img = C2D_SpriteSheetGetImage(sprites, sprites_logo_idx);
    const auto border_img = C2D_SpriteSheetGetImage(sprites, sprites_border_idx);
    constexpr u32 clear_color = C2D_Color32(0,0,0,255);

    C2D_Text title_text;
    float title_text_height = 0;

    C2D_TextFontParse(&title_text, font, textBuf, "Press any button\nto start!");
    C2D_TextOptimize(&title_text);
    C2D_TextGetDimensions(&title_text, 1, 1, nullptr, &title_text_height);

    TickCounter counter;
    osTickCounterStart(&counter);

    InputState input_state;

    std::optional<Level> level;
    float title_dt{0.0f};
    int title_fade_out_dir = 0;
    int title_fade_alpha = 275;

    while(aptMainLoop())
    {
        hidScanInput();

        input_state.kDown = hidKeysDown();
        input_state.kHeld = hidKeysHeld();
        input_state.kUp = hidKeysUp();
        hidCircleRead(&input_state.circle);
        hidTouchRead(&input_state.touch);

        if (input_state.kDown & KEY_START)
            break; // break in order to return to hbmenu

        input_state.dt = osTickCounterRead(&counter);
        
        if(level)
        {
            if(level->update(input_state))
            {
                level.reset();
                level.emplace(sprites, textBuf, font);
            }
        }
        else if(input_state.kDown && title_fade_out_dir == 0)
        {
            if(have_audio) charge_sfx->play();
            title_fade_out_dir = 5;
        }
        else if(title_fade_alpha)
        {
            title_fade_alpha -= title_fade_out_dir;
        }
        else
        {
            level.emplace(sprites, textBuf, font);
        }

        // Render the scene
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, clear_color);
        C2D_TargetClear(bottom, clear_color);

        const u32 title_fade_color = C2D_Color32(255,255,255, std::min(title_fade_alpha, 255));

        C2D_SceneBegin(top);
        C2D_DrawImageAt(border_img, 0, 0, 0, nullptr, 1, 1);
        C2D_DrawImageAt(border_img, 400 - 40, 0, 0, nullptr, -1, 1);
        C2D_DrawImageAt(bg_light_img, 40, 0, 0);

        if(level)
        {
            level->drawOdd();
        }
        else
        {
            title_dt += input_state.dt;
            C2D_ImageTint tint;
            C2D_AlphaImageTint(&tint, std::min(title_fade_alpha, 255)/255.0f);
            C2D_DrawImageAt(title_img, 0, 10.0f * std::sin(title_dt / 800.0f), 0, &tint);
        }

        C2D_SceneBegin(bottom);
        C2D_DrawImageAt(bg_dark_img, 0, 0, 0);

        if(level)
        {
            level->drawEven();
        }
        else
        {
            C2D_DrawRectSolid(60, 30, 0.25f, 200, 180, C2D_Color32f(0,0,0, std::min(title_fade_alpha, 255)/255.0f * 0.5f));
            C2D_DrawText(&title_text, C2D_AlignCenter | C2D_WithColor, 160, (240 - title_text_height) / 2.0f, 0.5f, 1.0f, 1.0f, title_fade_color);
            // C2D_DrawText(&title_text, C2D_AlignCenter | C2D_WithColor, 160 + 1, (240 - title_text_height) / 2.0f + 1, 0.75f, 1.0f, 1.0f, C2D_Color32(0,0,0, std::min(title_fade_alpha, 255)));
        }

        C3D_FrameEnd(0);

        osTickCounterUpdate(&counter);
    }

    C2D_SpriteSheetFree(sprites);
    C2D_TextBufDelete(textBuf);
    C2D_FontFree(font);

    if(have_audio)
    {
        for(auto& sfx : sfx_list)
        {
            sfx.reset();
        }
        ndspExit();
    }

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
}
