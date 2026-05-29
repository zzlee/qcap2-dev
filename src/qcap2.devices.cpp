#include "qcap2.devices_priv.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include <new>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <errno.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Destructor definition for qcap2_video_source_priv_t
qcap2_video_source_priv_t::~qcap2_video_source_priv_t() {
    qcap2_video_source_stop(reinterpret_cast<qcap2_video_source_t*>(this));
    if (queue) {
        qcap2_rcbuffer_queue_delete(queue);
    }
}

// Convert QCAP colorspace to V4L2 pixel format
static uint32_t qcap_colorspace_to_v4l2(ULONG colorspace) {
    switch (colorspace) {
    case QCAP_COLORSPACE_TYPE_YUY2: return V4L2_PIX_FMT_YUYV;
    case QCAP_COLORSPACE_TYPE_UYVY: return V4L2_PIX_FMT_UYVY;
    case QCAP_COLORSPACE_TYPE_NV12: return V4L2_PIX_FMT_NV12;
    case QCAP_COLORSPACE_TYPE_MJPG: return V4L2_PIX_FMT_MJPEG;
    case QCAP_COLORSPACE_TYPE_BGR24: return V4L2_PIX_FMT_BGR24;
    case QCAP_COLORSPACE_TYPE_RGB24: return V4L2_PIX_FMT_RGB24;
    default: return V4L2_PIX_FMT_YUYV;
    }
}

static void qcap2_v4l2_buffer_on_free(PVOID pData) {
    (void)pData;
}

struct qcap2_tpg_buffer_slot_t {
    uint8_t* raw_buffer;
    size_t buffer_size;
    qcap2_av_frame_t frame;
    qcap2_rcbuffer_t* rcbuf;
    class qcap2_tpg_video_source_backend_t* pSource;
};

class qcap2_tpg_video_source_backend_t : public qcap2_video_source_backend_t {
private:
    qcap2_video_source_priv_t* p;
    qcap2_tpg_buffer_slot_t* slots;
    int slot_count;
    std::thread* sim_thread;
    std::atomic<bool> running;
    std::mutex mtx;
    std::vector<qcap2_rcbuffer_t*> idle_buffers;
    uint64_t frame_index;

    static void sim_thread_func(qcap2_tpg_video_source_backend_t* self) {
        double fps = self->p->frame_rate > 0.0 ? self->p->frame_rate : 30.0;
        int interval_ms = (int)(1000.0 / fps);
        if (interval_ms <= 0) interval_ms = 33;

        auto next_time = std::chrono::steady_clock::now();

        while (self->running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_until(next_time);
            next_time += std::chrono::milliseconds(interval_ms);

            qcap2_rcbuffer_t* rcbuf = nullptr;
            {
                std::lock_guard<std::mutex> lock(self->mtx);
                if (!self->idle_buffers.empty()) {
                    rcbuf = self->idle_buffers.back();
                    self->idle_buffers.pop_back();
                }
            }

            if (rcbuf) {
                // Generate a simulated frame (e.g. solid color with frame index)
                PVOID pData = qcap2_rcbuffer_get_data(rcbuf);
                if (pData) {
                    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
                    uint8_t* pPixels = nullptr;
                    int stride = 0;
                    qcap2_av_frame_get_buffer(pFrame, &pPixels, &stride);

                    if (pPixels && stride > 0) {
                        uint8_t color_val = (uint8_t)((self->frame_index * 4) & 0xFF);
                        memset(pPixels, color_val, self->p->height * stride);
                    }

                    // Set PTS and sample time
                    uint64_t pts = self->frame_index * (1000000ULL / (uint64_t)fps);
                    qcap2_av_frame_set_pts(pFrame, pts);
                    qcap2_av_frame_set_sample_time(pFrame, (double)pts / 1000000.0);
                }

                self->frame_index++;

                // Push to the user queue (increases ref count)
                qcap2_rcbuffer_add_ref(rcbuf);
                QRESULT qres = qcap2_rcbuffer_queue_push(self->p->queue, rcbuf);
                if (qres != QCAP_RS_SUCCESSFUL) {
                    qcap2_rcbuffer_release(rcbuf);
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->idle_buffers.push_back(rcbuf);
                }
            }
        }
    }

public:
    qcap2_tpg_video_source_backend_t(qcap2_video_source_priv_t* owner)
        : p(owner), slots(nullptr), slot_count(0), sim_thread(nullptr), frame_index(0) {
        running.store(false);
    }

