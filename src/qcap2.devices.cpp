#include "qcap2.devices_priv.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include "qcap2.dmabuf.h"
#include "qcap2.drm.h"
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
#include <stdlib.h>

#if HAVE_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm.h>
#endif

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

static void qcap2_v4l2_buffer_on_free(PVOID pData);

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
                QRESULT qres = qcap2_rcbuffer_queue_push(self->p->queue, rcbuf);
                if (qres != QCAP_RS_SUCCESSFUL) {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->idle_buffers.push_back(rcbuf);
                } else {
                    qcap2_rcbuffer_release(rcbuf);
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
            for (auto buf : idle_buffers) {
                qcap2_rcbuffer_release(buf);
            }
            idle_buffers.clear();
        }

        if (slots) {
            for (int i = 0; i < slot_count; ++i) {
                delete[] slots[i].raw_buffer;
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
                qcap2_rcbuffer_add_ref(pRCBuffer);
                idle_buffers.push_back(pRCBuffer);
            }
        }

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
    void* pUserPtr;
    int dma_fd;
    size_t nLength;
    qcap2_av_frame_t frame;
    qcap2_rcbuffer_t* rcbuf;
    class qcap2_v4l2_video_source_backend_t* pSource;
    bool bIsQueued;
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

    void cleanup_slots(int up_to_index) {
        if (slots) {
            for (int j = 0; j < up_to_index; ++j) {
                qcap2_v4l2_buffer_slot_t* slot = &slots[j];
                if (slot->pMappedMemory && slot->pMappedMemory != MAP_FAILED) {
                    munmap(slot->pMappedMemory, slot->nLength);
                }
                if (slot->pUserPtr) {
                    free(slot->pUserPtr);
                }
                qcap2_dmabuf_t* pDMABuf = nullptr;
                if (qcap2_av_frame_get_dmabuf(&slot->frame, &pDMABuf) == QCAP_RS_SUCCESSFUL && pDMABuf) {
                    free(pDMABuf);
                }
                if (slot->dma_fd >= 0) {
                    close(slot->dma_fd);
                }
                if (slot->rcbuf) {
                    qcap2_rcbuffer_delete(slot->rcbuf);
                }
            }
            delete[] slots;
            slots = nullptr;
        }
        slot_count = 0;
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

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
                buf.memory = self->p->v4l2_memory_val;

                if (ioctl(self->fd, VIDIOC_DQBUF, &buf) < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    break;
                }

                if (buf.index < (uint32_t)self->slot_count) {
                    qcap2_v4l2_buffer_slot_t* slot = &self->slots[buf.index];
                    slot->v4l2_buf = buf;
                    slot->bIsQueued = false;

                    qcap2_av_frame_set_pts(&slot->frame, buf.timestamp.tv_sec * 1000000LL + buf.timestamp.tv_usec);
                    qcap2_av_frame_set_sample_time(&slot->frame, buf.timestamp.tv_sec + buf.timestamp.tv_usec / 1000000.0);

                    qcap2_rcbuffer_queue_push(self->p->queue, slot->rcbuf);
                    qcap2_rcbuffer_release(slot->rcbuf);
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
        size_t buffer_size = fmt.fmt.pix.sizeimage;

        enum v4l2_memory mem_type = static_cast<enum v4l2_memory>(p->v4l2_memory_val);
        if (mem_type == V4L2_MEMORY_DMABUF) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = p->frame_count > 0 ? p->frame_count : 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = mem_type;

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
            slot->pMappedMemory = nullptr;
            slot->pUserPtr = nullptr;
            slot->dma_fd = -1;
            slot->nLength = buffer_size;
            slot->bIsQueued = false;
            qcap2_av_frame_init(&slot->frame);

            memset(&slot->v4l2_buf, 0, sizeof(slot->v4l2_buf));
            slot->v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            slot->v4l2_buf.memory = mem_type;
            slot->v4l2_buf.index = i;

            if (mem_type == V4L2_MEMORY_MMAP) {
                if (ioctl(fd, VIDIOC_QUERYBUF, &slot->v4l2_buf) < 0) {
                    cleanup_slots(i);
                    return QCAP_RS_ERROR_GENERAL;
                }

                slot->nLength = slot->v4l2_buf.length;
                slot->pMappedMemory = mmap(NULL, slot->nLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, slot->v4l2_buf.m.offset);
                if (slot->pMappedMemory == MAP_FAILED) {
                    cleanup_slots(i);
                    return QCAP_RS_ERROR_GENERAL;
                }

                qcap2_av_frame_set_video_property(&slot->frame, p->color_space, p->width, p->height);
                qcap2_av_frame_set_buffer(&slot->frame, (uint8_t*)slot->pMappedMemory, stride);

                if (p->v4l2_exp_buf) {
                    struct v4l2_exportbuffer expbuf;
                    memset(&expbuf, 0, sizeof(expbuf));
                    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    expbuf.index = i;
                    expbuf.flags = O_CLOEXEC | O_RDWR;
                    if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0) {
                        cleanup_slots(i);
                        return QCAP_RS_ERROR_GENERAL;
                    }
                    slot->dma_fd = expbuf.fd;
                }
            }
            else if (mem_type == V4L2_MEMORY_USERPTR) {
                void* ptr = nullptr;
                if (posix_memalign(&ptr, 4096, buffer_size) != 0) {
                    cleanup_slots(i);
                    return QCAP_RS_ERROR_GENERAL;
                }
                slot->pUserPtr = ptr;
                slot->v4l2_buf.m.userptr = (unsigned long)slot->pUserPtr;
                slot->v4l2_buf.length = slot->nLength;

                qcap2_av_frame_set_video_property(&slot->frame, p->color_space, p->width, p->height);
                qcap2_av_frame_set_buffer(&slot->frame, (uint8_t*)slot->pUserPtr, stride);
            }


            if (slot->dma_fd >= 0) {
                qcap2_dmabuf_t* pDMABuf = (qcap2_dmabuf_t*)calloc(1, sizeof(qcap2_dmabuf_t));
                if (pDMABuf) {
                    pDMABuf->fd = slot->dma_fd;
                    pDMABuf->dmabuf_size = slot->nLength;
                    pDMABuf->pVirAddr = slot->pMappedMemory;
                    pDMABuf->nSize = slot->nLength;
                    qcap2_av_frame_set_dmabuf(&slot->frame, pDMABuf);
                }
            }

            slot->rcbuf = qcap2_rcbuffer_new(&slot->frame, qcap2_v4l2_buffer_on_free);
        }

        for (int i = 0; i < slot_count; ++i) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = mem_type;
            buf.index = i;
            if (mem_type == V4L2_MEMORY_USERPTR) {
                buf.m.userptr = (unsigned long)slots[i].pUserPtr;
                buf.length = slots[i].nLength;
            }

            if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                cleanup_slots(slot_count);
                return QCAP_RS_ERROR_GENERAL;
            }
            slots[i].bIsQueued = true;
        }

        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            cleanup_slots(slot_count);
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

        cleanup_slots(slot_count);
        return QCAP_RS_SUCCESSFUL;
    }

    void requeue_slot(qcap2_v4l2_buffer_slot_t* slot) {
        if (!slot->bIsQueued) {
            if (fd >= 0) {
                struct v4l2_buffer buf;
                memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = p->v4l2_memory_val;
                buf.index = slot->index;

                if (buf.memory == V4L2_MEMORY_USERPTR) {
                    buf.m.userptr = (unsigned long)slot->pUserPtr;
                    buf.length = slot->nLength;
                }

                ioctl(fd, VIDIOC_QBUF, &buf);
            }
            slot->bIsQueued = true;
        }

        // Recreate the rcbuf since the previous one was deleted
        slot->rcbuf = qcap2_rcbuffer_new(&slot->frame, qcap2_v4l2_buffer_on_free);
    }

    QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
        PVOID pData = qcap2_rcbuffer_get_data(pRCBuffer);
        if (!pData) return QCAP_RS_ERROR_GENERAL;

        qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
        qcap2_v4l2_buffer_slot_t* slot = qcap2_container_of(pFrame, qcap2_v4l2_buffer_slot_t, frame);

        if (slot->pSource != this) {
            return QCAP_RS_ERROR_GENERAL;
        }

        if (!slot->bIsQueued) {
            if (fd >= 0) {
                struct v4l2_buffer buf;
                memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = p->v4l2_memory_val;
                buf.index = slot->index;

                if (buf.memory == V4L2_MEMORY_USERPTR) {
                    buf.m.userptr = (unsigned long)slot->pUserPtr;
                    buf.length = slot->nLength;
                }

                if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                    return QCAP_RS_ERROR_GENERAL;
                }
            }
            slot->bIsQueued = true;
        }

        return QCAP_RS_SUCCESSFUL;
    }
};

