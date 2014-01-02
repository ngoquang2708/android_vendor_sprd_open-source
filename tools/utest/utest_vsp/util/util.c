#include <stdlib.h>
#include "util.h"


void* vsp_malloc(unsigned int size, unsigned char alignment)
{
    unsigned char *mem_ptr;
    if (!alignment)
    {
        if ((mem_ptr = (unsigned char*) malloc(size + 1)) != NULL)
        {
            *mem_ptr = (unsigned char)1;
            return ((void *)(mem_ptr+1));
        }
    }
    else
    {
        unsigned char *tmp;

        if ((tmp = (unsigned char *) malloc(size + alignment)) != NULL)
        {
            mem_ptr = (unsigned char *) ((int) (tmp + alignment - 1) & (~(int)(alignment - 1)));

            if (mem_ptr == tmp)
                mem_ptr += alignment;

            *(mem_ptr - 1) = (unsigned char) (mem_ptr - tmp);
            return ((void *)mem_ptr);
        }
    }

    return(NULL);
}


void vsp_free(void *mem_ptr)
{
    unsigned char *ptr;

    if (mem_ptr == NULL)
        return;

    ptr = mem_ptr;
    ptr -= *(ptr - 1);

    free(ptr);
}



void yuv420p_to_yuv420sp(unsigned char* py_src, unsigned char* pu_src, unsigned char* pv_src, unsigned char* py_dst, unsigned char* puv_dst, unsigned int width, unsigned int height)
{
    unsigned int i;

    memcpy(py_dst, py_src, width*height);
    for (i=0; i<width*height/4; i++)
    {
        *puv_dst ++ = *pu_src ++;
        *puv_dst ++ = *pv_src ++;
    }
}

void yuv420p_to_yvu420sp(unsigned char* py_src, unsigned char* pu_src, unsigned char* pv_src, unsigned char* py_dst, unsigned char* pvu_dst, unsigned int width, unsigned int height)
{
    unsigned int i;

    memcpy(py_dst, py_src, width*height);
    for (i=0; i<width*height/4; i++)
    {
        *pvu_dst ++ = *pv_src ++;
        *pvu_dst ++ = *pu_src ++;
    }
}

void yuv420p_to_yuv420sp_opt(unsigned int* py_src, unsigned int* pu_src, unsigned int* pv_src, unsigned int* py_dst, unsigned int* puv_dst, unsigned int width, unsigned int height)
{
    unsigned int a, b;
    unsigned int i;

    memcpy(py_dst, py_src, width*height);
    for (i=0; i<width*height/4/4; i++)
    {
        unsigned int u = *pu_src ++;
        unsigned int v = *pv_src ++;

        a = (u & 0x000000ff) | ((v & 0x000000ff) << 8);
        b = ((u & 0x0000ff00) >> 8) | (v & 0x0000ff00);
        *puv_dst ++ = (b << 16) | a;

        a = ((u & 0xff000000) >> 8) | (v & 0xff000000);
        b = (u & 0x00ff0000) | ((v & 0x00ff0000) << 8);
        *puv_dst ++ =  b | (a >> 16);
    }
}



void yuv420sp_to_yuv420p(unsigned char* py_src, unsigned char* puv_src, unsigned char* py_dst, unsigned char* pu_dst, unsigned char* pv_dst, unsigned int width, unsigned int height)
{
    unsigned int i;

    memcpy(py_dst, py_src, width*height);
    for (i=0; i<width*height/4; i++)
    {
        *pu_dst ++ = *puv_src ++;
        *pv_dst ++ = *puv_src ++;
    }
}

void yvu420sp_to_yuv420p(unsigned char* py_src, unsigned char* pvu_src, unsigned char* py_dst, unsigned char* pu_dst, unsigned char* pv_dst, unsigned int width, unsigned int height)
{
    unsigned int i;

    memcpy(py_dst, py_src, width*height);
    for (i=0; i<width*height/4; i++)
    {
        *pv_dst ++ = *pvu_src ++;
        *pu_dst ++ = *pvu_src ++;
    }
}

void yuv420sp_to_yuv420p_opt(unsigned int* py_src, unsigned int* puv_src, unsigned int* py_dst, unsigned int* pu_dst, unsigned int* pv_dst, unsigned int width, unsigned int height)
{
    unsigned int a, b;
    unsigned int i;

    memcpy(py_dst, py_src, width*height);
    for (i=0; i<width*height/4/4; i++)
    {
        unsigned int uv0 = *puv_src ++;
        unsigned int uv1 = *puv_src ++;

        a = (uv0 & 0x00ff0000) | ((uv1 & 0x00ff0000) >> 16);
        b = ((uv0 & 0x000000ff) << 16) | (uv1 & 0x000000ff);
        *pu_dst ++ =  a | (b << 8);

        a = (uv0 & 0xff000000) | ((uv1 & 0xff000000) >> 16);
        b = ((uv0 & 0x0000ff00) << 16) | (uv1 & 0x0000ff00);
        *pv_dst ++ = (a >> 8) | b;
    }
}





unsigned int find_frame(unsigned char* pbuffer, unsigned int size, unsigned int startcode, unsigned int maskcode)
{
    unsigned int len = 0;
    unsigned int i;

    int flag = 0;

    if (size <= 4)
    {
        return 0;
    }

    for (i=0; i<=size-4; i++)
    {
        unsigned int code = (pbuffer[i] << 24) | (pbuffer[i+1] << 16) | (pbuffer[i+2] << 8) | pbuffer[i+3];
        if ((code & (~maskcode)) == (startcode & (~maskcode)))
        {
            flag = 1;
            break;
        }
    }

    if (flag)
    {
        for (i+=4; i<=size-4; i++)
        {
            unsigned int code = (pbuffer[i] << 24) | (pbuffer[i+1] << 16) | (pbuffer[i+2] << 8) | pbuffer[i+3];
            if ((code & (~maskcode)) == (startcode & (~maskcode)))
            {
                len = i;
                break;
            }
        }
    }

    return len;
}



int read_yuv_frame(unsigned char* py, unsigned char* pu, unsigned char* pv, unsigned int width, unsigned int height, FILE* fp_yuv)
{
    if (fread(py, sizeof(unsigned char), width*height, fp_yuv) != width*height)
    {
        return -1;
    }
    if (fread(pu, sizeof(unsigned char), width/2*height/2, fp_yuv) != width/2*height/2)
    {
        return -1;
    }
    if (fread(pv, sizeof(unsigned char), width/2*height/2, fp_yuv) != width/2*height/2)
    {
        return -1;
    }
    return 0;
}

int write_yuv_frame(unsigned char* py, unsigned char* pu, unsigned char* pv, unsigned int width, unsigned int height, FILE* fp_yuv)
{
    if (fwrite(py, sizeof(unsigned char), width*height, fp_yuv) != width*height)
    {
        return -1;
    }
    if (fwrite(pu, sizeof(unsigned char), width*height/4, fp_yuv) != width*height/4)
    {
        return -1;
    }
    if (fwrite(pv, sizeof(unsigned char), width*height/4, fp_yuv) != width*height/4)
    {
        return -1;
    }
    return 0;
}