    ~qcap2_tpg_video_source_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        int count = p->frame_count > 0 ? p->frame_count : 4;
        slot_count = count;
        slots = new (std::nothrow) qcap2_tpg_buffer_slot_t[slot_count];
        if (!slots) return QCAP_RS_ERROR_GENERAL;

        ULONG width = p->width > 0 ? p->width : 640;
        ULONG height = p->height > 0 ? p->height : 480;
        ULONG color_space = p->color_space > 0 ? p->color_space : (ULONG)QCAP_COLORSPACE_TYPE_YUY2;

        int bytes_per_pixel = 2;
        if (color_space == QCAP_COLORSPACE_TYPE_BGR24 || color_space == QCAP_COLORSPACE_TYPE_RGB24) {
            bytes_per_pixel = 3;
        } else if (color_space == QCAP_COLORSPACE_TYPE_NV12) {
            bytes_per_pixel = 2;
        }

        int stride = width * bytes_per_pixel;
        size_t buffer_size = stride * height;

        {
            std::lock_guard<std::mutex> lock(mtx);
            idle_buffers.clear();
        }

        for (int i = 0; i < slot_count; ++i) {
            qcap2_tpg_buffer_slot_t* slot = &slots[i];
            slot->pSource = this;
            slot->buffer_size = buffer_size;
            slot->raw_buffer = new (std::nothrow) uint8_t[buffer_size];
            if (!slot->raw_buffer) {
                for (int j = 0; j < i; ++j) {
                    delete[] slots[j].raw_buffer;
                    qcap2_rcbuffer_delete(slots[j].rcbuf);
                }
                delete[] slots;
                slots = nullptr;
                slot_count = 0;
                return QCAP_RS_ERROR_GENERAL;
            }

            memset(slot->raw_buffer, 128, buffer_size);

            qcap2_av_frame_init(&slot->frame);
            qcap2_av_frame_set_video_property(&slot->frame, color_space, width, height);
            qcap2_av_frame_set_buffer(&slot->frame, slot->raw_buffer, stride);

            slot->rcbuf = qcap2_rcbuffer_new(&slot->frame, [](PVOID){});
            idle_buffers.push_back(slot->rcbuf);
        }

        running.store(true, std::memory_order_release);
        sim_thread = new std::thread(sim_thread_func, this);

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT stop() override {
        if (!running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        running.store(false, std::memory_order_release);
        if (sim_thread) {
            sim_thread->join();
            delete sim_thread;
            sim_thread = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            idle_buffers.clear();
        }

        if (slots) {
            for (int i = 0; i < slot_count; ++i) {
                delete[] slots[i].raw_buffer;
                qcap2_rcbuffer_delete(slots[i].rcbuf);
            }
            delete[] slots;
            slots = nullptr;
            slot_count = 0;
        }

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
        if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;

        PVOID pData = qcap2_rcbuffer_get_data(pRCBuffer);
        if (!pData) return QCAP_RS_ERROR_GENERAL;

        qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
        qcap2_tpg_buffer_slot_t* slot = qcap2_container_of(pFrame, qcap2_tpg_buffer_slot_t, frame);

        // Verify if it belongs to our slot list
        bool found = false;
        if (slots) {
            for (int i = 0; i < slot_count; ++i) {
                if (&slots[i] == slot) {
                    found = true;
                    break;
                }
            }
        }

        if (!found || slot->pSource != this) {
            return qcap2_rcbuffer_queue_push(p->queue, pRCBuffer);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            if (std::find(idle_buffers.begin(), idle_buffers.end(), pRCBuffer) == idle_buffers.end()) {
                idle_buffers.push_back(pRCBuffer);
            }
        }

        qcap2_rcbuffer_release(pRCBuffer);
        return QCAP_RS_SUCCESSFUL;
    }
};

class qcap2_user_video_source_backend_t : public qcap2_video_source_backend_t {
private:
    qcap2_video_source_priv_t* p;

public:
    qcap2_user_video_source_backend_t(qcap2_video_source_priv_t* owner)
        : p(owner) {}

    ~qcap2_user_video_source_backend_t() override = default;