static void qcap2_v4l2_buffer_on_free(PVOID pData) {
    if (!pData) return;
    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    qcap2_v4l2_buffer_slot_t* slot = qcap2_container_of(pFrame, qcap2_v4l2_buffer_slot_t, frame);
    slot->pSource->requeue_slot(slot);
}

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
                    qcap2_rcbuffer_queue_push(self->p->queue, rcbuf);
                    qcap2_rcbuffer_release(rcbuf);
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
                qcap2_rcbuffer_queue_push(self->p->queue, rcbuf);
                qcap2_rcbuffer_release(rcbuf);
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

class qcap2_alsa_audio_sink_backend_t : public qcap2_audio_sink_backend_t {
private:
    qcap2_audio_sink_priv_t* p;
    int fd;
    std::thread* playback_thread;
    std::atomic<bool> thread_running;

    static void playback_thread_func(qcap2_alsa_audio_sink_backend_t* self) {
        while (self->thread_running.load(std::memory_order_acquire)) {
            qcap2_rcbuffer_t* rcbuf = nullptr;
            QRESULT qres = qcap2_rcbuffer_queue_pop(self->p->queue, &rcbuf);
            if (qres == QCAP_RS_SUCCESSFUL && rcbuf) {
                PVOID pData = qcap2_rcbuffer_lock_data(rcbuf);
                if (pData) {
                    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)pData;
                    uint8_t* pBuf = nullptr;
                    int nSize = 0;
                    qcap2_av_frame_get_buffer(frame, &pBuf, &nSize);
                    if (pBuf && nSize > 0) {
                        ssize_t bytes_written = write(self->fd, pBuf, nSize);
                        (void)bytes_written;
                    }
                    qcap2_rcbuffer_unlock_data(rcbuf);
                }
                qcap2_rcbuffer_release(rcbuf);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

public:
    qcap2_alsa_audio_sink_backend_t(qcap2_audio_sink_priv_t* owner)
        : p(owner), fd(-1), playback_thread(nullptr) {
        thread_running.store(false);
    }

    ~qcap2_alsa_audio_sink_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (thread_running.load(std::memory_order_acquire)) return QCAP_RS_SUCCESSFUL;

        char dev_path[64];
        snprintf(dev_path, sizeof(dev_path), "/dev/snd/pcmC%dD%dp", p->card, p->device);

        fd = open(dev_path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) {
            return QCAP_RS_ERROR_GENERAL;
        }

        thread_running.store(true, std::memory_order_release);
        playback_thread = new (std::nothrow) std::thread(playback_thread_func, this);
        if (!playback_thread) {
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
        
        qcap2_rcbuffer_queue_stop(p->queue);

        if (playback_thread && playback_thread->joinable()) {
            playback_thread->join();
            delete playback_thread;
            playback_thread = nullptr;
        }

        if (fd >= 0) {
            close(fd);
            fd = -1;
        }

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
        return qcap2_rcbuffer_queue_push(p->queue, pRCBuffer);
    }
};

qcap2_audio_sink_priv_t::~qcap2_audio_sink_priv_t() {
    qcap2_audio_sink_stop(reinterpret_cast<qcap2_audio_sink_t*>(this));
    if (backend) {
        delete backend;
        backend = nullptr;
    }
    if (queue) {
        qcap2_rcbuffer_queue_delete(queue);
        queue = nullptr;
    }
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

// ==============================================================================
// qcap2_audio_sink_t C APIs
// ==============================================================================

qcap2_audio_sink_t* qcap2_audio_sink_new() {
    qcap2_audio_sink_priv_t* priv = new (std::nothrow) qcap2_audio_sink_priv_t();
    return reinterpret_cast<qcap2_audio_sink_t*>(priv);
}

void qcap2_audio_sink_delete(qcap2_audio_sink_t* pThis) {
    if (pThis) {
        delete reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis);
    }
}

void qcap2_audio_sink_set_backend_type(qcap2_audio_sink_t* pThis, int nBackendType) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis)->backend_type = nBackendType;
    }
}

