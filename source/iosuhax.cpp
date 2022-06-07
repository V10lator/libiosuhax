/***************************************************************************
 * Copyright (C) 2016
 * by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 ***************************************************************************/
#include "iosuhax.h"
#include <coreinit/debug.h>
#include <coreinit/filesystem.h>
#include <coreinit/filesystem_fsa.h>
#include <coreinit/ios.h>
#include <cstring>
#include <malloc.h>

#define IOSUHAX_MAGIC_WORD     0x4E696365

#define IOCTL_MEM_WRITE        0x00
#define IOCTL_MEM_READ         0x01
#define IOCTL_SVC              0x02
#define IOCTL_MEMCPY           0x04
#define IOCTL_REPEATED_WRITE   0x05
#define IOCTL_KERN_READ32      0x06
#define IOCTL_KERN_WRITE32     0x07
#define IOCTL_READ_OTP         0x08

#define IOCTL_CHECK_IF_IOSUHAX 0x5B

static int iosuhaxHandle = -1;

#define ALIGN(align)      __attribute__((aligned(align)))
#define ALIGN_0x40        ALIGN(0x40)
#define ROUNDUP(x, align) (((x) + ((align) -1)) & ~((align) -1))

FSError IOSUHAX_UnlockFSClient(FSClient *client) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }

    return IOSUHAX_UnlockFSClientEx(FSGetClientBody(client)->clientHandle);
}

FSError IOSUHAX_UnlockFSClientEx(int clientHandle) {
    ALIGN(0x40)
    int dummy[0x40 >> 2];

    if (IOS_Ioctl(clientHandle, 0x28, dummy, sizeof(dummy), dummy, sizeof(dummy)) == 0) {
        return static_cast<FSError>(0);
    }
    return FS_ERROR_PERMISSION_ERROR;
}

#define __FSAShimSetupRequestMount   ((FSError(*)(FSAShimBuffer *, uint32_t, const char *, const char *, uint32_t, void *, uint32_t))(0x101C400 + 0x042f88))
#define __FSAShimSetupRequestUnmount ((FSError(*)(FSAShimBuffer *, uint32_t, const char *, uint32_t))(0x101C400 + 0x43130))
#define __FSAShimSend                ((FSError(*)(FSAShimBuffer *, uint32_t))(0x101C400 + 0x042d90))
#define __FSAStatusToFSStatus        ((FSStatus(*)(FSError))(0x101C400 + 0x58ad4))


FSError IOSUHAX_FSAMount(FSClient *client, const char *source, const char *target, uint32_t flags, void *arg_buf, uint32_t arg_len) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }
    return IOSUHAX_FSAMountEx(FSGetClientBody(client)->clientHandle, source, target, flags, arg_buf, arg_len);
}

FSError IOSUHAX_FSAMountEx(int clientHandle, const char *source, const char *target, uint32_t flags, void *arg_buf, uint32_t arg_len) {
    auto *buffer = (FSAShimBuffer *) memalign(0x40, sizeof(FSAShimBuffer));
    if (!buffer) {
        return FS_ERROR_INVALID_BUFFER;
    }
    auto res = __FSAShimSetupRequestMount(buffer, clientHandle, source, target, flags, arg_buf, arg_len);
    if (res != 0) {
        free(buffer);
        return res;
    }
    res = __FSAShimSend(buffer, 0);
    free(buffer);
    return res;
}

FSError IOSUHAX_FSAUnmount(FSClient *client, const char *mountedTarget) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }
    return IOSUHAX_FSAUnmountEx(FSGetClientBody(client)->clientHandle, mountedTarget);
}

FSError IOSUHAX_FSAUnmountEx(int clientHandle, const char *mountedTarget) {
    auto *buffer = (FSAShimBuffer *) memalign(0x40, sizeof(FSAShimBuffer));
    if (!buffer) {
        return FS_ERROR_INVALID_BUFFER;
    }

    auto res = __FSAShimSetupRequestUnmount(buffer, clientHandle, mountedTarget, 0 /*0x80000000 for FSBindUnmount*/);
    if (res != 0) {
        free(buffer);
        return res;
    }
    res = __FSAShimSend(buffer, 0);
    free(buffer);
    return res;
}

FSError IOSUHAX_FSARawOpen(FSClient *client, char *device_path, int32_t *outHandle) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }
    return IOSUHAX_FSARawOpenEx(FSGetClientBody(client)->clientHandle, device_path, outHandle);
}

