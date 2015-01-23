#include <sys/select.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#define LOG_TAG "ext_contrl"

#define DefaultBufferLength 30000*100
#define FILE_PATH_MUSIC  "data/local/media/audio_dumpmusic.pcm"
#define FILE_PATH_SCO  "data/local/media/audio_dumpsco.pcm"
#define FILE_PATH_BTSCO  "data/local/media/audio_dumpbtsco.pcm"
#define FILE_PATH_VAUDIO  "data/local/media/audio_dumpvaudio.pcm"

#define FILE_PATH_MUSIC_WAV  "data/local/media/audio_dumpmusic.wav"
#define FILE_PATH_SCO_WAV  "data/local/media/audio_dumpsco.wav"
#define FILE_PATH_BTSCO_WAV  "data/local/media/audio_dumpbtsco.wav"
#define FILE_PATH_VAUDIO_WAV  "data/local/media/audio_dumpvaudio.wav"

#define FILE_PATH_HAL_INFO  "data/local/media/audio_hw_info.txt"
#define FILE_PATH_HELP  "data/local/media/help.txt"
pthread_t control_audio_loop;

static void *control_audio_loop_process(void *arg);

int ext_control_open(struct tiny_audio_device *adev){
    ALOGI("%s---",__func__);
    if(pthread_create(&control_audio_loop, NULL, control_audio_loop_process, (void *)adev)) {
        ALOGE("control_audio_loop thread creating failed !!!!");
        return -1;
    }
    return 0;
}

static int read_noblock_l(int fd,int8_t *buf,int bytes){
    int ret = 0;
    ret = read(fd,buf,bytes);
    return ret;
}

static void empty_command_pipe(int fd){
    char buff[16];
    int ret;
    do {
        ret = read(fd, &buff, sizeof(buff));
    } while (ret > 0 || (ret < 0 && errno == EINTR));
}

/***********************************************************
 *function: init dump buffer info;
 *
 * *********************************************************/
int init_dump_info(out_dump_t* out_dump,const char* filepath,size_t buffer_length,bool need_cache,bool save_as_wav){
    ALOGE("%s ",__func__);
    if(out_dump == NULL){
        ALOGE("%s, can not init dump info ,out_dump is null",__func__);
        return -1;
    }
    //1,create dump fife
    out_dump->dump_fd = fopen(filepath,"wb");
    if(out_dump->dump_fd == NULL){
        ALOGE("%s, creat dump file err",__func__);
    }
    if(save_as_wav){
        out_dump->wav_fd = open(filepath,O_RDWR);
        if(out_dump->wav_fd <= 0){
           LOG_E("%s creat wav file error",__func__);
        }
    }else{
        out_dump->wav_fd = NULL;
    }

    //2,malloc cache buffer
    if(need_cache){
        out_dump->cache_buffer = malloc(buffer_length);
        if(out_dump->cache_buffer == NULL){
            ALOGE("malloc cache buffer err!");
            if(out_dump->dump_fd > 0){
                fclose(out_dump->dump_fd);
                out_dump->dump_fd = NULL;
            }
            if(out_dump->wav_fd > 0){
                close(out_dump->wav_fd);
                out_dump->wav_fd = 0;
            }
            return -1;
        }
        memset(out_dump->cache_buffer,0,buffer_length);
    }else{
        out_dump->cache_buffer = NULL;
    }
    out_dump->buffer_length = buffer_length;
    out_dump->write_flag = 0;
    out_dump->more_one = false;
    out_dump->total_length = 0;
    out_dump->sampleRate = 44100;
    out_dump->channels = 2;

    return 0;
}

/***********************************************************
 *function: release dump buffer info;
 *
 * *********************************************************/
int release_dump_info(out_dump_t* out_dump){
    LOG_I("%s ",__func__);
    if(out_dump == NULL){
        ALOGE("out_dump is null");
        return -1;
    }
    //1 relese buffer
    LOG_I("release buffer");
    if(out_dump->cache_buffer){
        free(out_dump->cache_buffer);
        out_dump->cache_buffer = NULL;
    }

    //2 close file fd
    LOG_I("release fd");
    if(out_dump->dump_fd){
        fclose(out_dump->dump_fd);
        out_dump->dump_fd = NULL;
    }
    if(out_dump->wav_fd){
        close(out_dump->wav_fd);
        out_dump->wav_fd = 0;
    }

    out_dump->write_flag = 0;
    out_dump->more_one = false;
    out_dump->total_length = 0;
    return 0;
}

