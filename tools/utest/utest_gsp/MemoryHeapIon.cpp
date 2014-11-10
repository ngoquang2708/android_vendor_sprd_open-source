
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "ion.h"
#include "MemoryHeapIon.h"
 extern int s_log_out;

status_t MemoryHeapIon::mapIonFd(int fd, size_t size, unsigned long memory_type, int uflags)
{
    /* If size is 0, just fail the mmap. There is no way to get the size
     * with ion
     */
    //int map_fd;

    struct ion_allocation_data data;
    struct ion_fd_data fd_data;
    struct ion_handle_data handle_data;
    void *base = NULL;

    data.len = size;
    data.align = getpagesize();
    data.heap_mask = memory_type;
    //if cached buffer , force set the lowest two bits 11
    if((memory_type&(1<<31)))
    {
        data.flags = ((memory_type&(1<<31)) | 3);
    }
    else
    {
        data.flags = 0;
    }

    if (ioctl(fd, ION_IOC_ALLOC, &data) < 0)
    {
        ALOGE("%s: ION_IOC_ALLOC error:%s!\n",__func__,strerror(errno));
        close(fd);
        return -errno;
    }

    if ((uflags & DONT_MAP_LOCALLY) == 0)
    {
        int flags = 0;

        fd_data.handle = data.handle;

        if (ioctl(fd, ION_IOC_SHARE, &fd_data) < 0)
        {
            ALOGE("%s: ION_IOC_SHARE error!\n",__func__);
            handle_data.handle = data.handle;
            ioctl(fd, ION_IOC_FREE, &handle_data);
            close(fd);
            return -errno;
        }

        base = (uint8_t*)mmap(0, size,
                              PROT_READ|PROT_WRITE,
                              MAP_SHARED|flags, fd_data.fd, 0);
        if (base == MAP_FAILED)
        {
            ALOGE("mmap(fd=%d, size=%u) failed (%s)\n", fd, uint32_t(size), strerror(errno));
            handle_data.handle = data.handle;
            ioctl(fd, ION_IOC_FREE, &handle_data);
            close(fd);
            return -errno;
        }
        else
        {
            ALOGE_IF(s_log_out,"mmap success, fd=%d, size=%u, addr=%p\n", fd, uint32_t(size),base);
        }
    }
    mIonHandle = data.handle;
    mIonDeviceFd = fd;



    /*
     * Call this with NULL now and set device with set_device
     * above for consistency sake with how MemoryHeapPmem works.
     */
    mFD = fd_data.fd;
    mBase = base;
    mSize = size;
    mFlags = uflags;

    return NO_ERROR;
}

MemoryHeapIon::MemoryHeapIon() :  mIonHandle(NULL),mIonDeviceFd(-1)
{
}


MemoryHeapIon::MemoryHeapIon(const char* device, size_t size,
                             uint32_t flags, unsigned long memory_types): mIonHandle(NULL),mIonDeviceFd(-1)
{
    int open_flags = O_RDONLY;
    int fd = 0;

    if (flags & NO_CACHING)
        open_flags |= O_SYNC;

    fd = open(device, open_flags);
    if (fd >= 0)
    {
        const size_t pagesize = getpagesize();
        //const size_t pagesize = 4096;
        ALOGE_IF(s_log_out,"open ion success\n");
        size = ((size + pagesize-1) & ~(pagesize-1));
        if (mapIonFd(fd, size, memory_types, flags) == NO_ERROR)
        {
            mDevice = device;
            ALOGE_IF(s_log_out,"alloc ion buffer success\n");
        }
        else
        {
            ALOGE("alloc ion buffer failed\n");
        }
    }
    else
    {
        ALOGE("open ion fail\n");
    }
}


