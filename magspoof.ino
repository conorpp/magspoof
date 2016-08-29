/*
 * MagSpoof - "wireless" magnetic stripe/credit card emulator
*
 * by Samy Kamkar
 *
 * http://samy.pl/magspoof/
 *
 * - Allows you to store all of your credit cards and magstripes in one device
 * - Works on traditional magstripe readers wirelessly (no NFC/RFID required)
 * - Can disable Chip-and-PIN (code not included)
 * - Correctly predicts Amex credit card numbers + expirations from previous card number (code not included)
 * - Supports all three magnetic stripe tracks, and even supports Track 1+2 simultaneously
 * - Easy to build using Arduino or ATtiny
 *
 */

#include <avr/sleep.h>
#include <avr/interrupt.h>

#define UPIN_A1 5
#define UPIN_B1 6
#define ENABLE_PIN1 4 // also green LED

#define UPIN_A2 10
#define UPIN_B2 9
#define ENABLE_PIN2 8 // also green LED

#define BUTTON_PIN 7
#define CLOCK_US 200

#define BETWEEN_ZERO 53 // 53 zeros between track1 & 2

#define TRACKS 2

// consts get stored in flash as we don't adjust them
const char* tracks[] = {
"%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0", // Track 1
";123456781234567=YYMMSSSDDDDDDDDDDDDDD?\0" // Track 2
};

struct track_profile
{
    uint8_t data[70];
    int data_size;
    int track_num;

    int chan_a;
    int chan_b;
    int en;

    int tmp, crc, lrc;
};

char revTrack[41];

const int sublen[] = {
  32, 48, 48 };
const int bitlen[] = {
  7, 5, 5 };

unsigned int curTrack = 0;
int dir;

void setup()
{
  pinMode(UPIN_A1, OUTPUT);
  pinMode(UPIN_B1, OUTPUT);
  pinMode(ENABLE_PIN1, OUTPUT);

  pinMode(UPIN_A2, OUTPUT);
  pinMode(UPIN_B2, OUTPUT);
  pinMode(ENABLE_PIN2, OUTPUT);

  
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // blink to show we started up
  blink(ENABLE_PIN1, 200, 3);

}

void blink(int pin, int msdelay, int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

// send a single bit out
void playBit(struct track_profile * tp, int sendBit)
{
  dir ^= 1;
  digitalWrite(tp->chan_a, dir);
  digitalWrite(tp->chan_b, !dir);
  delayMicroseconds(CLOCK_US);

  if (sendBit)
  {
    dir ^= 1;
    digitalWrite(tp->chan_a, dir);
    digitalWrite(tp->chan_b, !dir);
  }
  delayMicroseconds(CLOCK_US);

}

// when reversing
//void reverseTrack(int track)
//{
//  int i = 0;
//  track--; // index 0
//  dir = 0;
//
//  while (revTrack[i++] != '\0');
//  i--;
//  while (i--)
//    for (int j = bitlen[track]-1; j >= 0; j--)
//      playBit((revTrack[i] >> j) & 1);
//}

// plays out a full track, calculating CRCs and LRC
void playTrack(track_profile * tp1, track_profile * tp2)
{
  tp1->lrc = 0;
  tp2->lrc = 0;
  
  dir = 0;

  // enable H-bridge and LED
  digitalWrite(tp1->en, HIGH);
  digitalWrite(tp2->en, HIGH);

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
  {
    playBit(tp1, 0);
    playBit(tp2, 0);
  }

  //
  for (int i = 0; i < tp1->data_size; i++)
  {
    tp1->crc = 1;
    tp1->tmp = tp1->data[i] - sublen[tp1->track_num-1];
    if (i<tp2->data_size)
    {
      tp2->crc = 1;
      tp2->tmp = tp2->data[i] - sublen[tp2->track_num-1];
    }

    for (int j = 0; j < bitlen[tp1->track_num-1]-1; j++)
    {
      tp1->crc ^= tp1->tmp & 1;
      tp1->lrc ^= (tp1->tmp & 1) << j;
      playBit(tp1, tp1->tmp & 1);
      tp1->tmp >>= 1;

      if (i < tp2->data_size)
      {
         tp2->crc ^= tp2->tmp & 1;
         tp2->lrc ^= (tp2->tmp & 1) << j;
         playBit(tp2, tp2->tmp & 1);
         tp2->tmp >>= 1;
      }
      
    }
    playBit(tp1,tp1->crc);
    if (i<tp2->data_size)
    {
      playBit(tp2,tp2->crc);
    }
    
  }

  // finish calculating and send last "byte" (LRC)
  tp1->tmp = tp1->lrc;
  tp1->crc = 1;
  for (int j = 0; j < bitlen[tp1->track_num-1]-1; j++)
  {
    tp1->crc ^= tp1->tmp & 1;
    playBit(tp1,tp1->tmp & 1);
    tp1->tmp >>= 1;
  }
  playBit(tp1,tp1->crc);

  tp2->tmp = tp2->lrc;
  tp2->crc = 1;
  for (int j = 0; j < bitlen[tp2->track_num-1]-1; j++)
  {
    tp2->crc ^= tp2->tmp & 1;
    playBit(tp2,tp2->tmp & 1);
    tp2->tmp >>= 1;
  }
  playBit(tp2,tp2->crc);

  // if track 1, play 2nd track in reverse (like swiping back?)

  // finish with 0's
  for (int i = 0; i < 5 * 5; i++)
  {
    playBit(tp1, 0);
    playBit(tp2, 0);
  }

  digitalWrite(tp1->chan_a, LOW);
  digitalWrite(tp1->chan_a, LOW);
  digitalWrite(tp1->en, LOW);

  digitalWrite(tp2->chan_a, LOW);
  digitalWrite(tp2->chan_a, LOW);
  digitalWrite(tp2->en, LOW);
}

void loop()
{

  //for(int i=0;i<10;i++){playTrack(1+(curTrack++%2));delay(3000);}

  track_profile t1, t2;

  t1.track_num = 1;
  memmove(t1.data, tracks[0], strlen(tracks[0]));
  t1.data_size = strlen(tracks[0]);
  t1.chan_a = UPIN_A1;
  t1.chan_b = UPIN_B1;
  t1.en = ENABLE_PIN1;
  
  t2.track_num = 2;
  memmove(t2.data, tracks[1], strlen(tracks[1]));
  t2.data_size = strlen(tracks[1]);
  t2.chan_a = UPIN_A2;
  t2.chan_b = UPIN_B2;
  t2.en = ENABLE_PIN2;

  noInterrupts();
  while (digitalRead(BUTTON_PIN) == HIGH);
  while (digitalRead(BUTTON_PIN) == LOW);
  delay(50);
  playTrack(&t1, &t2);
  //playTrack(&t2);


}
