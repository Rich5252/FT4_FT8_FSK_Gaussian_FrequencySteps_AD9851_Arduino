#include "AD9851.h"

//DDS chip
#define AD9851_FQ_UD_PIN      2
#define AD9851_RESET_PIN      3
// And MOSI=11, SCK=13            //NOTE these drivers need to use SPI

class MyAD9851 : public AD9851<AD9851_RESET_PIN, AD9851_FQ_UD_PIN> {};
MyAD9851 dds;


long gTranSteps = 128;       //+/- 10% tone_duration, eack step 0.5ms (tone_duration = 160ms)
long gTranMicrosPerStep = 250;         //per gaussian transition step
int gTranInterpFactor = gTranSteps / 32;     //number of interpolation steps per increment of LookUpTable (LUT)

//double dbl_gTranSteps = gTranSteps;
//double dbl_gXincr = dbl_gXstart / dbl_gTranSteps;       //incr for gaussian formulae

/**
 * @brief Gaussian Pulse Shaping Lookup Table (LUT).
 * * Contains 32 steps representing the rising curve of the Gaussian function, 
 * Fractional increments scaled up by 2^16 (65536) for high-speed integer interpolation.
 * index[0] = 0 to simplify interpolation. Will scan table from 1 to 32 using
 *    an additional binary factor to interpolate between [i-1] to [i]
 * Used for FT4/FT8 FTW transition calculations.
 */
const uint16_t Gaussian_G_Scaled[33] = {
    0, 518, 705, 948, 1264, 1667, 2177, 2815, 3602, 
    4565, 5726, 7110, 8741, 10638, 12817, 15288, 18052, 
    21102, 24421, 27978, 31732, 35630, 39604, 43582, 47477, 
    51203, 54668, 57782, 60461, 62631, 64228, 65206, 65535 
};

//key and TX power control (see circuit diagram)
#define TXlevelPin  5   //PWM out for DDS output level
#define KeyIn       8   //active low
#define KeyOut      7   //active low
#define TXout       9   //active hi
#define ExtPAout    4     //PPT on external PA, pin active high
#define FanPin      10      // enable fan active hi


void setup() {
  // put your setup code here, to run once:
Serial.begin(115200);
while (!Serial) ;
Serial.println("{~TX}");

 // setup control pins
   pinMode(TXout, OUTPUT);
   digitalWrite(TXout, LOW);
   pinMode(ExtPAout, OUTPUT);
   digitalWrite(ExtPAout, LOW); 
   pinMode(KeyIn, INPUT_PULLUP);
   pinMode(KeyOut, OUTPUT);
   digitalWrite(KeyOut, HIGH);
   pinMode(FanPin, OUTPUT);
   analogWrite(FanPin,0);
   delay(500);                        //needed for proper start at zero - don't know why!
   //analogWrite(FanPin,FanPWMCur);

  //CalcFreqCorrection(20);       //set calibration at nominal 20deg
  
  analogWrite(TXlevelPin,  255);    //0 = max out, 255 =min
delay(100);
}



void loop() {
  gTranSteps = 256;       //+/- 10% tone_duration, eack step 0.5ms (tone_duration = 160ms)
  gTranMicrosPerStep = 64;         //per gaussian transition step
  gTranInterpFactor = gTranSteps / 32;     //number of interpolation steps per increment of LookUpTable (LUT)

  #define FT4
  #ifdef FT4
gTranSteps = 128;       //+/- 16% tone_duration, eack step 0.5ms (tone_duration = 48ms)
gTranMicrosPerStep = 80;         //per gaussian transition step
gTranInterpFactor = gTranSteps / 32;     //number of interpolation steps per increment of LookUpTable (LUT)
  #endif


      long lastFTW = 668257756;
      //long nextFTW = 1044;        //6.25*7 Hz in FTW units
      long nextFTW = lastFTW + 1491;        //20.8333 * 3 Hz in FTW units
      bool PowerUp = false;
const uint32_t ROUNDING_OFFSET = 32768UL; // 2^(16-1) for rounding during division by 2^16
    uint32_t gStart = micros();
    uint32_t t = 0;

//gaussian transition to next tone
      uint32_t next_gStep_time = micros();
      // Gaussian LookUpTable version
      // LUT contains UINT16_t gaussian fractions values scaled up by x2^16
      // There are 32 increments in table (33 elements starting at 0, ending at 1 / 2^16 (-1))
      // Remember that freq goes up and down so take care of signs
      int32_t gTranChangeFTW = (int32_t) (nextFTW - lastFTW); //+/- Freq Transition expressed in AD9851 FTW units
  
      // top level loop scans Gaussian_G_Scaled[] LookupTable
      for (int g = 1; g <= gTranSteps / gTranInterpFactor ; g++)      //LUT[0] = 0 plus 32 increments
      {                                                               //for all steps in LUT
        //sp(gTranChangeFTW);

        //Prog mem for array to save on RAM
        uint16_t G_scaled_current = pgm_read_word(&Gaussian_G_Scaled[g]);
        uint16_t G_scaled_previous = pgm_read_word(&Gaussian_G_Scaled[g - 1]);

        // 1. Find the gaussian step size for interpolation
        uint16_t gLUTDelta = G_scaled_current - G_scaled_previous;

        // Find the step size for the interpolation (this assumes gTranInterpFactor is power of 2)
        uint16_t gLUTStepDeltaIncr = gLUTDelta / gTranInterpFactor;

        //inner loop interpolates between LUT points.
        //gTranInterpFactor is a binary number equal to the interpolation points required
        for (int h = 1; h <= gTranInterpFactor; h++)
        {                                                   //interpolate additional gTranInterpFactor steps
          // calc interpolated gaussian fraction
          uint16_t gLUTinterp = G_scaled_previous + gLUTStepDeltaIncr * h;

          // calc FTW change for this step (using 64-bit multiply and 2^16 shift to re-scale LUT fractions
          int64_t FTW_CHANGE_SCALED = (uint64_t) gLUTinterp * (int64_t) gTranChangeFTW;   //signed scaled change

          // Apply rounding and divide by 2^16 to derive FTW change for this step
          int32_t gTranFTW_delta = (int32_t)((FTW_CHANGE_SCALED + ROUNDING_OFFSET) >> 16);
          
          // new transition step frequency
          uint32_t gTranFTW = lastFTW + gTranFTW_delta;
          dds.setFTW(gTranFTW, PowerUp);   //set DDS output
        
        //calc end time for this step
         next_gStep_time += gTranMicrosPerStep;
/*
        
        Serial.print((micros()-next_gStep_time)/gTranMicrosPerStep); Serial.print(" ");
        sp(g); sp(h); sp(G_scaled_previous); sp(G_scaled_current);
        Serial.print(gLUTStepDeltaIncr); Serial.print(" ");
        Serial.print(gLUTinterp); Serial.print(" ");
        sp(gTranChangeFTW); sp((uint32_t)FTW_SCALED); sp(freeRam());
        Serial.println(gTranFTW);
*/
         //delay to next time increment
         while (micros() < next_gStep_time) {}

         t++;
        }
      
      }      
    
    Serial.print(t); Serial.print(" "); Serial.println(micros() - gStart);
    while (true) { delay(10); }    
  }

template <typename T>
void sp(T msg)
{
    Serial.print(msg); Serial.print(" ");

}
