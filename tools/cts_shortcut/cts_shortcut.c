/* Spreadtrum Communication */

/*
 * This entrance provide shortcut to improve
 * the condition of the compatibility test
 * suite
 */

# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>

# define MAX_NAME_LEN 128
# define VP8_OUTPUT_PATH "/data/local/tmp/test"

void handleVp8Case()
{
    char cmd[MAX_NAME_LEN];

    sprintf(cmd, "rm -r %s", VP8_OUTPUT_PATH);
    system(cmd);

    sprintf(cmd, "mkdir %s", VP8_OUTPUT_PATH);
    system(cmd);

    sprintf(cmd, "chmod 0777 %s", VP8_OUTPUT_PATH);
    system(cmd);
}

/*
 * Entrance
 */
int main(int argc, char *argv[])
{
   int opt;

   while ( -1 != (opt = getopt(argc, argv, "c:")))
   {
       switch (opt) {
           case 'c':
               if (!strcmp(optarg, "vp8"))
               {
                  handleVp8Case();
               }
               break;
           default:
               break;
       }
   }
   return 0; 
}
