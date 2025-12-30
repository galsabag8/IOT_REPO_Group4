#ifndef MADGWICK_ALGO_H
#define MADGWICK_ALGO_H

#include <Arduino.h>

// Expose variables needed by main (for screen mapping)
extern float q0, q1, q2, q3; 
extern float beta;

void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt);

#endif