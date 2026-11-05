package me.magnum.melonds.ui.settings.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import me.magnum.melonds.domain.repositories.RetroAchievementsRepository
import me.magnum.melonds.domain.repositories.RomMetadataRepository
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncScheduler
import me.magnum.melonds.ui.settings.model.RAWebApiKeyTestState
import me.magnum.melonds.ui.settings.model.RetroAchievementsAccountState
import me.magnum.rcheevosapi.RAWebApi
import me.magnum.rcheevosapi.RAWebApiKeyStore
import me.magnum.rcheevosapi.exception.RAWebApiException
import me.magnum.rcheevosapi.model.RAUserAuth
import javax.inject.Inject

@HiltViewModel
class RetroAchievementsSettingsViewModel @Inject constructor(
    private val retroAchievementsRepository: RetroAchievementsRepository,
    private val romMetadataRepository: RomMetadataRepository,
    private val webApiKeyStore: RAWebApiKeyStore,
    private val raWebApi: RAWebApi,
    private val romMetadataSyncScheduler: RomMetadataSyncScheduler,
): ViewModel() {

    private val _hasWebApiKey = MutableStateFlow(webApiKeyStore.getApiKey() != null)
    val hasWebApiKey = _hasWebApiKey.asStateFlow()

    private val _webApiKeyTestState = MutableStateFlow(RAWebApiKeyTestState.IDLE)
    val webApiKeyTestState = _webApiKeyTestState.asStateFlow()

    private var webApiKeyTestJob: Job? = null

    private val _accountState = MutableStateFlow<RetroAchievementsAccountState>(RetroAchievementsAccountState.Unknown)
    val accountState by lazy {
        viewModelScope.launch {
            updateLoggedInState()
        }
        _accountState.asStateFlow()
    }

    private val _loggingIn = MutableStateFlow(false)
    val loggingIn = _loggingIn.asStateFlow()

    private val _loginErrorEvent = MutableSharedFlow<Unit>(extraBufferCapacity = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val loginErrorEvent = _loginErrorEvent.asSharedFlow()

    fun logoutFromRetroAchievements() {
        viewModelScope.launch {
            retroAchievementsRepository.logout()
            _accountState.value = RetroAchievementsAccountState.LoggedOut
        }
    }

    fun login(username: String, password: String) {
        viewModelScope.launch {
            _loggingIn.value = true
            retroAchievementsRepository.login(username, password)
                .onSuccess {
                    updateLoggedInState()
                }
                .onFailure {
                    _loginErrorEvent.tryEmit(Unit)
                }
            _loggingIn.value = false
        }
    }

    /**
     * Saves [apiKey] and starts a sync with it. A blank one changes nothing, since the dialog's field starts empty even when a key is saved.
     */
    fun saveWebApiKey(apiKey: String) {
        val trimmedKey = apiKey.trim()
        if (trimmedKey.isEmpty()) {
            return
        }

        webApiKeyStore.storeApiKey(trimmedKey)
        romMetadataSyncScheduler.onKeySavedOrRefreshRequested()
        _hasWebApiKey.value = true
    }

    fun removeWebApiKey() {
        webApiKeyStore.clearApiKey()
        romMetadataSyncScheduler.cancelSync()
        _hasWebApiKey.value = false
    }

    /**
     * Tests [apiKey], or the saved key if it's blank.
     */
    fun testWebApiKey(apiKey: String) {
        val keyToTest = apiKey.trim().ifEmpty { webApiKeyStore.getApiKey() }
        if (keyToTest == null) {
            _webApiKeyTestState.value = RAWebApiKeyTestState.EMPTY
            return
        }

        webApiKeyTestJob?.cancel()
        webApiKeyTestJob = viewModelScope.launch {
            _webApiKeyTestState.value = RAWebApiKeyTestState.TESTING
            _webApiKeyTestState.value = raWebApi.testApiKey(keyToTest).fold(
                onSuccess = { RAWebApiKeyTestState.VALID },
                onFailure = {
                    if (it is RAWebApiException.InvalidKey) RAWebApiKeyTestState.REJECTED else RAWebApiKeyTestState.FAILED
                },
            )
        }
    }

    fun resetWebApiKeyTest() {
        webApiKeyTestJob?.cancel()
        _webApiKeyTestState.value = RAWebApiKeyTestState.IDLE
    }

    fun refreshMetadata() {
        viewModelScope.launch {
            romMetadataRepository.markMetadataForRefresh()
            romMetadataSyncScheduler.onKeySavedOrRefreshRequested()
        }
    }

    fun clearDownloadedArt() {
        // Or the sync would just put the art back. The next scan starts it again
        romMetadataSyncScheduler.cancelSync()
        viewModelScope.launch(Dispatchers.IO) {
            romMetadataRepository.clearDownloadedArt()
        }
    }

    private suspend fun updateLoggedInState() {
        val userAuth = retroAchievementsRepository.getUserAuthentication()
        _accountState.value = when (userAuth) {
            is RAUserAuth.Authenticated -> RetroAchievementsAccountState.LoggedIn(userAuth.username)
            is RAUserAuth.AuthenticationExpired -> RetroAchievementsAccountState.LoginExpired(userAuth.username)
            null -> RetroAchievementsAccountState.LoggedOut
        }
    }
}