FSError IOSUHAX_FSARawOpenEx(int clientHandle, char *device_path, int32_t *outHandle) {
    if (!outHandle) {
        return FS_ERROR_INVALID_BUFFER;
    }
    if (!device_path) {
        return FS_ERROR_INVALID_PATH;
    }
    auto *shim = (FSAShimBuffer *) memalign(0x40, sizeof(FSAShimBuffer));
    if (!shim) {
        return FS_ERROR_INVALID_BUFFER;
    }

    shim->clientHandle   = clientHandle;
    shim->command        = FSA_COMMAND_RAW_OPEN;
    shim->ipcReqType     = FSA_IPC_REQUEST_IOCTL;
    shim->response.word0 = 0xFFFFFFFF;

    FSARequestRawOpen *requestBuffer = &shim->request.rawOpen;

    strncpy(requestBuffer->path, device_path, 0x27F);

    auto res = __FSAShimSend(shim, 0);
    if (res >= 0) {
        *outHandle = shim->response.rawOpen.handle;
    }
    free(shim);
    return res;
}

FSError IOSUHAX_FSARawClose(FSClient *client, int32_t device_handle) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }
    return IOSUHAX_FSARawCloseEx(FSGetClientBody(client)->clientHandle, device_handle);
}

FSError IOSUHAX_FSARawCloseEx(int clientHandle, int32_t device_handle) {
    auto *buffer = (FSAShimBuffer *) memalign(0x40, sizeof(FSAShimBuffer));
    if (!buffer) {
        return FS_ERROR_INVALID_BUFFER;
    }

    buffer->clientHandle   = clientHandle;
    buffer->command        = FSA_COMMAND_RAW_CLOSE;
    buffer->ipcReqType     = FSA_IPC_REQUEST_IOCTL;
    buffer->response.word0 = 0xFFFFFFFF;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    auto *requestBuffer = &buffer->request.rawClose;
#pragma GCC diagnostic pop

    requestBuffer->handle = device_handle;

    auto res = __FSAShimSend(buffer, 0);
    free(buffer);
    return res;
}

FSError IOSUHAX_FSARawRead(FSClient *client, void *data, uint32_t size_bytes, uint32_t cnt, uint64_t blocks_offset, int device_handle) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }
    return IOSUHAX_FSARawReadEx(FSGetClientBody(client)->clientHandle, data, size_bytes, cnt, blocks_offset, device_handle);
}

FSError IOSUHAX_FSARawReadEx(int clientHandle, void *data, uint32_t size_bytes, uint32_t cnt, uint64_t blocks_offset, int device_handle) {
    if (data == nullptr) {
        return FS_ERROR_INVALID_BUFFER;
    }
    auto *shim = (FSAShimBuffer *) memalign(0x40, sizeof(FSAShimBuffer));
    if (!shim) {
        return FS_ERROR_INVALID_BUFFER;
    }

    shim->clientHandle = clientHandle;
    shim->ipcReqType   = FSA_IPC_REQUEST_IOCTLV;
    shim->command      = FSA_COMMAND_RAW_READ;

    shim->ioctlvVecIn  = uint8_t{1};
    shim->ioctlvVecOut = uint8_t{2};

    shim->ioctlvVec[0].vaddr = &shim->request;
    shim->ioctlvVec[0].len   = sizeof(FSARequest);

    auto *tmp = data;

    if ((uint32_t) data & 0x3F) {
        auto *alignedBuffer = memalign(0x40, ROUNDUP(size_bytes * cnt, 0x40));
        if (!alignedBuffer) {
            OSReport("## ERROR: IOSUHAX_FSARawReadEx buffer not aligned (%08X).\n", data);
            return FS_ERROR_INVALID_ALIGNMENT;
        }
        OSReport("## WARNING: IOSUHAX_FSARawReadEx buffer not aligned (%08X). Align to 0x40 for best performance\n", data);
        tmp = alignedBuffer;
    }

    shim->ioctlvVec[1].vaddr = (void *) tmp;
    shim->ioctlvVec[1].len   = size_bytes * cnt;

    shim->ioctlvVec[2].vaddr = &shim->response;
    shim->ioctlvVec[2].len   = sizeof(FSAResponse);

    auto &request         = shim->request.rawRead;
    request.blocks_offset = blocks_offset;
    request.count         = cnt;
    request.size          = size_bytes;
    request.device_handle = device_handle;

    auto res = __FSAShimSend(shim, 0);
    if (res >= 0 && tmp != data) {
        memcpy(data, tmp, size_bytes * cnt);
    }
    if (tmp != data) {
        free(tmp);
    }

    free(shim);
    return res;
}

FSError IOSUHAX_FSARawWrite(FSClient *client, const void *data, uint32_t size_bytes, uint32_t cnt, uint64_t blocks_offset, int device_handle) {
    if (!client) {
        return FS_ERROR_INVALID_CLIENTHANDLE;
    }
    return IOSUHAX_FSARawWriteEx(FSGetClientBody(client)->clientHandle, data, size_bytes, cnt, blocks_offset, device_handle);
}

