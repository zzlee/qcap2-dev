#include "qcap2.buffer.h"
#include "qcap2.user.h"
#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void test_qcap2_av_frame() {
    qcap2_av_frame_t frame;
    qcap2_av_frame_init(&frame);

    qcap2_av_frame_set_video_property(&frame, 1, 1920, 1080);
    ULONG color, width, height;
    qcap2_av_frame_get_video_property(&frame, &color, &width, &height);
    assert(color == 1);
    assert(width == 1920);
    assert(height == 1080);

    qcap2_av_frame_set_pts(&frame, 12345);
    int64_t pts;
    qcap2_av_frame_get_pts(&frame, &pts);
    assert(pts == 12345);

    qcap2_av_frame_set_buffer(&frame, (uint8_t*)0x1234, 1920*2);
    uint8_t* pBuf;
    int stride;
    qcap2_av_frame_get_buffer(&frame, &pBuf, &stride);
    assert(pBuf == (uint8_t*)0x1234);
    assert(stride == 1920*2);

    bool alloc_res = qcap2_av_frame_alloc_buffer(&frame, 1, 1);
    assert(alloc_res);
    qcap2_av_frame_get_buffer(&frame, &pBuf, &stride);
    assert(pBuf != NULL);
    qcap2_av_frame_free_buffer(&frame);
}

void test_qcap2_av_frame_alloc_buffer_layout() {
    qcap2_av_frame_t frame;
    qcap2_av_frame_init(&frame);

    qcap2_av_frame_set_video_property(&frame, QCAP_COLORSPACE_TYPE_NV12, 13, 7);
    assert(qcap2_av_frame_alloc_buffer(&frame, 16, 4));

    uint8_t* buffer[4];
    int stride[4];
    qcap2_av_frame_get_buffer1(&frame, buffer, stride);
    assert(buffer[0] != NULL);
    assert(buffer[1] != NULL);
    assert(buffer[2] == NULL);
    assert(stride[0] == 16);
    assert(stride[1] == 16);
    assert(buffer[1] == buffer[0] + 16 * 8);
    qcap2_av_frame_free_buffer(&frame);

    qcap2_av_frame_set_video_property(&frame, QCAP_COLORSPACE_TYPE_YV12, 13, 7);
    assert(qcap2_av_frame_alloc_buffer(&frame, 16, 4));
    qcap2_av_frame_get_buffer1(&frame, buffer, stride);
    assert(buffer[0] != NULL);
    assert(buffer[1] != NULL);
    assert(buffer[2] != NULL);
    assert(stride[0] == 16);
    assert(stride[1] == 16);
    assert(stride[2] == 16);
    assert(buffer[1] == buffer[0] + 16 * 8);
    assert(buffer[2] == buffer[1] + 16 * 4);
    qcap2_av_frame_free_buffer(&frame);

    qcap2_av_frame_set_video_property(&frame, QCAP_COLORSPACE_TYPE_H264, 1920, 1080);
    assert(!qcap2_av_frame_alloc_buffer(&frame, 16, 4));
}

void test_qcap2_av_packet() {
    qcap2_av_packet_t packet;
    qcap2_av_packet_init(&packet);

    qcap2_av_packet_set_property(&packet, 2, TRUE);
    int streamIndex;
    BOOL isKeyFrame;
    qcap2_av_packet_get_property(&packet, &streamIndex, &isKeyFrame);
    assert(streamIndex == 2);
    assert(isKeyFrame == TRUE);

    bool alloc_res = qcap2_av_packet_alloc_buffer(&packet, 1024);
    assert(alloc_res);
    uint8_t* pBuf;
    int size;
    qcap2_av_packet_get_buffer(&packet, &pBuf, &size);
    assert(pBuf != NULL);
    assert(size == 1024);
    qcap2_av_packet_free_buffer(&packet);
}

struct TestMyVideoFrame {
    int index;
    int free_resource_count;
    void* buffers[4];
    qcap2_av_frame_t av_frame;

    TestMyVideoFrame() : index(0), free_resource_count(0) {
        qcap2_av_frame_init(&av_frame);
        memset(buffers, 0, sizeof(buffers));
    }

    ~TestMyVideoFrame() {
        for (int i = 0; i < 4; ++i) {
            free(buffers[i]);
        }
    }

    static void on_free_resource(PVOID pData) {
        TestMyVideoFrame* pThis = qcap2_container_of(pData, TestMyVideoFrame, av_frame);
        assert((uintptr_t)pData == (uintptr_t)pThis + offsetof(TestMyVideoFrame, av_frame));
        assert(&pThis->av_frame == pData);
        uint8_t* buffer[4];
        int stride[4];
        qcap2_av_frame_get_buffer1(&pThis->av_frame, buffer, stride);
        assert(buffer[0] == pThis->buffers[0]);
        assert(buffer[1] == pThis->buffers[1]);
        assert(stride[0] == 16);
        assert(stride[1] == 16);
        pThis->free_resource_count++;
    }
};

