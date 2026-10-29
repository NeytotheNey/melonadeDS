#include <jni.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MelonDS.h"
#include "scripting/LuaEngine.h"
#include "scripting/LuaStorage.h"

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

/**
 * The field IDs of LuaScriptOptionValue
 */
struct OptionValueFields
{
    jfieldID key;
    jfieldID type;
    jfieldID integer;
    jfieldID number;
    jfieldID text;

    explicit OptionValueFields(JNIEnv* env)
    {
        jclass optionClass = env->FindClass("me/magnum/melonds/domain/model/emulator/LuaScriptOptionValue");
        key = env->GetFieldID(optionClass, "key", "Ljava/lang/String;");
        type = env->GetFieldID(optionClass, "type", "I");
        integer = env->GetFieldID(optionClass, "integer", "J");
        number = env->GetFieldID(optionClass, "number", "D");
        text = env->GetFieldID(optionClass, "text", "[B");
        env->DeleteLocalRef(optionClass);
    }
};

/**
 * The option values in a LuaScriptOptionValue array. The types match LuaScriptOptionValue's constants
 */
static std::vector<MelonDSAndroid::LuaOptionValue> getOptionValues(JNIEnv* env, jobjectArray optionArray, const OptionValueFields& fields)
{
    std::vector<MelonDSAndroid::LuaOptionValue> options;
    if (!optionArray)
        return options;

    jsize count = env->GetArrayLength(optionArray);
    options.reserve(count);
    for (jsize i = 0; i < count; i++)
    {
        jobject optionObject = env->GetObjectArrayElement(optionArray, i);
        MelonDSAndroid::LuaOptionValue option;
        option.key = getString(env, optionObject, fields.key);
        switch (env->GetIntField(optionObject, fields.type))
        {
            case 0:
                option.type = MelonDSAndroid::LuaOptionValue::Type::Boolean;
                option.boolean = env->GetLongField(optionObject, fields.integer) != 0;
                break;
            case 1:
                option.type = MelonDSAndroid::LuaOptionValue::Type::Integer;
                option.integer = env->GetLongField(optionObject, fields.integer);
                break;
            case 2:
                option.type = MelonDSAndroid::LuaOptionValue::Type::Number;
                option.number = env->GetDoubleField(optionObject, fields.number);
                break;
            default:
                option.type = MelonDSAndroid::LuaOptionValue::Type::String;
                option.string = getBytes(env, optionObject, fields.text).value_or("");
                break;
        }
        env->DeleteLocalRef(optionObject);
        if (env->ExceptionCheck())
            return {};
        options.push_back(std::move(option));
    }
    return options;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setupLuaScripts(JNIEnv* env, jobject thiz, jobjectArray scriptSetups, jobjectArray modules, jobjectArray images)
{
    jclass setupClass = env->FindClass("me/magnum/melonds/domain/model/emulator/LuaScriptSetup");
    jfieldID setupNameField = env->GetFieldID(setupClass, "name", "Ljava/lang/String;");
    jfieldID setupSourceField = env->GetFieldID(setupClass, "source", "[B");
    jfieldID setupSavedDataField = env->GetFieldID(setupClass, "savedData", "[B");
    jfieldID setupDataGameKeyField = env->GetFieldID(setupClass, "dataGameKey", "Ljava/lang/String;");
    jfieldID setupOptionsField = env->GetFieldID(setupClass, "options", "[Lme/magnum/melonds/domain/model/emulator/LuaScriptOptionValue;");
    OptionValueFields optionFields(env);

    std::vector<MelonDSAndroid::LuaScriptSource> scripts;
    jsize scriptCount = env->GetArrayLength(scriptSetups);
    scripts.reserve(scriptCount);
    for (jsize i = 0; i < scriptCount; i++)
    {
        jobject setup = env->GetObjectArrayElement(scriptSetups, i);
        MelonDSAndroid::LuaScriptSource script;
        script.name = getString(env, setup, setupNameField);
        script.source = getBytes(env, setup, setupSourceField).value_or("");
        script.savedData = getBytes(env, setup, setupSavedDataField).value_or("");
        script.dataGameKey = getString(env, setup, setupDataGameKeyField);
        auto optionArray = (jobjectArray) env->GetObjectField(setup, setupOptionsField);
        script.options = getOptionValues(env, optionArray, optionFields);
        if (optionArray)
            env->DeleteLocalRef(optionArray);
        scripts.push_back(std::move(script));
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
Java_me_magnum_melonds_MelonEmulator_restartLuaScript(JNIEnv* env, jobject thiz, jint scriptId, jobjectArray options)
{
    OptionValueFields optionFields(env);
    auto optionValues = getOptionValues(env, options, optionFields);
    if (env->ExceptionCheck())
        return;
    MelonDSAndroid::restartLuaScript(scriptId, std::move(optionValues));
}

/**
 * Takes the storage snapshots waiting in the outbox, as LuaScriptDataSnapshot objects. The outbox isn't part of the emulator instance, so this
 * also works after the emulator stopped. If the objects can't be made (out of memory), the snapshots go back in the outbox for the next drain
 * and the exception is left for the app
 */
JNIEXPORT jobjectArray JNICALL
Java_me_magnum_melonds_MelonEmulator_drainLuaData(JNIEnv* env, jobject thiz)
{
    jclass snapshotClass = env->FindClass("me/magnum/melonds/domain/model/emulator/LuaScriptDataSnapshot");
    if (!snapshotClass)
        return nullptr;
    jmethodID snapshotConstructor = env->GetMethodID(snapshotClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;[B)V");
    if (!snapshotConstructor)
        return nullptr;
    std::vector<MelonDSAndroid::LuaStorage::Snapshot> snapshots = MelonDSAndroid::LuaStorage::drainSnapshots();
    auto fail = [&]() -> jobjectArray {
        MelonDSAndroid::LuaStorage::restoreSnapshots(std::move(snapshots));
        return nullptr;
    };

    jobjectArray result = env->NewObjectArray((jsize) snapshots.size(), snapshotClass, nullptr);
    if (!result)
        return fail();
    for (jsize i = 0; i < (jsize) snapshots.size(); i++)
    {
        const auto& snapshot = snapshots[i];
        // The names came from the app as modified UTF-8, so they go back the same way
        jstring gameKey = env->NewStringUTF(snapshot.gameKey.c_str());
        jstring scriptName = env->NewStringUTF(snapshot.scriptName.c_str());
        jbyteArray data = env->NewByteArray((jsize) snapshot.data.size());
        if (!gameKey || !scriptName || !data)
            return fail();
        env->SetByteArrayRegion(data, 0, (jsize) snapshot.data.size(), (const jbyte*) snapshot.data.data());
        jobject snapshotObject = env->NewObject(snapshotClass, snapshotConstructor, gameKey, scriptName, data);
        if (!snapshotObject)
            return fail();
        env->SetObjectArrayElement(result, i, snapshotObject);
        env->DeleteLocalRef(snapshotObject);
        env->DeleteLocalRef(data);
        env->DeleteLocalRef(scriptName);
        env->DeleteLocalRef(gameKey);
    }
    return result;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_updateLuaSticks(JNIEnv* env, jobject thiz, jfloat leftX, jfloat leftY, jfloat rightX, jfloat rightY)
{
    MelonDSAndroid::updateLuaSticks(leftX, leftY, rightX, rightY);
}

}
