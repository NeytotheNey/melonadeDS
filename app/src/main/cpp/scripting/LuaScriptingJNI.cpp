#include <jni.h>
#include <string>
#include <vector>
#include "MelonDS.h"
#include "scripting/LuaEngine.h"

extern "C"
{

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setupLuaScripts(JNIEnv* env, jobject thiz, jobjectArray names, jobjectArray sources)
{
    std::vector<MelonDSAndroid::LuaScriptSource> scripts;
    jsize scriptCount = env->GetArrayLength(names);
    scripts.reserve(scriptCount);

    for (jsize i = 0; i < scriptCount; i++)
    {
        auto name = (jstring) env->GetObjectArrayElement(names, i);
        auto source = (jbyteArray) env->GetObjectArrayElement(sources, i);

        const char* nameChars = env->GetStringUTFChars(name, nullptr);
        // The source comes as bytes, since modified UTF-8 can't carry everything a Lua file may contain
        std::string sourceString(env->GetArrayLength(source), '\0');
        env->GetByteArrayRegion(source, 0, (jsize) sourceString.size(), (jbyte*) sourceString.data());
        scripts.push_back({ nameChars, std::move(sourceString) });

        env->ReleaseStringUTFChars(name, nameChars);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(source);
    }

    MelonDSAndroid::setLuaScripts(std::move(scripts));
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_disableLuaScript(JNIEnv* env, jobject thiz, jint scriptId)
{
    MelonDSAndroid::disableLuaScript(scriptId);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_enableLuaScript(JNIEnv* env, jobject thiz, jint scriptId)
{
    MelonDSAndroid::enableLuaScript(scriptId);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_updateLuaSticks(JNIEnv* env, jobject thiz, jfloat leftX, jfloat leftY, jfloat rightX, jfloat rightY)
{
    MelonDSAndroid::updateLuaSticks(leftX, leftY, rightX, rightY);
}

}