static TestMyVideoFrame* test_new_video_frame_with_buffers() {
    TestMyVideoFrame* video_frame = new TestMyVideoFrame();
    assert(posix_memalign(&video_frame->buffers[0], 16, 16 * 4) == 0);
    assert(posix_memalign(&video_frame->buffers[1], 16, 16 * 2) == 0);

    uint8_t* buffer[4] = { (uint8_t*)video_frame->buffers[0], (uint8_t*)video_frame->buffers[1], NULL, NULL };
    int stride[4] = { 16, 16, 0, 0 };
    qcap2_av_frame_set_buffer1(&video_frame->av_frame, buffer, stride);
    return video_frame;
}

void test_qcap2_rcbuffer() {
    TestMyVideoFrame* video_frame = test_new_video_frame_with_buffers();
    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(&video_frame->av_frame, TestMyVideoFrame::on_free_resource);
    assert(rcbuf != NULL);
    assert(qcap2_rcbuffer_get_data(rcbuf) == &video_frame->av_frame);
    assert(qcap2_rcbuffer_use_count(rcbuf) == 1);
    assert(qcap2_rcbuffer_res_count(rcbuf) == 1);

    qcap2_rcbuffer_add_ref(rcbuf);
    assert(qcap2_rcbuffer_use_count(rcbuf) == 2);

    qcap2_rcbuffer_release(rcbuf);
    assert(qcap2_rcbuffer_use_count(rcbuf) == 1);
    assert(video_frame->free_resource_count == 0);

    qcap2_rcbuffer_release(rcbuf);
    assert(video_frame->free_resource_count == 1);
    delete video_frame;
}

void test_qcap2_rcbuffer_lock_pins_resource() {
    TestMyVideoFrame* video_frame = test_new_video_frame_with_buffers();
    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(&video_frame->av_frame, TestMyVideoFrame::on_free_resource);
    assert(rcbuf != NULL);

    void* data = qcap2_rcbuffer_lock_data(rcbuf);
    assert(data == &video_frame->av_frame);
    assert(qcap2_rcbuffer_res_count(rcbuf) == 2);

    qcap2_rcbuffer_release(rcbuf);
    assert(video_frame->free_resource_count == 0);
    assert(qcap2_rcbuffer_use_count(rcbuf) == 0);
    assert(qcap2_rcbuffer_res_count(rcbuf) == 1);

    qcap2_rcbuffer_unlock_data(rcbuf);
    assert(video_frame->free_resource_count == 1);
    delete video_frame;
}

void test_qcap2_rcbuffer_embedded_av_frame_free_callback() {
    TestMyVideoFrame* video_frame = test_new_video_frame_with_buffers();
    video_frame->index = 7;

    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(&video_frame->av_frame, TestMyVideoFrame::on_free_resource);
    assert(rcbuf != NULL);

    qcap2_av_frame_t* av_frame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(rcbuf);
    assert(av_frame == &video_frame->av_frame);
    assert((uintptr_t)av_frame == (uintptr_t)video_frame + offsetof(TestMyVideoFrame, av_frame));

    qcap2_rcbuffer_delete(rcbuf);
    assert(video_frame->free_resource_count == 1);
    delete video_frame;
}

void test_qcap2_rcbuffer_new_av_frame() {
    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new_av_frame();
    assert(rcbuf != NULL);

    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(rcbuf);
    assert(frame != NULL);
    assert(qcap2_rcbuffer_lock_data(rcbuf) == frame);
    qcap2_rcbuffer_unlock_data(rcbuf);

    qcap2_av_frame_set_video_property(frame, 3, 16, 4);
    qcap2_av_frame_set_buffer(frame, NULL, 16);
    assert(qcap2_av_frame_alloc_buffer(frame, 16, 1));

    uint8_t* buffer = NULL;
    int stride = 0;
    qcap2_av_frame_get_buffer(frame, &buffer, &stride);
    assert(buffer != NULL);
    assert(stride == 64);

    qcap2_rcbuffer_release(rcbuf);
}

void test_qcap2_rcbuffer_new_av_packet() {
    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new_av_packet();
    assert(rcbuf != NULL);

    qcap2_av_packet_t* packet = (qcap2_av_packet_t*)qcap2_rcbuffer_get_data(rcbuf);
    assert(packet != NULL);
    assert(qcap2_rcbuffer_lock_data(rcbuf) == packet);
    qcap2_rcbuffer_unlock_data(rcbuf);

    qcap2_av_packet_set_property(packet, 5, TRUE);
    assert(qcap2_av_packet_alloc_buffer(packet, 32));

    int streamIndex = 0;
    BOOL isKeyFrame = FALSE;
    qcap2_av_packet_get_property(packet, &streamIndex, &isKeyFrame);
    assert(streamIndex == 5);
    assert(isKeyFrame == TRUE);

    uint8_t* buffer = NULL;
    int size = 0;
    qcap2_av_packet_get_buffer(packet, &buffer, &size);
    assert(buffer != NULL);
    assert(size == 32);

    qcap2_rcbuffer_release(rcbuf);
}

int main() {
    test_qcap2_av_frame();
    test_qcap2_av_frame_alloc_buffer_layout();
    test_qcap2_av_packet();
    test_qcap2_rcbuffer();
    test_qcap2_rcbuffer_lock_pins_resource();
    test_qcap2_rcbuffer_embedded_av_frame_free_callback();
    test_qcap2_rcbuffer_new_av_frame();
    test_qcap2_rcbuffer_new_av_packet();
    printf("All tests passed!\n");
    return 0;
}