    QRESULT start() override {
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT stop() override {
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
        if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
        return qcap2_rcbuffer_queue_push(p->queue, pRCBuffer);
    }
};

struct qcap2_v4l2_buffer_slot_t {
    int index;
    struct v4l2_buffer v4l2_buf;
    void* pMappedMemory;
    size_t nLength;
    qcap2_av_frame_t frame;
    qcap2_rcbuffer_t* rcbuf;
    class qcap2_v4l2_video_source_backend_t* pSource;
};

class qcap2_v4l2_video_source_backend_t : public qcap2_video_source_backend_t {
private:
    qcap2_video_source_priv_t* p;
    int fd;
    bool running;
    std::thread* capture_thread;
    std::atomic<bool> thread_running;
    qcap2_v4l2_buffer_slot_t* slots;
    int slot_count;

    static void capture_thread_func(qcap2_v4l2_video_source_backend_t* self) {
        struct pollfd pfd;
        pfd.fd = self->fd;
        pfd.events = POLLIN;

        while (self->thread_running.load(std::memory_order_acquire)) {
            int ret = poll(&pfd, 1, 50);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) continue;

            if (pfd.revents & POLLIN) {
                struct v4l2_buffer buf;
                memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                if (ioctl(self->fd, VIDIOC_DQBUF, &buf) < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    break;
                }

                if (buf.index < (uint32_t)self->slot_count) {
                    qcap2_v4l2_buffer_slot_t* slot = &self->slots[buf.index];
                    slot->v4l2_buf = buf;

                    qcap2_av_frame_set_pts(&slot->frame, buf.timestamp.tv_sec * 1000000LL + buf.timestamp.tv_usec);
                    qcap2_av_frame_set_sample_time(&slot->frame, buf.timestamp.tv_sec + buf.timestamp.tv_usec / 1000000.0);

                    qcap2_rcbuffer_add_ref(slot->rcbuf);

                    QRESULT qres = qcap2_rcbuffer_queue_push(self->p->queue, slot->rcbuf);
                    if (qres != QCAP_RS_SUCCESSFUL) {
                        qcap2_rcbuffer_release(slot->rcbuf);
                    }
                }
            }
        }
    }

public:
    qcap2_v4l2_video_source_backend_t(qcap2_video_source_priv_t* owner) 
        : p(owner), fd(-1), running(false), capture_thread(nullptr), slots(nullptr), slot_count(0) {
        thread_running.store(false);
    }

    ~qcap2_v4l2_video_source_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (running) return QCAP_RS_SUCCESSFUL;

        char dev_path[64];
        snprintf(dev_path, sizeof(dev_path), "/dev/video%d", p->device_index);

        fd = open(dev_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            return QCAP_RS_ERROR_GENERAL;
        }

        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = p->width > 0 ? p->width : 640;
        fmt.fmt.pix.height = p->height > 0 ? p->height : 480;
        fmt.fmt.pix.pixelformat = qcap_colorspace_to_v4l2(p->color_space);
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        p->width = fmt.fmt.pix.width;
        p->height = fmt.fmt.pix.height;
        int stride = fmt.fmt.pix.bytesperline;

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = p->frame_count > 0 ? p->frame_count : 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        slot_count = req.count;
        slots = new (std::nothrow) qcap2_v4l2_buffer_slot_t[slot_count];
        if (!slots) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        for (int i = 0; i < slot_count; ++i) {
            qcap2_v4l2_buffer_slot_t* slot = &slots[i];
            slot->index = i;
            slot->pSource = this;
            slot->rcbuf = nullptr;
            qcap2_av_frame_init(&slot->frame);

            memset(&slot->v4l2_buf, 0, sizeof(slot->v4l2_buf));
            slot->v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            slot->v4l2_buf.memory = V4L2_MEMORY_MMAP;
            slot->v4l2_buf.index = i;

            if (ioctl(fd, VIDIOC_QUERYBUF, &slot->v4l2_buf) < 0) {
                for (int j = 0; j < i; ++j) {
                    munmap(slots[j].pMappedMemory, slots[j].nLength);
                    if (slots[j].rcbuf) qcap2_rcbuffer_delete(slots[j].rcbuf);
                }
                delete[] slots;
                slots = nullptr;
                slot_count = 0;
                close(fd);
                fd = -1;
                return QCAP_RS_ERROR_GENERAL;
            }

            slot->nLength = slot->v4l2_buf.length;
            slot->pMappedMemory = mmap(NULL, slot->nLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, slot->v4l2_buf.m.offset);
            if (slot->pMappedMemory == MAP_FAILED) {
                for (int j = 0; j < i; ++j) {
                    munmap(slots[j].pMappedMemory, slots[j].nLength);
                    if (slots[j].rcbuf) qcap2_rcbuffer_delete(slots[j].rcbuf);
                }
                delete[] slots;
                slots = nullptr;
                slot_count = 0;
                close(fd);
                fd = -1;
                return QCAP_RS_ERROR_GENERAL;
            }

            qcap2_av_frame_set_video_property(&slot->frame, p->color_space, p->width, p->height);
            qcap2_av_frame_set_buffer(&slot->frame, (uint8_t*)slot->pMappedMemory, stride);

            slot->rcbuf = qcap2_rcbuffer_new(&slot->frame, qcap2_v4l2_buffer_on_free);
        }