/********************************************
 *function: save cache buffer to file
 *
 * ******************************************/
int save_cache_buffer(out_dump_t* out_dump, const char *filepath)
{
    LOG_I("%s ",__func__);
    if (out_dump == NULL || out_dump->cache_buffer == NULL) {
        LOG_E("adev or DumpBuffer is NULL");
        return -1;
    }
    size_t written = 0;
    if(out_dump->dump_fd == NULL){
        out_dump->dump_fd = (FILE *)fopen(filepath,"wb");
    }

   if (out_dump->more_one) {
        size_t size1 = out_dump->buffer_length - out_dump->write_flag;
        size_t size2 = out_dump->write_flag;
        LOG_I("size1:%d,size2:%d,buffer_length:%d",size1,size2,out_dump->buffer_length);
        written = fwrite(((uint8_t *)out_dump->cache_buffer + out_dump->write_flag), size1, 1, out_dump->dump_fd);
        written += fwrite((uint8_t *)out_dump->cache_buffer, size2, 1, out_dump->dump_fd);
        out_dump->total_length = out_dump->buffer_length;
    } else {
        written += fwrite((uint8_t *)out_dump->cache_buffer, out_dump->write_flag, 1, out_dump->dump_fd);
        out_dump->total_length = out_dump->write_flag;
    }
    LOG_E("writen:%ld",out_dump->total_length);
    return written;
}

/******************************************
 *function: write dump data to cache buffer
 *
 ******************************************/
size_t dump_to_buffer(out_dump_t *out_dump, void* buf, size_t size)
{

    LOG_I("%s  ",__func__);
    if (out_dump == NULL || out_dump->cache_buffer == NULL || buf == NULL ) {
         LOG_E("adev or DumpBuffer is NULL or buf is NULL");
        return -1;
    }
    size_t copy = 0;
    size_t bytes = size;
    uint8_t *src = (uint8_t *)buf;
    //size>BufferLength,  size larger then the left space,size smaller then the left space
    if (size > out_dump->buffer_length) {
        int Multi = size/out_dump->buffer_length;
        src= buf + (size - (Multi-1) * out_dump->buffer_length);
        bytes = out_dump->buffer_length;
        out_dump->write_flag = 0;
    }
    if (bytes > (out_dump->buffer_length - out_dump->write_flag)) {
        out_dump ->more_one = true;
        size_t size1 = out_dump->buffer_length - out_dump->write_flag;
        size_t size2 = bytes - size1;
        memcpy(out_dump->cache_buffer + out_dump->write_flag,src,size1);
        memcpy(out_dump->cache_buffer,src+size1,size2);
        out_dump->write_flag = size2;
    } else {
        memcpy(out_dump->cache_buffer + out_dump->write_flag,src,bytes);
        out_dump->write_flag += bytes;
        if (out_dump->write_flag >= out_dump->buffer_length) {
            out_dump->write_flag -= out_dump->buffer_length;
        }
    }
    copy = bytes;
    return copy;
}

/***********************************************
 *function: write dump to file directly
 *
 ***********************************************/
int dump_to_file(FILE *out_fd ,void* buffer, size_t size)
{
    LOG_I("%s ",__func__);
    int ret = 0;
    if(out_fd){
        ret = fwrite((uint8_t *)buffer,size, 1, out_fd);
        if(ret < 0){
            LOG_W("%s fwrite filed:%d",__func__,size);
        }
    }else{
        LOG_E("out_fd is NULL, can not write");
    }
    return ret;
}

/********************************************
 * function:add wav header
 *
 * *****************************************/