void qcap2_audio_sink_set_audio_format(qcap2_audio_sink_t* pThis, qcap2_audio_format_t* pAudioFormat) {
    if (pThis && pAudioFormat) {
        qcap2_audio_sink_priv_t* priv = reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis);
        ULONG channels = 0, bits = 0, freq = 0;
        qcap2_audio_format_get_property(pAudioFormat, &channels, &bits, &freq);
        priv->channels = channels;
        priv->sample_fmt = bits;
        priv->sample_frequency = freq;
    }
}

void qcap2_audio_sink_set_period_time(qcap2_audio_sink_t* pThis, int nPeriodTime) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis)->period_time = nPeriodTime;
    }
}

void qcap2_audio_sink_set_buffer_time(qcap2_audio_sink_t* pThis, int nBufferTime) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis)->buffer_time = nBufferTime;
    }
}

void qcap2_audio_sink_set_card(qcap2_audio_sink_t* pThis, int nCard) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis)->card = nCard;
    }
}

void qcap2_audio_sink_set_device(qcap2_audio_sink_t* pThis, int nDevice) {
    if (pThis) {
        reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis)->device = nDevice;
    }
}

QRESULT qcap2_audio_sink_start(qcap2_audio_sink_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_sink_priv_t* priv = reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis);
    
    if (priv->backend) return QCAP_RS_SUCCESSFUL;

    qcap2_rcbuffer_queue_start(priv->queue);

    if (priv->backend_type == QCAP2_AUDIO_SINK_BACKEND_TYPE_ALSA) {
        priv->backend = new (std::nothrow) qcap2_alsa_audio_sink_backend_t(priv);
        if (!priv->backend) return QCAP_RS_ERROR_OUT_OF_MEMORY;
        QRESULT qr = priv->backend->start();
        if (qr != QCAP_RS_SUCCESSFUL) {
            delete priv->backend;
            priv->backend = nullptr;
            return qr;
        }
    } else {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_sink_stop(qcap2_audio_sink_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_sink_priv_t* priv = reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis);

    qcap2_rcbuffer_queue_stop(priv->queue);

    if (priv->backend) {
        priv->backend->stop();
        delete priv->backend;
        priv->backend = nullptr;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_sink_pop(qcap2_audio_sink_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_sink_priv_t* priv = reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_pop(priv->queue, ppRCBuffer);
}

QRESULT qcap2_audio_sink_push(qcap2_audio_sink_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_sink_priv_t* priv = reinterpret_cast<qcap2_audio_sink_priv_t*>(pThis);
    if (priv->backend) {
        return priv->backend->push(pRCBuffer);
    }
    return qcap2_rcbuffer_queue_push(priv->queue, pRCBuffer);
}

// alsa.h specific audio sink APIs
void qcap2_audio_sink_set_alsa_card(qcap2_audio_sink_t* pThis, int nCard) {
    qcap2_audio_sink_set_card(pThis, nCard);
}

void qcap2_audio_sink_set_alsa_device(qcap2_audio_sink_t* pThis, int nDevice) {
    qcap2_audio_sink_set_device(pThis, nDevice);
}

#include "qcap2.v4l2.ioctl.h"

// qcap2_video_sink_priv_t destructor
qcap2_video_sink_priv_t::~qcap2_video_sink_priv_t() {
    qcap2_video_sink_stop(reinterpret_cast<qcap2_video_sink_t*>(this));
    if (queue) {
        qcap2_rcbuffer_queue_delete(queue);
    }
}

class qcap2_v4l2_video_sink_backend_t : public qcap2_video_sink_backend_t {
private:
    qcap2_video_sink_priv_t* p;
    int fd;
    bool running;
    std::thread* playback_thread;
    std::atomic<bool> thread_running;
    
    struct sink_slot_t {
        int index;
        void* pMappedMemory;
        void* pUserPtr;
        int dma_fd;
        size_t nLength;
    };
    sink_slot_t* slots;
    int slot_count;

    void cleanup_slots() {
        if (slots) {
            for (int i = 0; i < slot_count; ++i) {
                sink_slot_t* slot = &slots[i];
                if (slot->pMappedMemory && slot->pMappedMemory != MAP_FAILED) {
                    munmap(slot->pMappedMemory, slot->nLength);
                }
                if (slot->pUserPtr) {
                    free(slot->pUserPtr);
                }
                if (slot->dma_fd >= 0) {
                    close(slot->dma_fd);
                }
            }
            delete[] slots;
            slots = nullptr;
        }
        slot_count = 0;
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    static void playback_thread_func(qcap2_v4l2_video_sink_backend_t* self) {
        int queued_count = 0;
        bool stream_on = false;
        
        while (self->thread_running.load(std::memory_order_acquire)) {
            qcap2_rcbuffer_t* rcbuf = nullptr;
            QRESULT qr = qcap2_rcbuffer_queue_pop(self->p->queue, &rcbuf);
            if (qr != QCAP_RS_SUCCESSFUL || !rcbuf) {
                continue;
            }

            PVOID pFrameData = qcap2_rcbuffer_lock_data(rcbuf);
            if (!pFrameData) {
                qcap2_rcbuffer_release(rcbuf);
                continue;
            }

            qcap2_av_frame_t* pFrame = reinterpret_cast<qcap2_av_frame_t*>(pFrameData);
            
            uint8_t* pSrcBuf = nullptr;
            int nSrcStride = 0;
            qcap2_av_frame_get_buffer(pFrame, &pSrcBuf, &nSrcStride);
            
            ULONG nWidth = 0, nHeight = 0, nColorSpace = 0;
            qcap2_av_frame_get_video_property(pFrame, &nColorSpace, &nWidth, &nHeight);

            enum v4l2_memory mem_type = static_cast<enum v4l2_memory>(self->p->v4l2_memory_val);
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = self->p->v4l2_buf_type_val;
            buf.memory = mem_type;

            int target_slot_index = -1;

            if (queued_count < self->slot_count) {
                target_slot_index = queued_count;
                buf.index = target_slot_index;
            } else {
                struct pollfd pfd;
                pfd.fd = self->fd;
                pfd.events = POLLOUT;

                int poll_ret = poll(&pfd, 1, 50);
                if (poll_ret <= 0) {
                    qcap2_rcbuffer_unlock_data(rcbuf);
                    qcap2_rcbuffer_release(rcbuf);
                    continue;
                }

                if (ioctl(self->fd, VIDIOC_DQBUF, &buf) < 0) {
                    qcap2_rcbuffer_unlock_data(rcbuf);
                    qcap2_rcbuffer_release(rcbuf);
                    continue;
                }
                target_slot_index = buf.index;
            }

            if (target_slot_index >= 0 && target_slot_index < self->slot_count) {
                sink_slot_t* slot = &self->slots[target_slot_index];
                
                if (mem_type == V4L2_MEMORY_MMAP) {
                    if (slot->pMappedMemory && pSrcBuf) {
                        size_t copy_size = std::min(slot->nLength, (size_t)(nWidth * nHeight * 3));
                        memcpy(slot->pMappedMemory, pSrcBuf, copy_size);
                    }
                } else if (mem_type == V4L2_MEMORY_USERPTR) {
                    if (slot->pUserPtr && pSrcBuf) {
                        size_t copy_size = std::min(slot->nLength, (size_t)(nWidth * nHeight * 3));
                        memcpy(slot->pUserPtr, pSrcBuf, copy_size);
                    }
                    buf.m.userptr = (unsigned long)slot->pUserPtr;
                    buf.length = slot->nLength;
                } else if (mem_type == V4L2_MEMORY_DMABUF) {
                    qcap2_dmabuf_t* pDMABuf = nullptr;
                    if (qcap2_av_frame_get_dmabuf(pFrame, &pDMABuf) == QCAP_RS_SUCCESSFUL && pDMABuf) {
                        buf.m.fd = pDMABuf->fd;
                        buf.length = pDMABuf->dmabuf_size;
                    } else {
                        qcap2_rcbuffer_unlock_data(rcbuf);
                        qcap2_rcbuffer_release(rcbuf);
                        continue;
                    }
                }

                buf.index = target_slot_index;
                
                double dSampleTime = 0.0;
                qcap2_av_frame_get_sample_time(pFrame, &dSampleTime);
                buf.timestamp.tv_sec = (time_t)dSampleTime;
                buf.timestamp.tv_usec = (suseconds_t)((dSampleTime - buf.timestamp.tv_sec) * 1000000.0);

                if (ioctl(self->fd, VIDIOC_QBUF, &buf) >= 0) {
                    if (queued_count < self->slot_count) {
                        queued_count++;
                    }
                }
            }

            qcap2_rcbuffer_unlock_data(rcbuf);
            qcap2_rcbuffer_release(rcbuf);

            if (queued_count == self->slot_count && !stream_on) {
                enum v4l2_buf_type type = static_cast<enum v4l2_buf_type>(self->p->v4l2_buf_type_val);
                if (ioctl(self->fd, VIDIOC_STREAMON, &type) >= 0) {
                    stream_on = true;
                }
            }
        }

        if (stream_on) {
            enum v4l2_buf_type type = static_cast<enum v4l2_buf_type>(self->p->v4l2_buf_type_val);
            ioctl(self->fd, VIDIOC_STREAMOFF, &type);
        }
    }

public:
    qcap2_v4l2_video_sink_backend_t(qcap2_video_sink_priv_t* owner)
        : p(owner), fd(-1), running(false), playback_thread(nullptr), slots(nullptr), slot_count(0) {
        thread_running.store(false);
    }

    ~qcap2_v4l2_video_sink_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (running) return QCAP_RS_SUCCESSFUL;

        char dev_path[256];
        if (p->v4l2_name[0] != '\0') {
            strncpy(dev_path, p->v4l2_name, sizeof(dev_path) - 1);
            dev_path[sizeof(dev_path) - 1] = '\0';
        } else {
            snprintf(dev_path, sizeof(dev_path), "/dev/video%d", p->device_index);
        }

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

        if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = p->v4l2_buf_type_val;
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
        size_t buffer_size = fmt.fmt.pix.sizeimage;

        enum v4l2_memory mem_type = static_cast<enum v4l2_memory>(p->v4l2_memory_val);

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = p->frame_count > 0 ? p->frame_count : 4;
        req.type = p->v4l2_buf_type_val;
        req.memory = mem_type;

        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        slot_count = req.count;
        slots = new (std::nothrow) sink_slot_t[slot_count];
        if (!slots) {
            close(fd);
            fd = -1;
            return QCAP_RS_ERROR_GENERAL;
        }

        for (int i = 0; i < slot_count; ++i) {
            sink_slot_t* slot = &slots[i];
            slot->index = i;
            slot->pMappedMemory = nullptr;
            slot->pUserPtr = nullptr;
            slot->dma_fd = -1;
            slot->nLength = buffer_size;

            struct v4l2_buffer vbuf;
            memset(&vbuf, 0, sizeof(vbuf));
            vbuf.type = p->v4l2_buf_type_val;
            vbuf.memory = mem_type;
            vbuf.index = i;

            if (mem_type == V4L2_MEMORY_MMAP) {
                if (ioctl(fd, VIDIOC_QUERYBUF, &vbuf) < 0) {
                    cleanup_slots();
                    return QCAP_RS_ERROR_GENERAL;
                }
                slot->nLength = vbuf.length;
                slot->pMappedMemory = mmap(NULL, slot->nLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, vbuf.m.offset);
                if (slot->pMappedMemory == MAP_FAILED) {
                    cleanup_slots();
                    return QCAP_RS_ERROR_GENERAL;
                }
            } else if (mem_type == V4L2_MEMORY_USERPTR) {
                void* ptr = nullptr;
                if (posix_memalign(&ptr, 4096, buffer_size) != 0) {
                    cleanup_slots();
                    return QCAP_RS_ERROR_GENERAL;
                }
                slot->pUserPtr = ptr;
            } else if (mem_type == V4L2_MEMORY_DMABUF) {
                slot->dma_fd = -1;
                slot->pMappedMemory = nullptr;
            }
        }

        running = true;
        thread_running.store(true, std::memory_order_release);
        playback_thread = new std::thread(playback_thread_func, this);

        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT stop() override {
        if (!running) return QCAP_RS_SUCCESSFUL;

        thread_running.store(false, std::memory_order_release);
        
        qcap2_rcbuffer_queue_stop(p->queue);

        if (playback_thread) {
            if (playback_thread->joinable()) {
                playback_thread->join();
            }
            delete playback_thread;
            playback_thread = nullptr;
        }

        cleanup_slots();
        running = false;
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
        if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
        return qcap2_rcbuffer_queue_push(p->queue, pRCBuffer);
    }

    int get_fd() const { return fd; }
};

// Internal DRM Helper Functions
static int qcap2_drm_prime_fd_to_handle(int fd, int prime_fd, uint32_t* pHandle);
static int qcap2_drm_add_fb2(int fd, uint32_t nWidth, uint32_t nHeight, uint32_t nPixelFormat,
                             const uint32_t handles[4], const uint32_t pitches[4], const uint32_t offsets[4],
                             uint32_t* pFbId, uint32_t nFlags);
static int qcap2_drm_set_plane(int fd, uint32_t nPlaneId, uint32_t nCrtcId, uint32_t nFbId, uint32_t nFlags,
                               int32_t nCrtcX, int32_t nCrtcY, uint32_t nCrtcW, uint32_t nCrtcH,
                               uint32_t nSrcX, uint32_t nSrcY, uint32_t nSrcW, uint32_t nSrcH);
static int qcap2_drm_rm_fb(int fd, uint32_t nFbId);
static int qcap2_drm_gem_close(int fd, uint32_t nHandle);

class qcap2_drm_video_sink_backend_t : public qcap2_video_sink_backend_t {
private:
    qcap2_video_sink_priv_t* p;
    int fd;
    bool running;
    std::thread* playback_thread;
    std::atomic<bool> thread_running;

    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t crtc_id;
    uint32_t plane_id;
    uint32_t fb_id;

    void cleanup() {
        if (fb_id > 0) {
            qcap2_drm_rm_fb(fd, fb_id);
            fb_id = 0;
        }
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    static void playback_thread_func(qcap2_drm_video_sink_backend_t* self) {
        while (self->thread_running.load(std::memory_order_acquire)) {
            qcap2_rcbuffer_t* rcbuf = nullptr;
            QRESULT qr = qcap2_rcbuffer_queue_pop(self->p->queue, &rcbuf);
            if (qr != QCAP_RS_SUCCESSFUL || !rcbuf) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            PVOID pFrameData = qcap2_rcbuffer_lock_data(rcbuf);
            if (!pFrameData) {
                qcap2_rcbuffer_release(rcbuf);
                continue;
            }

            qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pFrameData;

            // DRM zero-copy DMABUF importing scheme
            qcap2_dmabuf_t* pDMABuf = nullptr;
            if (qcap2_av_frame_get_dmabuf(pFrame, &pDMABuf) == QCAP_RS_SUCCESSFUL && pDMABuf) {
                uint32_t gem_handle = 0;
                if (qcap2_drm_prime_fd_to_handle(self->fd, pDMABuf->fd, &gem_handle) == 0) {
                    ULONG width = 0, height = 0, color_space = 0;
                    qcap2_av_frame_get_video_property(pFrame, &color_space, &width, &height);

                    uint32_t handles[4] = { gem_handle, 0, 0, 0 };
                    uint32_t pitches[4] = { (uint32_t)(width * 4), 0, 0, 0 };
                    uint32_t offsets[4] = { 0, 0, 0, 0 };
                    uint32_t new_fb_id = 0;

                    if (qcap2_drm_add_fb2(self->fd, width, height, 0, handles, pitches, offsets, &new_fb_id, 0) == 0) {
                        uint32_t flags = 0;
                        int32_t crtc_x = 0, crtc_y = 0;
                        uint32_t crtc_w = width, crtc_h = height;
                        uint32_t src_x = 0, src_y = 0;
                        uint32_t src_w = width << 16, src_h = height << 16;

                        if (qcap2_drm_set_plane(self->fd, self->plane_id, self->crtc_id, new_fb_id, flags,
                                                crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h) == 0) {
                            if (self->fb_id > 0 && self->fb_id != new_fb_id) {
                                qcap2_drm_rm_fb(self->fd, self->fb_id);
                            }
                            self->fb_id = new_fb_id;
                        } else {
                            qcap2_drm_rm_fb(self->fd, new_fb_id);
                        }
                    }

                    qcap2_drm_gem_close(self->fd, gem_handle);
                }
            }

            qcap2_rcbuffer_unlock_data(rcbuf);
            qcap2_rcbuffer_release(rcbuf);
        }
    }

public:
    qcap2_drm_video_sink_backend_t(qcap2_video_sink_priv_t* owner)
        : p(owner), fd(-1), running(false), playback_thread(nullptr), connector_id(0), encoder_id(0), crtc_id(0), plane_id(0), fb_id(0) {
        thread_running.store(false);
    }

    ~qcap2_drm_video_sink_backend_t() override {
        stop();
    }

    QRESULT start() override {
        if (running) return QCAP_RS_SUCCESSFUL;

        char dev_path[64];
        snprintf(dev_path, sizeof(dev_path), "/dev/dri/card%d", p->device_index);

        fd = open(dev_path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            char mock_path[64];
            snprintf(mock_path, sizeof(mock_path), "/tmp/mock_drm_card%d", p->device_index);
            fd = open(mock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
            if (fd < 0) {
                return QCAP_RS_ERROR_GENERAL;
            }
        }

        connector_id = p->connector_id ? p->connector_id : 10;
        crtc_id = p->crtc_id ? p->crtc_id : 30;
        plane_id = p->native_handle ? (uint32_t)p->native_handle : 50;
        encoder_id = 20;

        running = true;
        thread_running.store(true, std::memory_order_release);
        playback_thread = new (std::nothrow) std::thread(playback_thread_func, this);
        if (!playback_thread) {
            cleanup();
            running = false;
            return QCAP_RS_ERROR_OUT_OF_MEMORY;
        }

        return QCAP_RS_SUCCESSFUL;
    }

      QRESULT stop() override {
          if (!running) return QCAP_RS_SUCCESSFUL;

          thread_running.store(false, std::memory_order_release);
          if (playback_thread) {
              if (playback_thread->joinable()) {
                  playback_thread->join();
              }
              delete playback_thread;
              playback_thread = nullptr;
          }

          cleanup();
          running = false;
          return QCAP_RS_SUCCESSFUL;
      }

      QRESULT push(qcap2_rcbuffer_t* pRCBuffer) override {
          if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
          return qcap2_rcbuffer_queue_push(p->queue, pRCBuffer);
      }

      int get_fd() const { return fd; }
  };

// qcap2_video_sink_t C APIs
qcap2_video_sink_t* qcap2_video_sink_new() {
    qcap2_video_sink_priv_t* priv = new (std::nothrow) qcap2_video_sink_priv_t();
    return reinterpret_cast<qcap2_video_sink_t*>(priv);
}

void qcap2_video_sink_delete(qcap2_video_sink_t* pThis) {
    if (pThis) {
        delete reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
    }
}

void qcap2_video_sink_set_backend_type(qcap2_video_sink_t* pThis, int nBackendType) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->backend_type = nBackendType;
    }
}

void qcap2_video_sink_set_video_format(qcap2_video_sink_t* pThis, qcap2_video_format_t* pVideoFormat) {
    if (pThis && pVideoFormat) {
        qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
        ULONG color_space = 0, width = 0, height = 0;
        BOOL interleaved = FALSE;
        double frame_rate = 0.0;
        qcap2_video_format_get_property(pVideoFormat, &color_space, &width, &height, &interleaved, &frame_rate);
        priv->color_space = color_space;
        priv->width = width;
        priv->height = height;
        priv->frame_rate = frame_rate;
    }
}

void qcap2_video_sink_set_frame_count(qcap2_video_sink_t* pThis, int nFrameCount) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->frame_count = nFrameCount;
    }
}

void qcap2_video_sink_set_multithread(qcap2_video_sink_t* pThis, bool bMultiThread) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->multithread = bMultiThread;
    }
}

