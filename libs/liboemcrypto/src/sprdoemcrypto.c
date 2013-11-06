/*********************************************************************
File: sprdoemcrypto.c
Author: robert lu
Creation Date; 2012-5-22
descritpion: low level API  
*********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include "sprdoemcrypto.h"
#include <openssl/aes.h>

#define LOG_TAG "SPRDOEMCRYPTO"   
#undef LOG   
#include <utils/Log.h>
#include "engat.h"
#include "engapi.h"
#include "engopt.h"

#define cryptodebug 1

#define  AT_ERROR_STR "ERROR"
#define  AT_ERROR_LEN 5
#define  AT_CMD_BUFF_LEN 16
#define  AT_READ_BUFF_MIN_LEN 64

#define KEYBOX_LENGTH 128
#define IV_LENGTH     16
#define KEY_LENGTH    16
#define KEYBOXFILENAME    "/productinfo/keybox.dat"

const unsigned char OEMRootKey[KEY_LENGTH] = "abcdefgh12345678";
const unsigned char enc_iv[IV_LENGTH] ="x3dxcfxbax43x9dx9exb4x30xb4x22xdax80x2cx9fxacx42"; 


static void engapi_quit(int fd)
{
    engapi_close(fd); 
}

/*
 * Encrypt and store the keybox to persistent memory. The device key or entire keybox must be
 * stored securely, encrypted by an OEM root key.
 * Parameters:
 *     keybox(in) - Pointer to clear keybox data.Must be encrypted with an OEM root key.
 *     keyboxLength(in) - Length of the keybox data in bytes.
 * Returns:
 *     OEMCryptoResult indicating success or failure
 */
OEMCryptoResult OEMCrypto_EncryptAndStoreKeyBox(
                          OEMCrypto_UINT8 *keybox,
                          OEMCrypto_UINT32 keyboxLength){
     AES_KEY akey;
     FILE *file;
     unsigned int retc;
     unsigned char iv[IV_LENGTH];
     unsigned char *out = NULL;
#ifdef cryptodebug 
     unsigned char *outt = NULL;
#endif 

     LOGE("OEMCrypto_EncryptAndStoreKeyBox, keyboxLength = %d",keyboxLength);

     if ((keybox == NULL) || (keyboxLength != KEYBOX_LENGTH)) {
        LOGE("OEMCrypto_EncryptAndStoreKeyBox,keyBoxLength is not equal to 128 or keybox is null");
        return OEMCrypto_FAILURE;
     }
#ifdef cryptodebug 
     LOGE("OEMCrypto_EncryptAndStoreKeyBox, keybox=%s",keybox);
#endif
     out = (unsigned char *) malloc(keyboxLength);

     if (out == NULL) {
         LOGE("OEMCrypto_EncryptAndStoreKeyBox, allocat out failed!");    
         return OEMCrypto_FAILURE;
     }
     memcpy(iv, enc_iv, IV_LENGTH);
     AES_set_encrypt_key(OEMRootKey, 128, &akey); 

     AES_cbc_encrypt(keybox, out, keyboxLength,&akey, &iv[0], AES_ENCRYPT);

     file = fopen(KEYBOXFILENAME, "wb");
     if (file == NULL) {
	LOGE("OEMCrypto_EncryptAndStoreKeyBox, Could not open %s!",KEYBOXFILENAME);
	return OEMCrypto_FAILURE;
     }
     retc = fwrite(out, 1, keyboxLength, file);
  
     fclose(file);
     return retc != keyboxLength ? OEMCrypto_FAILURE : OEMCrypto_SUCCESS;
}

/*
 * Return the device's unique identifier. the device identifier shall not come from the Widevine
 * keybox.
 * Parameters:
 *    deviceID(out) - Points to the buffer that should receive the key data.
 *    idLength(in) - Length of the device ID buffer. Maximum of 32 bytes allowed
 * Returns:
 *     OEMCryptoResult indicating success or failure
 */
OEMCryptoResult OEMCrypto_IdentifyDevice(
                          OEMCrypto_UINT8 *deviceID,
                          OEMCrypto_UINT32 idLength){
     char cmdbuf[AT_CMD_BUFF_LEN];
     char readbuf[AT_READ_BUFF_MIN_LEN];
     int readlen;
     int cmdlen=0;
     int fd = 0;


     LOGE("OEMCrypto_IdentifyDevice, idLength = %d", idLength);

     if ((deviceID == NULL) || (idLength > 32)) {
        LOGE("OEMCrypto_IdentifyDevice, idLength less than 32 or deviceID is null!");
        return OEMCrypto_FAILURE;
     }

     sprintf(cmdbuf, "%d,%d",ENG_AT_REQUEST_IMEI, 0); 
     cmdlen=strlen(cmdbuf);
        
     fd = engapi_open(0);

     if (fd < 0) {
         LOGE("OEMCrypto_IdentifyDevice, engapi_open failed =%d\n",fd);
         return OEMCrypto_FAILURE;
     }

     engapi_write(fd,cmdbuf,cmdlen);
     LOGE("OEMCrypto_IdentifyDevice,at cmd=%s,fd=%d\n",cmdbuf,fd);
	
     memset(readbuf, 0, sizeof(readbuf));

     readlen=engapi_read(fd, readbuf, AT_READ_BUFF_MIN_LEN);
     if ((readlen <= 0) || (readlen > AT_READ_BUFF_MIN_LEN)) {
         LOGE("OEMCrypto_IdentifyDevice,engapi_read failed =%d\n",readlen);
         engapi_quit(fd);
         return OEMCrypto_FAILURE;
     }
     LOGE("OEMCrypto_IdentifyDevice,at readbuf=%s,readlen=%d\n",readbuf,readlen);

     if (memcmp(readbuf,AT_ERROR_STR, AT_ERROR_LEN) == 0) {
         LOGE("OEMCrypto_IdentifyDevice,engapi_read failed =%s\n",readbuf);
         engapi_quit(fd);
         return OEMCrypto_FAILURE;
     }
      
     memset(deviceID, 0, idLength);
     if (readlen <= 31) {
         memcpy(deviceID,readbuf,readlen);
     }
     else {
         memcpy(deviceID,readbuf,31);
     }
     LOGE("OEMCrypto_IdentifyDevice,read imei=%s\n",deviceID);

     engapi_quit(fd);
     return OEMCrypto_SUCCESS;   
}


