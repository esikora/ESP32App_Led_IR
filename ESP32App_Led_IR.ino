/**
    ESP32App_Led_IR.ino:
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

// External library: JC_Button, https://github.com/JChristensen/JC_Button
#include <JC_Button.h>

// External library: FastLED, https://github.com/FastLED/FastLED
#include <FastLED.h> 

// External library: IRRemoteESP8266, https://github.com/crankyoldgit/IRremoteESP8266
#include <IRrecv.h> 
#include <IRremoteESP8266.h>

// HW: Pin assignments
const byte PIN_BUTTON = 39; // M5Stack Atom Lite: internal button
const byte PIN_LEDATOM = 27; // M5Stack Atom Lite: internel Neopixel LED
const byte PIN_LEDSTRIP = 26; // M5SAtack Atom Lite: Grove connector GPIO pin (yellow cable) -> Neopixel LED strip
const byte PIN_IRRECV = 32; // M5SAtack Atom Lite: Grove connector GPIO pin (white cable) -> IR Receiver

// HW: Neopixel LED strip number of leds
const byte NUM_LEDS = 29;

// Type declarations
enum State {OFF = 0, ON = 1, ECO = 2}; // Main system states
typedef enum State t_State;

enum LightMode {CONSTANT = 0, GRADIENT = 1, CHASE = 2, SPRITE = 3}; // Light effects for LED strip
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
const bool DEBUG_ON = false;

// Status LED: color definitions
const uint8_t COLOR_OFF[3] = {255, 0, 0}; // System state: OFF
const uint8_t COLOR_ON[3]  = {0, 255, 0}; // System state: ON
const uint8_t COLOR_ECO[3] = {0, 255, 0}; // System state: ECO

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
const int RENDER_SPRITES_NUM_SPRITES_MAX = 10; // Maximum number of sprites

// Time constant, i.e. time after which light effects are updated
const int TIME_CYCLE = 50; // ms

// IR Commands (values depend on the remote control used)
const uint64_t IR_ON_OFF         = 0xFF30CF; // Stand-By/ON
const uint64_t IR_BRIGHTNESS_INC = 0xFFF00F; // Volume +
const uint64_t IR_BRIGHTNESS_DEC = 0xFF708F; // Volume -
const uint64_t IR_MODE_CHANGE    = 0xFF28D7; // Mode
const uint64_t IR_PLAY_PAUSE     = 0xFFA857; // Play/Pause
const uint64_t IR_SLOWER         = 0xFF10EF; // Slower
const uint64_t IR_FASTER         = 0xFF6897; // Faster
const uint64_t IR_LEFT           = 0xFFC03F; // Left
const uint64_t IR_RIGHT          = 0xFFA05F; // Right

// IR receiver library parameters
const uint16_t IR_BUFFER_SIZE = 1024;
const uint8_t IR_MSG_TIMEOUT = 15;

// -----------------------------------------------------------------------------
// Object and variable definitions
// -----------------------------------------------------------------------------

// Internal button
Button Btn(PIN_BUTTON);

// IR receiver
IRrecv IrRecv(PIN_IRRECV, IR_BUFFER_SIZE, IR_MSG_TIMEOUT, true);

// Buffer for decoded IR command
decode_results irCmd;

// IR command status
bool irCmdAvailable = false; // True, if new IR code has been received and decoded

// Last IR command received and decoded
uint64_t irCmdValue = 0;

// Internal LED controller
CRGB ledAtom[1];

// LED strip controller
CRGB ledStrip[NUM_LEDS];

// Sprite buffer
t_LedSprite sprites[RENDER_SPRITES_NUM_SPRITES_MAX];

// System state
t_State state = t_State::OFF;

// Light mode
t_LightMode lightMode = t_LightMode::CHASE;
uint8_t renderCyclesHold = RENDER_CHASE_NUM_CYCLES_HOLD_INIT;

// Cycle counter for led strip effects
int cycleNr = 0;

// Base hue for led strip effects
uint8_t hueBase = 0;

// Brightness factor for LED strip
uint8_t brightness = BRIGHTNESS_OFF;

// Refresh of LED strip needed
bool refreshNeeded = false;

// Color effect paused
bool paused   = false;

// Color effect direction
bool dirLeft  = true;

/**
 * Let the Led library show the set colors.
 */
void showLeds()
{
    FastLED.show();
    refreshNeeded = false;
}

/**
 * Led strip effect: Constant white light e.g. for reading.
 */
