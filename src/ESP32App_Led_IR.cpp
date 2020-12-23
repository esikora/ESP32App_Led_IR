/**
    ESP32App_Led_IR:
    This application has been developed to run on an M5Stack Atom Lite
    ESP32 development board. It displays light effects on an led strip
    which can be adjusted using an infrared remote control.
        
    Copyright (C) 2020 by Ernst Sikora
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include<Arduino.h>

// External library: JC_Button, https://github.com/JChristensen/JC_Button
#include <JC_Button.h>

// External library: FastLED, https://github.com/FastLED/FastLED
#include <FastLED.h> 

// External library: IRRemoteESP8266, https://github.com/crankyoldgit/IRremoteESP8266
#include <IRrecv.h> 
#include <IRremoteESP8266.h>

// HW: Pin assignments
const byte PIN_BUTTON = 39; // M5Stack Atom Lite: internal button
const byte PIN_LEDATOM = 27; // M5Stack Atom Lite: internal LED
const byte PIN_LEDSTRIP = 26; // M5SAtack Atom Lite: Grove connector GPIO pin (yellow cable) -> LED strip
const byte PIN_IRRECV = 32; // M5SAtack Atom Lite: Grove connector GPIO pin (white cable) -> IR Receiver

// HW: LED strip, number of leds
const byte NUM_LEDS = 29;

// Type declarations
enum State {OFF = 0, ON = 1, ECO = 2}; // Main system states
typedef enum State t_State;

enum LightMode {CONSTANT = 0, GRADIENT = 1, CHASE = 2, SPRITE = 3, SPARKLE = 4}; // Light effects for LED strip
typedef enum LightMode t_LightMode;

// Simple sprite datatype used for light effects of type "SPRITE"
typedef struct LedSprite {
    bool active = false; // Only active sprites are shown on the LED strip
    int8_t activateAt = -1; // Step at which the sprite shall be activated
    int8_t pos = 0; // Current position of the sprite, i.e. LED number
    int8_t vel = 0; // Velocity of the sprite, positive or negative
    CRGB color = CRGB(255, 255, 255); // Color of the sprite
} t_LedSprite;

// Switch to receive debug messages via serial monitor
const bool DEBUG_ON = true;

// Status LED: color definitions
const CRGB COLOR_OFF = CRGB(255, 0, 0); // System state: OFF
const CRGB COLOR_ON  = CRGB(0, 255, 0); // System state: ON
const CRGB COLOR_ECO = CRGB(0, 255, 0); // System state: ECO

// Status LED and LED strip: Brightness constants
const uint8_t BRIGHTNESS_OFF = 8;  // System state: OFF
const uint8_t BRIGHTNESS_ON  = 20; // System state: ON
const uint8_t BRIGHTNESS_ECO = 10; // System state: ECO

const uint8_t BRIGHTNESS_MIN  = 2;  // Lowest brightness
const uint8_t BRIGHTNESS_MAX  = 50; // Highest brightness --> maximum current
const uint8_t BRIGHTNESS_STEP = 2;  // Increment for brightness adjustement via IR remote

// Light effects: Speed constants
const uint8_t RENDER_NUM_CYCLES_HOLD_MIN = 2;  // Fastest speed of light effects
const uint8_t RENDER_NUM_CYCLES_HOLD_MAX = 40; // Slowest speed of light effects
const uint8_t RENDER_NUM_CYCLES_HOLD_STEP = 2; // Increment for speed adjustment via IR remote

// Light effects: Hue spectrum constants
const uint16_t RENDER_HUE_MAX = 256; // Maximum hue value of HSV color model (FastLED)

// Light effect "Chase" constants
const uint8_t RENDER_CHASE_NUM_COLORS = 16; // Number of colors from HSV color spectrum
const uint8_t RENDER_CHASE_HUE_STEP = RENDER_HUE_MAX / RENDER_CHASE_NUM_COLORS; // Hue increment between two colors
const uint8_t RENDER_CHASE_NUM_CYCLES_HOLD_INIT = 20; // Default speed

// Light effect "Gradient" constants
const uint8_t RENDER_GRADIENT_NUM_STEPS = 64; // Number of colors from HSV color spectrum
const uint8_t RENDER_GRADIENT_HUE_STEP = RENDER_HUE_MAX / RENDER_GRADIENT_NUM_STEPS; // Hue increment between two colors
const uint8_t RENDER_GRADIENT_NUM_CYCLES_HOLD_INIT = 4; // Default speed

// Light effect "Sprite" constants
const uint8_t RENDER_SPRITES_NUM_CYCLES_HOLD_INIT = 2; // Default speed
const uint8_t RENDER_SPRITES_SPAWN_RATE = 30; // Probability in percent that a new sprite is spawned within one cycle
const uint8_t RENDER_SPRITES_NUM_SPRITES_MAX = 10; // Maximum number of sprites

// Light effect "Sparkle" constants
const uint8_t RENDER_SPARKLE_NUM_CYCLES_HOLD_INIT = 2; // Default speed
const uint8_t RENDER_SPARKLE_LIGHTNESS_STEP = 5; // Lightness step
const uint8_t RENDER_SPARKLE_LIGHTNESS_MIN = 50; // Minimum lightness value
// See FastLed color spectrum: https://github.com/FastLED/FastLED/wiki/FastLED-HSV-Colors
const uint8_t SPARKLE_COLOR_HUE[4] = {160, 96, 0, 0}; // Blue, Green, Red, White
const uint8_t SPARKLE_COLOR_SAT[4] = {255, 255, 255, 0}; // Blue, Green, Red, White

// Light effect "Constant"
const CRGB RENDER_CONSTANT_COLOR_DEFAULT = CRGB(255, 255, 255);

// Time constant, i.e. time after which light effects are updated
const int TIME_CYCLE = 50; // ms

// IR Commands (values depend on the remote control used)
const uint64_t IR_ON_OFF         = 0x20DF10EF; // Stand-By/ON
const uint64_t IR_BRIGHTNESS_INC = 0x20DF00FF; // +
const uint64_t IR_BRIGHTNESS_DEC = 0x20DF807F; // -
const uint64_t IR_MODE_CHANGE    = 0x20DFAE51; // OK
const uint64_t IR_PLAY           = 0x20DF0BF4; // Play
const uint64_t IR_PAUSE          = 0x20DF738C; // Pause
const uint64_t IR_SLOWER         = 0x20DF5AA5; // Reverse
const uint64_t IR_FASTER         = 0x20DFFD02; // Forward
const uint64_t IR_LEFT           = 0x20DF04FB; // Previous
const uint64_t IR_RIGHT          = 0x20DF6B94; // Right
const uint64_t IR_RED            = 0x20DF4EB1; // Button "Red"
const uint64_t IR_GREEN          = 0x20DF8E71; // Button "Green"
const uint64_t IR_YELLOW         = 0x20DFC639; // Button "Yellow"
const uint64_t IR_BLUE           = 0x20DF8679; // Button "Blue"
const uint64_t IR_WHITE          = 0x20DF55AA; // Button "Info"

// IR receiver library parameters
const uint16_t IR_BUFFER_SIZE = 1024;
const uint8_t IR_MSG_TIMEOUT = 15;

// -----------------------------------------------------------------------------
// Object and variable definitions
// -----------------------------------------------------------------------------

// Internal button
Button btn_(PIN_BUTTON);

// IR receiver
IRrecv irRecv_(PIN_IRRECV, IR_BUFFER_SIZE, IR_MSG_TIMEOUT, true);

// Buffer for decoded IR command
decode_results irCmd_ = {};

// IR command status
bool irCmdAvailable_ = false; // True, if new IR code has been received and decoded

// Last IR command received and decoded
uint64_t irCmdValue_ = 0;

// Internal LED controller
CRGB ledAtom_[1];

// LED strip controller
CRGB ledStrip_[NUM_LEDS];

// Sprite buffer
t_LedSprite sprites_[RENDER_SPRITES_NUM_SPRITES_MAX];

// Overall device state
t_State deviceState_ = t_State::OFF;

// Currently selected light effect
t_LightMode lightMode_ = t_LightMode::SPARKLE;

// Brightness factor for LED strip
uint8_t brightness_ = BRIGHTNESS_OFF;

// Flag: refresh of LED strip needed
bool refreshNeeded_ = false;

// Gradient, chase, sparkle, and sprite effect: Effect paused
bool paused_   = false;

// Gradient, chase, sparkle, and sprite effect: Number of cycles to hold each step
uint8_t renderCyclesHold_ = RENDER_SPARKLE_NUM_CYCLES_HOLD_INIT;

// Gradient, chase, sparkle, and sprite effect: Current cycle number
int cycleNr_ = 0;

// Gradient and chase effect: Current base hue value
uint8_t hueBase_ = 0;

// Chase effect: Moving direction
bool dirLeft_  = true;

// Sparkle effect: current color index
uint8_t sparkleColorIdx_ = 0;

// Sparkle effect: current lightness value
uint8_t sparkleValue_ = RENDER_SPARKLE_LIGHTNESS_MIN;

// Sparkle effect: current sparkle state
int8_t sparkleState_ = 1;


// --- Function declarations ---

void clearLedStrip();
void drawAndUpdateSprites(uint32_t stepNr);
void clearSprites();

/**
 * Let the Led library show the set colors.
 */
