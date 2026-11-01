import java.util.Properties
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.aboutlibraries.android)
    alias(libs.plugins.android.application)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.hilt.android)
    alias(libs.plugins.kotlin.parcelize)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.ksp)
    alias(libs.plugins.room)
}

android {
    signingConfigs {
        create("release") {
            val props = Properties().apply {
                val file = rootProject.file("local.properties")
                if (file.exists()) load(file.inputStream())
            }
            (props["MELONDS_KEYSTORE"] as String?)?.let { storeFile = file(it) }
            storePassword = props["MELONDS_KEYSTORE_PASSWORD"] as String? ?: ""
            keyAlias = props["MELONDS_KEY_ALIAS"] as String? ?: ""
            keyPassword = props["MELONDS_KEY_PASSWORD"] as String? ?: ""
        }
    }

    namespace = "me.magnum.melonds"
    compileSdk = AppConfig.compileSdkVersion
    ndkVersion = AppConfig.ndkVersion
    defaultConfig {
        applicationId = "me.magnum.melonds"
        minSdk = AppConfig.minSdkVersion
        targetSdk = AppConfig.targetSdkVersion
        versionCode = AppConfig.versionCode
        versionName = AppConfig.versionName
        buildConfigField("String", "UPSTREAM_VERSION_NAME", "\"${AppConfig.upstreamVersionName}\"")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk {
            abiFilters.addAll(listOf("arm64-v8a", "x86_64"))
        }
        externalNativeBuild {
            cmake {
                cppFlags("-std=c++20 -Wno-write-strings")
            }
        }
    }
    androidResources {
        // The app is English only. This also drops the translations that libraries bring in.
        localeFilters += listOf("en")
    }
    buildFeatures {
        viewBinding = true
        compose = true
        buildConfig = true
    }
    buildTypes {
        getByName("release") {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("release")
        }
        getByName("debug") {
            applicationIdSuffix = ".dev"
            // Debug builds are used for on-device perf testing, so optimize native code but keep symbols.
            // -PnativeBuildType=Debug builds it at -O0 instead, to check optimization-dependent bugs.
            // MELONADE_DEBUG_TOOLS turns on native debug-only tools, like the 2D frame hash log (MelonInstance.cpp).
            val nativeBuildType = providers.gradleProperty("nativeBuildType").getOrElse("RelWithDebInfo")
            externalNativeBuild {
                cmake {
                    arguments("-DCMAKE_BUILD_TYPE=$nativeBuildType", "-DMELONADE_DEBUG_TOOLS=ON")
                }
            }
        }
    }

    flavorDimensions += listOf("version", "build")
    productFlavors {
        create("playStore") {
            dimension = "version"
            versionNameSuffix = " PS"
        }
        create("gitHub") {
            dimension = "version"
            isDefault = true
            versionNameSuffix = " GH"
            ndk {
                // Add 32 bit support only on GitHub releases
                abiFilters.add("armeabi-v7a")
            }
        }

        create("prod") {
            dimension = "build"
            isDefault = true
        }
        create("nightly") {
            dimension = "build"
            applicationIdSuffix = ".nightly"
            versionNameSuffix = " (NIGHTLY)"
        }
    }
    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }
    sourceSets {
        // Adds exported schema location as test app assets.
        getByName("androidTest").assets.directories += "$projectDir/schemas"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
}

androidComponents {
    // minSdk >= 28 stores dex uncompressed, which doubles the unminified debug APK. Release builds are minified, so they keep the default
    onVariants(selector().withBuildType("debug")) { variant ->
        variant.packaging.dex.useLegacyPackaging.set(true)
    }
}

kotlin {
    compilerOptions {
        jvmTarget = JvmTarget.JVM_21
        freeCompilerArgs.add("-opt-in=kotlin.ExperimentalUnsignedTypes")
    }
}

room {
    schemaDirectory("$projectDir/schemas")
}

// Generates the open source licences list (res/raw/aboutlibraries.json) from the dependencies of each variant. It runs offline, so licence
// texts are never downloaded: they come from config/licenses, along with the native code and shaders Gradle can't see (config/libraries)
aboutLibraries {
    offlineMode = true
    collect {
        configPath = file("config")
    }
    export {
        // Not shown anywhere, and it would only make the file bigger
        excludeFields.add("funding")
    }
    library {
        requireLicense = true
    }
    license {
        // Fails the build when a dependency brings a licence that config/licenses has no text for. The shared BSD entries there hold the
        // copyright notice of the libraries below, so any other library under those licences needs its own entry first
        strictMode = com.mikepenz.aboutlibraries.plugin.StrictMode.FAIL
        allowedLicenses.addAll(
            "Apache-2.0", "0BSD", "CC0-1.0", "MIT", "Unlicense", "GPL-2.0-or-later", "GPL-3.0-only", "GPL-3.0-or-later", "LGPL-2.1-or-later",
            "public-domain", "see-source", "fatfs",
        )
        allowedLicensesMap = mapOf(
            "BSD-2-Clause" to listOf("com.atlassian.commonmark", "native:xxhash", "native:freebios"),
            "BSD-3-Clause" to listOf("androidx.camera:camera-core", "native:libslirp"),
        )
    }
}

dependencies {
    implementation(projects.rcheevosApi)
    implementation(projects.common)

    implementation(libs.androidx.activity)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.camera2)
    implementation(libs.androidx.camera.lifecycle)
    implementation(libs.androidx.cardview)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.core)
    implementation(libs.androidx.documentfile)
    implementation(libs.androidx.fragment)
    implementation(libs.androidx.hilt.work)
    implementation(libs.androidx.lifecycle.viewmodel)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.preference)
    implementation(libs.androidx.recyclerview)
    implementation(libs.androidx.room)
    implementation(libs.androidx.room.ktx)
    implementation(libs.androidx.room.rxjava)
    implementation(libs.androidx.splashscreen)
    implementation(libs.androidx.startup)
    implementation(libs.androidx.window)
    implementation(libs.androidx.work)
    implementation(libs.android.material)

    implementation(platform(libs.compose.bom))
    implementation(libs.compose.foundation)
    implementation(libs.compose.markdown)
    implementation(libs.compose.material3)
    implementation(libs.compose.material3.adaptive)
    implementation(libs.compose.material.icons)
    implementation(libs.compose.navigation)
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.tooling.preview)

    debugImplementation(libs.compose.ui.tooling)
    // Debug builds only. Configured in src/debug (LeakCanaryInitializer) so that it never dumps the heap while a game is open
    debugImplementation(libs.leakcanary)

    implementation(libs.aboutlibraries.compose.m3)
    implementation(libs.coil)
    implementation(libs.gson)
    implementation(libs.hilt)
    implementation(libs.kotlin.serialization)
    implementation(libs.kotlinx.coroutines.rx)
    implementation(libs.picasso)
    implementation(libs.commons.compress)
    implementation(libs.xz)

    ksp(libs.hilt.compiler)
    ksp(libs.hilt.compiler.android)
    ksp(libs.room.compiler)

    testImplementation(libs.junit)
    // The real org.json, since android.jar only has stubs in local unit tests
    testImplementation(libs.org.json)

    androidTestImplementation(libs.androidx.room.testing)
    androidTestImplementation(libs.androidx.test.core)
    androidTestImplementation(libs.androidx.test.junit)
    androidTestImplementation(libs.androidx.test.runner)
}