/*
 * Retrieve a range of bytes from the Widevine keybox. This function should decrypt the keybox and
 * return the specified bytes.
 * parameters:
 *     buffer(out) - Pointers to the buffer that should receive the keybox data
 *     offset(in) - Byte offet from the beginning of the keybox of the first byte to return
 *     length(in) - Number of bytes of data to return
 * Returns:
 *     OEMCryptoResult indicating success or failure
 */
OEMCryptoResult OEMCrypto_GetkeyboxData(
                          OEMCrypto_UINT8 *buffer,
                          OEMCrypto_UINT32 offset,
                          OEMCrypto_UINT32 length){
     AES_KEY akey;
     FILE *file;
     unsigned int retc;
     unsigned char iv[IV_LENGTH];
     unsigned char out[KEYBOX_LENGTH+1];
     unsigned char keybox[KEYBOX_LENGTH+1];
     LOGE("OEMCrypto_GetkeyboxData, offset = %d, length = %d", offset, length);

     if ((buffer == NULL) || (length > KEYBOX_LENGTH)) {
        LOGE("OEMCrypto_GetkeyboxData,length is greater than 128 or buffer is null!");
        return OEMCrypto_FAILURE;
     }

     if ((offset +length) > KEYBOX_LENGTH) {
        LOGE("OEMCrypto_GetkeyboxData,(offset +length) is greater than 128!");
        return OEMCrypto_FAILURE;
     }
     memset(out , 0, KEYBOX_LENGTH);
     memset(keybox, 0, KEYBOX_LENGTH);
     memcpy(iv, enc_iv, IV_LENGTH);
     AES_set_decrypt_key(OEMRootKey, 128, &akey); 
     
     file = fopen(KEYBOXFILENAME, "rb");
     if (file == NULL) {
	LOGE("OEMCrypto_GetkeyboxData, Could not open %s!",KEYBOXFILENAME);
	return OEMCrypto_FAILURE;
     }
     
     retc = fread(keybox, 1, KEYBOX_LENGTH, file);
     fclose(file);
     if (retc != KEYBOX_LENGTH) {
	LOGE("OEMCrypto_GetkeyboxData, read data failed %d!", retc);
	return OEMCrypto_FAILURE;
     }
   
     AES_cbc_encrypt(keybox, out, KEYBOX_LENGTH, &akey, &iv[0], AES_DECRYPT);
#ifdef cryptodebug 

     LOGE("OEMCrypto_GetkeyboxData, out=%s", out);
#endif     

     memcpy(buffer, &out[offset],length); 
     
     return OEMCrypto_SUCCESS;
}

/*
 * Return a buffer filled with hardware-generated random bytes, if suported by the hardware.
 * Parameters:
 *    randomData(out) - Pointed to the buffer that receives random data
 *    datalength(in) - Length of the random data buffer in bytes
 * Returns:
 *    OEMCrypto_SUCCESS success
 *    OEMCrypto_ERROR_RNG_FAILED failed to generate random number
 *    OEMCrypto_ERROR_RNG_NOT_SUPPORTED function not supported
 */
OEMCryptoResult OEMCrypto_GetRandom(
                          OEMCrypto_UINT8 *randomData,
                          OEMCrypto_UINT32 dataLength){
    FILE *file;
    unsigned int retc;
 
    LOGE("OEMCrypto_GetRandom, dataLength = %d", dataLength);
 
    if (randomData == NULL) {
        LOGE("OEMCrypto_GetRandom, randomData is null!");
        return OEMCrypto_FAILURE;
    }

    file = fopen("/dev/urandom", "rb");
    if (file == NULL) {
	LOGE("OEMCrypto_GetRandom, Could not open /dev/urandom!");
	return OEMCrypto_FAILURE;
    }

    retc = fread(randomData, 1, dataLength, file);

    if (retc != dataLength) {
        LOGE("OEMCrypto_GetRandom, read again!");
        retc = fread(randomData, 1, dataLength, file);
    }
    
    fclose(file);

    return retc != dataLength ? OEMCrypto_FAILURE : OEMCrypto_SUCCESS;
}