void showLeds()
{
    FastLED.show();
    refreshNeeded_ = false;
}

// Led color for light effect "Constant"
CRGB constantColor = RENDER_CONSTANT_COLOR_DEFAULT;

/**
 * Led strip effect: Constant white light e.g. for reading.
 */
void renderConstant()
{
    // Do something only if there is a reason for refreshing such as changed brightness.
    if (refreshNeeded_)
    {
        // Update color of each LED 
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED to chosen RGB value
            ledStrip_[ledNr] = constantColor;
        }
    
        showLeds();
    }
}

/**
 * Led strip effect: All leds have the same color.
 * The color cycles through the HSV spectrum according to the set speed.
 */
void renderGradient()
{
    // Update colors every n cycles (i.e. n procedure calls). Do not update colors if paused by user.
    if (cycleNr_ == 0 && !paused_)
    {
        refreshNeeded_ = true;
        
        // Update each color of each LED 
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED
            ledStrip_[ledNr].setHSV(hueBase_, 255, 255);
        }

        // Cycle through color spectrum    
        hueBase_ += RENDER_GRADIENT_HUE_STEP;
    }

    // Show in case something has changed such as brightness or colors
    if (refreshNeeded_)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr_ = (cycleNr_ + 1) % renderCyclesHold_;
}

