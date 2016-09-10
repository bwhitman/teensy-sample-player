// MIDI Sample player for Teensy audio board
// Brian Whitman
// brian@variogr.am

#define DEAD_TIME_MS 6000
#define LOW_MIDI_KEY 29
#define HIGH_MIDI_KEY 89
#define MAX_POLY 12 

#include <Audio.h>
#include <SerialFlash.h>

AudioPlaySerialflashRaw  sample[MAX_POLY];
AudioConnection          *patch_cord[32];
AudioMixer4              l_sub_mixer[3];
AudioMixer4              r_sub_mixer[3];
AudioMixer4              l_mixer;
AudioMixer4              r_mixer;
AudioOutputI2S           i2s1;
AudioControlSGTL5000     sgtl5000_1;

// Mapping from MIDI key to sample file to play
// These RAW files are LSB first 16-bit signed ints at 22050Hz, and played back at 44KHz.
// This sample set fits almost exactly in 16MB on the serial flash.
// Use the CopyFromSD example to move the LRA files over to the flash.
// key 29 (lowest key) is 06.LRA (F1)
// end key (MIDI 89) is 66.LRA (F6)
const char * fl[] = {
  "06.LRA","07.LRA","08.LRA","09.LRA","10.LRA","11.LRA","12.LRA","13.LRA","14.LRA","15.LRA","16.LRA","17.LRA",
  "18.LRA","19.LRA","20.LRA","21.LRA","22.LRA","23.LRA","24.LRA","25.LRA","26.LRA","27.LRA","28.LRA","29.LRA",
  "30.LRA","31.LRA","32.LRA","33.LRA","34.LRA","35.LRA","36.LRA","37.LRA","38.LRA","39.LRA","40.LRA","41.LRA",
  "42.LRA","43.LRA","44.LRA","45.LRA","46.LRA","47.LRA","48.LRA","49.LRA","50.LRA","51.LRA","52.LRA","53.LRA",
  "54.LRA","55.LRA","56.LRA","57.LRA","58.LRA","59.LRA","60.LRA","61.LRA","62.LRA","63.LRA","64.LRA","65.LRA",
  "66.LRA"
};


void setup() {
  SPI.setMOSI(7);  // Audio shield has MOSI on pin 7
  SPI.setSCK(14);  // Audio shield has SCK on pin 14  
  SerialFlash.begin(6); // SerialFlash on teensy (via Audio board) is pin 6
  Serial1.begin(31250); // pin 0 from Teensy to MIDI out of the keyboard

  // Set up the audio connections: mixers have only 4 inputs so we create 3 mixers for each channel (12 samples at once)
  // and then send the output of the sub mixers to a main L/R mixer, and those to the main outs.
  byte patch_counter = 0;
  for(byte sub=0;sub<3;sub++) {
    for(byte voice=0;voice<4;voice++) {
      patch_cord[patch_counter++] = new AudioConnection(sample[(sub * 4) + voice], 0, l_sub_mixer[sub], voice);
      patch_cord[patch_counter++] = new AudioConnection(sample[(sub * 4) + voice], 0, r_sub_mixer[sub], voice);
    }
    patch_cord[patch_counter++] = new AudioConnection(l_sub_mixer[sub], 0, l_mixer, sub);
    patch_cord[patch_counter++] = new AudioConnection(r_sub_mixer[sub], 0, r_mixer, sub);
  }
  patch_cord[patch_counter++] = new AudioConnection(l_mixer, 0, i2s1, 0);
  patch_cord[patch_counter++] = new AudioConnection(r_mixer, 0, i2s1, 1);

  AudioMemory(50);
  sgtl5000_1.enable();
  sgtl5000_1.volume(1.0);
  delay(500);
}

// Map of which voice has which key playing
byte active[MAX_POLY];
// Map of when each voice started playing, for note stealing
long when[MAX_POLY];


byte get_free_voice() {
  // Look for a free voice and return it or steal one
  for(byte voice=0;voice<MAX_POLY;voice++) {
    if(active[voice] == 0) return voice;
  }
  // All are active, steal the oldest one
  long oldest_time = millis();
  byte oldest = 0;
  for(byte voice=0;voice<MAX_POLY;voice++) {
    if(when[voice] < oldest_time) {
      oldest = voice;
      oldest_time = when[voice];
    }
  }
  sample[oldest].stop();
  active[oldest] = 0;
  return oldest;
}


void note(int key, int velocity) {  
  // If out of range don't do anything
  if(key < LOW_MIDI_KEY || key > HIGH_MIDI_KEY) { return; }  

  // Block audio interrutps while we set the note(s) and mixer(s)
  AudioNoInterrupts();
  
  if(velocity) { // note on
    byte voice = get_free_voice();
    // Map the pan to the key position so it sounds like the note is coming from inside
    float l_multiplier = ((float)key - (float)LOW_MIDI_KEY) / (float) (HIGH_MIDI_KEY - LOW_MIDI_KEY + 1);
    float r_multiplier = 1.0 - l_multiplier; 
    byte sub = (voice / 4);
    l_sub_mixer[sub].gain(voice % 4, (2.0/MAX_POLY) * l_multiplier);
    r_sub_mixer[sub].gain(voice % 4, (2.0/MAX_POLY) * r_multiplier);
    active[voice] = key;  
    when[voice] = millis();
    // You need a patch on the Audio library to support half_SR playback
    // https://github.com/PaulStoffregen/Audio/pull/201
    sample[voice].play(fl[key-LOW_MIDI_KEY], true); 
  } else { // note off
    for(byte voice=0;voice<MAX_POLY;voice++) {
      if(active[voice] == key) {
        sample[voice].stop();
        active[voice] = 0;
      }
    }
  }
  // Make all the changes at once
  AudioInterrupts();
  
}

void kill_dead_notes() {
  // Should not be needed, but just in case
  for(byte voice=0;voice<MAX_POLY;voice++) {
    if( (active[voice] > 0) && ( millis() - when[voice] > DEAD_TIME_MS )) {
      sample[voice].stop();
      active[voice] = 0;
    }
  }
}

void loop() {
  int incomingByte, key, velocity;
  if (Serial1.available() > 0) {
    incomingByte = Serial1.read();
    if (incomingByte == 144) { // Note on
      while(!Serial1.available());
      key = Serial1.read();
      while(!Serial1.available());
      velocity = Serial1.read();
      note(key, velocity);
    }
  }
  kill_dead_notes();
}

