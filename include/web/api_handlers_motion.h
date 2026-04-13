/**
 * @file api_handlers_motion.h
 * @brief API handlers for motion recording management
 */

#ifndef API_HANDLERS_MOTION_H
#define API_HANDLERS_MOTION_H

#include "web/request_response.h"

/**
 * @brief Handler for POST /api/motion/test/:stream
 * Simulate an ONVIF motion event for testing
 */
void handle_test_motion_event(const http_request_t *req, http_response_t *res);


#endif /* API_HANDLERS_MOTION_H */