/**
 * Led strip effect: Each led shows a successive color of the color spectrum.
 * Colors move over time according to the set direction and set speed.
 */
void renderChase()
{
    // Update colors every n cycles (i.e. n procedure calls). Do not update colors if paused by user.
    if (cycleNr_ == 0 && !paused_)
    {
        refreshNeeded_ = true;
      
        // Color of first LED
        uint8_t hueLed = hueBase_;
                
        // Update each color of each LED 
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED
            ledStrip_[ledNr].setHSV(hueLed, 255, 255);
    
            // Color of next LED
            hueLed += RENDER_CHASE_HUE_STEP;
        }

        // Cycle through color spectrum depending on the direction set by the user
        if (dirLeft_)
        {
            hueBase_ += RENDER_CHASE_HUE_STEP;
        }
        else
        {
            hueBase_ -= RENDER_CHASE_HUE_STEP;
        }
    }

    // Show in case something has changed such as brightness or colors
    if (refreshNeeded_)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr_ = (cycleNr_ + 1) % renderCyclesHold_;
}

/**
 * Led strip effect: Sprites appear randomly in the middle of the led strip, move either to the left or right
 * and vanish at the border of the led strip. User can adjust the speed.
 */
void renderSprite()
{
    // Update colors every n cycles (i.e. n procedure calls). Do not update colors if paused by user.
    if (cycleNr_ == 0 && !paused_)
    {
        refreshNeeded_ = true;

        // Generate a new sprite randomly
        if (random(0, 100) < RENDER_SPRITES_SPAWN_RATE)
        {
            t_LedSprite sp;
            sp.active = true;
            sp.activateAt = -1;
            sp.pos = NUM_LEDS / 2;
            sp.vel = 2 * random(0, 2) - 1;
            sp.color = CHSV(random(0, 256), random(128, 256), random(128, 256));

            // Insert the new sprite at a free position of the sprite array
            for (int i = 0; i < RENDER_SPRITES_NUM_SPRITES_MAX; i++)
            {
                if (sprites_[i].active == false)
                {
                    sprites_[i] = sp;
                    break;
                }
            }
        }

        // Draw and update all active sprites
        drawAndUpdateSprites(0);
    }

    if (refreshNeeded_)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr_ = (cycleNr_ + 1) % renderCyclesHold_;
}