        for (int i = 0; i < slot_count; ++i) {
            if (ioctl(fd, VIDIOC_QBUF, &slots[i].v4l2_buf) < 0) {
                for (int j = 0; j < slot_count; ++j) {
                    munmap(slots[j].pMappedMemory, slots[j].nLength);
                    if (slots[j].rcbuf) qcap2_rcbuffer_delete(slots[j].rcbuf);
                }
                delete[] slots;
                slots = nullptr;
                slot_count = 0;
                close(fd);
                fd = -1;
                return QCAP_RS_ERROR_GENERAL;
            }
        }

        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            for (int j = 0; j < slot_count; ++j) {
                munmap(slots[j].pMappedMemory, slots[j].nLength);
                if (slots[j].rcbuf) qcap2_rcbuffer_delete(slots[j].rcbuf);
            }
            delete[] slots;
            slots = nullptr;
            slot_count = 0;
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        running = true;
        thread_running.store(true, std::memory_order_release);
        capture_thread = new std::thread(capture_thread_func, this);
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT stop() override {
        if (!running) return QCAP_RS_SUCCESSFUL;

        running = false;
        if (thread_running.load(std::memory_order_acquire)) {
            thread_running.store(false, std::memory_order_release);
            if (capture_thread) {
                capture_thread->join();
                delete capture_thread;
                capture_thread = nullptr;
            }
        }

        if (fd >= 0) {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
        }

        if (slots) {
            for (int i = 0; i < slot_count; ++i) {
                qcap2_v4l2_buffer_slot_t* slot = &slots[i];
                if (slot->pMappedMemory && slot->pMappedMemory != MAP_FAILED) {
                    munmap(slot->pMappedMemory, slot->nLength);
                }
                if (slot->rcbuf) {
                    qcap2_rcbuffer_delete(slot->rcbuf);
                }
            }
            delete[] slots;
            slots = nullptr;
            slot_count = 0;
        }

        if (fd >= 0) {
            close(fd);
            fd = -1;
        }

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
        PVOID pData = qcap2_rcbuffer_get_data(pRCBuffer);
        if (!pData) return QCAP_RS_ERROR_GENERAL;

        qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
        qcap2_v4l2_buffer_slot_t* slot = qcap2_container_of(pFrame, qcap2_v4l2_buffer_slot_t, frame);

        if (slot->pSource != this) {
            return QCAP_RS_ERROR_GENERAL;
        }

        if (fd >= 0) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = slot->index;

            if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                return QCAP_RS_ERROR_GENERAL;
            }
        }

        qcap2_rcbuffer_release(pRCBuffer);
        return QCAP_RS_SUCCESSFUL;
    }
};

// qcap2_video_source_t implementation
qcap2_video_source_t* qcap2_video_source_new() {
    return reinterpret_cast<qcap2_video_source_t*>(new (std::nothrow) qcap2_video_source_priv_t());
}

void qcap2_video_source_delete(qcap2_video_source_t* pThis) {
    if (pThis) {
        delete reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
    }
}

void qcap2_video_source_set_backend_type(qcap2_video_source_t* pThis, int nBackendType) {
    if (pThis) {
        reinterpret_cast<qcap2_video_source_priv_t*>(pThis)->backend_type = nBackendType;
    }
}

void qcap2_video_source_set_frame_count(qcap2_video_source_t* pThis, int nFrameCount) {
    if (pThis) {
        reinterpret_cast<qcap2_video_source_priv_t*>(pThis)->frame_count = nFrameCount;
    }
}

void qcap2_video_source_set_event(qcap2_video_source_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_event(priv->queue, pEvent);
    }
}

void qcap2_video_source_set_auto_run(qcap2_video_source_t* pThis, bool bAutoRun) {
    (void)pThis; (void)bAutoRun;
}

