#ifndef _FACEFINDER_H_
#define _FACEFINDER_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct{
    int face_id;
    int sx;
    int sy;
    int ex;
    int ey;
    int brightness;
    int angle;
    int smile_level;
    int blink_level;
} FaceFinder_Data;


int FaceFinder_Init(int width, int height);
int FaceFinder_Function(unsigned char *src, FaceFinder_Data** ppDstFaces, int *pDstFaceNum ,int skip);
int FaceFinder_Finalize();

#ifdef __cplusplus
}
#endif

#endif
