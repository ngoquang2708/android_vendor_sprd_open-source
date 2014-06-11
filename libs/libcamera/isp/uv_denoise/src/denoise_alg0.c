#ifdef  TEST_ENV
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#else
#include <sys/types.h>
#include <utils/Log.h>
#include <utils/Timers.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#endif
#include <arm_neon.h>
#include "denoise.h"
#include "denoise_app.h"
//#include "image.h"
#include "isp_stub_proc.h"

#define ABS(_x)   ((_x) < 0 ? -(_x) : (_x))
#define CLIP(_val, _min, _max)  \
        if ((_val) < (_min))    \
            (_val) = (_min);    \
        else if  ((_val) > (_max))   \
            (_val) = (_max);    
		
#define DISABLE_CPU_HOTPLUG	"1"
#define ENABLE_CPU_HOTPLUG	"0"        

#define BOX_SIZE		25
#define BORDER		12
//#define MAX6DELTA	9
//#define MAX4DELTA	6
//#define MAX2DELTA	3
#define MAX_DIST 	32
#define PIXEL_STRIDE	2		//uv interleave


/*void isp_uv_denoise(struct isp_denoise_input uv_denoise_in)
{
	int    ret = 0;
	//struct img_addr den_out;
	int8_t *cnr_in = NULL;
	int8_t *cnr_out = NULL;
	struct uv_denoise uv_denoise_param;
	struct uv_denoise_param0 uv_param1;
	struct uv_denoise_param0 uv_param2;
	struct uv_denoise_param0 uv_param3;
	struct uv_denoise_param0 uv_param4;

	struct uv_denoise_param0 *uv_param1_ptr = &uv_param1;
	struct uv_denoise_param0 *uv_param2_ptr = &uv_param2;
	struct uv_denoise_param0 *uv_param3_ptr = &uv_param3;
	struct uv_denoise_param0 *uv_param4_ptr = &uv_param4;
	uint32_t part0_h = 0;
	uint32_t part1_h = 0;
	uint32_t part2_h = 0;
	uint32_t part3_h = 0;
	uint32_t SrcOffsetOne = 0;
	uint32_t SrcOffsetTwo = 0;
	uint32_t SrcOffsetThr = 0;
	uint32_t SrcOffsetFour = 0;
	uint32_t DstOffsetOne = 0;
	uint32_t DstOffsetTwo = 0;
	uint32_t DstOffsetThr = 0;
	uint32_t DstOffsetFour = 0;
	int8_t *task1src = NULL;
	int8_t *task1dst = NULL;
	int8_t *task2src = NULL;
	int8_t *task2dst = NULL;
	int8_t *task3src = NULL;
	int8_t *task3dst = NULL;
	int8_t *task4src = NULL;
	int8_t *task4dst = NULL;
	nsecs_t oldtime,newtime;
	
	ret = cpu_hotplug_disable(1);
	if(0 != ret) {
		ALOGE("Failed to disable cpu_hotplug, directly return!");
		return;
	}

	cnr_in = (int8_t *)malloc((uv_denoise_in.InputHeight * uv_denoise_in.InputWidth)>>1);
	cnr_out = (int8_t *)malloc((uv_denoise_in.InputHeight * uv_denoise_in.InputWidth)>>1);
	memcpy(cnr_in, (int8_t *)uv_denoise_in.InputAddr, (uv_denoise_in.InputHeight * uv_denoise_in.InputWidth)>>1);
		
		//isp_capability(ISP_DENOISE_INFO, (void*)&denoise_level);

	part0_h = uv_denoise_in.InputHeight / 4;
	part1_h = part0_h;
	part2_h = part0_h;
	part3_h = uv_denoise_in.InputHeight - 3 * part0_h;

	SrcOffsetOne = 0;
	DstOffsetOne = 0;

	SrcOffsetTwo = (part0_h / 2 - 12) * uv_denoise_in.InputWidth* sizeof(int8_t);
	DstOffsetTwo = part0_h / 2 * uv_denoise_in.InputWidth* sizeof(int8_t);

	SrcOffsetThr = (part0_h - 12) * uv_denoise_in.InputWidth* sizeof(int8_t);
	DstOffsetThr = part0_h * uv_denoise_in.InputWidth* sizeof(int8_t);

	SrcOffsetFour = (3 * part0_h /2 - 12) * uv_denoise_in.InputWidth* sizeof(int8_t);
	DstOffsetFour = 3 * part0_h / 2 * uv_denoise_in.InputWidth* sizeof(int8_t);

		
	uv_param1.dst_uv_image = cnr_out+DstOffsetOne;
	uv_param1.src_uv_image = cnr_in+SrcOffsetOne;
	uv_param1.in_width = uv_denoise_in.InputWidth;
	uv_param1.in_height = part0_h+24;
	uv_param1.out_width = 0;
	uv_param1.out_height = 0;
	uv_param1.max_6_delta = 3;
	uv_param1.max_4_delta = 6;
	uv_param1.max_2_delta = 9;
	uv_param1.task_no = 1;

	uv_param2.dst_uv_image = cnr_out+DstOffsetTwo;
	uv_param2.src_uv_image = cnr_in+SrcOffsetTwo;
	uv_param2.in_width = uv_denoise_in.InputWidth;
	uv_param2.in_height = part1_h+48;
	uv_param2.out_width = 0;
	uv_param2.out_height = 0;
	uv_param2.max_6_delta = 3;
	uv_param2.max_4_delta = 6;
	uv_param2.max_2_delta = 9;
	uv_param2.task_no = 2;

	uv_param3.dst_uv_image = cnr_out+DstOffsetThr;
	uv_param3.src_uv_image = cnr_in+SrcOffsetThr;
	uv_param3.in_width = uv_denoise_in.InputWidth;
	uv_param3.in_height = part2_h+48;
	uv_param3.out_width = 0;
	uv_param3.out_height = 0;
	uv_param3.max_6_delta = 3;
	uv_param3.max_4_delta = 6;
	uv_param3.max_2_delta = 9;
	uv_param3.task_no = 3;

	uv_param4.dst_uv_image = cnr_out+DstOffsetFour;
	uv_param4.src_uv_image = cnr_in+SrcOffsetFour;
	uv_param4.in_width = uv_denoise_in.InputWidth;
	uv_param4.in_height = part3_h+24;
	uv_param4.out_width = 0;
	uv_param4.out_height = 0;
	uv_param4.max_6_delta = 3;
	uv_param4.max_4_delta = 6;
	uv_param4.max_2_delta = 9;
	uv_param4.task_no = 4;
	sem_init(&denoise_sem_lock, 0, 0);
		
 	isp_stub_process(THREAD_0,
					uv_proc_func_neon58,
					uv_proc_cb,
					0,
					(void*)uv_param1_ptr);

	isp_stub_process(THREAD_1,
					uv_proc_func_neon58,
					uv_proc_cb,
					0,
					(void*)uv_param2_ptr);
				
	isp_stub_process(THREAD_2,
					uv_proc_func_neon58,
					uv_proc_cb,
					0,
					(void*)uv_param3_ptr);

	isp_stub_process(THREAD_3,
					uv_proc_func_neon58,
					uv_proc_cb,
					0,
					(void*)uv_param4_ptr);
		
		
	sem_wait(&denoise_sem_lock);
	sem_wait(&denoise_sem_lock);
	sem_wait(&denoise_sem_lock);
	sem_wait(&denoise_sem_lock);		
	isp_stub_process(THREAD_0, NULL, NULL, 1, NULL);
	isp_stub_process(THREAD_1, NULL, NULL, 1, NULL);
	isp_stub_process(THREAD_2, NULL, NULL, 1, NULL);
	isp_stub_process(THREAD_3, NULL, NULL, 1, NULL);
		
	//den_out.addr_u = (uint32_t)cnr_out;	
	memset((void*)uv_denoise_in.InputAddr, 0x00, (uv_denoise_in.InputHeight*uv_denoise_in.InputWidth)>>1);	
	memcpy((void*)uv_denoise_in.InputAddr, (void*)cnr_out,
				(uv_denoise_in.InputHeight*uv_denoise_in.InputWidth)>>1);

	if(cnr_in != NULL){
		free(cnr_in);
		cnr_in = NULL;
	}
	if(cnr_out != NULL){
		free(cnr_out);
		cnr_out = NULL;
	}	

	cpu_hotplug_disable(0);
}*/
int uv_proc_func_neon58(void* param_uv_in)
{	
	struct uv_denoise_param0 *uv_param = (struct uv_denoise_param0 *)param_uv_in;
	int8_t *dst = uv_param->dst_uv_image;
	int8_t *src = uv_param->src_uv_image;
	uint32_t w = uv_param->in_width;
	uint32_t h = uv_param->in_height;
	int32 MAX6DELTA = uv_param->max_6_delta;
	int32 MAX4DELTA = uv_param->max_4_delta;
	int32 MAX2DELTA = uv_param->max_2_delta;
	int32 task_num = uv_param->task_no;
	uv_param->out_width = w;
	if(2 == task_num || 3 == task_num){
		uv_param->out_height = h -24;
		}
	else{
		uv_param->out_height = h - 12;
		}
	const uint8_t dist_tbl[16]={17,15,12,10,8,7,5,4,3,2,2,1,1,0,0,0};
	const uint8_t pos_weight[9] = {28,31,28,31,32,31,28,31,28};
	
	const uint32_t border = 12;
	const uint8_t sample_tbl[8] = {1, 2, 4, 4, 6, 6, 6, 6};
	uint8_t *src_ptr = NULL;
	uint8_t *dst_ptr = NULL;
	uint32_t line_stride = 0; 
	uint32_t i, j;
	uint32_t skip_num = 0;
	uint32_t rel_h = 0;
	uint32_t rel_w = 0;
	uint32_t	shift_size;
	shift_size = 2 * border / 6;
	w /= 2;
	h /= 2;

	line_stride = w * 2;
	src_ptr = (uint8_t *)src;
	dst_ptr = (uint8_t *)dst;
	skip_num = border * line_stride;

	if(1 == task_num)
	{
		memcpy(dst_ptr, src_ptr, skip_num);
		src_ptr += skip_num;
		dst_ptr += skip_num;
	}
	else
	{
		src_ptr += skip_num;

	}

	rel_h = h - 2 * border;
	rel_w = w - 2 * border;

	for (i=0; i<rel_h; i++)
	{
		memcpy(dst_ptr, src_ptr, 2 * border);
		src_ptr += 2 * border;
		dst_ptr += 2 * border;
		for(j=0; j<rel_w; j+=8)
		{
			uint32_t shift_stride;
			uint8_t *tmp_ptr;
			int32_t k, l;
			int32_t u, v;
			uint32_t m,n;
			int32_t sum_weight;
			int32_t sum_value;
			int32_t d = 0;
			
			uint8x8x2_t v_center_uv;
			uint8x8x2_t v_uv;
			uint16x8x2_t v_delta;
			uint8_t *src_line_ptr;
			uint8_t center_u[8];
			uint8_t center_v[8];
			uint8_t sample_u[8];
			uint8_t sample_v[8];
			
			uint16x8_t v_one;
			uint16x8x2_t v_result;
			uint16x8_t   v_thr;
			uint16x8x2_t v_delta_thr;
			uint16x8x2_t v_sum_delta_0;
			uint16x8x2_t v_sum_delta_1;
			uint16x8x2_t v_sum_delta_2;
			uint16x8_t v_temp_u16x8;
			uint16x8x2_t v_result_0;
			uint16x8x2_t v_result_1;
			uint16x8x2_t v_result_2;
			uint8x8x2_t v_temp_8x8x2;
			uint8x8_t v_sample_tbl;
			uint8x8x2_t v_sample;

			uint32_t shift;
			
			v_center_uv = vld2_u8(src_ptr);
			vst1_u8(center_u, v_center_uv.val[0]);
			vst1_u8(center_v, v_center_uv.val[1]);
			v_delta.val[0] = vdupq_n_u16(0);	//val[0] for u
			v_delta.val[1] = vdupq_n_u16(0);	//val[1] for v
			//upper
			src_line_ptr = src_ptr - line_stride;
			v_uv = vld2_u8(src_line_ptr);
			v_delta.val[0] = vabal_u8(v_delta.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_delta.val[1] = vabal_u8(v_delta.val[1], v_uv.val[1], v_center_uv.val[1]);

			//left
			src_line_ptr = src_ptr - 2;
			v_uv = vld2_u8(src_line_ptr);
			v_delta.val[0] = vabal_u8(v_delta.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_delta.val[1] = vabal_u8(v_delta.val[1], v_uv.val[1], v_center_uv.val[1]);
		

			//right
			src_line_ptr = src_ptr + 2;
			v_uv = vld2_u8(src_line_ptr);
			v_delta.val[0] = vabal_u8(v_delta.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_delta.val[1] = vabal_u8(v_delta.val[1], v_uv.val[1], v_center_uv.val[1]);	

			//bottom
			src_line_ptr = src_ptr + line_stride;
			v_uv = vld2_u8(src_line_ptr);
			v_delta.val[0] = vabal_u8(v_delta.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_delta.val[1] = vabal_u8(v_delta.val[1], v_uv.val[1], v_center_uv.val[1]);	

			v_delta.val[0] = vrshrq_n_u16(v_delta.val[0], 2);
			v_delta.val[1] = vrshrq_n_u16(v_delta.val[1], 2);

			v_temp_u16x8 = vdupq_n_u16(0);
			v_one = vdupq_n_u16(1);
			v_temp_u16x8 = vcltq_u16(v_delta.val[0], v_one);
			v_delta.val[0] = vaddq_u16(v_delta.val[0], v_temp_u16x8);

			v_temp_u16x8 = vdupq_n_u16(0);
			v_temp_u16x8 = vcltq_u16(v_delta.val[1], v_one);
			v_delta.val[1] = vaddq_u16(v_delta.val[1], v_temp_u16x8);

			
			v_result.val[0] = vdupq_n_u16(0);
			v_result.val[1] = vdupq_n_u16(0);

			shift = shift_size * 1;
			shift_stride = shift * 2;
			v_thr = vdupq_n_u16(MAX2DELTA);  
			v_delta_thr.val[0] = vmulq_u16(v_delta.val[0], v_thr);
			v_delta_thr.val[1] = vmulq_u16(v_delta.val[1], v_thr);

			v_sum_delta_0.val[0] = vdupq_n_u16(0);
			v_sum_delta_0.val[1] = vdupq_n_u16(0);

			src_line_ptr = src_ptr - line_stride * shift;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_0.val[0] = vabal_u8(v_sum_delta_0.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_0.val[1] = vabal_u8(v_sum_delta_0.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_0.val[0] = vabal_u8(v_sum_delta_0.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_0.val[1] = vabal_u8(v_sum_delta_0.val[1], v_uv.val[1], v_center_uv.val[1]);

			src_line_ptr = src_ptr;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_0.val[0] = vabal_u8(v_sum_delta_0.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_0.val[1] = vabal_u8(v_sum_delta_0.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_0.val[0] = vabal_u8(v_sum_delta_0.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_0.val[1] = vabal_u8(v_sum_delta_0.val[1], v_uv.val[1], v_center_uv.val[1]);

			src_line_ptr = src_ptr + line_stride * shift;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_0.val[0] = vabal_u8(v_sum_delta_0.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_0.val[1] = vabal_u8(v_sum_delta_0.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_0.val[0] = vabal_u8(v_sum_delta_0.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_0.val[1] = vabal_u8(v_sum_delta_0.val[1], v_uv.val[1], v_center_uv.val[1]);

			
			//if (sum_delta < MAX2DELTA * delta), v_result = 1
			v_result.val[0] = vcltq_u16(v_sum_delta_0.val[0], v_delta_thr.val[0]);
			v_result.val[1] = vcltq_u16(v_sum_delta_0.val[1], v_delta_thr.val[1]);

			shift = shift_size * 2;
			shift_stride = shift * 2;
			v_thr = vdupq_n_u16(MAX4DELTA);
			v_delta_thr.val[0] = vmulq_u16(v_delta.val[0], v_thr);
			v_delta_thr.val[1] = vmulq_u16(v_delta.val[1], v_thr);

			v_sum_delta_1.val[0] = vdupq_n_u16(0);
			v_sum_delta_1.val[1] = vdupq_n_u16(0);
			
			src_line_ptr = src_ptr - line_stride * shift;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_1.val[0] = vabal_u8(v_sum_delta_1.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_1.val[1] = vabal_u8(v_sum_delta_1.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_1.val[0] = vabal_u8(v_sum_delta_1.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_1.val[1] = vabal_u8(v_sum_delta_1.val[1], v_uv.val[1], v_center_uv.val[1]);

			src_line_ptr = src_ptr;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_1.val[0] = vabal_u8(v_sum_delta_1.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_1.val[1] = vabal_u8(v_sum_delta_1.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_1.val[0] = vabal_u8(v_sum_delta_1.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_1.val[1] = vabal_u8(v_sum_delta_1.val[1], v_uv.val[1], v_center_uv.val[1]);

			src_line_ptr = src_ptr + line_stride * shift;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_1.val[0] = vabal_u8(v_sum_delta_1.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_1.val[1] = vabal_u8(v_sum_delta_1.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_1.val[0] = vabal_u8(v_sum_delta_1.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_1.val[1] = vabal_u8(v_sum_delta_1.val[1], v_uv.val[1], v_center_uv.val[1]);

			//if (sum_delta < MAX4DELTA * delta), v_result = 1
			v_temp_u16x8 = vcltq_u16(v_sum_delta_1.val[0], v_delta_thr.val[0]);
			//v_result << 1;
			v_result_1.val[0] = vshlq_n_u16(v_temp_u16x8, 1);
			v_temp_u16x8 = vcltq_u16(v_sum_delta_1.val[1], v_delta_thr.val[1]);
			v_result_1.val[1] = vshlq_n_u16(v_temp_u16x8, 1);

			v_result.val[0] = vaddq_u16(v_result.val[0], v_result_1.val[0]);
			v_result.val[1] = vaddq_u16(v_result.val[1], v_result_1.val[1]);
			//////////////////////////////////////////////////////////////////

			shift = shift_size * 3;
			shift_stride = shift * 2;
			v_thr = vdupq_n_u16(MAX6DELTA);
			
			v_delta_thr.val[0] = vmulq_u16(v_delta.val[0], v_thr);
			v_delta_thr.val[1] = vmulq_u16(v_delta.val[1], v_thr);

			v_sum_delta_2.val[0] = vdupq_n_u16(0);
			v_sum_delta_2.val[1] = vdupq_n_u16(0);

			src_line_ptr = src_ptr - line_stride * shift;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_2.val[0] = vabal_u8(v_sum_delta_2.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_2.val[1] = vabal_u8(v_sum_delta_2.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_2.val[0] = vabal_u8(v_sum_delta_2.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_2.val[1] = vabal_u8(v_sum_delta_2.val[1], v_uv.val[1], v_center_uv.val[1]);

			src_line_ptr = src_ptr;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_2.val[0] = vabal_u8(v_sum_delta_2.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_2.val[1] = vabal_u8(v_sum_delta_2.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_2.val[0] = vabal_u8(v_sum_delta_2.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_2.val[1] = vabal_u8(v_sum_delta_2.val[1], v_uv.val[1], v_center_uv.val[1]);

			src_line_ptr = src_ptr + line_stride * shift;
			v_uv = vld2_u8(src_line_ptr - shift_stride);
			v_sum_delta_2.val[0] = vabal_u8(v_sum_delta_2.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_2.val[1] = vabal_u8(v_sum_delta_2.val[1], v_uv.val[1], v_center_uv.val[1]);
			v_uv = vld2_u8(src_line_ptr + shift_stride);
			v_sum_delta_2.val[0] = vabal_u8(v_sum_delta_2.val[0], v_uv.val[0], v_center_uv.val[0]);
			v_sum_delta_2.val[1] = vabal_u8(v_sum_delta_2.val[1], v_uv.val[1], v_center_uv.val[1]);

			//if (sum_delta < MAX6DELTA * delta), v_result = 1
			v_temp_u16x8 = vcltq_u16(v_sum_delta_2.val[0], v_delta_thr.val[0]);			
			//v_result << 2;
			v_result_2.val[0] = vshlq_n_u16(v_temp_u16x8, 2);
			v_temp_u16x8 = vcltq_u16(v_sum_delta_2.val[1], v_delta_thr.val[1]);
			v_result_2.val[1] = vshlq_n_u16(v_temp_u16x8, 2);

			v_result.val[0] = vaddq_u16(v_result.val[0], v_result_2.val[0]);
			v_result.val[1] = vaddq_u16(v_result.val[1], v_result_2.val[1]);

			///////////////////////////////////////////////////////////
			v_temp_8x8x2.val[0] = vmovn_u16(v_result.val[0]);
			v_temp_8x8x2.val[1] = vmovn_u16(v_result.val[1]);

			v_sample_tbl = vld1_u8(sample_tbl);
			v_sample.val[0] = vtbl1_u8(v_sample_tbl, v_temp_8x8x2.val[0]);
			v_sample.val[1] = vtbl1_u8(v_sample_tbl, v_temp_8x8x2.val[1]);

			vst1_u8(sample_u, v_sample.val[0]);
			vst1_u8(sample_v, v_sample.val[1]);	



			for (m=0; m<8; m++)
			{
				n = 0;
				sum_weight = 0;
				sum_value = 0;
				for (k=-1; k<=1; k++)
				{
					tmp_ptr = src_ptr + m * 2 + (int32_t)line_stride * k * sample_u[m];
					tmp_ptr += sample_u[m] * (-2) * 2;
					for (l=-1; l<=1; l++)
					{
						uint32_t delta;
						uint32_t pos_w = pos_weight[n++];
						uint32_t dist_w;
						uint32_t w;
						
						u = *tmp_ptr;
						tmp_ptr += (sample_u[m] * 2);
						delta = ABS(u - center_u[m]);
						if (delta >= 16)
							dist_w  = 0;
						else
							dist_w = dist_tbl[delta];
						
						w = pos_w * dist_w;
						sum_weight += w;
						sum_value += w * u;
					}
				}

				if (sum_weight > 0)
				{
					u = sum_value / sum_weight;
									}
				else
				{
					u = center_u[m];
				}
				
				*dst_ptr++ = u;

				n=0;
				sum_weight = 0;
				sum_value = 0;
				for (k=-1; k<=1; k++)
				{
					tmp_ptr = src_ptr + m * 2 + (int32_t)line_stride * k * sample_v[m] + 1;
					tmp_ptr += sample_v[m] * (-2) * 2;
					for (l=-1; l<=1; l++)
					{
						uint32_t delta;
						uint32_t pos_w = pos_weight[n++];
						uint32_t dist_w;
						uint32_t w;
						
						v = *tmp_ptr;
						tmp_ptr += (sample_v[m] * 2);
						delta = ABS(v - center_v[m]);
						if (delta >= 16)
							dist_w  = 0;
						else
							dist_w = dist_tbl[delta];
						
						w = pos_w * dist_w;
						sum_weight += w;
						sum_value += w * v;
					}
				}

				if (sum_weight > 0)
				{
					v = sum_value / sum_weight;
					
				}
				else
				{
					v = center_v[m];
				}
				
				*dst_ptr++ = v;
				
			}

			src_ptr += 16;
		}
		memcpy(dst_ptr, src_ptr, border * 2);

		src_ptr += border * 2;
		dst_ptr += border * 2;

	}

	if(4 == task_num)
	{
		memcpy(dst_ptr, src_ptr, skip_num);
	}

	return 0;
}
/*
void uv_proc_cb(int evt, void* param)
{
	ALOGE("[STUB PROC] uv_proc_cb called!");
}

int cpu_hotplug_disable(uint8_t is_disable)
{
	const char* const hotplug_disable = "/sys/devices/system/cpu/cpufreq/sprdemand/cpu_hotplug_disable";
	const char* cmd_str  = DISABLE_CPU_HOTPLUG;
	uint8_t org_flag = 0;
	//int	ret = 0;

	FILE* fp = fopen(hotplug_disable, "w");

	if (!fp) {
		ALOGE("Failed to open: cpu_hotplug_disable");
		return 7;
	}

	ALOGE("cpu hotplug disable %d", is_disable);
	if(1 == is_disable) {
		cmd_str = DISABLE_CPU_HOTPLUG;
	} else {
		cmd_str = ENABLE_CPU_HOTPLUG;
	}
	fprintf(fp, "%s", cmd_str);
	fclose(fp);

	return 0;
}*/