void qcap2_video_source_set_video_format(qcap2_video_source_t* pThis, qcap2_video_format_t* pVideoFormat) {
    if (pThis && pVideoFormat) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        ULONG color_space = 0;
        ULONG width = 0;
        ULONG height = 0;
        BOOL interleaved = FALSE;
        double frame_rate = 0.0;
        qcap2_video_format_get_property(pVideoFormat, &color_space, &width, &height, &interleaved, &frame_rate);
        priv->color_space = color_space;
        priv->width = width;
        priv->height = height;
        priv->frame_rate = frame_rate;
    }
}

void qcap2_video_source_get_video_format(qcap2_video_source_t* pThis, qcap2_video_format_t* pVideoFormat) {
    if (pThis && pVideoFormat) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        qcap2_video_format_set_property(pVideoFormat, priv->color_space, priv->width, priv->height, FALSE, priv->frame_rate);
    }
}

void qcap2_video_source_set_buffers(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_buffers(priv->queue, pBuffers);
    }
}

void qcap2_video_source_set_device_index(qcap2_video_source_t* pThis, int nDeviceIndex) {
    if (pThis) {
        reinterpret_cast<qcap2_video_source_priv_t*>(pThis)->device_index = nDeviceIndex;
    }
}

void qcap2_video_source_set_stream_index(qcap2_video_source_t* pThis, int nStreamIndex) {
    if (pThis) {
        reinterpret_cast<qcap2_video_source_priv_t*>(pThis)->stream_index = nStreamIndex;
    }
}

void qcap2_video_source_set_src_ss_type(qcap2_video_source_t* pThis, int nSrcSSType) {
    (void)pThis; (void)nSrcSSType;
}

void qcap2_video_source_set_dst_ss_type(qcap2_video_source_t* pThis, int nDstSSType) {
    (void)pThis; (void)nDstSSType;
}

QRESULT qcap2_video_source_start(qcap2_video_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);

    if (priv->backend) return QCAP_RS_SUCCESSFUL;

    if (priv->backend_type == QCAP2_VIDEO_SOURCE_BACKEND_TYPE_V4L2) {
        priv->backend = new (std::nothrow) qcap2_v4l2_video_source_backend_t(priv);
    } else if (priv->backend_type == QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG) {
        priv->backend = new (std::nothrow) qcap2_tpg_video_source_backend_t(priv);
    } else {
        priv->backend = new (std::nothrow) qcap2_user_video_source_backend_t(priv);
    }

    if (!priv->backend) return QCAP_RS_ERROR_GENERAL;

    QRESULT res = priv->backend->start();
    if (res != QCAP_RS_SUCCESSFUL) {
        delete priv->backend;
        priv->backend = nullptr;
        return res;
    }

    return qcap2_rcbuffer_queue_start(priv->queue);
}

