#include "MadgwickAlgo.h"
#include <math.h>

// Global variables definition
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
float beta = 0.03f;

void MadgwickUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
  float recipNorm;
  float s0, s1, s2, s3;
  float qDot1, qDot2, qDot3, qDot4;
  float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

  qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
  qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
  qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
  qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

  if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
    recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    _2q0 = 2.0f * q0;
    _2q1 = 2.0f * q1;
    _2q2 = 2.0f * q2;
    _2q3 = 2.0f * q3;
    _4q0 = 4.0f * q0;
    _4q1 = 4.0f * q1;
    _4q2 = 4.0f * q2;
    _8q1 = 8.0f * q1;
    _8q2 = 8.0f * q2;
    q0q0 = q0 * q0;
    q1q1 = q1 * q1;
    q2q2 = q2 * q2;
    q3q3 = q3 * q3;

    s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
    s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
    s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
    s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
    recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
    s0 *= recipNorm;
    s1 *= recipNorm;
    s2 *= recipNorm;
    s3 *= recipNorm;

    qDot1 -= beta * s0;
    qDot2 -= beta * s1;
    qDot3 -= beta * s2;
    qDot4 -= beta * s3;
  }

  q0 += qDot1 * dt;
  q1 += qDot2 * dt;
  q2 += qDot3 * dt;
  q3 += qDot4 * dt;

  recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;
}

// Helper function to convert raw Gyro to Earth Frame Gyro
// Add this function prototype at the top and implementation at the bottom
void getEarthFrameGyro(float gx, float gy, float gz, float *wx, float *wy, float *wz) {
    // Convert raw gyro to quaternion form (pure quaternion with 0 scalar part)
    // q_gyro = (0, gx, gy, gz)
    
    // We need to perform the operation: q_out = q * q_gyro * q_conjugate
    // Where q is the current orientation from Madgwick
    
    // 1. Calculate q_conjugate
    float q0c = q0;
    float q1c = -q1;
    float q2c = -q2;
    float q3c = -q3;

    // 2. Multiply q * q_gyro (intermediate result t)
    // t = q x (0, gx, gy, gz)
    float t0 = -q1*gx - q2*gy - q3*gz;
    float t1 =  q0*gx + q2*gz - q3*gy;
    float t2 =  q0*gy - q1*gz + q3*gx;
    float t3 =  q0*gz + q1*gy - q2*gx;

    // 3. Multiply t * q_conjugate (result w)
    // w = t x q_conjugate
    // The scalar part (w0) should theoretically be 0, so we ignore it.
    *wx = t0*q1c + t1*q0c + t2*q3c - t3*q2c;
    *wy = t0*q2c - t1*q3c + t2*q0c + t3*q1c;
    *wz = t0*q3c + t1*q2c - t2*q1c + t3*q0c;
}