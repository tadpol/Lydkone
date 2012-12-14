/*
Copyright 2009, Michael Conrad Tadpol Tilstra <tadpol@tadpol.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions, and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.    
*/
#include <stdint.h>

struct voice_s {
    uint8_t pos;
    uint8_t end;
    uint8_t vol;  // 0 thru 16, 16 is full volume, 0 is silence.
    uint8_t *note;
};

struct voice_s VoiceA;
struct voice_s VoiceB;
struct voice_s VoiceC;
struct voice_s *Voices[] = {&VoiceA, &VoiceB, &VoiceC};

uint8_t note_0[30] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14};
uint8_t note_1[27] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 12, 13, 13, 14, 14, 15};
uint8_t note_2[24] = {1, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 12, 13, 14, 14, 15};
uint8_t note_3[20] = {1, 1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11, 12, 13, 13, 14, 15};
uint8_t note_4[18] = {0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 10, 10, 11, 12, 13, 14, 15};
struct note_s {
    uint8_t len;
    uint8_t *wave;
};
struct note_s notes[5] = {
    {30,note_0},
    {27,note_1},
    {24,note_2},
    {20,note_3},
    {18,note_4}
};

uint8_t inline voiceUpdateAndSample(struct voice_s *v) // (???)
{
    if(v->vol > 0) {
        if(++v->pos >= v->end)
            v->pos = 0;
        return ((v->note[v->pos]) * v->vol) >> 4;
    }
    return 0;
}

void inline LoadDAC(uint8_t a, uint8_t b, uint8_t c) // (5.5u)
{
    PORTD = (a & 0x0f) | (b << 4);
    PORTB = (PINB & 0xf0) | (c & 0x0f); // need to leave high bits 'untouched'
}

/* called every 8kHz
 * estimated longest path usage puts this at using about 25% of the cpu.
 */
void inline every125u(void) // (3.63u)
{
    LoadDAC(voiceUpdateAndSample(&VoiceA),
            voiceUpdateAndSample(&VoiceB),
            voiceUpdateAndSample(&VoiceC) );
}

/********************************/
/*       period = 1/Fcpu  * pre * OCR
 * 8Hz:  125us  = 1/16MHz * 8 * 250
 * 16Hz: 62.5us = 1/16MHz * 8 * 125
 */
volatile byte OCR_reset = 250;
void setup_timer(void)
{
  OCR2A = OCR_reset;
  TCCR2A = _BV(WGM21); // all other bits are zero
  TCCR2B = _BV(CS21); // prescaler /8, other bits zero.
  TIMSK2 = _BV(OCIE2A);
}
ISR(TIMER2_COMPA_vect) {
  OCR2A = OCR_reset;
  every125u();
}

/********************************
 * This chunk below is a nice, fast way to read all six ADC inputs.
 * Useful if you need something more responsive than analog_read().
 */
volatile uint8_t adc_idx = 0;
volatile uint8_t adc_val[6]={11,12,13,14,15,16};

void setup_analog(void)
{
    ADMUX = _BV(REFS0) | _BV(ADLAR); // Default AREF, left adjusted, Select ADC input 0
    DIDR0 = _BV(ADC5D) | _BV(ADC4D) | _BV(ADC3D) | _BV(ADC2D) | _BV(ADC1D) | _BV(ADC0D); // only analog
    ADCSRB = 0; // Free Running Mode.
    ADCSRA = _BV(ADEN) | _BV(ADSC) | _BV(ADATE) | _BV(ADIE) | 7; // Enable ADC, Start conversion, AutoTriggered
}

ISR(ADC_vect) {
    int8_t temp;
    if( (temp=adc_idx-1) < 0) temp = 5;
    adc_val[temp] = 255 - ADCH;  // This needs to load one behind. Also, I flip the value to match how I use it.
    if(++adc_idx > 5) adc_idx = 0;
    ADMUX = _BV(REFS0) | _BV(ADLAR) | adc_idx; //  Default AREF, left adjusted, next ADC
}
/********************************/

/************************
 * Stuff below is outside of interrupts.
 */

/* Because of how the R2R works, we don't want to be putting any output unless
 * we are actually putting a wave on that ladder.  So this handles the switching
 * of the pin directions based on the voices.
 */
void voicePinMode(struct voice_s *v, byte on)
{
  if(&VoiceA == v) {
    if(on) DDRD |= 0x0f;
    else DDRD &= ~0x0f;
  }else
  if(&VoiceB == v) {
    if(on) DDRD |= 0xf0;
    else DDRD &= ~0xf0;
  }else
  if(&VoiceC == v) {
    if(on) DDRB |= 0x0f;
    else DDRB &= ~0x0f;
  }
}

void inline voiceVolume(struct voice_s *v, byte vol)
{
  if(vol < 16) 
    v->vol = vol;
  else
    v->vol = 16;
}

void voicePlayNote(struct voice_s *v, uint8_t note, uint8_t volume)
{
  if(v->note != notes[note].wave) { // if we are already playing this note, don't change it
    v->vol = 0; // stop playback while we change things.
    v->note = notes[note].wave;
    v->end = notes[note].len;
    v->pos = 0;
  }
  voiceVolume(v, volume);
  voicePinMode(v, 1);
}

void voiceSilence(struct voice_s *v)
{
    voicePinMode(v, 0);
    v->vol = 0;
}

/********************************/
/* Reading keys */

uint8_t inline rescale(uint8_t val, uint8_t vmax, uint8_t vmin, uint8_t nmax, uint8_t nmin)
{
  return (((uint16_t)(val - vmin) * (uint16_t)nmax) / (vmax - vmin)) + nmin;
}

uint8_t key_maxs[5]={200, 200, 200, 200, 200};
uint8_t key_mins[5]={60, 60, 60, 60, 60};

void inline self_cal(uint8_t idx, uint8_t nval)
{
  if(key_maxs[idx] < nval) key_maxs[idx] = nval;
  if(key_mins[idx] > nval) key_mins[idx] = nval;
}

void playKeys(void)
{
    uint8_t i, j=0, vol;

    // Overall pitch adjust
    OCR_reset = rescale(adc_val[0], key_maxs[0], key_mins[0], 255, 125);

    // Figure out which keys are pressed, and attach their note to a voice. (unless all voices used.)
    for(i=0; i<5; i++) {
        vol = adc_val[i+1];
        self_cal(i, vol);
        vol = rescale(vol, key_maxs[i], key_mins[i], 16, 0);
        if( vol > 8 && j<3 ) {
          voicePlayNote(Voices[j], i, vol);
          j++;
        }
    }
    // Silence any unused voices.
    for(;j<3;j++) {
      voiceSilence(Voices[j]);
    }
}

/********************************/
void free_blink(void)
{
    static long prev = 0;
    static uint8_t last = LOW;
    long cur = millis();
    
    if(cur - prev > 500) { // only change every 500ms
        prev = cur;
        if(last == LOW) {
            last = HIGH;
        }else{
            last = LOW;
        }
        digitalWrite(13,last);
    }
}

void setup(void)
{
  pinMode(13,OUTPUT);
  digitalWrite(13,LOW);
  setup_timer();
  setup_analog();
  
  voiceSilence(&VoiceA);
  voiceSilence(&VoiceB);
  voiceSilence(&VoiceC);
  
  delay(1000); // Give adc time to get going.
}

void loop(void)
{
  free_blink();
  playKeys();
}

/********************************/