void qcap2_video_sink_set_native_handle(qcap2_video_sink_t* pThis, uintptr_t nHandle) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->native_handle = nHandle;
    }
}

void qcap2_video_sink_set_low_bandwidth(qcap2_video_sink_t* pThis, bool bLowBandwidth) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->low_bandwidth = bLowBandwidth;
    }
}

void qcap2_video_sink_set_display_system(qcap2_video_sink_t* pThis, int nDisplaySystem) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->display_system = nDisplaySystem;
    }
}

void qcap2_video_sink_set_graphic_window_system(qcap2_video_sink_t* pThis, int nGraphicWindowSystem) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->graphic_window_system = nGraphicWindowSystem;
    }
}

void qcap2_video_sink_set_gpu_direct(qcap2_video_sink_t* pThis, bool bGPUDirect) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->gpu_direct = bGPUDirect;
    }
}

void qcap2_video_sink_set_scale_style(qcap2_video_sink_t* pThis, ULONG nScaleStyle) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->scale_style = nScaleStyle;
    }
}

void qcap2_video_sink_set_device_index(qcap2_video_sink_t* pThis, int nDeviceIndex) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->device_index = nDeviceIndex;
    }
}

void qcap2_video_sink_set_src_ss_type(qcap2_video_sink_t* pThis, int nSrcSSType) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->src_ss_type = nSrcSSType;
    }
}

