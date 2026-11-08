/*
 * dBviewNeoPixels - Standing Wave Visualizer
 * 
 * A portable acoustic measurement tool for visualizing standing waves
 * and acoustic interference patterns in rooms using NeoPixel LEDs.
 * 
 * MIT License
 * 
 * Copyright (c) 2025 Mathieu (madees)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 */

#include <Arduino.h>

#include <Adafruit_CircuitPlayground.h>

#include <math.h>

#define MIC_PIN A4               

#define SAMPLE_RATE_HZ 1000      

const float sample_period_us = 1000000.0 / SAMPLE_RATE_HZ;

// PARAMÈTRES DU FILTRE (20Hz - 200Hz)

float target_frequency_hz = 100.0; 

float Q = 6.0; 

float b0, b1, b2, a1, a2;

float x_1 = 0, x_2 = 0, y_1 = 0, y_2 = 0;

// DEBUG MODE - Pour tester
#define DEBUG_MODE 0  // Mets à 1 pour tester, 0 pour fonctionnement normal

// PARAMÈTRES DU DB-MÈTRE

float reference_level = 1.0;     // Calculé dynamiquement post-filtre au démarrage

float manual_offset_db = 0.0;     // Modifié par les boutons (+/- 3dB)

const float DB_PER_LED = 3.0;

// --- LISSAGE IEC 61672-1 ---
// Fast (F) : time constant = 125 ms → atteint le nouveau niveau en ~0.6s
// Slow (S) : time constant = 1 s → atteint le nouveau niveau en ~5s

#define DT 0.001                  // Intervalle en secondes (1/1000Hz)
#define TAU_FAST 0.125            // 125 ms time constant
#define TAU_SLOW 1.0              // 1 s time constant

// Pré-calculer les facteurs de lissage
float SMOOTHING_FACTOR_FAST = 1.0 - exp(-DT / TAU_FAST);  // ≈ 0.00797
float SMOOTHING_FACTOR_SLOW = 1.0 - exp(-DT / TAU_SLOW);  // ≈ 0.000999

float current_smoothing_factor = SMOOTHING_FACTOR_FAST;  // Commence en Fast
float smoothed_db = 0.0;

// Définition de la structure pour le tableau RVB

struct ColorRGB {

  uint8_t r;

  uint8_t g;

  uint8_t b;

};

// TABLEAU RVB DE L'ÉCHELLE (10 NeoPixels)

const ColorRGB vu_colors[10] = {

  {32, 0, 32},     // LED 0 : violet (+0 dB)

  {64, 0, 64},     // LED 1 : violet     (+3 dB)

  {0, 0, 255},    // LED 2 : bleu foncé (+6 dB)

  {0, 204, 255},   // LED 3 : bleu clair (+9 dB)

  {0, 120, 0},   // LED 4 : vert foncé    (+12 dB) - PUR VERT

  {0, 255, 0},   // LED 5 : vert clair  (+15 dB)

  {255, 200, 0},   // LED 6 : jaune doré     (+18 dB)

  {255, 100, 0},   // LED 7 : orange vif  (+21 dB)

  {255, 40, 0},    // LED 8 : orange foncé  (+24 dB)

  {255, 0, 0}      // LED 9 : rouge pur      (+27 dB)

};

bool last_btn_left = false;

bool last_btn_right = false;

bool last_slide_switch = false;  // Pour détecter les changements FAST/SLOW

void computeBiquadCoefficients(float fc, float fs, float q) {

  float omega = 2.0 * PI * fc / fs;

  float alpha = sin(omega) / (2.0 * q);

  float cos_omega = cos(omega);

  float b0_raw = alpha;

  float b1_raw = 0.0;

  float b2_raw = -alpha;

  float a0_raw = 1.0 + alpha;

  float a1_raw = -2.0 * cos_omega;

  float a2_raw = 1.0 - alpha;

  b0 = b0_raw / a0_raw;

  b1 = b1_raw / a0_raw;

  b2 = b2_raw / a0_raw;

  a1 = a1_raw / a0_raw;

  a2 = a2_raw / a0_raw;

}

void calibrateOnBoot() {

  Serial.println("Calibration du bruit de fond... Ne faites pas de bruit.");

  // 3 flashs bleus au début
  for(int i=0; i<3; i++) {
    for(int j=0; j<10; j++) CircuitPlayground.setPixelColor(j, 0, 0, 100);
    delay(100);
    CircuitPlayground.clearPixels();
    delay(100);
  }
  
  delay(50);  // Stabilisation avant calibration
  
  float sum_magnitude = 0.0;

  int sample_count = 1000;

  for (int i = 0; i < sample_count; i++) {

    unsigned long start_time = micros();

    int raw_sample = analogRead(MIC_PIN) - 512;

    float x0 = (float)raw_sample;

    float y0 = (b0 * x0) + (b1 * x_1) + (b2 * x_2) - (a1 * y_1) - (a2 * y_2);

    x_2 = x_1; x_1 = x0; y_2 = y_1; y_1 = y0;

    sum_magnitude += abs(y0);

    while (micros() - start_time < sample_period_us) {}

  }

  reference_level = sum_magnitude / sample_count;

  if (reference_level < 0.5) reference_level = 0.5; 

  Serial.print("Calibration post-filtre terminee. Niveau SPL de base : ");

  Serial.println(reference_level);

  // 2 flashs bleus à la fin
  for(int i=0; i<2; i++) {
    for(int j=0; j<10; j++) CircuitPlayground.setPixelColor(j, 0, 0, 100);
    delay(100);
    CircuitPlayground.clearPixels();
    delay(100);
  }

}