/**
 * Led strip effect: TBD
 * User can adjust the speed.
 */
void renderSparkle()
{
    // Update colors every n cycles (i.e. n procedure calls). Do not update colors if paused by user.
    if (cycleNr_ == 0 && !paused_)
    {
        refreshNeeded_ = true;

        // Update lightness value
        switch (sparkleState_)
        {
            case 1:
                if (sparkleValue_ < 255 - RENDER_SPARKLE_LIGHTNESS_STEP)
                {
                    sparkleValue_ += RENDER_SPARKLE_LIGHTNESS_STEP;
                }
                else
                {
                    sparkleValue_ = 255;
                    sparkleState_ = -1;
                }
                break;

            case -1:
                if (sparkleValue_ >= RENDER_SPARKLE_LIGHTNESS_MIN + RENDER_SPARKLE_LIGHTNESS_STEP)
                {
                    sparkleValue_ -= RENDER_SPARKLE_LIGHTNESS_STEP;
                }
                else
                {
                    sparkleValue_ = 0;
                    sparkleState_ = 0;
                }
                break;

            default:
                sparkleValue_ = RENDER_SPARKLE_LIGHTNESS_MIN;
                sparkleState_ = 1;
                sparkleColorIdx_ = (sparkleColorIdx_ + 1) % 4;
        }

        
        // Update all sprites
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED
            if (ledNr % 4 == sparkleColorIdx_)
            {
                ledStrip_[ledNr] = CHSV(SPARKLE_COLOR_HUE[sparkleColorIdx_], SPARKLE_COLOR_SAT[sparkleColorIdx_], sparkleValue_);
            }
            else
            {
                ledStrip_[ledNr] = CHSV(0, 0, 0);
            }
        }

    }

    if (refreshNeeded_)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr_ = (cycleNr_ + 1) % renderCyclesHold_;
}

/**
 * Fade brightness from start value to end value with a defined delay after each step.
 */
void fadeBrightness(uint8_t startBr, uint8_t endBr, uint16_t stepDelay)
{
    int8_t brStep;
    uint8_t numSteps;

    // Determine increment and number of steps
    if (startBr <= endBr)
    {
        brStep = 1;
        numSteps = endBr - startBr + 1;
    }
    else
    {
        brStep = -1;
        numSteps = startBr - endBr + 1;
    }

    uint8_t curBr = startBr;

    // Change the brightness gradually    
    for (uint8_t i = 0; i < numSteps; i++)
    {
        FastLED.setBrightness(curBr);
        FastLED.show();
        delay(stepDelay);
        curBr += brStep;
    }
}

