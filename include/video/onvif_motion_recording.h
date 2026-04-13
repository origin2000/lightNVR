#ifndef LIGHTNVR_ONVIF_MOTION_RECORDING_H
#define LIGHTNVR_ONVIF_MOTION_RECORDING_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <libavformat/avformat.h>
#include "core/config.h"
#include "video/packet_buffer.h"

/**
 * ONVIF Motion Detection Recording Module
 * 
 * This module implements automated recording triggered by ONVIF motion detection events.
 * It provides:
 * - Event-based recording triggered by ONVIF motion events
 * - Configurable pre/post-event buffer recording
 * - Integration with existing LightNVR detection framework
 */

// Maximum number of motion events in queue
#define MAX_MOTION_EVENT_QUEUE 100

// Motion event structure
typedef struct {
    char stream_name[MAX_STREAM_NAME];
    time_t timestamp;
    char event_type[64];            // Type of motion event
    float confidence;               // Event confidence (if available)
    bool active;                    // Whether motion is currently active
    bool is_propagated;             // True if this event was already propagated from
                                    // another stream — prevents infinite cross-stream loops
} motion_event_t;

/**
 * Process a motion event (called by ONVIF detection system)
 * 
 * @param stream_name Name of the stream
 * @param motion_detected Whether motion was detected
 * @param timestamp Event timestamp
 * @return 0 on success, non-zero on failure
 */
int process_motion_event(const char *stream_name, bool motion_detected, time_t timestamp, bool is_propagated);

#endif /* LIGHTNVR_ONVIF_MOTION_RECORDING_H */

