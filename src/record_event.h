#ifndef __record_event_h__
#define __record_event_h__

#include <Arduino.h>

struct NoteEvent {
  uint32_t timestamp;
  int8_t note;
};

#endif