void renderConstant()
{
    // Do something only if there is a reason for refreshing such as changed brightness.
    if (refreshNeeded)
    {
        // Update color of each LED 
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED to white.
            ledStrip[ledNr].setHSV(0, 0, 255);
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
    if (cycleNr == 0 && !paused)
    {
        refreshNeeded = true;
        
        // Update each color of each LED 
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED
            ledStrip[ledNr].setHSV(hueBase, 255, 255);
        }

        // Cycle through color spectrum    
        hueBase += RENDER_GRADIENT_HUE_STEP;
    }

    // Show in case something has changed such as brightness or colors
    if (refreshNeeded)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr = (cycleNr + 1) % renderCyclesHold;
}

/**
 * Led strip effect: Each led shows a successive color of the color spectrum.
 * Colors move over time according to the set direction and set speed.
 */
void renderChase()
{
    // Update colors every n cycles (i.e. n procedure calls). Do not update colors if paused by user.
    if (cycleNr == 0 && !paused)
    {
        refreshNeeded = true;
      
        // Color of first LED
        uint8_t hueLed = hueBase;
                
        // Update each color of each LED 
        for (int ledNr = 0; ledNr < NUM_LEDS; ledNr++)
        {
            // Update color of the current LED
            ledStrip[ledNr].setHSV(hueLed, 255, 255);
    
            // Color of next LED
            hueLed += RENDER_CHASE_HUE_STEP;
        }

        // Cycle through color spectrum depending on the direction set by the user
        if (dirLeft)
        {
            hueBase += RENDER_CHASE_HUE_STEP;
        }
        else
        {
            hueBase -= RENDER_CHASE_HUE_STEP;
        }
    }

    // Show in case something has changed such as brightness or colors
    if (refreshNeeded)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr = (cycleNr + 1) % renderCyclesHold;
}

/**
 * Led strip effect: Sprites appear randomly in the middle of the led strip, move either to the left or right
 * and vanish at the border of the led strip. User can adjust the speed.
 */
void renderSprite()
{
    // Update colors every n cycles (i.e. n procedure calls). Do not update colors if paused by user.
    if (cycleNr == 0 && !paused)
    {
        refreshNeeded = true;

        // Generate a new sprite randomly
        if (random(0, 100) < RENDER_SPRITES_SPAWN_RATE)
        {
            t_LedSprite sp;
            sp.active = true;
            sp.activateAt = -1;
            sp.pos = NUM_LEDS / 2;
            sp.vel = 2 * random(0, 2) - 1;
            sp.color = CHSV(random(0, 255), random(128, 255), random(128, 255));

            // Insert the new sprite a free position of the sprite array
            for (int i = 0; i < RENDER_SPRITES_NUM_SPRITES_MAX; i++)
            {
                if (sprites[i].active == false)
                {
                    sprites[i] = sp;
                    break;
                }
            }
        }

        // Draw and update all active sprites
        drawAndUpdateSprites(0);
    }

    if (refreshNeeded)
    {
        showLeds();
    }

    // Increase the cycle number after each procedure call
    cycleNr = (cycleNr + 1) % renderCyclesHold;
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
        if (sprites[k].activateAt == stepNr)
            sprites[k].active = true;

        if (sprites[k].active)
        {
            // Set led colors based on active sprites
            if (sprites[k].pos >= 0 && sprites[k].pos < NUM_LEDS)
                ledStrip[sprites[k].pos] = sprites[k].color;

            // Move sprites
            sprites[k].pos += sprites[k].vel;

            // Deactivate sprites that moved outside the led strip
            if (sprites[k].pos < 0 || sprites[k].pos >= NUM_LEDS)
                sprites[k].active = false;
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
        ledStrip[ledNr] = CRGB(0, 0, 0);
    }
}

/**
 * Show an animtion at startup using the sprite functionlity.
 */
