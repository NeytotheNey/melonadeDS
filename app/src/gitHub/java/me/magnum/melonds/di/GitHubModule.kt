package me.magnum.melonds.di

import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import me.magnum.melonds.domain.services.UpdateInstallManager
import me.magnum.melonds.github.services.DisabledUpdateInstallManager
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object GitHubModule {
    @Provides
    @Singleton
    fun provideUpdateInstallManager(): UpdateInstallManager {
        return DisabledUpdateInstallManager()
    }
}
