/**
 * @file test_mp4_writer_audio.c
 * @brief Layer 3 Unity regression tests for audio stream registration in
 *        mp4_writer_initialize().
 *
 * Regression for the bug fixed in PR #NNN: when the UDT triggers a detection
 * recording the audio stream must be declared in the MP4 output context
 * BEFORE avformat_write_header() is called.  Adding a stream afterwards is
 * illegal for MP4 containers and silently produced video-only files.
 *
 * The fix stores AAC AVCodecParameters in writer->pending_audio_codecpar
 * (set by udt_start_recording()) so that mp4_writer_initialize() can register
 * the audio stream during the same call that writes the container header.
 *
 * Tests:
 *   test_audio_stream_declared_before_header
 *       Simulates the UDT path: set pending_audio_codecpar, call
 *       mp4_writer_initialize() with a video keyframe, assert the output
 *       context has exactly 2 streams (video + audio) and that
 *       writer->audio.stream_idx is valid.
 *
 *   test_no_audio_when_pending_null
 *       Verifies that when pending_audio_codecpar is NULL (audio disabled or
 *       non-UDT path) the output has exactly 1 stream (video only).
 *
 *   test_audio_time_base_stored
 *       Verifies writer->audio.time_base is taken from pending_audio_time_base
 *       and not reconstructed from sample_rate.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#include "unity.h"
#include "video/mp4_writer.h"
#include "video/mp4_writer_internal.h"

/* ---- helpers ---- */

#define TMP_TEMPLATE "/tmp/test_mp4_writer_audio_XXXXXX"

static char g_tmp_path[256];

/* Create a temporary file path (not opened — avformat opens it) */
static void make_tmp_path(void) {
    char tmpl[] = TMP_TEMPLATE;
    int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);
    snprintf(g_tmp_path, sizeof(g_tmp_path), "%s.mp4", tmpl);
    /* remove the bare temp file; avformat will create the .mp4 */
    unlink(tmpl);
}

/* Build a minimal H.264 AVStream (no real SPS/PPS — enough for header) */
static AVFormatContext *make_input_ctx_with_h264(AVStream **vs_out) {
    AVFormatContext *ic = avformat_alloc_context();
    if (!ic) return NULL;

    AVStream *vs = avformat_new_stream(ic, NULL);
    if (!vs) { avformat_free_context(ic); return NULL; }

    vs->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vs->codecpar->codec_id   = AV_CODEC_ID_H264;
    vs->codecpar->width      = 640;
    vs->codecpar->height     = 480;
    vs->codecpar->codec_tag  = 0;
    vs->time_base            = (AVRational){1, 90000};

    if (vs_out) *vs_out = vs;
    return ic;
}

/* Build a minimal keyframe video packet */
static AVPacket *make_video_keyframe(void) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) return NULL;
    if (av_new_packet(pkt, 256) < 0) { av_packet_free(&pkt); return NULL; }
    memset(pkt->data, 0, 256);
    pkt->flags        |= AV_PKT_FLAG_KEY;
    pkt->stream_index  = 0;
    pkt->pts = pkt->dts = 0;
    return pkt;
}

/* Build AAC AVCodecParameters (simulates transcode_pcm_to_aac() output) */
static AVCodecParameters *make_aac_codecpar(int sample_rate) {
    AVCodecParameters *cp = avcodec_parameters_alloc();
    if (!cp) return NULL;
    cp->codec_type  = AVMEDIA_TYPE_AUDIO;
    cp->codec_id    = AV_CODEC_ID_AAC;
    cp->sample_rate = sample_rate;
    cp->bit_rate    = 32000;
    cp->codec_tag   = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 37, 100)
    av_channel_layout_default(&cp->ch_layout, 1);
#else
    cp->channels        = 1;
    cp->channel_layout  = AV_CH_LAYOUT_MONO;
#endif
    return cp;
}

/* ---- Unity boilerplate ---- */

void setUp(void) {
    make_tmp_path();
}

void tearDown(void) {
    unlink(g_tmp_path);
}

/* ================================================================
 * test: audio stream declared before header (UDT path)
 * ================================================================ */