QRESULT qcap2_video_source_stop(qcap2_video_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);

    qcap2_rcbuffer_queue_stop(priv->queue);

    if (priv->backend) {
        priv->backend->stop();
        delete priv->backend;
        priv->backend = nullptr;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_source_run(qcap2_video_source_t* pThis) {
    (void)pThis;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_source_pop(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_pop(priv->queue, ppRCBuffer);
}

QRESULT qcap2_video_source_push(qcap2_video_source_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);

    if (priv->backend) {
        return priv->backend->push(pRCBuffer);
    }
    return qcap2_rcbuffer_queue_push(priv->queue, pRCBuffer);
}

// Destructor for qcap2_audio_source_priv_t
qcap2_audio_source_priv_t::~qcap2_audio_source_priv_t() {
    qcap2_audio_source_stop(reinterpret_cast<qcap2_audio_source_t*>(this));
    if (queue) {
        qcap2_rcbuffer_queue_delete(queue);
    }
}

struct qcap2_audio_payload_t {
    uint8_t* raw_buffer;
    qcap2_av_frame_t frame;
};

class qcap2_user_audio_source_backend_t : public qcap2_audio_source_backend_t {
private:
    qcap2_audio_source_priv_t* m_pOwner;
public:
    qcap2_user_audio_source_backend_t(qcap2_audio_source_priv_t* pOwner) : m_pOwner(pOwner) {}
    QRESULT start() override { return QCAP_RS_SUCCESSFUL; }
    QRESULT stop() override { return QCAP_RS_SUCCESSFUL; }
};

class qcap2_alsa_audio_source_backend_t : public qcap2_audio_source_backend_t {
private:
    qcap2_audio_source_priv_t* p;
    int fd;
    std::thread* capture_thread;
    std::atomic<bool> thread_running;

    static void capture_thread_func(qcap2_alsa_audio_source_backend_t* self) {
        ULONG channels = self->p->channels > 0 ? self->p->channels : 2;
        ULONG sample_fmt = self->p->sample_fmt > 0 ? self->p->sample_fmt : 16;
        ULONG sample_frequency = self->p->sample_frequency > 0 ? self->p->sample_frequency : 48000;
        int period_time = self->p->period_time > 0 ? self->p->period_time : 20;

        size_t nSamples = (sample_frequency * period_time) / 1000;
        size_t buffer_size = nSamples * channels * (sample_fmt / 8);
        if (buffer_size == 0) buffer_size = 4096;

        struct pollfd pfd;
        pfd.fd = self->fd;
        pfd.events = POLLIN;

        while (self->thread_running.load(std::memory_order_acquire)) {
            int ret = poll(&pfd, 1, 50);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) continue;

            if (pfd.revents & POLLIN) {
                qcap2_audio_payload_t* payload = new (std::nothrow) qcap2_audio_payload_t();
                if (!payload) continue;
                payload->raw_buffer = new (std::nothrow) uint8_t[buffer_size];
                if (!payload->raw_buffer) {
                    delete payload;
                    continue;
                }

                ssize_t bytes_read = read(self->fd, payload->raw_buffer, buffer_size);
                if (bytes_read <= 0) {
                    delete[] payload->raw_buffer;
                    delete payload;
                    if (bytes_read < 0 && errno == EAGAIN) continue;
                    break;
                }

                qcap2_av_frame_init(&payload->frame);
                qcap2_av_frame_set_audio_property(&payload->frame, channels, sample_fmt, sample_frequency, bytes_read);
                qcap2_av_frame_set_buffer(&payload->frame, payload->raw_buffer, bytes_read);

                qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(&payload->frame, [](PVOID pData) {
                    qcap2_audio_payload_t* pl = qcap2_container_of((qcap2_av_frame_t*)pData, qcap2_audio_payload_t, frame);
                    delete[] pl->raw_buffer;
                    delete pl;
                });

                if (rcbuf) {
                    QRESULT qres = qcap2_rcbuffer_queue_push(self->p->queue, rcbuf);
                    if (qres != QCAP_RS_SUCCESSFUL) {
                        qcap2_rcbuffer_release(rcbuf);
                    }
                }
            }
        }
    }

public:
    qcap2_alsa_audio_source_backend_t(qcap2_audio_source_priv_t* owner)
        : p(owner), fd(-1), capture_thread(nullptr) {
        thread_running.store(false);
    }

    ~qcap2_alsa_audio_source_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (thread_running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        char dev_path[64];
        snprintf(dev_path, sizeof(dev_path), "/dev/snd/pcmC%dD%dc", p->card, p->device);

        fd = open(dev_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            return QCAP_RS_ERROR_GENERAL;
        }

        thread_running.store(true, std::memory_order_release);
        capture_thread = new (std::nothrow) std::thread(capture_thread_func, this);
        if (!capture_thread) {
            close(fd);
            fd = -1;
            thread_running.store(false);
            return QCAP_RS_ERROR_GENERAL;
        }

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT stop() override {
        if (!thread_running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        thread_running.store(false, std::memory_order_release);
        if (capture_thread) {
            capture_thread->join();
            delete capture_thread;
            capture_thread = nullptr;
        }

        if (fd >= 0) {
            close(fd);
            fd = -1;
        }

        return QCAP_RS_SUCCESSFUL;
    }
};

class qcap2_tpg_audio_source_backend_t : public qcap2_audio_source_backend_t {
private:
    qcap2_audio_source_priv_t* p;
    std::thread* sim_thread;
    std::atomic<bool> running;
    uint64_t total_samples_generated;

    static void sim_thread_func(qcap2_tpg_audio_source_backend_t* self) {
        ULONG channels = self->p->channels > 0 ? self->p->channels : 2;
        ULONG sample_fmt = self->p->sample_fmt > 0 ? self->p->sample_fmt : 16;
        ULONG sample_frequency = self->p->sample_frequency > 0 ? self->p->sample_frequency : 48000;
        int period_time = self->p->period_time > 0 ? self->p->period_time : 20;

        size_t nSamples = (sample_frequency * period_time) / 1000;
        size_t buffer_size = nSamples * channels * (sample_fmt / 8);
        if (buffer_size == 0) buffer_size = 4096;

        auto next_time = std::chrono::steady_clock::now();

        while (self->running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_until(next_time);
            next_time += std::chrono::milliseconds(period_time);

            qcap2_audio_payload_t* payload = new (std::nothrow) qcap2_audio_payload_t();
            if (!payload) continue;

            payload->raw_buffer = new (std::nothrow) uint8_t[buffer_size];
            if (!payload->raw_buffer) {
                delete payload;
                continue;
            }

            // Generate 440Hz sine wave tone
            if (sample_fmt == 16) {
                int16_t* pSamples = (int16_t*)payload->raw_buffer;
                for (size_t i = 0; i < nSamples; ++i) {
                    double t = (double)(self->total_samples_generated + i) / (double)sample_frequency;
                    int16_t val = (int16_t)(32767.0 * sin(2.0 * M_PI * 440.0 * t));
                    for (ULONG c = 0; c < channels; ++c) {
                        pSamples[i * channels + c] = val;
                    }
                }
            } else {
                memset(payload->raw_buffer, 0, buffer_size);
            }

            self->total_samples_generated += nSamples;

            qcap2_av_frame_init(&payload->frame);
            qcap2_av_frame_set_audio_property(&payload->frame, channels, sample_fmt, sample_frequency, buffer_size);
            qcap2_av_frame_set_buffer(&payload->frame, payload->raw_buffer, buffer_size);

            // Set PTS and sample time
            uint64_t pts = (self->total_samples_generated - nSamples) * 1000000ULL / sample_frequency;
            qcap2_av_frame_set_pts(&payload->frame, pts);
            qcap2_av_frame_set_sample_time(&payload->frame, (double)pts / 1000000.0);

            qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(&payload->frame, [](PVOID pData) {
                qcap2_audio_payload_t* pl = qcap2_container_of((qcap2_av_frame_t*)pData, qcap2_audio_payload_t, frame);
                delete[] pl->raw_buffer;
                delete pl;
            });

            if (rcbuf) {
                QRESULT qres = qcap2_rcbuffer_queue_push(self->p->queue, rcbuf);
                if (qres != QCAP_RS_SUCCESSFUL) {
                    qcap2_rcbuffer_release(rcbuf);
                }
            }
        }
    }

public:
    qcap2_tpg_audio_source_backend_t(qcap2_audio_source_priv_t* owner)
        : p(owner), sim_thread(nullptr), total_samples_generated(0) {
        running.store(false);
    }

    ~qcap2_tpg_audio_source_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        running.store(true, std::memory_order_release);
        sim_thread = new (std::nothrow) std::thread(sim_thread_func, this);
        if (!sim_thread) {
            running.store(false);
            return QCAP_RS_ERROR_GENERAL;
        }

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT stop() override {
        if (!running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        running.store(false, std::memory_order_release);
        if (sim_thread) {
            sim_thread->join();
            delete sim_thread;
            sim_thread = nullptr;
        }

        return QCAP_RS_SUCCESSFUL;
    }
};

// qcap2_audio_source_t implementation
qcap2_audio_source_t* qcap2_audio_source_new() {
    return reinterpret_cast<qcap2_audio_source_t*>(new (std::nothrow) qcap2_audio_source_priv_t());
}

void qcap2_audio_source_delete(qcap2_audio_source_t* pThis) {
    if (pThis) {
        delete reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
    }
}

void qcap2_audio_source_set_backend_type(qcap2_audio_source_t* pThis, int nBackendType) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->backend_type = nBackendType;
    }
}

void qcap2_audio_source_set_frame_count(qcap2_audio_source_t* pThis, int nFrameCount) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->frame_count = nFrameCount;
    }
}

