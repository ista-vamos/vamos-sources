#include <thread>

#include "program_data.h"
#include "src.h"

int main(int argc, char *argv[]) {
    ProgramData pd(argc, argv);
    void *data = &pd;
#ifdef SOURCE_THREAD
    std::thread thrd([data]{source_thrd(data);});
    thrd.join();
#else
    source_thrd(data);
#endif

}