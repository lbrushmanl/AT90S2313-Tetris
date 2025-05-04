#include <stdint.h>
#include <stdbool.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "utils.h"
#include "tetromino_definitions.h"


#define LEFT_BUTTON PD2
#define ROTATE_BUTTON PD3
#define DOWN_BUTTON PD4
#define RIGHT_BUTTON PD5
#define BUTTONS PIND

#define LEFT_BUTTON_STATE()   (!READ_PIN(BUTTONS, LEFT_BUTTON))
#define RIGHT_BUTTON_STATE()  (!READ_PIN(BUTTONS, RIGHT_BUTTON))
#define DOWN_BUTTON_STATE()   (!READ_PIN(BUTTONS, DOWN_BUTTON))
#define ROTATE_BUTTON_STATE() (!READ_PIN(BUTTONS, ROTATE_BUTTON))

#define VIDEO_SYNC_PIN _BV(PB3) // 1k 
#define VIDEO_DATA_PIN _BV(PB2) // 470R

#define WIDTH_PIXELS             (10)
#define PIXEL_HIGHT_IN_LINES     (12U)
#define LINES_IN_FRAME           (312)
#define HIGHT_OF_GAME_AREA       (21)
#define HIGHT_OF_PLAYABLE_AREA   (21)

#define LOOP_COUNTER      (UBRR)
#define sub_pixel_counter (TCNT0)
#define pixel_line_number (EEDR)

#define FRAMES_BEFORE_GAME_LOGIC            (5U)
#define GAME_EXICUTION_CYCLES_TO_DROP_TETRO (3U)


typedef struct
{
    tetros_e tetro;
    uint8_t rotation;
    int8_t x;
    int8_t y;
} tetromino_t;


typedef enum
{
    CLEAR = 0,
    DRAW = 1,
    COLLISION = 2
} command_e;


static tetromino_t block = {4, 0, 7, HIGHT_OF_PLAYABLE_AREA};
static tetromino_t next_tetro = {0, 3, 0, 10};

static volatile uint16_t screen_data[HIGHT_OF_GAME_AREA] =
{
    0b111111111111000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
    0b100000000001000,
};

static volatile uint8_t wait = 0;
static uint8_t score = 0;


#pragma GCC push_options
#pragma GCC optimize ("-O1")
ISR (TIMER1_COMP1_vect)
{
    static uint16_t line_number = 0;
    // static uint8_t sub_pixel_counter = 0;
    // static uint8_t pixel_line_number = 0;

    SET_OUTPUT_LOW(PORTB, VIDEO_SYNC_PIN);    

    if (line_number > 2)
    {
        __builtin_avr_delay_cycles(65); // Sync 4.7us
        SET_OUTPUT_HIGH(PORTB, VIDEO_SYNC_PIN);

        if (line_number > 22 && line_number < 309) // Draw to screen
        {
            _delay_us(8); // Front porch

            if (pixel_line_number > 20)
            {
                pixel_line_number = 20;
            }
            
            
            uint16_t tmp_line_data = screen_data[pixel_line_number];

            // Draw pixels in line
            while (tmp_line_data)
            {
                if (tmp_line_data & 0x1)
                {
                    SET_OUTPUT_HIGH(PORTB, VIDEO_DATA_PIN);
                }
                else
                {
                    SET_OUTPUT_LOW(PORTB, VIDEO_DATA_PIN);
                }

                __builtin_avr_delay_cycles(30); // Sync 4.7us Pixel width
                tmp_line_data >>= 1;
            }

            SET_OUTPUT_LOW(PORTB, VIDEO_DATA_PIN);

            if (sub_pixel_counter < PIXEL_HIGHT_IN_LINES)
            {
                sub_pixel_counter++;
            }
            else
            {
                sub_pixel_counter = 0;
                pixel_line_number++;
            }
        }
    }

    if (line_number < LINES_IN_FRAME)
    {
        line_number++;
        return;
    } 

    line_number = 0;
    pixel_line_number = 0;
    wait++;
}
#pragma GCC pop_options


static inline void init_line_clock(void)
{
    sub_pixel_counter = 0;
    pixel_line_number = 0;
    CONFIG_PIN(DDRB, VIDEO_SYNC_PIN | VIDEO_DATA_PIN);
    SET_OUTPUT_HIGH(PORTB, VIDEO_SYNC_PIN);
    SET_OUTPUT_LOW(PORTB, VIDEO_DATA_PIN);

    TCCR1B |= _BV(CS10); // Set CLK divider to 1
    TCCR1B |= _BV(CTC1); // Reset timer after valid compear
    TIMSK  |= _BV(OCIE1A); // Enable timer interupt

    // 15.625Khz
    // Have to tune to get the correct freq
    OCR1AH = 4;
    OCR1AL = 3;

    sei();
}


