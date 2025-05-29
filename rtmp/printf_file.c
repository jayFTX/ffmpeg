#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(stderr, "main argc error!\n");
        return -1;
    }
    const char *in_filename = argv[1];
    const char *out_filename = argv[2];
    int ret;
    unsigned char c;
    // char buffer[1024];

    FILE *in_f = fopen(in_filename, "rb");
    if(!in_f){
        fprintf(stderr, "fopen error!\n");
        return -1;
    }

    FILE *out_f = fopen(out_filename, "wb");
    if(!out_f){
        fprintf(stderr, "fopen error!\n");
        return -1;
    }

    printf("stream f position = %ld \n", ftell(out_f));

    while(!feof(in_f)){
        c = fgetc(in_f);
        // printf("[in_position: %ld]: = %#x \n", ftell(in_f), c);
        // fputc(c, out_f);
        fprintf(out_f, "0x%02X_", c);
        // printf("[out_position: %ld]: = %#x \n", ftell(out_f), c);
        // sleep(1);
    }

    fclose(in_f);
    fclose(out_f);
    return 0;
}