void qcap2_audio_source_set_event(qcap2_audio_source_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_event(priv->queue, pEvent);
    }
}

void qcap2_audio_source_set_audio_format(qcap2_audio_source_t* pThis, qcap2_audio_format_t* pAudioFormat) {
    if (pThis && pAudioFormat) {
        qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
        qcap2_audio_format_get_property(pAudioFormat, &priv->channels, &priv->sample_fmt, &priv->sample_frequency);
    }
}

void qcap2_audio_source_get_audio_format(qcap2_audio_source_t* pThis, qcap2_audio_format_t* pAudioFormat) {
    if (pThis && pAudioFormat) {
        qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
        qcap2_audio_format_set_property(pAudioFormat, priv->channels, priv->sample_fmt, priv->sample_frequency);
    }
}

void qcap2_audio_source_set_buffers(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    if (pThis) {
        qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_buffers(priv->queue, pBuffers);
    }
}

void qcap2_audio_source_set_period_time(qcap2_audio_source_t* pThis, int nPeriodTime) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->period_time = nPeriodTime;
    }
}

void qcap2_audio_source_set_buffer_time(qcap2_audio_source_t* pThis, int nBufferTime) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->buffer_time = nBufferTime;
    }
}

void qcap2_audio_source_set_ideal_timer(qcap2_audio_source_t* pThis, bool bIdealTimer) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->ideal_timer = bIdealTimer;
    }
}