void qcap2_video_sink_set_dst_ss_type(qcap2_video_sink_t* pThis, int nDstSSType) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->dst_ss_type = nDstSSType;
    }
}

QRESULT qcap2_video_sink_start(qcap2_video_sink_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);

    if (priv->backend) return QCAP_RS_SUCCESSFUL;

    qcap2_rcbuffer_queue_start(priv->queue);

    if (priv->backend_type == QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2) {
        priv->backend = new (std::nothrow) qcap2_v4l2_video_sink_backend_t(priv);
        if (!priv->backend) return QCAP_RS_ERROR_OUT_OF_MEMORY;
        QRESULT qr = priv->backend->start();
        if (qr != QCAP_RS_SUCCESSFUL) {
            delete priv->backend;
            priv->backend = nullptr;
            return qr;
        }
    } else if (priv->backend_type == QCAP2_VIDEO_SINK_BACKEND_TYPE_DRM) {
        priv->backend = new (std::nothrow) qcap2_drm_video_sink_backend_t(priv);
        if (!priv->backend) return QCAP_RS_ERROR_OUT_OF_MEMORY;
        QRESULT qr = priv->backend->start();
        if (qr != QCAP_RS_SUCCESSFUL) {
            delete priv->backend;
            priv->backend = nullptr;
            return qr;
        }
    } else {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_sink_stop(qcap2_video_sink_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);

    qcap2_rcbuffer_queue_stop(priv->queue);

    if (priv->backend) {
        priv->backend->stop();
        delete priv->backend;
        priv->backend = nullptr;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_sink_pop(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_pop(priv->queue, ppRCBuffer);
}

QRESULT qcap2_video_sink_push(qcap2_video_sink_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
    if (priv->backend) {
        return priv->backend->push(pRCBuffer);
    }
    return qcap2_rcbuffer_queue_push(priv->queue, pRCBuffer);
}

// v4l2.h specific video sink APIs
void qcap2_video_sink_set_v4l2_name(qcap2_video_sink_t* pThis, const char* strName) {
    if (pThis) {
        qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
        if (strName) {
            strncpy(priv->v4l2_name, strName, sizeof(priv->v4l2_name) - 1);
            priv->v4l2_name[sizeof(priv->v4l2_name) - 1] = '\0';
        }
    }
}

const char* qcap2_video_sink_get_v4l2_name(qcap2_video_sink_t* pThis) {
    if (pThis) {
        qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
        return priv->v4l2_name;
    }
    return nullptr;
}

void qcap2_video_sink_set_buf_type(qcap2_video_sink_t* pThis, enum v4l2_buf_type nBufType) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->v4l2_buf_type_val = nBufType;
    }
}