void printFilterResponse() {
  // Affiche les coefficients du filtre pour vérification
  Serial.println("\n=== FILTER COEFFICIENTS ===");
  Serial.print("Fc (Hz): "); Serial.println(target_frequency_hz);
  Serial.print("Q: "); Serial.println(Q);
  Serial.print("b0: "); Serial.println(b0, 6);
  Serial.print("b1: "); Serial.println(b1, 6);
  Serial.print("b2: "); Serial.println(b2, 6);
  Serial.print("a1: "); Serial.println(a1, 6);
  Serial.print("a2: "); Serial.println(a2, 6);
  Serial.println("=========================\n");
}

void setup() {

  CircuitPlayground.begin();

  pinMode(MIC_PIN, INPUT);

  Serial.begin(115200);

  target_frequency_hz = constrain(target_frequency_hz, 20.0, 200.0);

  computeBiquadCoefficients(target_frequency_hz, SAMPLE_RATE_HZ, Q);

  calibrateOnBoot();

  printFilterResponse();

}

void loop() {

  unsigned long start_time = micros();

  // 1. Gestion des boutons (+/- 3dB sur l'échelle)

  bool current_btn_left = CircuitPlayground.leftButton();

  bool current_btn_right = CircuitPlayground.rightButton();

  if (current_btn_left && !last_btn_left) {

    manual_offset_db += 3.0;

    smoothed_db = 0.0;  // Reset pour réaction immédiate

    Serial.print("Offset: "); Serial.print(manual_offset_db); Serial.println(" dB");

  }

  if (current_btn_right && !last_btn_right) {

    manual_offset_db -= 3.0;

    smoothed_db = 0.0;  // Reset pour réaction immédiate

    Serial.print("Offset: "); Serial.print(manual_offset_db); Serial.println(" dB");

  }

  last_btn_left = current_btn_left;

  last_btn_right = current_btn_right;

  // 1b. Détection du mode Fast/Slow via le slide switch
  bool slide_switch = CircuitPlayground.slideSwitch();
  if (slide_switch) {
    current_smoothing_factor = SMOOTHING_FACTOR_SLOW;  // Slide vers la gauche = SLOW
  } else {
    current_smoothing_factor = SMOOTHING_FACTOR_FAST;  // Slide vers la droite = FAST
  }
  
  // Affiche le changement de mode seulement quand il change
  if (slide_switch != last_slide_switch) {
    Serial.println(slide_switch ? "Mode: SLOW" : "Mode: FAST");
    last_slide_switch = slide_switch;
  }

  // 2. Acquisition et Filtrage Passe-Bande

  int raw_sample = analogRead(MIC_PIN) - 512; 

  float x0 = (float)raw_sample;

  float y0 = (b0 * x0) + (b1 * x_1) + (b2 * x_2) - (a1 * y_1) - (a2 * y_2);

  x_2 = x_1; x_1 = x0;

  y_2 = y_1; y_1 = y0;

  // 3. Calcul des Décibels instantanés

  float magnitude = abs(y0);

  float instant_db = 0.0;

  if (magnitude > reference_level) {

    float raw_db = 20.0 * log10(magnitude / reference_level);

    instant_db = raw_db - manual_offset_db;

  }

  // 4. Lissage exponentiel (Filtre passe-bas sur l'affichage)

  // Plus SMOOTHING_FACTOR est petit, plus la transition vers la nouvelle valeur prend du temps.

  smoothed_db = (current_smoothing_factor * instant_db) + ((1.0 - current_smoothing_factor) * smoothed_db);

  // 5. Conversion de la valeur lissée en nombre de LED actives

  int leds_to_light = (int)(smoothed_db / DB_PER_LED);

  leds_to_light = constrain(leds_to_light, 0, 10);

  // 6. Affichage avec le tableau RVB personnalisé

  CircuitPlayground.clearPixels();

  for (int i = 0; i < leds_to_light; i++) {

    CircuitPlayground.setPixelColor(i, vu_colors[i].r, vu_colors[i].g, vu_colors[i].b);

  }

  // Visualisation sur le traceur série (mode DEBUG seulement)

  #if DEBUG_MODE
  Serial.print(" | Instantane_dB:"); Serial.print(instant_db);

  Serial.print(" | Lisse_dB:"); Serial.println(smoothed_db);
  #endif

  // Maintien du timing (1000 Hz)

  while (micros() - start_time < sample_period_us) {

    // Attente active

  }

}
 