/**
 * Sets led colors according to positions of all active sprites in the sprite list.
 * Updates the sprites in the sprite list e.g. according to their velocity.
 */
void drawAndUpdateSprites(uint32_t stepNr)
{
    clearLedStrip();

    // Update sprites
    for (int k = 0; k < RENDER_SPRITES_NUM_SPRITES_MAX; k++)
    {
        // Activate sprites based on current step number
        if (sprites_[k].activateAt == stepNr)
            sprites_[k].active = true;

        if (sprites_[k].active)
        {
            // Set led colors based on active sprites
            if (sprites_[k].pos >= 0 && sprites_[k].pos < NUM_LEDS)
                ledStrip_[sprites_[k].pos] = sprites_[k].color;

            // Move sprites
            sprites_[k].pos += sprites_[k].vel;

            // Scale down lightness to approx 90% of its current value, i.e. 230/256
            sprites_[k].color.nscale8_video(230);

            // Deactivate sprites that moved outside the led strip
            if (sprites_[k].pos < 0 || sprites_[k].pos >= NUM_LEDS)
                sprites_[k].active = false;
        }
    }
}

/**
 * Set all led colors to black.
 */
void clearLedStrip()
{
    // Clear all sprites
    for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
    {
        // Update color of the current LED
        ledStrip_[ledNr] = CRGB(0, 0, 0);
    }
}

/**
 * Show an animtion at startup using the sprite functionlity.
 */
void startupAnimation()
{
    // Define a set of sprites
    sprites_[0].active = true;
    sprites_[0].activateAt = 0;
    sprites_[0].pos = NUM_LEDS / 2;
    sprites_[0].vel = 0;
    sprites_[0].color = CRGB(255, 255, 0);
    
    sprites_[1].active = false;
    sprites_[1].activateAt = 5;
    sprites_[1].pos = NUM_LEDS / 2 - 1;
    sprites_[1].vel = -1;
    sprites_[1].color = CRGB(32, 32, 128);
    
    sprites_[2].active = false;
    sprites_[2].activateAt = 5;
    sprites_[2].pos = NUM_LEDS / 2 + 1;
    sprites_[2].vel = +1;
    sprites_[2].color = CRGB(32, 32, 128);
    
    sprites_[3].active = false;
    sprites_[3].activateAt = 10;
    sprites_[3].pos = NUM_LEDS / 2 - 1;
    sprites_[3].vel = -1;
    sprites_[3].color = CRGB(128, 0, 0);
    
    sprites_[4].active = false;
    sprites_[4].activateAt = 10;
    sprites_[4].pos = NUM_LEDS / 2 + 1;
    sprites_[4].vel = +1;
    sprites_[4].color = CRGB(128, 0, 0);

    sprites_[5].active = false;
    sprites_[5].activateAt = 15;
    sprites_[5].pos = NUM_LEDS / 2 - 1;
    sprites_[5].vel = -1;
    sprites_[5].color = CRGB(0, 128, 0);
    
    sprites_[6].active = false;
    sprites_[6].activateAt = 15;
    sprites_[6].pos = NUM_LEDS / 2 + 1;
    sprites_[6].vel = +1;
    sprites_[6].color = CRGB(0, 128, 0);

    // Do the sprite animation
    for (int stepNr = 0; stepNr < 30; stepNr++)
    {
        drawAndUpdateSprites(stepNr);
        FastLED.show();
        delay(100);
    }

    // Fade out
    fadeBrightness(brightness_, 0, 50);

    // Invalidate all sprites and clear led strip
    clearSprites();
    clearLedStrip();

    // Reset led strip to its actual brightness
    FastLED.setBrightness(brightness_);
    FastLED.show();
}

/**
 * Helper function that invalidates all sprites in the sprite array.
 */
void clearSprites()
{
    // Set all sprites to inactive
    for (int i = 0; i < RENDER_SPRITES_NUM_SPRITES_MAX; i++)
    {
        sprites_[i].active = false;
        sprites_[i].activateAt = -1;
    }
}

