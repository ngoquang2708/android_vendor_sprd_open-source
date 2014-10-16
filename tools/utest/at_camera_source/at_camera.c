#include <stdio.h>
#include <dlfcn.h>
#include<stdlib.h>
#include <signal.h>
#include <cutils/properties.h>
#include <unistd.h>

#define MIPI_YUV_YUV 0x0A
#define MIPI_YUV_JPG 0x09
#define MIPI_RAW_RAW 0x0C
#define MIPI_RAW_JPG 0x0D

enum auto_test_calibration_cmd_id {
	AUTO_TEST_CALIBRATION_AWB= 0,
	AUTO_TEST_CALIBRATION_LSC,
	AUTO_TEST_CALIBRATION_FLASHLIGHT,
	AUTO_TEST_CALIBRATION_CAP_JPG,
	AUTO_TEST_CALIBRATION_CAP_YUV,
	AUTO_TEST_CALIBRATION_MAX
};

#define  MAX_LEN 200
char path_arr[MAX_LEN] = " ";
char func_set_testmode[MAX_LEN] = "_Z21autotest_set_testmodeiiiiii";
char func_cam_from_buf[MAX_LEN] = "_Z21autotest_cam_from_bufPPviPi";
char func_close_testmode[MAX_LEN] = "_Z23autotest_close_testmodev";

/** Base path of the hal modules */
#define HAL_LIBRARY_PATH1 "/system/lib/hw"
#define HAL_LIBRARY_PATH2 "/vendor/lib/hw"

static const char *variant_keys[] = {
    "ro.hardware",  /* This goes first so that it can pick up a different file on the emulator. */
    "ro.product.board",
    "ro.board.platform",
    "ro.arch"
};

static const int HAL_VARIANT_KEYS_COUNT =
    (sizeof(variant_keys)/sizeof(variant_keys[0]));

int find_cam_lib_path(char*path,int len)
{
	char prop[PATH_MAX];
	char name[PATH_MAX]="camera";
	int i = 0;

	if(path==NULL)
		return 1;

	/* Loop through the configuration variants looking for a module */
	for (i=0 ; i<HAL_VARIANT_KEYS_COUNT+1 ; i++) {
		if (i < HAL_VARIANT_KEYS_COUNT) {
			if (property_get(variant_keys[i], prop, NULL) == 0) {
				continue;
			}
			snprintf(path, len, "%s/%s.%s.so",
					 HAL_LIBRARY_PATH2, name, prop);
			if (access(path, R_OK) == 0) break;

			snprintf(path, len, "%s/%s.%s.so",
					 HAL_LIBRARY_PATH1, name, prop);
			if (access(path, R_OK) == 0) break;
		} else {
			snprintf(path, len, "%s/%s.default.so",
					 HAL_LIBRARY_PATH2, name);
			if (access(path, R_OK) == 0) break;

			snprintf(path, len, "%s/%s.default.so",
					 HAL_LIBRARY_PATH1, name);
			if (access(path, R_OK) == 0) break;
		}
	}

return 0;

}

int main(int argc, char **argv)
{
#if 1
	FILE *fp = NULL;
	int filesize ;
	char prefix[MAX_LEN] = "/data/image_save_%s.raw";
	char prefix_jpg[MAX_LEN] = "/data/image_save_%s.jpg";
    char test_name[MAX_LEN] = "/data/image_save_%s.raw";

	void *handle;
 	int ret;

	int camera_interface = MIPI_RAW_RAW;
	int maincmd = 0;
	int image_width = 640 ;
	int image_height = 480;

	typedef int32_t (*autotest_set_testmode)(int camerinterface,int maincmd ,int subcmd,int cameraid,int width,int height);
	typedef int (*autotest_cam_from_buf)(void**pp_image_addr,int size,int *out_size);
	typedef int (*autotest_close_testmode)(void);

	memset(path_arr,0x00,sizeof(path_arr));
	find_cam_lib_path(path_arr,sizeof(path_arr));
    	printf("path =%s\n",path_arr);

	handle = dlopen(path_arr, RTLD_NOW | RTLD_GLOBAL);
	if (!handle){
		char const *err_str = dlerror();
		printf("load: module=%s\n%s", path_arr, err_str?err_str:"unknown");
		printf("%s line= %d \n",__func__,__LINE__);
		return -1;
	}
	printf("%s line= %d \n",__func__,__LINE__);

	// open auto test mode
	autotest_set_testmode set_testmode;
	set_testmode = (autotest_set_testmode)dlsym(handle, func_set_testmode);
	if (set_testmode == NULL) {
		printf("dlopen err:%s.\n",dlerror());
		dlclose(handle);
		printf("%s line=  %d \n",__func__,__LINE__);
		return -1;
	}

	if(MIPI_YUV_JPG == camera_interface || MIPI_RAW_JPG == camera_interface) {
		maincmd = AUTO_TEST_CALIBRATION_CAP_JPG;
		sprintf(test_name,prefix_jpg,(MIPI_RAW_JPG == camera_interface) ? "raw":"yuv");
	} else {
		maincmd = AUTO_TEST_CALIBRATION_CAP_YUV;
		sprintf(test_name,prefix,(MIPI_YUV_YUV == camera_interface) ? "yuv":"raw");
	}
	printf("%s line= %d \n",__func__,__LINE__);
	set_testmode(camera_interface,maincmd,0,0,image_width,image_height);

#if 1
	// get image frome buffer
	autotest_cam_from_buf get_image_from_buf;
	get_image_from_buf = (autotest_cam_from_buf)dlsym(handle, func_cam_from_buf);

	if (get_image_from_buf == NULL) {
		printf("dlopen err:%s.\n",dlerror());
		dlclose(handle);
		printf("%s line= %d \n",__func__,__LINE__);
		return -1;
	}

    printf("%s line= %d \n",__func__,__LINE__);

	int output_image_size=0;

    void *temp_addr = (void *)malloc(image_width * image_height * 2);
	if(temp_addr) {
		   memset((void*)temp_addr, 0x00, image_width *image_height * 2);
	 }

    int result = get_image_from_buf(&temp_addr,image_width*image_height * 2,&output_image_size);

    printf("test_name=%s  output_image_size =%d \n",test_name,output_image_size);

	fp = fopen(test_name, "wb");
	if (fp != NULL) {
		fwrite((void *)temp_addr , 1, output_image_size, fp);
		fclose(fp);
		printf("autotest_cam_from_buf: successed to open save_file.\n");
	} else {
		printf("autotest_cam_from_buf: failed to open save_file.\n");

	}
#endif
	// close auto test mode
	autotest_close_testmode close_testmode;
	close_testmode = (autotest_close_testmode)dlsym(handle, func_close_testmode);
	if (close_testmode == NULL) {
		printf("dlopen err:%s.\n",dlerror());
		dlclose(handle);
		printf("%s line= %d \n",__func__,__LINE__);
		return -1;
	}
	close_testmode();
#else

typedef void (*sighandler_t)(int);

int pox_system(const char *cmd_line){
	   int ret = 0;
	   sighandler_t old_handler;

	   old_handler = signal(SIGCHLD, SIG_DFL);
	   ret = system(cmd_line);
	   signal(SIGCHLD, old_handler);

	   return ret;
}


const char cmd_line[]={"/system/xbin/autest_camera_sc8830   w 640 maincmd 0    h 480 mipi 1 subcmd  0  id   8 close 1"};
//system("/system/xbin/autest_camera_sc8830   w 640 maincmd 0    h 480 mipi 1 subcmd  0  id   8 close 1");

 pox_system(cmd_line);


#endif

	return 0;
}

