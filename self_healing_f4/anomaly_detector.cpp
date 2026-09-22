#include "anomaly_detector.h"
#include <Arduino.h>
#include <cmath>

#define INPUT_SIZE   4
#define HIDDEN_SIZE  3
#define OUTPUT_SIZE  4   

static const float W_enc[3][4] = {
    { -1.211962f, 1.183310f, -0.001584f, -0.532579f },
    { 0.464873f, -0.350778f, 0.000204f, -1.906641f },
    { 1.202984f, -1.322724f, -0.000000f, 0.715308f }
};

static const float b_enc[] = { 1.140003f, -0.025462f, -0.904528f };

static const float W_dec[4][3] = {
    { -1.435416f, 0.390066f, 0.920213f },
    { 1.092849f, -0.451194f, -1.238099f },
    { 0.325190f, -0.049690f, 0.318127f },
    { -0.355703f, -1.747862f, 0.614971f }
};

static const float b_dec[] = { 0.484695f, -0.117156f, -0.302895f, 0.920696f };

static const float FEAT_MEAN[4] = {
    17.306667f,
    626.933333f,
    0.000000f,
    24.105000f
};

static const float FEAT_STD[4] = {
    24.415964f,
    244.159638f,
    500.000000f,
    15.000000f
};

static const float MSE_MAX = 0.727618f;

static inline float sigmoid(float x)
{
    return 1.0f / (1.0f + (float)std::exp((double)-x));
}

static inline float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

float calculate_anomaly_score(float* features)
{
    float x[INPUT_SIZE];
    for (int i = 0; i < INPUT_SIZE; i++)
    {
        x[i] = (features[i] - FEAT_MEAN[i]) / FEAT_STD[i];
    }

    float hidden[HIDDEN_SIZE];
    for (int h = 0; h < HIDDEN_SIZE; h++)
    {
        float sum = b_enc[h];
        for (int i = 0; i < INPUT_SIZE; i++)
        {
            sum += W_enc[h][i] * x[i];
        }
        hidden[h] = sigmoid(sum);
    }

    float reconstructed[OUTPUT_SIZE];
    for (int o = 0; o < OUTPUT_SIZE; o++)
    {
        float sum = b_dec[o];
        for (int h = 0; h < HIDDEN_SIZE; h++)
        {
            sum += W_dec[o][h] * hidden[h];
        }
        reconstructed[o] = sum;
    }

    float mse = 0.0f;
    for (int i = 0; i < INPUT_SIZE; i++)
    {
        float diff = x[i] - reconstructed[i];
        mse += diff * diff;
    }
    mse /= (float)INPUT_SIZE;

    float score = mse / MSE_MAX;
    score = clampf(score, 0.0f, 1.0f);

    return score;
}
