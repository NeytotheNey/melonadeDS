#include <jni.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MelonDS.h"
#include "scripting/LuaEngine.h"

extern "C"
{

/**
 * The string in a string field, or "" if it's null. Also "" when the JVM is out of memory, with an OutOfMemoryError pending: callers check
 * ExceptionCheck() and give up
 */
static std::string getString(JNIEnv* env, jobject object, jfieldID field)
{
    auto string = (jstring) env->GetObjectField(object, field);
    if (!string)
        return {};

    const char* chars = env->GetStringUTFChars(string, nullptr);
    std::string result = chars ? chars : "";
    if (chars)
        env->ReleaseStringUTFChars(string, chars);
    env->DeleteLocalRef(string);
    return result;
}

/**
 * The bytes of a byte array field, or nothing if it's null. Sources come as bytes, since modified UTF-8 can't carry everything a Lua file may
 * contain
 */
static std::optional<std::string> getBytes(JNIEnv* env, jobject object, jfieldID field)
{
    auto bytes = (jbyteArray) env->GetObjectField(object, field);
    if (!bytes)
        return std::nullopt;

    std::string result(env->GetArrayLength(bytes), '\0');
    env->GetByteArrayRegion(bytes, 0, (jsize) result.size(), (jbyte*) result.data());
    env->DeleteLocalRef(bytes);
    return result;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setupLuaScripts(JNIEnv* env, jobject thiz, jobjectArray scriptSetups, jobjectArray modules, jobjectArray images)
{
    jclass setupClass = env->FindClass("me/magnum/melonds/domain/model/emulator/LuaScriptSetup");
    jfieldID setupNameField = env->GetFieldID(setupClass, "name", "Ljava/lang/String;");
    jfieldID setupSourceField = env->GetFieldID(setupClass, "source", "[B");

    std::vector<MelonDSAndroid::LuaScriptSource> scripts;
    jsize scriptCount = env->GetArrayLength(scriptSetups);
    scripts.reserve(scriptCount);
    for (jsize i = 0; i < scriptCount; i++)
    {
        jobject setup = env->GetObjectArrayElement(scriptSetups, i);
        scripts.push_back({ getString(env, setup, setupNameField), getBytes(env, setup, setupSourceField).value_or("") });
        env->DeleteLocalRef(setup);
        // Out of memory: the error goes on to the app, and no scripts are set up
        if (env->ExceptionCheck())
            return;
    }

    // Shared by every script's state for the session, and never changed
    auto folderContent = std::make_shared<MelonDSAndroid::LuaFolderContent>();

    jclass moduleClass = env->FindClass("me/magnum/melonds/domain/model/emulator/LuaScriptModule");
    jfieldID moduleNameField = env->GetFieldID(moduleClass, "name", "Ljava/lang/String;");
    jfieldID modulePathField = env->GetFieldID(moduleClass, "path", "Ljava/lang/String;");
    jfieldID moduleSourceField = env->GetFieldID(moduleClass, "source", "[B");
    jfieldID moduleReasonField = env->GetFieldID(moduleClass, "leftOutReason", "Ljava/lang/String;");
    jsize moduleCount = env->GetArrayLength(modules);
    for (jsize i = 0; i < moduleCount; i++)
    {
        jobject module = env->GetObjectArrayElement(modules, i);
        folderContent->modules.emplace(getString(env, module, moduleNameField), MelonDSAndroid::LuaFolderContent::Module {
            getString(env, module, modulePathField),
            getBytes(env, module, moduleSourceField),
            getString(env, module, moduleReasonField),
        });
        env->DeleteLocalRef(module);
        if (env->ExceptionCheck())
            return;
    }

    jclass imageClass = env->FindClass("me/magnum/melonds/domain/model/emulator/LuaScriptImage");
    jfieldID imagePathField = env->GetFieldID(imageClass, "path", "Ljava/lang/String;");
    jfieldID imageWidthField = env->GetFieldID(imageClass, "width", "I");
    jfieldID imageHeightField = env->GetFieldID(imageClass, "height", "I");
    jfieldID imagePixelsField = env->GetFieldID(imageClass, "pixels", "[I");
    jfieldID imageReasonField = env->GetFieldID(imageClass, "leftOutReason", "Ljava/lang/String;");
    jsize imageCount = env->GetArrayLength(images);
    for (jsize i = 0; i < imageCount; i++)
    {
        jobject imageObject = env->GetObjectArrayElement(images, i);
        MelonDSAndroid::LuaFolderContent::Image image;
        image.leftOutReason = getString(env, imageObject, imageReasonField);

        auto pixels = (jintArray) env->GetObjectField(imageObject, imagePixelsField);
        jint width = env->GetIntField(imageObject, imageWidthField);
        jint height = env->GetIntField(imageObject, imageHeightField);
        // The app checked the size, but a mismatch would read past the array
        if (pixels && width > 0 && height > 0 && (int64_t) width * height == env->GetArrayLength(pixels))
        {
            image.width = width;
            image.height = height;
            image.pixels.resize((size_t) width * height);
            env->GetIntArrayRegion(pixels, 0, width * height, (jint*) image.pixels.data());
            // ARGB ints from the bitmap, not premultiplied
            for (melonDS::u32& pixel : image.pixels)
                pixel = MelonDSAndroid::premultiplyColor(pixel);
        }
        else if (image.leftOutReason.empty())
        {
            image.leftOutReason = "the file couldn't be read as an image";
        }
        if (pixels)
            env->DeleteLocalRef(pixels);

        folderContent->images.emplace(getString(env, imageObject, imagePathField), std::move(image));
        env->DeleteLocalRef(imageObject);
        if (env->ExceptionCheck())
            return;
    }

    MelonDSAndroid::setLuaScripts(std::move(scripts), std::move(folderContent));
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