void qcap2_audio_source_set_card(qcap2_audio_source_t* pThis, int nCard) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->card = nCard;
    }
}

void qcap2_audio_source_set_device(qcap2_audio_source_t* pThis, int nDevice) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_source_priv_t*>(pThis)->device = nDevice;
    }
}

QRESULT qcap2_audio_source_start(qcap2_audio_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);

    if (priv->backend) return QCAP_RS_SUCCESSFUL;

    if (priv->backend_type == QCAP2_AUDIO_SOURCE_BACKEND_TYPE_ALSA) {
        priv->backend = new (std::nothrow) qcap2_alsa_audio_source_backend_t(priv);
    } else if (priv->backend_type == QCAP2_AUDIO_SOURCE_BACKEND_TYPE_TPG) {
        priv->backend = new (std::nothrow) qcap2_tpg_audio_source_backend_t(priv);
    } else {
        priv->backend = new (std::nothrow) qcap2_user_audio_source_backend_t(priv);
    }

    if (!priv->backend) return QCAP_RS_ERROR_GENERAL;

    QRESULT res = priv->backend->start();
    if (res != QCAP_RS_SUCCESSFUL) {
        delete priv->backend;
        priv->backend = nullptr;
        return res;
    }

    return qcap2_rcbuffer_queue_start(priv->queue);
}

QRESULT qcap2_audio_source_stop(qcap2_audio_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);

    qcap2_rcbuffer_queue_stop(priv->queue);

    if (priv->backend) {
        priv->backend->stop();
        delete priv->backend;
        priv->backend = nullptr;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_source_pop(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_pop(priv->queue, ppRCBuffer);
}

extern "C" {

// V4L2 APIs
void qcap2_video_source_set_v4l2_name(qcap2_video_source_t* pThis, const char* strName) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        if (strName) {
            strncpy(priv->v4l2_name, strName, sizeof(priv->v4l2_name) - 1);
            priv->v4l2_name[sizeof(priv->v4l2_name) - 1] = '\0';
        }
    }
}

const char* qcap2_video_source_get_v4l2_name(qcap2_video_source_t* pThis) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        return priv->v4l2_name;
    }
    return nullptr;
}

void qcap2_video_source_set_buf_type(qcap2_video_source_t* pThis, enum v4l2_buf_type nBufType) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        priv->v4l2_buf_type_val = nBufType;
    }
}

void qcap2_video_source_set_memory(qcap2_video_source_t* pThis, enum v4l2_memory nMemory) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        priv->v4l2_memory_val = nMemory;
    }
}

void qcap2_video_source_set_exp_buf(qcap2_video_source_t* pThis, bool bExpBuf) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        priv->v4l2_exp_buf = bExpBuf;
    }
}

QRESULT qcap2_video_source_get_fd(qcap2_video_source_t* pThis, int* pFd) {
    if (pThis && pFd) {
        *pFd = -1;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_INVALID_PARAMETER;
}

void qcap2_video_source_set_v4l2_sg_name(qcap2_video_source_t* pThis, int index, const char* strName) {
    if (pThis && index >= 0 && index < 16) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        if (strName) {
            strncpy(priv->v4l2_sg_names[index], strName, sizeof(priv->v4l2_sg_names[index]) - 1);
            priv->v4l2_sg_names[index][sizeof(priv->v4l2_sg_names[index]) - 1] = '\0';
        }
    }
}

const char* qcap2_video_source_get_v4l2_sg_name(qcap2_video_source_t* pThis, int index) {
    if (pThis && index >= 0 && index < 16) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        return priv->v4l2_sg_names[index];
    }
    return nullptr;
}

}