void startupAnimation()
{
    // Define a set of sprites
    sprites[0].active = true;
    sprites[0].activateAt = 0;
    sprites[0].pos = NUM_LEDS / 2;
    sprites[0].vel = 0;
    sprites[0].color = CRGB(255, 255, 0);
    
    sprites[1].active = false;
    sprites[1].activateAt = 5;
    sprites[1].pos = NUM_LEDS / 2 - 1;
    sprites[1].vel = -1;
    sprites[1].color = CRGB(32, 32, 128);
    
    sprites[2].active = false;
    sprites[2].activateAt = 5;
    sprites[2].pos = NUM_LEDS / 2 + 1;
    sprites[2].vel = +1;
    sprites[2].color = CRGB(32, 32, 128);
    
    sprites[3].active = false;
    sprites[3].activateAt = 10;
    sprites[3].pos = NUM_LEDS / 2 - 1;
    sprites[3].vel = -1;
    sprites[3].color = CRGB(128, 0, 0);
    
    sprites[4].active = false;
    sprites[4].activateAt = 10;
    sprites[4].pos = NUM_LEDS / 2 + 1;
    sprites[4].vel = +1;
    sprites[4].color = CRGB(128, 0, 0);

    sprites[5].active = false;
    sprites[5].activateAt = 15;
    sprites[5].pos = NUM_LEDS / 2 - 1;
    sprites[5].vel = -1;
    sprites[5].color = CRGB(0, 128, 0);
    
    sprites[6].active = false;
    sprites[6].activateAt = 15;
    sprites[6].pos = NUM_LEDS / 2 + 1;
    sprites[6].vel = +1;
    sprites[6].color = CRGB(0, 128, 0);

    // Do the sprite animation
    for (int stepNr = 0; stepNr < 30; stepNr++)
    {
        drawAndUpdateSprites(stepNr);
        FastLED.show();
        delay(100);
    }

    // Fade out
    fadeBrightness(brightness, 0, 50);

    // Invalidate all sprites and clear led strip
    clearSprites();
    clearLedStrip();

    // Reset led strip to its actual brightness
    FastLED.setBrightness(brightness);
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
        sprites[i].active = false;
        sprites[i].activateAt = -1;
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
  
    Btn.begin();  // initialize the button object
    
    FastLED.addLeds<NEOPIXEL, PIN_LEDATOM>(ledAtom, 1);
    FastLED.addLeds<NEOPIXEL, PIN_LEDSTRIP>(ledStrip, NUM_LEDS);
    FastLED.clear();
    FastLED.setBrightness(brightness);
    ledAtom[0].setRGB(COLOR_OFF[0], COLOR_OFF[1], COLOR_OFF[2]);
    FastLED.show();

    startupAnimation();
    
    IrRecv.enableIRIn(); // Switch on IR receiver after initialization
}

// -----------------------------------------------------------------------------
// Main routine
// -----------------------------------------------------------------------------

