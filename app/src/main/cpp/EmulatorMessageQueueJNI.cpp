#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <Platform.h>

// messagePipes[0] -> read
// messagePipes[1] -> write
static int messagePipes[2] = { -1, -1 };
// Must match DATA_SIZE_BYTES in EmulatorMessageQueue.kt, which checks it at startup
static constexpr int MAX_DATA_LENGTH = 512;

extern "C"
{

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_impl_emulator_EmulatorMessageQueue_initMessagePipe(JNIEnv* env, jobject thiz)
{
    if (messagePipes[0] != -1) {
        return messagePipes[0];
    }

    if (pipe(messagePipes) == -1) {
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Error, "Failed to create message queue pipes");
        return -1;
    }

    // Make read end non-blocking
    int flags = fcntl(messagePipes[0], F_GETFL, 0);
    fcntl(messagePipes[0], F_SETFL, flags | O_NONBLOCK);

    return messagePipes[0];
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_impl_emulator_EmulatorMessageQueue_closeMessagePipe(JNIEnv* env, jobject thiz)
{
    if (messagePipes[0] != -1) {
        close(messagePipes[0]);
        messagePipes[0] = -1;
    }
    if (messagePipes[1] != -1) {
        close(messagePipes[1]);
        messagePipes[1] = -1;
    }
}

}

namespace MelonDSAndroid {
    void fireEmulatorEvent(int type, int dataLength, void* data) {
        if (messagePipes[1] == -1) {
            return;
        }

        if (data == nullptr || dataLength < 0)
            dataLength = 0;
        if (dataLength > MAX_DATA_LENGTH)
        {
            melonDS::Platform::Log(melonDS::Platform::LogLevel::Error, "Emulator event %d dropped: %d bytes of data is over the limit", type, dataLength);
            return;
        }

        struct {
            int type;
            int dataLength;
            char data[MAX_DATA_LENGTH];
        } event = { type, dataLength };
        if (dataLength > 0)
            memcpy(event.data, data, dataLength);

        // A single write (under PIPE_BUF bytes, so it's atomic), so that the reader never sees a header without its data, and events written
        // from different threads can't interleave
        write(messagePipes[1], &event, 2 * sizeof(int) + dataLength);
    }
}