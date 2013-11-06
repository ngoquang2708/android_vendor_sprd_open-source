/******************************************************************************
 ** File Name:    vpx_dec_api.h                                                  *
 ** Author:                                     		                      *
 ** DATE:         3/15/2007                                                   *
 ** Copyright:    2007 Spreadtrum, Incorporated. All Rights Reserved.         *
 ** Description:  define data structures for Video Codec                      *
 *****************************************************************************/
/******************************************************************************
 **                   Edit    History                                         *
 **---------------------------------------------------------------------------*
 ** DATE          NAME            DESCRIPTION                                 *
 ** 3/15/2007     			      Create.                                     *
 *****************************************************************************/
#ifndef _VPX_DEC_API_H_
#define _VPX_DEC_API_H_

/*----------------------------------------------------------------------------*
**                        Dependencies                                        *
**---------------------------------------------------------------------------*/
//#include "mmcodec.h"
/**---------------------------------------------------------------------------*
 **                             Compiler Flag                                 *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
extern   "C"
{
#endif

#define VP8_DECODER_INTERNAL_BUFFER_SIZE  (0x200000) //(MP4DEC_OR_RUN_SIZE+MP4DEC_OR_INTER_MALLOC_SIZE)  
#define ONEFRAME_BITSTREAM_BFR_SIZE	(1500*1024)  //for bitstream size of one encoded frame.

typedef unsigned char		BOOLEAN;
//typedef unsigned char		Bool;
typedef unsigned char		uint8;
typedef unsigned short		uint16;
typedef unsigned int		uint32;
//typedef unsigned int		uint;

typedef signed char			int8;
typedef signed short		int16;
typedef signed int			int32;

/*standard*/
typedef enum {
    ITU_H263 = 0,
    MPEG4,
    JPEG,
    FLV_V1,
    H264,
    RV8,
    RV9
} VIDEO_STANDARD_E;

typedef enum
{
    MMDEC_OK = 0,
    MMDEC_ERROR = -1,
    MMDEC_PARAM_ERROR = -2,
    MMDEC_MEMORY_ERROR = -3,
    MMDEC_INVALID_STATUS = -4,
    MMDEC_STREAM_ERROR = -5,
    MMDEC_OUTPUT_BUFFER_OVERFLOW = -6,
    MMDEC_HW_ERROR = -7,
    MMDEC_NOT_SUPPORTED = -8,
    MMDEC_FRAME_SEEK_IVOP = -9,
    MMDEC_MEMORY_ALLOCED = -10
} MMDecRet;

// decoder video format structure
typedef struct
{
    int32 	video_std;			//video standard, 0: VSP_ITU_H263, 1: VSP_MPEG4, 2: VSP_JPEG, 3: VSP_FLV_V1
    int32	frame_width;
    int32	frame_height;
    int32	i_extra;
    void 	*p_extra;
//#ifdef _VSP_LINUX_
    void *p_extra_phy;
//#endif
    int32	uv_interleaved;				//tmp add
} MMDecVideoFormat;

// Decoder buffer for decoding structure
typedef struct
{
    uint8	*common_buffer_ptr;     // Pointer to buffer used when decoding
//#ifdef _VSP_LINUX_
    void *common_buffer_ptr_phy;
//#endif
    uint32	size;            		// Number of bytes decoding buffer

    int32 	frameBfr_num;			//YUV frame buffer number

    uint8   *int_buffer_ptr;		// internal memory address
    int32 	int_size;				//internal memory size
} MMCodecBuffer;

typedef MMCodecBuffer MMDecBuffer;

typedef struct
{
    uint16 start_pos;
    uint16 end_pos;
} ERR_POS_T;

#define MAX_ERR_PKT_NUM		30

// Decoder input structure
typedef struct
{
    uint8		*pStream;          	// Pointer to stream to be decoded. Virtual address.
    uint8		*pStream_phy;          	// Pointer to stream to be decoded. Physical address.
    uint32		dataLen;           	// Number of bytes to be decoded
    int32		beLastFrm;			// whether the frame is the last frame.  1: yes,   0: no

    int32		expected_IVOP;		// control flag, seek for IVOP,
    int32		pts;                // presentation time stamp

    int32		beDisplayed;		// whether the frame to be displayed    1: display   0: not //display

    int32		err_pkt_num;		// error packet number
    ERR_POS_T	err_pkt_pos[MAX_ERR_PKT_NUM];		// position of each error packet in bitstream
} MMDecInput;

// Decoder output structure
typedef struct
{
    uint8	*pOutFrameY;     //Pointer to the recent decoded picture
    uint8	*pOutFrameU;
    uint8	*pOutFrameV;

    uint32	frame_width;
    uint32	frame_height;

    int32   is_transposed;	//the picture is transposed or not, in 8800H5, it should always 0.

    int32	pts;            //presentation time stamp
    int32	frameEffective;

    int32	err_MB_num;		//error MB number
//#ifdef _VSP_LINUX_
    void *pBufferHeader;
    int VopPredType;
//#endif
} MMDecOutput;