void qcap2_video_sink_set_memory(qcap2_video_sink_t* pThis, enum v4l2_memory nMemory) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->v4l2_memory_val = nMemory;
    }
}

QRESULT qcap2_video_sink_get_fd(qcap2_video_sink_t* pThis, int* pFd) {
    if (pThis && pFd) {
        qcap2_video_sink_priv_t* priv = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis);
        if (priv->backend) {
            auto* v4l2_back = static_cast<qcap2_v4l2_video_sink_backend_t*>(priv->backend);
            *pFd = v4l2_back->get_fd();
            return QCAP_RS_SUCCESSFUL;
        }
        *pFd = -1;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_INVALID_PARAMETER;
}

void qcap2_video_sink_set_connector_id(qcap2_video_sink_t* pThis, uint32_t nConnectorId) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->connector_id = nConnectorId;
    }
}

void qcap2_video_sink_set_crtc_id(qcap2_video_sink_t* pThis, uint32_t nCrtcId) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->crtc_id = nCrtcId;
    }
}

void qcap2_video_sink_set_plane_id(qcap2_video_sink_t* pThis, uint32_t nPlaneId) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->plane_id = nPlaneId;
    }
}

void qcap2_video_sink_set_drm_modifier(qcap2_video_sink_t* pThis, uint64_t nModifier) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->drm_modifier = nModifier;
    }
}