/**
 * Helper function to read and preprocess IR commands.
 */
void readIrCommand() {
    irCmdAvailable_ = irRecv_.decode(&irCmd_);

    // Preprocessing of the IR command
    if (irCmdAvailable_)
    {
        // Is it a repetition of the previous IR command?
        if (irCmd_.repeat)
        {
            // Is repetition of the previous command allowed?
            bool irCmdRepeatable =
                (irCmdValue_ == IR_BRIGHTNESS_DEC) ||
                (irCmdValue_ == IR_BRIGHTNESS_INC) ||
                (irCmdValue_ == IR_SLOWER) ||
                (irCmdValue_ == IR_FASTER);

            // Ignore IR repeat command if repetition of the last command is not supported
            if (!irCmdRepeatable)
            {
                irCmdAvailable_ = false;
                irCmdValue_ = 0x0;
            }
        }
        else {
            // No repetition: retrieve the IR command
            irCmdValue_ = irCmd_.value;
        }
        
        irRecv_.resume();

        if (DEBUG_ON)
        {
            Serial.print("IR: ");
            Serial.println((unsigned long) irCmd_.value, HEX);
        }
    }
}

/**
 * Helper function that adjusts color effects based on user input, mainly IR commands.
 */
void processUserInputColorEffects() {
    switch (irCmdValue_) {
              
        case IR_BRIGHTNESS_INC:
            brightness_ += BRIGHTNESS_STEP;
            if (brightness_ > BRIGHTNESS_MAX) brightness_ = BRIGHTNESS_MAX;
            FastLED.setBrightness(brightness_);
            refreshNeeded_ = true;

            if (DEBUG_ON)
            {
                Serial.print("Brightness+: ");
                Serial.println(brightness_);
            }
            
            break;
        
        case IR_BRIGHTNESS_DEC:
            brightness_ -= BRIGHTNESS_STEP;
            if (brightness_ < BRIGHTNESS_MIN) brightness_ = BRIGHTNESS_MIN;
            FastLED.setBrightness(brightness_);
            refreshNeeded_ = true;

            if (DEBUG_ON)
            {
                Serial.print("Brightness-: ");
                Serial.println(brightness_);
            }
            
            break;

        case IR_SLOWER:
            renderCyclesHold_ += RENDER_NUM_CYCLES_HOLD_STEP;
            if (renderCyclesHold_ > RENDER_NUM_CYCLES_HOLD_MAX) renderCyclesHold_ = RENDER_NUM_CYCLES_HOLD_MAX;

            if (DEBUG_ON)
            {
                Serial.print("Speed-: ");
                Serial.println(0.001 * renderCyclesHold_ * TIME_CYCLE);
            }
            
            break;

        case IR_FASTER:
            renderCyclesHold_ -= RENDER_NUM_CYCLES_HOLD_STEP;
            if (renderCyclesHold_ < RENDER_NUM_CYCLES_HOLD_MIN) renderCyclesHold_ = RENDER_NUM_CYCLES_HOLD_MIN;
            paused_ = false;

            if (DEBUG_ON)
            {
                Serial.print("Speed+: ");
                Serial.println(0.001 * renderCyclesHold_ * TIME_CYCLE);
            }
            
            break;

        case IR_LEFT:
            if (lightMode_ == t_LightMode::CHASE && (!dirLeft_ || paused_))
            {                   
                dirLeft_ = true;
                paused_ = false;
                cycleNr_ = 0;

                if (DEBUG_ON)
                {
                    Serial.println("Direction L");
                }
            }
            
            break;

        case IR_RIGHT:
            if (lightMode_ == t_LightMode::CHASE && (dirLeft_ || paused_))
            {                   
                dirLeft_ = false;
                paused_ = false;
                cycleNr_ = 0;

                if (DEBUG_ON)
                {
                    Serial.println("Direction R");
                }
            }
            
            break;
        
        case IR_MODE_CHANGE:

            refreshNeeded_ = true;
            paused_ = false;
            
            switch (lightMode_) {
                case t_LightMode::CONSTANT:
                    lightMode_ = t_LightMode::GRADIENT;
                    renderCyclesHold_ = RENDER_GRADIENT_NUM_CYCLES_HOLD_INIT;
                    cycleNr_ = 0;
                    hueBase_ = 0;
                    break;
            
                case t_LightMode::GRADIENT:
                    lightMode_ = t_LightMode::CHASE;
                    renderCyclesHold_ = RENDER_CHASE_NUM_CYCLES_HOLD_INIT;
                    cycleNr_ = 0;
                    hueBase_ = 0;
                    break;

                case t_LightMode::CHASE:
                    lightMode_ = t_LightMode::SPRITE;
                    renderCyclesHold_ = RENDER_SPRITES_NUM_CYCLES_HOLD_INIT;
                    cycleNr_ = 0;
                    break;

                case t_LightMode::SPRITE:
                    lightMode_ = t_LightMode::SPARKLE;
                    cycleNr_ = 0;
                    sparkleValue_ = RENDER_SPARKLE_LIGHTNESS_MIN;
                    sparkleColorIdx_ = 0;
                    sparkleState_ = 1;
                    break;

                case t_LightMode::SPARKLE:
                    lightMode_ = t_LightMode::CONSTANT;
                    break;
            }

            if (DEBUG_ON)
            {
                Serial.print("Light mode: ");
                Serial.println(lightMode_);
            }
            
            break;

        case IR_PLAY:
            if ( (lightMode_ == t_LightMode::GRADIENT) ||
                    (lightMode_ == t_LightMode::CHASE) ||
                    (lightMode_ == t_LightMode::SPRITE) ||
                    (lightMode_ == t_LightMode::SPARKLE) )
            {
                paused_ = false;
            }

            if (DEBUG_ON)
            {
                Serial.print("Pause: ");
                Serial.println(paused_);
            }
            
            break;
        
        case IR_PAUSE:
            if ( (lightMode_ == t_LightMode::GRADIENT) ||
                    (lightMode_ == t_LightMode::CHASE) ||
                    (lightMode_ == t_LightMode::SPRITE) ||
                    (lightMode_ == t_LightMode::SPARKLE) )
            {
                paused_ = true;
            }

            if (DEBUG_ON)
            {
                Serial.print("Pause: ");
                Serial.println(paused_);
            }
            
            break;

        case IR_RED:
            if (lightMode_ == t_LightMode::CONSTANT)
            {
                constantColor.setRGB(255, 0, 0);
                refreshNeeded_ = true;
            }
            break;

        case IR_GREEN:
            if (lightMode_ == t_LightMode::CONSTANT)
            {
                constantColor.setRGB(0, 255, 0);
                refreshNeeded_ = true;
            }
            break;

        case IR_YELLOW:
            if (lightMode_ == t_LightMode::CONSTANT)
            {
                constantColor.setRGB(255, 255, 0);
                refreshNeeded_ = true;
            }
            break;

        case IR_BLUE:
            if (lightMode_ == t_LightMode::CONSTANT)
            {
                constantColor.setRGB(0, 0, 255);
                refreshNeeded_ = true;
            }
            break;

        case IR_WHITE:
            if (lightMode_ == t_LightMode::CONSTANT)
            {
                constantColor.setRGB(255, 255, 255);
                refreshNeeded_ = true;
            }
            break;

    }
}

