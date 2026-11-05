package me.magnum.melonds.ui.settings.fragments

import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.widget.Toast
import androidx.annotation.StringRes
import androidx.fragment.app.viewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.preference.Preference
import androidx.preference.SwitchPreference
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import me.magnum.melonds.R
import me.magnum.melonds.databinding.DialogRaWebApiKeyBinding
import me.magnum.melonds.databinding.DialogRetroachievementsLoginBinding
import me.magnum.melonds.extensions.addOnPreferenceChangeListener
import me.magnum.melonds.ui.common.LoadingDialog
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.flow.observeAsFlow
import me.magnum.melonds.ui.settings.model.RAWebApiKeyTestState
import me.magnum.melonds.ui.settings.model.RetroAchievementsAccountState
import me.magnum.melonds.ui.settings.viewmodel.RetroAchievementsSettingsViewModel

@AndroidEntryPoint
class RetroAchievementsPreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    private val viewModel by viewModels<RetroAchievementsSettingsViewModel>()

    private var loginProgressDialog: LoadingDialog? = null
    private var webApiKeyDialogBinding: DialogRaWebApiKeyBinding? = null

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_retroachievements, rootKey)

        val accountPreference = findPreference<Preference>("ra_login")!!
        val hardcoreModePreference = findPreference<SwitchPreference>("ra_hardcore_enabled")!!
        val richPresencePreference = findPreference<SwitchPreference>("ra_rich_presence")!!
        val webApiKeyPreference = findPreference<Preference>("ra_web_api_key")!!
        val removeWebApiKeyPreference = findPreference<Preference>("ra_web_api_key_remove")!!
        val refreshMetadataPreference = findPreference<Preference>("ra_refresh_metadata")!!
        val clearArtPreference = findPreference<Preference>("ra_clear_art")!!

        hardcoreModePreference.addOnPreferenceChangeListener { _, newValue ->
            val isEnabled = newValue as Boolean

            // Rich preference must be on when hardcore mode is enabled. As such, when hardcore is enabled, disable the preference and force it to be checked
            richPresencePreference.isEnabled = !isEnabled
            if (isEnabled) {
                richPresencePreference.isChecked = true
            }
            true
        }

        accountPreference.setOnPreferenceClickListener {
            val accountState = viewModel.accountState.value
            when (accountState) {
                is RetroAchievementsAccountState.LoggedIn -> showLogoutConfirmationDialog()
                is RetroAchievementsAccountState.LoginExpired -> showLoginDialog(accountState.existingUsername)
                RetroAchievementsAccountState.LoggedOut -> showLoginDialog(null)
                RetroAchievementsAccountState.Unknown -> {
                    // Do nothing until a proper state is known
                }
            }
            true
        }

        webApiKeyPreference.setOnPreferenceClickListener {
            showWebApiKeyDialog()
            true
        }

        removeWebApiKeyPreference.setOnPreferenceClickListener {
            viewModel.removeWebApiKey()
            Toast.makeText(requireContext(), R.string.ra_web_api_key_removed, Toast.LENGTH_SHORT).show()
            true
        }

        refreshMetadataPreference.setOnPreferenceClickListener {
            viewModel.refreshMetadata()
            Toast.makeText(requireContext(), R.string.ra_refresh_metadata_scheduled, Toast.LENGTH_LONG).show()
            true
        }

        clearArtPreference.setOnPreferenceClickListener {
            viewModel.clearDownloadedArt()
            Toast.makeText(requireContext(), R.string.ra_clear_art_done, Toast.LENGTH_SHORT).show()
            clearArtPreference.summary = getString(R.string.ra_clear_art_done)
            true
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.hasWebApiKey.collect { hasKey ->
                    webApiKeyPreference.summary = getString(if (hasKey) R.string.ra_web_api_key_summary_set else R.string.ra_web_api_key_summary_unset)
                    removeWebApiKeyPreference.isVisible = hasKey
                    refreshMetadataPreference.isEnabled = hasKey
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.webApiKeyTestState.collect { testState ->
                    webApiKeyDialogBinding?.layoutApiKey?.helperText = testState.message()?.let(::getString)
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.accountState.collect {
                    when (it) {
                        is RetroAchievementsAccountState.LoggedIn -> {
                            accountPreference.title = getString(R.string.retroachievements_logout)
                            accountPreference.summary = getString(R.string.retroachievements_login_status, it.accountName)
                            accountPreference.notifyDependencyChange(false)
                        }
                        is RetroAchievementsAccountState.LoginExpired -> {
                            accountPreference.title = getString(R.string.login)
                            accountPreference.summary = getString(R.string.retroachievements_login_expired_status)
                            accountPreference.notifyDependencyChange(true)
                        }
                        RetroAchievementsAccountState.LoggedOut -> {
                            accountPreference.title = getString(R.string.login_with_retro_achievements)
                            accountPreference.summary = getString(R.string.retroachievements_login_summary)
                            accountPreference.notifyDependencyChange(true)
                        }
                        RetroAchievementsAccountState.Unknown -> {
                            accountPreference.title = getString(R.string.ellipsis)
                            accountPreference.summary = getString(R.string.ellipsis)
                            accountPreference.notifyDependencyChange(true)
                        }
                    }
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                val isLoggedInFlow = viewModel.accountState.map { it is RetroAchievementsAccountState.LoggedIn }
                combine(isLoggedInFlow, hardcoreModePreference.observeAsFlow()) { isLoggedIn, isHardcoreEnabled ->
                    isLoggedIn && !isHardcoreEnabled
                }.collect { isRichPresenceEnabled ->
                    richPresencePreference.isEnabled = isRichPresenceEnabled
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.loggingIn.collect { loggingIn ->
                    loginProgressDialog = if (loggingIn) {
                        // The state is collected again after every restart, so keep the dialog that's already showing
                        loginProgressDialog ?: LoadingDialog(requireContext()).apply {
                            show()
                        }
                    } else {
                        loginProgressDialog?.dismiss()
                        null
                    }
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
                viewModel.loginErrorEvent.collect {
                    Toast.makeText(requireContext(), R.string.retro_achievements_login_error_short, Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun showLoginDialog(existingUsername: String?) {
        val dialogBuilder = MaterialAlertDialogBuilder(requireContext())
        val binding = DialogRetroachievementsLoginBinding.inflate(LayoutInflater.from(dialogBuilder.context))
        if (existingUsername != null) {
            binding.textUsername.setText(existingUsername)
        }

        dialogBuilder
            .setTitle(R.string.login_with_retro_achievements)
            .setView(binding.root)
            .setPositiveButton(R.string.login) { dialog, _ ->
                viewModel.login(
                    binding.textUsername.text?.toString() ?: "",
                    binding.textPassword.text?.toString() ?: "",
                )
                dialog.dismiss()
            }
            .setNegativeButton(R.string.cancel) { dialog, _ ->
                dialog.dismiss()
            }
            .show()
    }

    /**
     * The field starts empty rather than with the saved key, so opening this can't show the key to anyone looking at the screen.
     */
    private fun showWebApiKeyDialog() {
        val dialogBuilder = MaterialAlertDialogBuilder(requireContext())
        val binding = DialogRaWebApiKeyBinding.inflate(LayoutInflater.from(dialogBuilder.context))
        viewModel.resetWebApiKeyTest()
        webApiKeyDialogBinding = binding

        val dialog = dialogBuilder
            .setTitle(R.string.ra_web_api_key)
            .setMessage(R.string.ra_web_api_key_dialog_message)
            .setView(binding.root)
            .setPositiveButton(R.string.save) { _, _ ->
                viewModel.saveWebApiKey(binding.textApiKey.text?.toString() ?: "")
            }
            .setNeutralButton(R.string.ra_web_api_key_test, null)
            .setNegativeButton(R.string.cancel, null)
            .setOnDismissListener {
                webApiKeyDialogBinding = null
                viewModel.resetWebApiKeyTest()
            }
            .show()

        // Set here, since a listener given to the builder would also close the dialog
        dialog.getButton(DialogInterface.BUTTON_NEUTRAL).setOnClickListener {
            viewModel.testWebApiKey(binding.textApiKey.text?.toString() ?: "")
        }
    }

    @StringRes
    private fun RAWebApiKeyTestState.message(): Int? = when (this) {
        RAWebApiKeyTestState.IDLE -> null
        RAWebApiKeyTestState.EMPTY -> R.string.ra_web_api_key_test_empty
        RAWebApiKeyTestState.TESTING -> R.string.ra_web_api_key_testing
        RAWebApiKeyTestState.VALID -> R.string.ra_web_api_key_test_valid
        RAWebApiKeyTestState.REJECTED -> R.string.ra_web_api_key_test_rejected
        RAWebApiKeyTestState.FAILED -> R.string.ra_web_api_key_test_failed
    }

    private fun showLogoutConfirmationDialog() {
        MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.retroachievements_logout)
            .setMessage(R.string.retroachievements_logout_confirmation)
            .setPositiveButton(R.string.retroachievements_logout) { dialog, _ ->
                viewModel.logoutFromRetroAchievements()
                dialog.dismiss()
            }
            .setNegativeButton(R.string.cancel) { dialog, _ ->
                dialog.dismiss()
            }
            .show()
    }

    override fun getTitle() = getString(R.string.retroachievements)
}