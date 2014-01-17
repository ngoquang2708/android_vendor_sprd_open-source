#ifndef UTEST_VSP_UTIL_H_
#define UTEST_VSP_UTIL_H_

#include <stdio.h>

//#define SPRD_ION_WITH_MMU  "/dev/sprd_iommu_mm"
#define SPRD_ION_WITH_MMU "/dev/ion"
#define SPRD_ION_NO_MMU      "/dev/ion"

#ifdef __cplusplus
extern "C"
{
#endif

void* vsp_malloc(unsigned int size, unsigned char alignment);
void vsp_free(void *mem_ptr);

void yuv420p_to_yuv420sp(unsigned char* py_src, unsigned char* pu_src, unsigned char* pv_src, unsigned char* py_dst, unsigned char* puv_dst, unsigned int width, unsigned int height);
void yuv420p_to_yvu420sp(unsigned char* py_src, unsigned char* pu_src, unsigned char* pv_src, unsigned char* py_dst, unsigned char* puv_dst, unsigned int width, unsigned int height);
void yuv420p_to_yuv420sp_opt(unsigned int* py_src, unsigned int* pu_src, unsigned int* pv_src, unsigned int* py_dst, unsigned int* puv_dst, unsigned int width, unsigned int height);

void yuv420sp_to_yuv420p(unsigned char* py_src, unsigned char* puv_src, unsigned char* py_dst, unsigned char* pu_dst, unsigned char* pv_dst, unsigned int width, unsigned int height);
void yvu420sp_to_yuv420p(unsigned char* py_src, unsigned char* pvu_src, unsigned char* py_dst, unsigned char* pu_dst, unsigned char* pv_dst, unsigned int width, unsigned int height);
void yuv420sp_to_yuv420p_opt(unsigned int* py_src, unsigned int* puv_src, unsigned int* py_dst, unsigned int* pu_dst, unsigned int* pv_dst, unsigned int width, unsigned int height);


/* to find a frame in buffer, if found return the length or return 0 */
unsigned int find_frame(unsigned char* pbuffer, unsigned int size, unsigned int startcode, unsigned int maskcode);



int read_yuv_frame(unsigned char* py, unsigned char* pu, unsigned char* pv, unsigned int width, unsigned int height, FILE* fp_yuv);

int write_yuv_frame(unsigned char* py, unsigned char* pu, unsigned char* pv, unsigned int width, unsigned int height, FILE* fp_yuv);



#ifdef __cplusplus
}
#endif


#endif // UTEST_VSP_UTIL_H_