void qcap2_video_sink_set_drm_format(qcap2_video_sink_t* pThis, uint32_t nFormat) {
    if (pThis) {
        reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->drm_format = nFormat;
    }
}

void qcap2_video_sink_get_connector_id(qcap2_video_sink_t* pThis, uint32_t* pConnectorId) {
    if (pThis && pConnectorId) {
        *pConnectorId = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->connector_id;
    }
}

void qcap2_video_sink_get_crtc_id(qcap2_video_sink_t* pThis, uint32_t* pCrtcId) {
    if (pThis && pCrtcId) {
        *pCrtcId = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->crtc_id;
    }
}

void qcap2_video_sink_get_plane_id(qcap2_video_sink_t* pThis, uint32_t* pPlaneId) {
    if (pThis && pPlaneId) {
        *pPlaneId = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->plane_id;
    }
}

void qcap2_video_sink_get_drm_modifier(qcap2_video_sink_t* pThis, uint64_t* pModifier) {
    if (pThis && pModifier) {
        *pModifier = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->drm_modifier;
    }
}

void qcap2_video_sink_get_drm_format(qcap2_video_sink_t* pThis, uint32_t* pFormat) {
    if (pThis && pFormat) {
        *pFormat = reinterpret_cast<qcap2_video_sink_priv_t*>(pThis)->drm_format;
    }
}