FSError IOSUHAX_FSARawWriteEx(int clientHandle, const void *data, uint32_t size_bytes, uint32_t cnt, uint64_t blocks_offset, int device_handle) {
    auto *shim = (FSAShimBuffer *) memalign(0x40, sizeof(FSAShimBuffer));
    if (!shim) {
        return FS_ERROR_INVALID_BUFFER;
    }

    shim->clientHandle = clientHandle;
    shim->ipcReqType   = FSA_IPC_REQUEST_IOCTLV;
    shim->command      = FSA_COMMAND_RAW_WRITE;

    shim->ioctlvVecIn  = uint8_t{2};
    shim->ioctlvVecOut = uint8_t{1};

    shim->ioctlvVec[0].vaddr = &shim->request;
    shim->ioctlvVec[0].len   = sizeof(FSARequest);

    void *tmp = (void *) data;
    if ((uint32_t) data & 0x3F) {
        auto *alignedBuffer = memalign(0x40, ROUNDUP(size_bytes * cnt, 0x40));
        if (!alignedBuffer) {
            OSReport("## ERROR: IOSUHAX_FSARawWriteEx buffer not aligned (%08X).\n", data);
            return FS_ERROR_INVALID_ALIGNMENT;
        }
        OSReport("## WARNING: IOSUHAX_FSARawWriteEx buffer not aligned (%08X). Align to 0x40 for best performance\n", data);
        tmp = alignedBuffer;
        memcpy(tmp, data, size_bytes * cnt);
    }

    shim->ioctlvVec[1].vaddr = tmp;
    shim->ioctlvVec[1].len   = size_bytes * cnt;

    shim->ioctlvVec[2].vaddr = &shim->response;
    shim->ioctlvVec[2].len   = sizeof(FSAResponse);

    auto &request         = shim->request.rawRead;
    request.blocks_offset = blocks_offset;
    request.count         = cnt;
    request.size          = size_bytes;
    request.device_handle = device_handle;

    auto res = __FSAShimSend(shim, 0);

    if (tmp != data) {
        free(tmp);
    }

    free(shim);
    return res;
}

int IOSUHAX_Open(const char *dev) {
    if (iosuhaxHandle >= 0)
        return iosuhaxHandle;

    iosuhaxHandle = IOS_Open((char *) (dev ? dev : "/dev/iosuhax"), static_cast<IOSOpenMode>(0));
    if (iosuhaxHandle >= 0 && dev) { //make sure device is actually iosuhax

        ALIGN_0x40 int res[0x40 >> 2];
        *res = 0;

        IOS_Ioctl(iosuhaxHandle, IOCTL_CHECK_IF_IOSUHAX, (void *) nullptr, 0, res, 4);
        if (*res != IOSUHAX_MAGIC_WORD) {
            IOS_Close(iosuhaxHandle);
            iosuhaxHandle = -1;
        }
    }

    return iosuhaxHandle;
}

int IOSUHAX_Close(void) {
    if (iosuhaxHandle < 0)
        return 0;

    int res       = IOS_Close(iosuhaxHandle);
    iosuhaxHandle = -1;
    return res;
}

int IOSUHAX_memwrite(uint32_t address, const uint8_t *buffer, uint32_t size) {
    if (iosuhaxHandle < 0)
        return iosuhaxHandle;

    uint32_t *io_buf = (uint32_t *) memalign(0x40, ROUNDUP(size + 4, 0x40));
    if (!io_buf)
        return -2;

    io_buf[0] = address;
    memcpy(io_buf + 1, buffer, size);

    int res = IOS_Ioctl(iosuhaxHandle, IOCTL_MEM_WRITE, io_buf, size + 4, 0, 0);

    free(io_buf);
    return res;
}

int IOSUHAX_ODM_GetDiscKey(uint8_t *discKey) {
    int res = -1;
    if (discKey == NULL) {
        return -2;
    }
    int odm_handle = IOS_Open("/dev/odm", static_cast<IOSOpenMode>(1));
    res            = odm_handle;
    if (odm_handle >= 0) {
        uint32_t io_buffer[0x40 / 4];
        // disc encryption key, only works with patched IOSU
        io_buffer[0] = 3;
        res          = IOS_Ioctl(odm_handle, 0x06, io_buffer, 0x14, io_buffer, 0x20);
        if (res == 0) {
            memcpy(discKey, io_buffer, 16);
        }
        IOS_Close(odm_handle);
    }
    return res;
}