void loop() {

    /* ---------- Process power on/off commands ---------- */
    
    // Read the button state
    Btn.read();

    bool irCmdOnOff = false;

    // Determine the IR command state
    irCmdAvailable = IrRecv.decode(&irCmd);

    if (irCmdAvailable)
    {
        if (irCmd.repeat) // Is it a repetition of the previous IR command?
        {
            // Is repetition of the previous command allowed?
            bool irCmdRepeatable = (irCmdValue == IR_BRIGHTNESS_DEC) || (irCmdValue == IR_BRIGHTNESS_INC) ||
                 (irCmdValue == IR_SLOWER) || (irCmdValue == IR_FASTER);

            if (!irCmdRepeatable)
            {
                irCmdAvailable = false;
                irCmdValue = 0x0;
            }
        }
        else {
            // No repetition: retrieve the IR command
            irCmdValue = irCmd.value;
            irCmdOnOff = (irCmdValue == IR_ON_OFF);
        }
        
        IrRecv.resume();

        if (DEBUG_ON)
        {
            Serial.print("IR: ");
            Serial.println((unsigned long) irCmd.value, HEX);
        }
    }
    
    // Process events "button released" or "IR on/off" respectively
    if (Btn.wasReleased() || irCmdOnOff)
    {
        switch (state)
        {
            case t_State::OFF:
                if (DEBUG_ON)
                    Serial.println("Switching to state 'ON'");
                
                // Switch on
                state = t_State::ON;

                FastLED.clear();
                brightness = BRIGHTNESS_ON;
                FastLED.setBrightness(brightness);
                ledAtom[0].setRGB(COLOR_ON[0], COLOR_ON[1], COLOR_ON[2]);

                refreshNeeded = true;
                paused = false;
                cycleNr = 0;
                hueBase = 0;

                break;

            case t_State::ON:
                if (DEBUG_ON)
                    Serial.println("Switching to state 'ECO'");
            
                // Switch to Eco
                state = t_State::ECO;

                FastLED.clear();
                brightness = BRIGHTNESS_ECO;
                FastLED.setBrightness(brightness);
                ledAtom[0].setRGB(COLOR_ECO[0], COLOR_ECO[1], COLOR_ECO[2]);

                refreshNeeded = true;
                paused = false;
                cycleNr = 0;
                hueBase = 0;

                break;
        
            case t_State::ECO:
                if (DEBUG_ON)
                    Serial.println("Switching to state 'OFF'");
            
                // Switch off
                state = t_State::OFF;

                fadeBrightness(brightness, 0, 50);

                FastLED.clear();
                brightness = BRIGHTNESS_OFF;
                FastLED.setBrightness(brightness);
                ledAtom[0].setRGB(COLOR_OFF[0], COLOR_OFF[1], COLOR_OFF[2]);
                FastLED.show();
                break;
        }
    }

    // Update LED strip colors while system is in state ON or ECO
    if (state == t_State::ON || state == t_State::ECO)
    {
        /* ---------- Process IR commands for color effects ---------- */
        if (irCmdAvailable) {
          
            switch (irCmdValue) {
              
                case IR_BRIGHTNESS_INC:
                   brightness += BRIGHTNESS_STEP;
                   if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
                   FastLED.setBrightness(brightness);
                   refreshNeeded = true;

                   if (DEBUG_ON)
                   {
                      Serial.print("Brightness+: ");
                      Serial.println(brightness);
                   }
                   
                   break;
                
                case IR_BRIGHTNESS_DEC:
                   brightness -= BRIGHTNESS_STEP;
                   if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
                   FastLED.setBrightness(brightness);
                   refreshNeeded = true;

                   if (DEBUG_ON)
                   {
                      Serial.print("Brightness-: ");
                      Serial.println(brightness);
                   }
                   
                   break;

                case IR_SLOWER:
                   renderCyclesHold += RENDER_NUM_CYCLES_HOLD_STEP;
                   if (renderCyclesHold > RENDER_NUM_CYCLES_HOLD_MAX) renderCyclesHold = RENDER_NUM_CYCLES_HOLD_MAX;

                   if (DEBUG_ON)
                   {
                      Serial.print("Speed-: ");
                      Serial.println(0.001 * renderCyclesHold * TIME_CYCLE);
                   }
                   
                   break;

                case IR_FASTER:
                   renderCyclesHold -= RENDER_NUM_CYCLES_HOLD_STEP;
                   if (renderCyclesHold < RENDER_NUM_CYCLES_HOLD_MIN) renderCyclesHold = RENDER_NUM_CYCLES_HOLD_MIN;
                   paused = false;

                   if (DEBUG_ON)
                   {
                      Serial.print("Speed+: ");
                      Serial.println(0.001 * renderCyclesHold * TIME_CYCLE);
                   }
                   
                   break;

                case IR_LEFT:
                   if (lightMode == t_LightMode::CHASE && (!dirLeft || paused))
                   {                   
                      dirLeft = true;
                      paused = false;
                      cycleNr = 0;

                      if (DEBUG_ON)
                      {
                          Serial.println("Direction L");
                      }
                   }
                   
                   break;

                case IR_RIGHT:
                   if (lightMode == t_LightMode::CHASE && (dirLeft || paused))
                   {                   
                      dirLeft = false;
                      paused = false;
                      cycleNr = 0;

                      if (DEBUG_ON)
                      {
                          Serial.println("Direction R");
                      }
                   }
                   
                   break;
                
                case IR_MODE_CHANGE:

                    refreshNeeded = true;
                    paused = false;
                    
                    switch (lightMode) {
                        case t_LightMode::CONSTANT:
                            lightMode = t_LightMode::GRADIENT;
                            renderCyclesHold = RENDER_GRADIENT_NUM_CYCLES_HOLD_INIT;
                            cycleNr = 0;
                            hueBase = 0;
                            break;
                    
                        case t_LightMode::GRADIENT:
                            lightMode = t_LightMode::CHASE;
                            renderCyclesHold = RENDER_CHASE_NUM_CYCLES_HOLD_INIT;
                            cycleNr = 0;
                            hueBase = 0;
                            break;
      
                        case t_LightMode::CHASE:
                            lightMode = t_LightMode::SPRITE;
                            renderCyclesHold = RENDER_SPRITES_NUM_CYCLES_HOLD_INIT;
                            cycleNr = 0;
                            break;

                        case t_LightMode::SPRITE:
                            lightMode = t_LightMode::CONSTANT;
                            break;
                    }

                    if (DEBUG_ON)
                    {
                        Serial.print("Light mode: ");
                        Serial.println(lightMode);
                    }
                    
                    break;

                case IR_PLAY_PAUSE:
                    if ( (lightMode == t_LightMode::GRADIENT) || (lightMode == t_LightMode::CHASE) || (lightMode == t_LightMode::SPRITE))
                    {
                        paused = !paused;
                    }

                    if (DEBUG_ON)
                    {
                        Serial.print("Pause: ");
                        Serial.println(paused);
                    }
                    
                    break;
            }
        }

        // Update colors of led strip according the active color effect and user settings
        switch (lightMode) {
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
        }

    } // State is ON or ECO
  
    delay(TIME_CYCLE); // Pause before next pass through loop
}