static std::mutex s_drm_fd_mutex;
static int s_drm_fd = -1;
static int s_drm_fd_refcount = 0;

int qcap2_get_drm_fd() {
    std::lock_guard<std::mutex> lock(s_drm_fd_mutex);
    if (s_drm_fd == -1) {
        s_drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (s_drm_fd < 0) {
            s_drm_fd = open("/tmp/mock_drm_card0", O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        }
    }
    if (s_drm_fd >= 0) {
        s_drm_fd_refcount++;
        return s_drm_fd;
    }
    return -1;
}

void qcap2_put_drm_fd(int fd) {
    std::lock_guard<std::mutex> lock(s_drm_fd_mutex);
    if (s_drm_fd == fd) {
        s_drm_fd_refcount--;
        if (s_drm_fd_refcount <= 0) {
            close(s_drm_fd);
            s_drm_fd = -1;
            s_drm_fd_refcount = 0;
        }
    }
}

static int qcap2_drm_prime_fd_to_handle(int fd, int prime_fd, uint32_t* pHandle) {
#if HAVE_DRM
    if (fd < 0 || prime_fd < 0 || !pHandle) return -1;
    return drmPrimeFDToHandle(fd, prime_fd, pHandle);
#else
    if (fd < 0 || prime_fd < 0 || !pHandle) return -1;
    *pHandle = (uint32_t)prime_fd + 100;
    return 0;
#endif
}

static int qcap2_drm_add_fb2(int fd, uint32_t nWidth, uint32_t nHeight, uint32_t nPixelFormat,
                             const uint32_t handles[4], const uint32_t pitches[4], const uint32_t offsets[4],
                             uint32_t* pFbId, uint32_t nFlags) {
#if HAVE_DRM
    if (fd < 0 || nWidth == 0 || nHeight == 0 || !handles || !pFbId) return -1;
    return drmModeAddFB2(fd, nWidth, nHeight, nPixelFormat, handles, pitches, offsets, pFbId, nFlags);
#else
    (void)nPixelFormat;
    (void)pitches;
    (void)offsets;
    (void)nFlags;
    if (fd < 0 || nWidth == 0 || nHeight == 0 || !handles || !pFbId) return -1;
    *pFbId = handles[0] + 1000;
    return 0;
#endif
}

static int qcap2_drm_set_plane(int fd, uint32_t nPlaneId, uint32_t nCrtcId, uint32_t nFbId, uint32_t nFlags,
                               int32_t nCrtcX, int32_t nCrtcY, uint32_t nCrtcW, uint32_t nCrtcH,
                               uint32_t nSrcX, uint32_t nSrcY, uint32_t nSrcW, uint32_t nSrcH) {
#if HAVE_DRM
    if (fd < 0) return -1;
    return drmModeSetPlane(fd, nPlaneId, nCrtcId, nFbId, nFlags,
                           nCrtcX, nCrtcY, nCrtcW, nCrtcH,
                           nSrcX, nSrcY, nSrcW, nSrcH);
#else
    (void)nPlaneId;
    (void)nCrtcId;
    (void)nFbId;
    (void)nFlags;
    (void)nCrtcX;
    (void)nCrtcY;
    (void)nCrtcW;
    (void)nCrtcH;
    (void)nSrcX;
    (void)nSrcY;
    (void)nSrcW;
    (void)nSrcH;
    if (fd < 0) return -1;
    return 0;
#endif
}

static int qcap2_drm_rm_fb(int fd, uint32_t nFbId) {
#if HAVE_DRM
    if (fd < 0 || nFbId == 0) return -1;
    return drmModeRmFB(fd, nFbId);
#else
    (void)nFbId;
    if (fd < 0) return -1;
    return 0;
#endif
}

static int qcap2_drm_gem_close(int fd, uint32_t nHandle) {
#if HAVE_DRM
    if (fd < 0 || nHandle == 0) return -1;
    struct drm_gem_close close_args = {};
    close_args.handle = nHandle;
    return ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_args);
#else
    (void)nHandle;
    if (fd < 0) return -1;
    return 0;
#endif
}

void qcap2_video_sink_set_gst_sink_name(qcap2_video_sink_t* pThis, const char* strGstSinkName) { (void)pThis; (void)strGstSinkName; }
void qcap2_video_sink_set_nvbuf(qcap2_video_sink_t* pThis, bool bNVBuf) { (void)pThis; (void)bNVBuf; }
void qcap2_video_sink_set_vout_id(qcap2_video_sink_t* pThis, int nVoutId) { (void)pThis; (void)nVoutId; }

int qcap2_video_sink_set_panel(int nFd, qcap2_v4l2_ctrl_mdin_panel_t* pParams) { (void)nFd; (void)pParams; return -1; }
int qcap2_video_sink_get_panel(int nFd, qcap2_v4l2_ctrl_mdin_panel_t* pParams) { (void)nFd; (void)pParams; return -1; }
int qcap2_video_sink_set_pip(int nFd, qcap2_v4l2_ctrl_mdin_pip_t* pParams) { (void)nFd; (void)pParams; return -1; }
int qcap2_video_sink_get_pip(int nFd, qcap2_v4l2_ctrl_mdin_pip_t* pParams) { (void)nFd; (void)pParams; return -1; }
int qcap2_video_sink_set_key(int nFd, qcap2_v4l2_ctrl_mdin_key_t* pParams) { (void)nFd; (void)pParams; return -1; }
int qcap2_video_sink_get_key(int nFd, qcap2_v4l2_ctrl_mdin_key_t* pParams) { (void)nFd; (void)pParams; return -1; }

}