void switchDeviceState()
{
    switch (deviceState_)
    {
        case t_State::OFF:
            if (DEBUG_ON)
                Serial.println("Switching to state 'ON'");
            
            // Switch on
            deviceState_ = t_State::ON;

            FastLED.clear();
            brightness_ = BRIGHTNESS_ON;
            FastLED.setBrightness(brightness_);
            ledAtom_[0] = COLOR_ON;

            refreshNeeded_ = true;
            paused_ = false;
            cycleNr_ = 0;
            hueBase_ = 0;

            sparkleValue_ = RENDER_SPARKLE_LIGHTNESS_MIN;
            sparkleColorIdx_ = 0;
            sparkleState_ = 1;

            break;

        case t_State::ON:
            if (DEBUG_ON)
                Serial.println("Switching to state 'ECO'");
        
            // Switch to Eco
            deviceState_ = t_State::ECO;

            FastLED.clear();
            brightness_ = BRIGHTNESS_ECO;
            FastLED.setBrightness(brightness_);
            ledAtom_[0] = COLOR_ECO;

            refreshNeeded_ = true;
            paused_ = false;
            cycleNr_ = 0;
            hueBase_ = 0;

            break;
    
        case t_State::ECO:
            if (DEBUG_ON)
                Serial.println("Switching to state 'OFF'");
        
            // Switch off
            deviceState_ = t_State::OFF;

            fadeBrightness(brightness_, 0, 50);

            FastLED.clear();
            brightness_ = BRIGHTNESS_OFF;
            FastLED.setBrightness(brightness_);
            ledAtom_[0] = COLOR_OFF;
            FastLED.show();
            break;
    }
}

