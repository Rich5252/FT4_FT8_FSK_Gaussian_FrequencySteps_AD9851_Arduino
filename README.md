# FT4_FT8_FSK_Gaussian_FrequencySteps_AD9851_Arduino
Clean high speed frequency transitions for FSK keying using Arduino and AD9851 DDS chip

Example stand alone code extracted from my TXLink project

Further improvements in FT4 & FT8 signal bandwidths with much more precise gaussian transitions. The slow exp function calcs using double type variables have been replaced with an interpolated look up table and special 64 bit integer calculations. This fully capitalises on the capabilities of the AD9851 DDS chip to make rapid phase continuous frequency changes with 0.0419Hz resolution that can now be made up to every 60 microsecs.