int add_wav_header(out_dump_t* out_dump){
    char header[44];
    long totalAudioLen = out_dump->total_length;
    long totalDataLen = totalAudioLen + 36;
    long longSampleRate = out_dump->sampleRate;
    int channels = out_dump->channels;
    long byteRate = out_dump->sampleRate * out_dump->channels * 2;
    LOG_E("%s ",__func__);
    header[0] = 'R'; // RIFF/WAVE header
    header[1] = 'I';
    header[2] = 'F';
    header[3] = 'F';
    header[4] = (char) (totalDataLen & 0xff);
    header[5] = (char) ((totalDataLen >> 8) & 0xff);
    header[6] = (char) ((totalDataLen >> 16) & 0xff);
    header[7] = (char) ((totalDataLen >> 24) & 0xff);
    header[8] = 'W';
    header[9] = 'A';
    header[10] = 'V';
    header[11] = 'E';
    header[12] = 'f'; // 'fmt ' chunk
    header[13] = 'm';
    header[14] = 't';
    header[15] = ' ';
    header[16] = 16; // 4 bytes: size of 'fmt ' chunk
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;
    header[20] = 1; // format = 1
    header[21] = 0;
    header[22] = (char) channels;
    header[23] = 0;
    header[24] = (char) (longSampleRate & 0xff);
    header[25] = (char) ((longSampleRate >> 8) & 0xff);
    header[26] = (char) ((longSampleRate >> 16) & 0xff);
    header[27] = (char) ((longSampleRate >> 24) & 0xff);
    header[28] = (char) (byteRate & 0xff);
    header[29] = (char) ((byteRate >> 8) & 0xff);
    header[30] = (char) ((byteRate >> 16) & 0xff);
    header[31] = (char) ((byteRate >> 24) & 0xff);
    header[32] = (char) (2 * 16 / 8); // block align
    header[33] = 0;
    header[34] = 16; // bits per sample
    header[35] = 0;
    header[36] = 'd';
    header[37] = 'a';
    header[38] = 't';
    header[39] = 'a';
    header[40] = (char) (totalAudioLen & 0xff);
    header[41] = (char) ((totalAudioLen >> 8) & 0xff);
    header[42] = (char) ((totalAudioLen >> 16) & 0xff);
    header[43] = (char) ((totalAudioLen >> 24) & 0xff);
    if(out_dump == NULL){
        log_e("%s,err:out_dump is null",__func__);
        return -1;
    }
    lseek(out_dump->wav_fd,0,SEEK_SET);
    write(out_dump->wav_fd,header,sizeof(header));

    return 0;
}

/*************************************************
 *function:the interface of dump
 *
 * ***********************************************/
void do_dump(dump_info_t* dump_info, void* buffer, size_t size){
    if(dump_info == NULL){
        LOG_E("err:out_dump or ext_contrl is null");
        return;
    }
    if(dump_info->dump_to_cache){
        if(dump_info->dump_music){
            dump_to_buffer(dump_info->out_music,buffer,size);
        }else if(dump_info->dump_vaudio){
            dump_to_buffer(dump_info->out_vaudio,buffer,size);
        }else if(dump_info->dump_sco){
            dump_to_buffer(dump_info->out_sco,buffer,size);
        }else if(dump_info->dump_bt_sco){
            dump_to_buffer(dump_info->out_bt_sco,buffer,size);
        }
    }else{
        if(dump_info->dump_music){
            dump_to_file(dump_info->out_music->dump_fd,buffer,size);
        }else if(dump_info->dump_vaudio){
            dump_to_file(dump_info->out_vaudio->dump_fd,buffer,size);
        }else if(dump_info->dump_sco){
            dump_to_file(dump_info->out_sco->dump_fd,buffer,size);
        }else if(dump_info->dump_bt_sco){
            dump_to_file(dump_info->out_bt_sco->dump_fd,buffer,size);
        }
    }
    return;
}

/*************************************************
 *function:dump hal info to file
 *
 * ***********************************************/