static void game_over(void)
{
    for (LOOP_COUNTER = 0; LOOP_COUNTER < HIGHT_OF_GAME_AREA; LOOP_COUNTER++)
    {
        screen_data[LOOP_COUNTER] = 0;
    }

    uint16_t number = pgm_read_word(&numbers[score % 10]);

    for (LOOP_COUNTER = 0; LOOP_COUNTER < 5; LOOP_COUNTER++)
    {
        screen_data[10 + LOOP_COUNTER] = ((number >> (LOOP_COUNTER * 3)) & 0b111) << 10;
    }

    score /= 10;

    number = pgm_read_word(&numbers[score % 10]);

    for (LOOP_COUNTER = 0; LOOP_COUNTER < 5; LOOP_COUNTER++)
    {
        screen_data[10 + LOOP_COUNTER] = ((number >> (LOOP_COUNTER * 3)) & 0b111) << 4;
    }

    while (true);
}


static bool game_logic(command_e command, tetromino_t *tetro)
{
    uint16_t line = pgm_read_word(&tetros[tetro->tetro][tetro->rotation]);
    uint8_t mask = 0b111;
    uint8_t bits = 3;

    if (tetro->tetro == TETRO_STRAIGHT)
    {
        mask = 0b1111;
        bits = 4;
    }

    for (LOOP_COUNTER = 0; LOOP_COUNTER < bits; LOOP_COUNTER++)
    {
        if (tetro->y + LOOP_COUNTER > HIGHT_OF_PLAYABLE_AREA)
        {
            return false;
        }

        uint16_t row = ((line >> (LOOP_COUNTER * bits)) & mask) << tetro->x;

        switch (command)
        {
        case DRAW:
            screen_data[tetro->y + LOOP_COUNTER] |= row;
            break;

        case COLLISION:
            if (screen_data[tetro->y + LOOP_COUNTER] & row)
            {
                if (tetro->y + LOOP_COUNTER >= HIGHT_OF_PLAYABLE_AREA)
                {
                    game_over();
                }
                
                return true;
            }

            break;
        
        case CLEAR:
            screen_data[tetro->y + LOOP_COUNTER] &= ~row;
            break;
        }
    }

    return false;
}


static inline bool poll_direction_buttons(void)
{
    static uint8_t time_to_move_down = 0;

    if (time_to_move_down == 3)
    {
        time_to_move_down = 0;
    } else
    {
        time_to_move_down++;
    }

    if (ROTATE_BUTTON_STATE())
    {
        block.rotation++;
        block.rotation %= 4;
        block.rotation -= game_logic(COLLISION, &block);
        if (block.rotation == 255)
        {
            block.rotation = 3;
        }
    }
    
    if (LEFT_BUTTON_STATE())
    {
        block.x++;
        block.x -= game_logic(COLLISION, &block);
    } 
    
    if (RIGHT_BUTTON_STATE())
    {
        block.x--;
        block.x += game_logic(COLLISION, &block);
    }
    
    if (DOWN_BUTTON_STATE() || (time_to_move_down == GAME_EXICUTION_CYCLES_TO_DROP_TETRO))
    {
        block.y--;
        bool collision = game_logic(COLLISION, &block);
        block.y += collision;
        return collision;
    }

    return false;
}

// https://www.analog.com/en/resources/design-notes/random-number-generation-using-lfsr.html
// XOR-based feedback shift register
static uint8_t seed = 0xB8; // Initial seed (can be any non-zero value)

static tetros_e random(void)
{
    // Linear Feedback Shift Register (LFSR)
    seed ^= (seed << 1);
    seed ^= (seed >> 1);
    seed ^= (seed << 5);

    return (seed & 0xFF) % 6; // Generate numbers in range [0, 5]
}

static void update_next_tetro(void)
{
    game_logic(CLEAR, &next_tetro);
    next_tetro.tetro = random();
    game_logic(DRAW, &next_tetro);
}


static inline bool check_full_line(void)
{
    for (LOOP_COUNTER = 1; LOOP_COUNTER < HIGHT_OF_PLAYABLE_AREA; LOOP_COUNTER++)
    {
        if ((screen_data[LOOP_COUNTER] & 0b11111111110000) == 0b11111111110000)
        {
            for (; LOOP_COUNTER < HIGHT_OF_PLAYABLE_AREA; LOOP_COUNTER++)
            {
                screen_data[LOOP_COUNTER] = screen_data[LOOP_COUNTER + 1]; // TODO: Only copy game area
            }

            score++;

            return true;
        }                
    }

    return false;
}


int main(void)
{
    init_line_clock();

    CONFIG_PIN(DDRD, _BV(PD6));

    update_next_tetro();

    SET_OUTPUT_LOW(PORTD, _BV(PD6));

    while (true)
    {
        if (wait == FRAMES_BEFORE_GAME_LOGIC)
        {
            bool collision = false;

            if (!check_full_line())
            {
                // Clear the tetro from the screen buffer
                game_logic(CLEAR, &block);

                // Get new direction
                collision = poll_direction_buttons();  
            }          

            // Draw tetro to screen buffer
            game_logic(DRAW, &block);

            if (collision)
            {
                block.x = 7;
                block.y = HIGHT_OF_PLAYABLE_AREA;
                block.rotation = 0;
                block.tetro = next_tetro.tetro;
                update_next_tetro();
            }
            
            wait = 0;
        }
    }
    
    return 0;
}