// -----------------------------------------------------------------------------
// Setup routine
// -----------------------------------------------------------------------------

void setup()
{
    delay(1000);
    
    if (DEBUG_ON)
    {
        Serial.begin(115200);
        Serial.print("RENDER_HUE_MAX = ");
        Serial.println(RENDER_HUE_MAX);
        Serial.print("RENDER_CHASE_HUE_STEP = ");
        Serial.println(RENDER_CHASE_HUE_STEP);
        Serial.print("RENDER_GRADIENT_HUE_STEP = ");
        Serial.println(RENDER_GRADIENT_HUE_STEP);
    }
  
    btn_.begin();  // initialize the button object
    
    FastLED.addLeds<NEOPIXEL, PIN_LEDATOM>(ledAtom_, 1);
    FastLED.addLeds<NEOPIXEL, PIN_LEDSTRIP>(ledStrip_, NUM_LEDS);
    FastLED.clear();
    FastLED.setBrightness(brightness_);
    ledAtom_[0] = COLOR_OFF;
    FastLED.show();

    startupAnimation();
    
    irRecv_.enableIRIn(); // Switch on IR receiver after initialization
}

// -----------------------------------------------------------------------------
// Main routine
// -----------------------------------------------------------------------------

void loop() {

    // Read IR command
    readIrCommand();

    // Flag to indicate that the IR button for the Off/On/Eco state has been activated
    bool irCmdOnOff = false;

    if (irCmdAvailable_)
    {
        irCmdOnOff = (irCmdValue_ == IR_ON_OFF);
    }

    // Read the device button state
    btn_.read();
    
    // Process change of the device state (Off/On/Eco)
    if (btn_.wasReleased() || irCmdOnOff)
    {
        switchDeviceState();
    }

    // Update LED strip colors while system is in state ON or ECO
    if (deviceState_ == t_State::ON || deviceState_ == t_State::ECO)
    {
        /* ---------- Process IR commands for color effects ---------- */
        if (irCmdAvailable_) {
            processUserInputColorEffects();
        }

        // Update colors of led strip according the active color effect and user settings
        switch (lightMode_) {
            case t_LightMode::CONSTANT:
                renderConstant();
                break;
        
            case t_LightMode::GRADIENT:
                renderGradient();
                break;
  
            case t_LightMode::CHASE:
                renderChase();
                break;

            case t_LightMode::SPRITE:
                renderSprite();
                break;

            case t_LightMode::SPARKLE:
                renderSparkle();
                break;
        }

    } // State is ON or ECO
  
    delay(TIME_CYCLE); // Pause before next pass through loop
}
