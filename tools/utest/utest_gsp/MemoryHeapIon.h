#include "ion_sprd.h"


class MemoryHeapIon
{
public:
    MemoryHeapIon(const char*, size_t, uint32_t, long unsigned int);
    MemoryHeapIon();
    ~MemoryHeapIon();
    void* get_virt_addr_from_ion();
    int get_phy_addr_from_ion(unsigned long *phy_addr, size_t *size);
    int get_gsp_iova(unsigned long *mmu_addr, size_t *size);
    int free_gsp_iova(unsigned long mmu_addr, size_t size);
    status_t mapIonFd(int fd, size_t size, unsigned long memory_type, int flags);

    struct ion_handle   *mIonHandle; //handle of data buffer, get from ION_IOC_ALLOC

private:
    int                 mIonDeviceFd;  /*fd we get from open("/dev/ion")*/
    const char*         mDevice;// "/dev/ion"

    int                 mFD;// fd of data buffer
    size_t              mSize;// size of data buffer
    void*               mBase;// arm virtual addr of data buffer, get from mmap
    uint32_t            mFlags;// cache or not ? property of data buffer
};