int IOSUHAX_memread(uint32_t address, uint8_t *out_buffer, uint32_t size) {
    if (iosuhaxHandle < 0)
        return iosuhaxHandle;

    ALIGN_0x40 int io_buf[0x40 >> 2];
    io_buf[0] = address;

    void *tmp_buf = NULL;

    if (((uintptr_t) out_buffer & 0x1F) || (size & 0x1F)) {
        tmp_buf = (uint32_t *) memalign(0x40, ROUNDUP(size, 0x40));
        if (!tmp_buf)
            return -2;
    }

    int res = IOS_Ioctl(iosuhaxHandle, IOCTL_MEM_READ, io_buf, sizeof(address), tmp_buf ? tmp_buf : out_buffer, size);

    if (res >= 0 && tmp_buf)
        memcpy(out_buffer, tmp_buf, size);

    free(tmp_buf);
    return res;
}

int IOSUHAX_memcpy(uint32_t dst, uint32_t src, uint32_t size) {
    if (iosuhaxHandle < 0)
        return iosuhaxHandle;

    ALIGN_0x40 uint32_t io_buf[0x40 >> 2];
    io_buf[0] = dst;
    io_buf[1] = src;
    io_buf[2] = size;

    return IOS_Ioctl(iosuhaxHandle, IOCTL_MEMCPY, io_buf, 3 * sizeof(uint32_t), 0, 0);
}

int IOSUHAX_kern_write32(uint32_t address, uint32_t value) {
    if (iosuhaxHandle < 0)
        return iosuhaxHandle;

    ALIGN_0x40 uint32_t io_buf[0x40 >> 2];
    io_buf[0] = address;
    io_buf[1] = value;

    return IOS_Ioctl(iosuhaxHandle, IOCTL_KERN_WRITE32, io_buf, 2 * sizeof(uint32_t), 0, 0);
}

int IOSUHAX_read_otp(uint8_t *out_buffer, uint32_t size) {
    if (iosuhaxHandle < 0) {
        return iosuhaxHandle;
    }

    ALIGN_0x40 uint32_t io_buf[0x400 >> 2];

    int res = IOS_Ioctl(iosuhaxHandle, IOCTL_READ_OTP, 0, 0, io_buf, 0x400);

    if (res >= 0) {
        memcpy(out_buffer, io_buf, size > 0x400 ? 0x400 : size);
    }

    return res;
}

extern int bspRead(const char *, uint32_t, const char *, uint32_t, uint16_t *);

int IOSUHAX_read_seeprom(uint8_t *out_buffer, uint32_t offset, uint32_t size) {
    if (out_buffer == nullptr || offset > 0x200 || offset & 0x01) {
        return -1;
    }

    uint32_t sizeInShorts   = size >> 1;
    uint32_t offsetInShorts = offset >> 1;
    int32_t maxReadCount    = 0x100 - offsetInShorts;

    if (maxReadCount <= 0) {
        return 0;
    }

    uint32_t count = sizeInShorts > (uint32_t) maxReadCount ? (uint32_t) maxReadCount : sizeInShorts;
    auto *ptr      = (uint16_t *) out_buffer;

    int res = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (bspRead("EE", offsetInShorts + i, "access", 2, ptr) != 0) {
            return -2;
        }
        res += 2;
        ptr++;
    }

    return res;
}

int IOSUHAX_kern_read32(uint32_t address, uint32_t *out_buffer, uint32_t count) {
    if (iosuhaxHandle < 0)
        return iosuhaxHandle;

    ALIGN_0x40 uint32_t io_buf[0x40 >> 2];
    io_buf[0] = address;

    void *tmp_buf = NULL;

    if (((uintptr_t) out_buffer & 0x1F) || ((count * 4) & 0x1F)) {
        tmp_buf = (uint32_t *) memalign(0x40, ROUNDUP((count * 4), 0x40));
        if (!tmp_buf)
            return -2;
    }

    int res = IOS_Ioctl(iosuhaxHandle, IOCTL_KERN_READ32, io_buf, sizeof(address), tmp_buf ? tmp_buf : out_buffer, count * 4);

    if (res >= 0 && tmp_buf)
        memcpy(out_buffer, tmp_buf, count * 4);

    free(tmp_buf);
    return res;
}

int IOSUHAX_SVC(uint32_t svc_id, uint32_t *args, uint32_t arg_cnt) {
    if (iosuhaxHandle < 0)
        return iosuhaxHandle;

    ALIGN_0x40 uint32_t arguments[0x40 >> 2];
    arguments[0] = svc_id;

    if (args && arg_cnt) {
        if (arg_cnt > 8)
            arg_cnt = 8;

        memcpy(arguments + 1, args, arg_cnt * 4);
    }

    ALIGN_0x40 int result[0x40 >> 2];
    int ret = IOS_Ioctl(iosuhaxHandle, IOCTL_SVC, arguments, (1 + arg_cnt) * 4, result, 4);
    if (ret < 0)
        return ret;

    return *result;
}