void dump_hal_info(struct tiny_audio_device * adev){
    FILE* fd = fopen(FILE_PATH_HAL_INFO,"w+");
    if(fd == NULL){
        LOG_E("%s, open file err",__func__);
        return;
    }
    LOG_D("%s",__func__);
    fprintf(fd,"audio_mode_t:%d \n",adev->mode);
    fprintf(fd,"out_devices:%d \n",adev->out_devices);
    fprintf(fd,"in_devices:%d \n",adev->in_devices);
    fprintf(fd,"prev_out_devices:%d \n",adev->prev_out_devices);
    fprintf(fd,"prev_in_devices:%d \n",adev->prev_in_devices);
    fprintf(fd,"routeDev:%d \n",adev->routeDev);
    fprintf(fd,"cur_vbpipe_fd:%d \n",adev->cur_vbpipe_fd);
    fprintf(fd,"cp_type:%d \n",adev->cp_type);
    fprintf(fd,"call_start:%d \n",adev->call_start);
    fprintf(fd,"call_connected:%d \n",adev->call_connected);
    fprintf(fd,"call_prestop:%d \n",adev->call_prestop);
    fprintf(fd,"vbc_2arm:%d \n",adev->vbc_2arm);
    fprintf(fd,"voice_volume:%f \n",adev->voice_volume);
    fprintf(fd,"mic_mute:%d \n",adev->mic_mute);
    fprintf(fd,"bluetooth_nrec:%d \n",adev->bluetooth_nrec);
    fprintf(fd,"bluetooth_type:%d \n",adev->bluetooth_type);
    fprintf(fd,"low_power:%d \n",adev->low_power);
    fprintf(fd,"realCall:%d \n",adev->realCall);
    fprintf(fd,"num_dev_cfgs:%d \n",adev->num_dev_cfgs);
    fprintf(fd,"num_dev_linein_cfgs:%d \n",adev->num_dev_linein_cfgs);
    fprintf(fd,"eq_available:%d \n",adev->eq_available);
    fprintf(fd,"bt_sco_state:%d \n",adev->bt_sco_state);
    fprintf(fd,"voip_state:%d \n",adev->voip_state);
    fprintf(fd,"voip_start:%d \n",adev->voip_start);
    fprintf(fd,"master_mute:%d \n",adev->master_mute);
    fprintf(fd,"cache_mute:%d \n",adev->cache_mute);
    fprintf(fd,"fm_volume:%d \n",adev->fm_volume);
    fprintf(fd,"fm_open:%d \n",adev->fm_open);
    fprintf(fd,"requested_channel_cnt:%d \n",adev->requested_channel_cnt);
    fprintf(fd,"input_source:%d \n",adev->input_source);

    fprintf(fd,"adev dump info: \n");
    fprintf(fd,"loglevel:%d \n",log_level);
    fprintf(fd,"dump_to_cache:%d \n",adev->ext_contrl->dump_info->dump_to_cache);
    fprintf(fd,"dump_as_wav:%d \n",adev->ext_contrl->dump_info->dump_as_wav);
    fprintf(fd,"dump_music:%d \n",adev->ext_contrl->dump_info->dump_music);
    fprintf(fd,"dump_vaudio:%d \n",adev->ext_contrl->dump_info->dump_vaudio);
    fprintf(fd,"dump_sco:%d \n",adev->ext_contrl->dump_info->dump_sco);
    fprintf(fd,"dump_bt_sco:%d \n",adev->ext_contrl->dump_info->dump_bt_sco);

    fclose(fd);
    return;
}

