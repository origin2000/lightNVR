#define _XOPEN_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "web/api_handlers_motion.h"
#include "web/api_handlers.h"
#include "web/request_response.h"
#include "web/httpd_utils.h"
#define LOG_COMPONENT "MotionAPI"
#include "core/logger.h"
#include "database/db_motion_config.h"
#include "utils/strings.h"
#include "video/onvif_motion_recording.h"
#include "video/unified_detection_thread.h"
#include <cjson/cJSON.h>

/**
 * Handler for POST /api/motion/test/:stream
 * Simulates a motion event for testing purposes
 */
void handle_test_motion_event(const http_request_t *req, http_response_t *res) {
    char stream_name[256] = {0};

    if (http_request_extract_path_param(req, "/api/motion/test/", stream_name, sizeof(stream_name)) != 0) {
        http_response_set_json_error(res, 400, "Invalid stream name");
        return;
    }

    log_info("POST /api/motion/test/%s", stream_name);

    // Trigger a motion event
    int rc = 0;
    unified_detection_notify_motion(stream_name, true);

    // Build response
    cJSON *response = cJSON_CreateObject();
    if (!response) {
        http_response_set_json_error(res, 500, "Failed to create JSON response");
        return;
    }
    cJSON_AddStringToObject(response, "stream_name", stream_name);
    cJSON_AddBoolToObject(response, "success", rc == 0);
    cJSON_AddStringToObject(response, "message", rc == 0 ? "Test motion event triggered" : "Failed to trigger test motion event");

    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json_str) {
        http_response_set_json(res, 200, json_str);
        free(json_str);
    } else {
        http_response_set_json_error(res, 500, "Failed to serialize JSON");
    }
}