typedef int (*FunctionType_BufCB)(void *userdata,void *pHeader,int flag);
//typedef int (*FunctionType_MemAllocCB)(/*void *decCtrl,*/ void *userData, unsigned int width,unsigned int height);

/* Application controls, this structed shall be allocated */
/*    and initialized in the application.                 */
typedef struct tagVPXHandle
{
    /* The following fucntion pointer is copied to BitstreamDecVideo structure  */
    /*    upon initialization and never used again. */
//    int (*readBitstreamData)(uint8 *buf, int nbytes_required, void *appData);
//    applicationData appData;

//    uint8 *outputFrame;
    void *videoDecoderData;     /* this is an internal pointer that is only used */
    /* in the decoder library.   */
#ifdef PV_MEMORY_POOL
    int32 size;
#endif
//    int nLayers;
    /* pointers to VOL data for frame-based decoding. */
//    uint8 *volbuf[2];           /* maximum of 2 layers for now */
//    int32 volbuf_size[2];

    void *userdata;

    FunctionType_BufCB VSP_bindCb;
    FunctionType_BufCB VSP_unbindCb;
//        FunctionType_MemAllocCB VSP_extMemCb;
//	void *g_user_data;


} VPXHandle;

/**----------------------------------------------------------------------------*
**                           Function Prototype                               **
**----------------------------------------------------------------------------*/

//void MP4DecReleaseRefBuffers(MP4Handle *mp4Handle);
//int MP4DecGetLastDspFrm(MP4Handle *mp4Handle,void **pOutput);
void VP8DecSetCurRecPic(VPXHandle *vpxHandle, uint8	*pFrameY,uint8 *pFrameY_phy,void *pBufferHeader);
//void MP4DecSetReferenceYUV(MP4Handle *mp4Handle, uint8 *pFrameY);

/*****************************************************************************/
//  Description: Init vpx decoder
//	Global resource dependence:
//  Author:
//	Note:
/*****************************************************************************/
MMDecRet VP8DecInit(VPXHandle *vpxHandle, MMCodecBuffer * pBuffer);

MMDecRet VP8DecHeader(VPXHandle *vpxHandle, MMDecVideoFormat *pVideoFormat);

/*****************************************************************************/
//  Description: Decode one vop
//	Global resource dependence:
//  Author:
//	Note:
/*****************************************************************************/
MMDecRet VP8DecDecode(VPXHandle *vpxHandle, MMDecInput *pInput, MMDecOutput *pOutput);

/*****************************************************************************/
//  Description: Close vpx decoder
//	Global resource dependence:
//  Author:
//	Note:
/*****************************************************************************/
MMDecRet VP8DecRelease(VPXHandle *vpxHandle);

/*****************************************************************************/
//  Description: check whether VSP can used for video decoding or not
//	Global resource dependence:
//  Author:
//	Note: return VSP status:
//        1: dcam is idle and can be used for vsp   0: dcam is used by isp
/*****************************************************************************/
BOOLEAN VPXDEC_VSP_Available (void);

/*****************************************************************************/
//  Description: for display, return one frame for display
//	Global resource dependence:
//  Author:
//	Note:  the transposed type is passed from MMI "req_transposed"
//         req_transposed£º 1£ºtranposed  0: normal
/*****************************************************************************/
void vpxdec_GetOneDspFrm (VPXHandle *vpxHandle, MMDecOutput * pOutput, int req_transposed, int is_last_frame);

//typedef int (*FT_VPXDecGetLastDspFrm)(MP4Handle *mp4Handle,void **pOutput);
typedef void (*FT_VPXDecSetCurRecPic)(VPXHandle *vpxHandle, uint8	*pFrameY,uint8 *pFrameY_phy,void *pBufferHeader);
//typedef void (*FT_VPXDecSetReferenceYUV)(VPXHandle *vpxHandle, uint8 *pFrameY);
typedef MMDecRet (*FT_VPXDecInit)(VPXHandle *vpxHandle, MMCodecBuffer * pBuffer);
typedef MMDecRet (*FT_VPXDecDecode)(VPXHandle *vpxHandle, MMDecInput *pInput,MMDecOutput *pOutput);
typedef MMDecRet (*FT_VPXDecRelease)(VPXHandle *vpxHandle);

//typedef BOOLEAN (*FT_VPXDEC_VSP_Available) (void);

//typedef void (*FT_mpeg4dec_GetOneDspFrm) (MP4Handle *mp4Handle, MMDecOutput * pOutput, int req_transposed, int is_last_frame);

/**----------------------------------------------------------------------------*
**                         Compiler Flag                                      **
**----------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
/**---------------------------------------------------------------------------*/
#endif
// End