static void *control_audio_loop_process(void *arg){
    int pipe_fd,max_fd;
    fd_set fds_read;
    int result;
    int count;
    void* data;
    int val_int;
    struct str_parms *parms;
    char value[30];
    int ret = 0;
    int retdump;
    struct tiny_audio_device *adev = (struct tiny_audio_device *)arg;
    FILE* help_fd = NULL;
    void* help_buffer = NULL;
    FILE* pipe_d = NULL;

    pipe_fd = open("/dev/pipe/mmi.audio.ctrl", O_RDWR);
    if(pipe_fd < 0){
        LOG_E("%s, open pipe error!! ",__func__);
    }
    max_fd = pipe_fd + 1;
    if((fcntl(pipe_fd,F_SETFL,O_NONBLOCK)) <0){
        LOG_E("set flag RROR --------");
    }
    data = (char*)malloc(1024);
    if(data == NULL){
        LOG_E("malloc data err");
        return NULL;
    }
    while(1){
        FD_ZERO(&fds_read);
        FD_SET(pipe_fd,&fds_read);
        result = select(max_fd,&fds_read,NULL,NULL,NULL);
        if(result < 0){
            LOG_E("select error ");
            continue;
        }
        if(FD_ISSET(pipe_fd,&fds_read) <= 0 ){
            LOG_E("SELECT OK BUT NO fd is set");
            continue;
        }
        memset(data,0,1024);
        count = read_noblock_l(pipe_fd,data,1024);
        if(count < 0){
            LOG_E("read data err");
            empty_command_pipe(pipe_fd);
            continue;
        }
        LOG_E("data:%s ",data);
        parms = str_parms_create_str(data);

        ret = str_parms_get_str(parms,"dumpmusic", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_E("dumpmusic is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_music,FILE_PATH_MUSIC_WAV,
                                adev->ext_contrl->dump_info->out_music->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_music,FILE_PATH_MUSIC,
                                adev->ext_contrl->dump_info->out_music->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_music = true;
            }else{
                adev->ext_contrl->dump_info->dump_music = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_music,FILE_PATH_MUSIC);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_music);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_music);
            }
        }

        ret = str_parms_get_str(parms,"dumpsco", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_sco,FILE_PATH_SCO_WAV,
                                adev->ext_contrl->dump_info->out_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_sco,FILE_PATH_SCO,
                                adev->ext_contrl->dump_info->out_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_sco = true;
            }else{
                adev->ext_contrl->dump_info->dump_sco = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_sco,FILE_PATH_SCO);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_sco);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_sco);
            }
        }

        ret = str_parms_get_str(parms,"dumpbtsco", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpbtsco is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_bt_sco,FILE_PATH_BTSCO_WAV,
                                adev->ext_contrl->dump_info->out_bt_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_bt_sco,FILE_PATH_BTSCO,
                                adev->ext_contrl->dump_info->out_bt_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_bt_sco = true;
            }else{
                adev->ext_contrl->dump_info->dump_bt_sco = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_bt_sco,FILE_PATH_BTSCO);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_bt_sco);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_bt_sco);
            }
         }

        ret = str_parms_get_str(parms,"dumpvaudio", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpvaudio is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_vaudio,FILE_PATH_VAUDIO_WAV,
                                adev->ext_contrl->dump_info->out_vaudio->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_vaudio,FILE_PATH_VAUDIO,
                                adev->ext_contrl->dump_info->out_vaudio->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_vaudio = true;
            }else{
                adev->ext_contrl->dump_info->dump_vaudio = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_vaudio,FILE_PATH_VAUDIO);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_vaudio);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_vaudio);
            }
        }
        ret = str_parms_get_str(parms,"dumpcache", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dump to cache :%d",val_int);
            if(val_int){
                adev->ext_contrl->dump_info->dump_to_cache = true;
             }else{
                adev->ext_contrl->dump_info->dump_to_cache = false;
             }
        }
        ret = str_parms_get_str(parms,"dumpwav", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dump as wav :%d",val_int);
            if(val_int){
                adev->ext_contrl->dump_info->dump_as_wav = true;
             }else{
                adev->ext_contrl->dump_info->dump_as_wav = false;
             }
        }

        ret = str_parms_get_str(parms,"bufferlength", value, sizeof(value));
        {
            if(ret >= 0){
                val_int = atoi(value);
                LOG_D("set buffer length:%d",val_int);
                adev->ext_contrl->dump_info->out_music->buffer_length = val_int;
                adev->ext_contrl->dump_info->out_bt_sco->buffer_length = val_int;
                adev->ext_contrl->dump_info->out_sco->buffer_length = val_int;
                adev->ext_contrl->dump_info->out_vaudio->buffer_length = val_int;
            }
        }

        ret = str_parms_get_str(parms,"loglevel", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("log is :%d",val_int);
            if(val_int >= 0){
                log_level = val_int;
            }
        }

        ret = str_parms_get_str(parms,"help",value,sizeof(value));
        if(ret >= 0){
            help_fd = fopen(FILE_PATH_HELP,"rb");
            pipe_d = fopen("/dev/pipe/mmi.audio.ctrl","wb");
            help_buffer = (void*)malloc(1024);
            if(help_fd == NULL || pipe_d == NULL){
                LOG_E("ERROR ------------");
            }else{
                while(fgets(help_buffer,1024,help_fd)){
                    fputs(help_buffer,pipe_d);
                }
            }
            fclose(pipe_d);
            fclose(help_fd);
            free(help_buffer);
            sleep(5);
        }

        ret = str_parms_get_str(parms,"dumphalinfo",value,sizeof(value));
        if(ret >= 0){
            LOG_D("dump audio hal info");
            dump_hal_info(adev);
        }

        str_parms_destroy(parms);

    }
    free(data);
    return NULL;
}
