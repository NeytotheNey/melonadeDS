package me.magnum.melonds.di

import android.content.Context
import android.content.SharedPreferences
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import kotlinx.serialization.json.Json
import me.magnum.melonds.common.network.MelonOkHttpInterceptor
import me.magnum.melonds.common.retroachievements.AndroidRASignatureProvider
import me.magnum.melonds.common.retroachievements.AndroidRAUserAuthStore
import me.magnum.melonds.common.retroachievements.AndroidRAWebApiKeyStore
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtStore
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncScheduler
import me.magnum.rcheevosapi.RASignatureProvider
import me.magnum.rcheevosapi.RAApi
import me.magnum.rcheevosapi.RAUserAuthStore
import me.magnum.rcheevosapi.RAWebApi
import me.magnum.rcheevosapi.RAWebApiKeyStore
import okhttp3.OkHttpClient
import java.io.File
import javax.inject.Named
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object RAModule {

    @Provides
    fun provideMelonOkHttpInterceptor(@ApplicationContext context: Context): MelonOkHttpInterceptor {
        return MelonOkHttpInterceptor(context)
    }

    @Provides
    @Named("ra-api-client")
    fun provideRAApiOkHttpClient(melonOkHttpInterceptor: MelonOkHttpInterceptor): OkHttpClient {
        return OkHttpClient.Builder()
            .addInterceptor(melonOkHttpInterceptor)
            .build()
    }

    @Provides
    @Singleton
    fun provideRAUserAuthStore(sharedPreferences: SharedPreferences): RAUserAuthStore {
        return AndroidRAUserAuthStore(sharedPreferences)
    }

    @Provides
    @Singleton
    fun provideRAAchievementSignatureProvider(): RASignatureProvider {
        return AndroidRASignatureProvider()
    }

    @Provides
    @Singleton
    fun provideRAApi(@Named("ra-api-client") client: OkHttpClient, json: Json, userAuthStore: RAUserAuthStore, achievementSignatureProvider: RASignatureProvider): RAApi {
        return RAApi(
            okHttpClient = client,
            json = json,
            userAuthStore = userAuthStore,
            signatureProvider = achievementSignatureProvider,
        )
    }

    @Provides
    @Singleton
    fun provideRAWebApiKeyStore(@ApplicationContext context: Context): RAWebApiKeyStore {
        return AndroidRAWebApiKeyStore(context.getSharedPreferences(AndroidRAWebApiKeyStore.PREFERENCES_FILE, Context.MODE_PRIVATE))
    }

    @Provides
    @Singleton
    fun provideRAWebApi(@Named("ra-api-client") client: OkHttpClient, json: Json, keyStore: RAWebApiKeyStore): RAWebApi {
        return RAWebApi(client, json, keyStore)
    }

    @Provides
    @Singleton
    fun provideRetroAchievementsArtStore(@ApplicationContext context: Context): RetroAchievementsArtStore {
        return RetroAchievementsArtStore { context.externalCacheDir?.let { File(it, "ra_art") } }
    }

    @Provides
    @Singleton
    fun provideRomMetadataSyncScheduler(@ApplicationContext context: Context, keyStore: RAWebApiKeyStore): RomMetadataSyncScheduler {
        return RomMetadataSyncScheduler(context, keyStore)
    }
}