MemoryHeapIon::~MemoryHeapIon()
{
    struct ion_handle_data data;



    /*
     * Due to the way MemoryHeapBase is set up, munmap will never
     * be called so we need to call it ourselves here.
     */
    if(mBase && mSize > 0)
    {
        if (munmap(mBase, mSize) < 0)
        {
            ALOGE("%s:unmap buffer virt addr:%p,size:%d failed, errno:%s\n",__func__,mBase,mSize,strerror(errno));
        }
        else
        {
            ALOGE_IF(s_log_out,"%s:unmap buffer virt addr:%p,size:%d success.\n",__func__,mBase,mSize);
        }
        mBase = 0;
        mSize=0;
    }
    if (mIonDeviceFd > 0)
    {
        data.handle = mIonHandle;
        ioctl(mIonDeviceFd, ION_IOC_FREE, &data);
        close(mIonDeviceFd);
        if(mFD>=0)
        {
            close(mFD);
            mFD = -1;
        }
        ALOGE_IF(s_log_out,"%s:free ion buffer fd:%d\n",__func__,mIonDeviceFd);
        mIonDeviceFd = -1;
        mIonHandle = NULL;
    }
}

int MemoryHeapIon::get_phy_addr_from_ion(unsigned long *phy_addr, size_t *size)
{
    if(mIonDeviceFd<0 || mIonHandle == NULL)
    {
        ALOGE("%s:open dev ion error!\n",__func__);
        return -1;
    }
    else
    {
        int ret;
        struct ion_phys_data phys_data;
        struct ion_custom_data  custom_data;
        phys_data.fd_buffer = mFD;
        custom_data.cmd = ION_SPRD_CUSTOM_PHYS;
        custom_data.arg = (unsigned long)&phys_data;
        ret = ioctl(mIonDeviceFd,ION_IOC_CUSTOM,&custom_data);
        *phy_addr = phys_data.phys;
        *size = phys_data.size;
        if(ret)
        {
            ALOGE("%s: get phy addr error:%d!\n",__func__,ret);
            return -2;
        }
        ALOGE_IF(s_log_out,"%s: get phy addr success:%p!\n",__func__,(void*)*phy_addr);
    }
    return 0;
}

void* MemoryHeapIon::get_virt_addr_from_ion()
{
    return mBase;
}

int MemoryHeapIon::get_gsp_iova(unsigned long *mmu_addr, size_t *size)
{
    if(mIonDeviceFd<0)
    {
        ALOGE("%s:open dev ion error!\n",__func__);
        return -1;
    }
    else
    {
        int ret;
        struct ion_mmu_data mmu_data;
        struct ion_custom_data  custom_data;
        mmu_data.fd_buffer = mFD;
        custom_data.cmd = ION_SPRD_CUSTOM_GSP_MAP;
        custom_data.arg = (unsigned long)&mmu_data;
        ret = ioctl(mIonDeviceFd,ION_IOC_CUSTOM,&custom_data);
        *mmu_addr = mmu_data.iova_addr;
        *size = mmu_data.iova_size;
        if(ret)
        {
            ALOGE("%s: get gsp iova error!\n",__func__);
            return -2;
        }
        //ALOGE_IF(s_log_out,"%s: get iova success! 0x%08x!\n",__func__,*mmu_addr);
    }
    return 0;
}

int MemoryHeapIon::free_gsp_iova(unsigned long mmu_addr, size_t size)
{
    if(mIonDeviceFd<0)
    {
        ALOGE("%s:open dev ion error!\n",__func__);
        return -1;
    }
    else
    {
        int ret;
        struct ion_mmu_data mmu_data;
        struct ion_custom_data  custom_data;
        mmu_data.fd_buffer = mFD;
        mmu_data.iova_addr = mmu_addr;
        mmu_data.iova_size = size;
        custom_data.cmd = ION_SPRD_CUSTOM_GSP_UNMAP;
        custom_data.arg = (unsigned long)&mmu_data;
        ret = ioctl(mIonDeviceFd,ION_IOC_CUSTOM,&custom_data);
        if(ret)
        {
            ALOGE("%s: put gsp iova error!\n",__func__);
            return -2;
        }
    }
    //ALOGE_IF(s_log_out,"%s[%d]: put gsp iova:0x%08x success!\n",__func__,__LINE__,mmu_addr);
    return 0;
}


