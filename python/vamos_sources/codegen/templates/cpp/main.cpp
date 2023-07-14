#include <thread>

#include "new_trace.h"
#include "src.h"

int main(int argc, char *argv[]) {
    void *data = nullptr;
#ifdef SOURCE_THREAD
    std::thread thrd([data]{source_thrd(data);});
    thrd.join();
#else
    source_thrd(data);
#endif

}