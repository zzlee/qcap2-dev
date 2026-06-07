#ifndef __QCAP2_BUFFER_PRIV_H__
#define __QCAP2_BUFFER_PRIV_H__

#include "qcap2.buffer.h"
#include "qcap2.dmabuf.h"
#include <atomic>

// The internal representation of qcap2_rcbuffer_t is a C++ class/struct
struct qcap2_rcbuffer_t {
protected:
    std::atomic<int32_t> use_count_{1};
    std::atomic<int32_t> res_count_{1};

public:
    virtual ~qcap2_rcbuffer_t() = default;

    void add_ref() {
        use_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (use_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            decrement_res_count();
        }
    }

    int32_t use_count() const {
        return use_count_.load(std::memory_order_acquire);
    }

    int32_t res_count() const {
        return res_count_.load(std::memory_order_acquire);
    }

    PVOID lock_data() {
        int32_t n = res_count_.load(std::memory_order_acquire);
        while (n > 0) {
            if (res_count_.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return get_data();
            }
            n = res_count_.load(std::memory_order_acquire);
        }
        return NULL;
    }

    void unlock_data() {
        decrement_res_count();
    }

    virtual void on_release_resource() = 0;

    virtual PVOID get_data() const = 0;
    virtual qcap2_buffer_type_t get_type() const = 0;
    virtual PVOID get_native_handle() const = 0;

    virtual QRESULT get_pts(int64_t* pts) = 0;
    virtual QRESULT set_pts(int64_t pts) = 0;
    virtual QRESULT get_dts(int64_t* dts) = 0;
    virtual QRESULT set_dts(int64_t dts) = 0;
    virtual QRESULT get_stream_index(int* idx) = 0;
    virtual QRESULT set_stream_index(int idx) = 0;
    virtual QRESULT is_keyframe(BOOL* key) = 0;
    virtual QRESULT set_keyframe(BOOL key) = 0;

    virtual QRESULT get_data_ptr(uint8_t** data, int* size) = 0;
    virtual QRESULT get_video_property(ULONG* colorspace, ULONG* width, ULONG* height) = 0;
    virtual QRESULT get_plane(int plane, uint8_t** data, int* stride) = 0;

    virtual QRESULT map_system_memory(PVOID* ppDataOut) { return QCAP_RS_ERROR_NON_SUPPORT; }
    virtual QRESULT unmap_system_memory() { return QCAP_RS_ERROR_NON_SUPPORT; }

private:
    void decrement_res_count() {
        if (res_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            on_release_resource();
        }
        maybe_delete();
    }

    void maybe_delete() {
        if (use_count_.load(std::memory_order_acquire) == 0 &&
            res_count_.load(std::memory_order_acquire) == 0) {
            delete this;
        }
    }
};

class qcap2_system_buffer : public qcap2_rcbuffer_t {
    PVOID pData_;
    ULONG nDataSize_;
    qcap2_on_free_resource_t pOnFreeResource_;

    int64_t pts_{0};
    int64_t dts_{0};
    int stream_idx_{0};
    BOOL keyframe_{FALSE};

protected:
    void on_release_resource() override {
        if (pOnFreeResource_) {
            pOnFreeResource_(pData_);
        }
    }

public:
    qcap2_system_buffer(PVOID pData, qcap2_on_free_resource_t pOnFreeResource)
        : pData_(pData), nDataSize_(0), pOnFreeResource_(pOnFreeResource) {}

    void set_data_size(ULONG size) { nDataSize_ = size; }
    ULONG get_data_size() const { return nDataSize_; }

    PVOID get_data() const override { return pData_; }
    qcap2_buffer_type_t get_type() const override { return QCAP2_BUFFER_TYPE_SYSTEM; }
    PVOID get_native_handle() const override { return NULL; }

    QRESULT get_pts(int64_t* pts) override {
        if (!pts) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *pts = pts_;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_pts(int64_t pts) override {
        pts_ = pts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_dts(int64_t* dts) override {
        if (!dts) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *dts = dts_;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_dts(int64_t dts) override {
        dts_ = dts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_stream_index(int* idx) override {
        if (!idx) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *idx = stream_idx_;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_stream_index(int idx) override {
        stream_idx_ = idx;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT is_keyframe(BOOL* key) override {
        if (!key) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *key = keyframe_;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_keyframe(BOOL key) override {
        keyframe_ = key;
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT get_data_ptr(uint8_t** data, int* size) override {
        if (!data || !size) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *data = (uint8_t*)pData_;
        *size = (int)nDataSize_;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_video_property(ULONG* colorspace, ULONG* width, ULONG* height) override {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }
    QRESULT get_plane(int plane, uint8_t** data, int* stride) override {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }
};

// Private legacy overlay structures for casting
struct qcap2_av_frame_priv_t {
    ULONG nColorSpaceType;
    ULONG nWidth;
    ULONG nHeight;

    ULONG nChannels;
    ULONG nSampleFmt;
    ULONG nSampleFrequency;
    ULONG nFrameSize;

    int nFieldType;
    double dSampleTime;
    int64_t nPTS;
    int64_t nPktPos;

    int64_t nVideoBits;
    int64_t nAudioBits;

    uint8_t* pBuffer[4];
    int pStride[4];
    bool bOwnsBuffer;

    qcap2_dmabuf_t* pDMABuf;
    bool bOwnsDMABuf;
};

struct qcap2_av_packet_priv_t {
    int nStreamIndex;
    BOOL bIsKeyFrame;
    double dSampleTime;
    int64_t nPTS;
    int64_t nDTS;

    uint8_t* pBuffer;
    int nSize;
    bool bOwnsBuffer;
};

#endif // __QCAP2_BUFFER_PRIV_H__
