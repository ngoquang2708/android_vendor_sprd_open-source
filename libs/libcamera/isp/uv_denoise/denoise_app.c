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
#include "isp_stub_proc.h"
#include "denoise_app.h"
static sem_t denoise_sem_lock;
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


void isp_uv_denoise(struct isp_denoise_input* uv_denoise_in)
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

	cnr_in = (int8_t *)malloc((uv_denoise_in->InputHeight * uv_denoise_in->InputWidth)>>1);
	cnr_out = (int8_t *)malloc((uv_denoise_in->InputHeight * uv_denoise_in->InputWidth)>>1);
	memcpy(cnr_in, (int8_t *)uv_denoise_in->InputAddr, (uv_denoise_in->InputHeight * uv_denoise_in->InputWidth)>>1);
		
		//isp_capability(ISP_DENOISE_INFO, (void*)&denoise_level);

	part0_h = uv_denoise_in->InputHeight / 4;
	part1_h = part0_h;
	part2_h = part0_h;
	part3_h = uv_denoise_in->InputHeight - 3 * part0_h;

	SrcOffsetOne = 0;
	DstOffsetOne = 0;

	SrcOffsetTwo = (part0_h / 2 - 12) * uv_denoise_in->InputWidth* sizeof(int8_t);
	DstOffsetTwo = part0_h / 2 * uv_denoise_in->InputWidth* sizeof(int8_t);

	SrcOffsetThr = (part0_h - 12) * uv_denoise_in->InputWidth* sizeof(int8_t);
	DstOffsetThr = part0_h * uv_denoise_in->InputWidth* sizeof(int8_t);

	SrcOffsetFour = (3 * part0_h /2 - 12) * uv_denoise_in->InputWidth* sizeof(int8_t);
	DstOffsetFour = 3 * part0_h / 2 * uv_denoise_in->InputWidth* sizeof(int8_t);

		/* uv denoise level*/
	uv_param1.dst_uv_image = cnr_out+DstOffsetOne;
	uv_param1.src_uv_image = cnr_in+SrcOffsetOne;
	uv_param1.in_width = uv_denoise_in->InputWidth;
	uv_param1.in_height = part0_h+24;
	uv_param1.out_width = 0;
	uv_param1.out_height = 0;
	uv_param1.max_6_delta = 3;
	uv_param1.max_4_delta = 6;
	uv_param1.max_2_delta = 9;
	uv_param1.task_no = 1;

	uv_param2.dst_uv_image = cnr_out+DstOffsetTwo;
	uv_param2.src_uv_image = cnr_in+SrcOffsetTwo;
	uv_param2.in_width = uv_denoise_in->InputWidth;
	uv_param2.in_height = part1_h+48;
	uv_param2.out_width = 0;
	uv_param2.out_height = 0;
	uv_param2.max_6_delta = 3;
	uv_param2.max_4_delta = 6;
	uv_param2.max_2_delta = 9;
	uv_param2.task_no = 2;

	uv_param3.dst_uv_image = cnr_out+DstOffsetThr;
	uv_param3.src_uv_image = cnr_in+SrcOffsetThr;
	uv_param3.in_width = uv_denoise_in->InputWidth;
	uv_param3.in_height = part2_h+48;
	uv_param3.out_width = 0;
	uv_param3.out_height = 0;
	uv_param3.max_6_delta = 3;
	uv_param3.max_4_delta = 6;
	uv_param3.max_2_delta = 9;
	uv_param3.task_no = 3;

	uv_param4.dst_uv_image = cnr_out+DstOffsetFour;
	uv_param4.src_uv_image = cnr_in+SrcOffsetFour;
	uv_param4.in_width = uv_denoise_in->InputWidth;
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
	memset((void*)uv_denoise_in->InputAddr, 0x00, (uv_denoise_in->InputHeight*uv_denoise_in->InputWidth)>>1);	
	memcpy((void*)uv_denoise_in->InputAddr, (void*)cnr_out,
				(uv_denoise_in->InputHeight*uv_denoise_in->InputWidth)>>1);

	if(cnr_in != NULL){
		free(cnr_in);
		cnr_in = NULL;
	}
	if(cnr_out != NULL){
		free(cnr_out);
		cnr_out = NULL;
	}	

	cpu_hotplug_disable(0);
}

void uv_proc_cb(int evt, void* param)
{
	ALOGE("[STUB PROC] uv_proc_cb called!");
	sem_post(&denoise_sem_lock);
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
}