void test_audio_stream_declared_before_header(void) {
    AVStream *vs = NULL;
    AVFormatContext *ic = make_input_ctx_with_h264(&vs);
    TEST_ASSERT_NOT_NULL(ic);

    mp4_writer_t *writer = mp4_writer_create(g_tmp_path, "test_stream");
    TEST_ASSERT_NOT_NULL(writer);

    /* Simulate udt_start_recording(): set pending params */
    writer->has_audio               = 1;
    writer->pending_audio_codecpar  = make_aac_codecpar(8000);
    writer->pending_audio_time_base = (AVRational){1, 8000};
    TEST_ASSERT_NOT_NULL(writer->pending_audio_codecpar);

    AVPacket *pkt = make_video_keyframe();
    TEST_ASSERT_NOT_NULL(pkt);

    int rc = mp4_writer_initialize(writer, pkt, vs);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "mp4_writer_initialize failed");

    /* Core assertion: both streams must be in the sealed header */
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)writer->output_ctx->nb_streams,
        "Expected 2 streams (video + audio) in MP4 output context");

    /* Audio stream index must be valid */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, writer->audio.stream_idx);

    /* Stream at audio index must be AUDIO type */
    TEST_ASSERT_EQUAL_INT(
        AVMEDIA_TYPE_AUDIO,
        writer->output_ctx->streams[writer->audio.stream_idx]->codecpar->codec_type);

    /* pending_audio_codecpar must be consumed (freed and NULLed) */
    TEST_ASSERT_NULL(writer->pending_audio_codecpar);

    av_packet_free(&pkt);
    mp4_writer_close(writer);
    avformat_free_context(ic);
}

/* ================================================================
 * test: no audio when pending_audio_codecpar is NULL
 * ================================================================ */

void test_no_audio_when_pending_null(void) {
    AVStream *vs = NULL;
    AVFormatContext *ic = make_input_ctx_with_h264(&vs);
    TEST_ASSERT_NOT_NULL(ic);

    mp4_writer_t *writer = mp4_writer_create(g_tmp_path, "test_stream_noaudio");
    TEST_ASSERT_NOT_NULL(writer);

    /* has_audio=1 but no pending params (non-UDT path, or audio disabled) */
    writer->has_audio              = 1;
    writer->pending_audio_codecpar = NULL;

    AVPacket *pkt = make_video_keyframe();
    TEST_ASSERT_NOT_NULL(pkt);

    int rc = mp4_writer_initialize(writer, pkt, vs);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "mp4_writer_initialize failed");

    /* Only video stream should be present */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)writer->output_ctx->nb_streams,
        "Expected 1 stream (video only) when pending_audio_codecpar is NULL");

    TEST_ASSERT_EQUAL_INT(-1, writer->audio.stream_idx);

    av_packet_free(&pkt);
    mp4_writer_close(writer);
    avformat_free_context(ic);
}

/* ================================================================
 * test: audio time_base comes from pending_audio_time_base
 * ================================================================ */

void test_audio_time_base_stored(void) {
    AVStream *vs = NULL;
    AVFormatContext *ic = make_input_ctx_with_h264(&vs);
    TEST_ASSERT_NOT_NULL(ic);

    mp4_writer_t *writer = mp4_writer_create(g_tmp_path, "test_stream_tb");
    TEST_ASSERT_NOT_NULL(writer);

    AVRational expected_tb = {1, 8000};
    writer->has_audio               = 1;
    writer->pending_audio_codecpar  = make_aac_codecpar(8000);
    writer->pending_audio_time_base = expected_tb;

    AVPacket *pkt = make_video_keyframe();
    TEST_ASSERT_NOT_NULL(pkt);

    int rc = mp4_writer_initialize(writer, pkt, vs);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "mp4_writer_initialize failed");

    /* writer->audio.time_base must match what we stored */
    TEST_ASSERT_EQUAL_INT(expected_tb.num, writer->audio.time_base.num);
    TEST_ASSERT_EQUAL_INT(expected_tb.den, writer->audio.time_base.den);

    av_packet_free(&pkt);
    mp4_writer_close(writer);
    avformat_free_context(ic);
}

/* ================================================================
 * test: zero sample_rate falls back to safe default (not {1,0})
 * ================================================================ */

void test_zero_sample_rate_uses_safe_default(void) {
    AVStream *vs = NULL;
    AVFormatContext *ic = make_input_ctx_with_h264(&vs);
    TEST_ASSERT_NOT_NULL(ic);

    mp4_writer_t *writer = mp4_writer_create(g_tmp_path, "test_stream_zerosr");
    TEST_ASSERT_NOT_NULL(writer);

    AVCodecParameters *cp = make_aac_codecpar(0); /* sample_rate = 0 */
    TEST_ASSERT_NOT_NULL(cp);

    writer->has_audio               = 1;
    writer->pending_audio_codecpar  = cp;
    writer->pending_audio_time_base = (AVRational){0, 1}; /* unset */

    AVPacket *pkt = make_video_keyframe();
    TEST_ASSERT_NOT_NULL(pkt);

    int rc = mp4_writer_initialize(writer, pkt, vs);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "mp4_writer_initialize failed");

    /* time_base.den must never be 0 */
    TEST_ASSERT_GREATER_THAN_INT(0, writer->audio.time_base.den);

    av_packet_free(&pkt);
    mp4_writer_close(writer);
    avformat_free_context(ic);
}

/* ================================================================
 * main
 * ================================================================ */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_audio_stream_declared_before_header);
    RUN_TEST(test_no_audio_when_pending_null);
    RUN_TEST(test_audio_time_base_stored);
    RUN_TEST(test_zero_sample_rate_uses_safe_default);
    return UNITY_END();
}
