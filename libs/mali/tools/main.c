#include <stdlib.h>
#include <stdio.h>
#include <string.h>


void xor_cipher(char* src,int src_len,char* key,int key_len)
{
	int done=0,i=0;
	while(done<src_len)
	{
		for(i=0;i<key_len;i++)
		{
			*(src+done)^=*(key+i);
		}
			done++;
		}
}

void xor_decipher(char* src,int src_len,char* key,int key_len)
{
	int done=0,i=0;
	while(done<src_len)
	{
		for(i=key_len-1;i>=0;i--)
		{
			*(src+done)^=*(key+i);
		}
		done++;
	}
}

void generate_cipher_data(FILE* fp,char* src,int src_len,char* key,int key_len)
{
	char buf[256];
	strcpy(buf, src);
	printf("%s -> ",src);
	xor_decipher(buf,src_len,key,key_len);
	printf("%s (%d)-> ",buf, strlen(buf));
	fputs(buf,fp);fputc('\n',fp);
	xor_decipher(buf,src_len,key,key_len);
	printf("%s\n",buf);
}

int main()
{
	int i=0;
	char cipher_key[]="mali";

	char* data_list_level3[] = {
		"com.glbenchmark.glbenchmark",
		"com.rightware.tdmm2v10jnifree",
		"com.rightware.BasemarkX_Free",
		"com.antutu.ABenchMark",
		"com.raytracing.wizapply",
		"com.aurorasoftworks.quadrant",
		"com.qualcomm.qx.neocore",
		"com.Vancete.GPUT",
		"com.rightware.BasemarkX_Free",
		"com.futuremark.dmandroid",
		"com.tactel.electopia",
		"com.epicgames.EpicCitadel",
		"se.nena.nenamark",
		"com.passmark",
		"com.threed.jpct",
		"com.smartbench",
		"fishnoodle.benchmark"
	};
	size_t app_num_level3 = sizeof(data_list_level3)/sizeof(data_list_level3[0]);
	char* data_list_level2[] = {
		"com.android.launcher"
	};
	size_t app_num_level2 = sizeof(data_list_level2)/sizeof(data_list_level2[0]);
	FILE* fp=NULL;
	fp=fopen("./libboost.so","wb");

	for(i=0;i<app_num_level3;i++)
	{
		generate_cipher_data(fp,data_list_level3[i],strlen(data_list_level3[i]),cipher_key,strlen(cipher_key));
	}
	fputs("\n",fp);
	for(i=0;i<app_num_level2;i++)
	{
		generate_cipher_data(fp,data_list_level2[i],strlen(data_list_level2[i]),cipher_key,strlen(cipher_key));
	}
	fputs("\n",fp);
	fclose(fp);

	return 0;
}
