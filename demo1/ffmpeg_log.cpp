#include <iostream>

#ifdef __cplusplus
extern "C"{
#endif

#include <libavutil/log.h>

#ifdef __cplusplus
}
#endif

using std::cout;
using std::cin;

int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_DEBUG);

    av_log(NULL, AV_LOG_INFO, "hello, world\n");

    return 0;
}
