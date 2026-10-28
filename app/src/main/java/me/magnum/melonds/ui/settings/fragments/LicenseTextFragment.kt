package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.fragment.app.Fragment
import com.mikepenz.aboutlibraries.ui.compose.android.produceLibraries
import me.magnum.melonds.R
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.settingsContentPadding
import me.magnum.melonds.ui.theme.MelonTheme

/**
 * Shows the full text of one licence from the generated open source licences list (res/raw/aboutlibraries.json).
 */
class LicenseTextFragment : Fragment(), PreferenceFragmentTitleProvider {

    companion object {
        private const val KEY_LICENSE_HASH = "license_hash"
        private const val KEY_TITLE = "title"

        /**
         * @param licenseHash The hash of the licence, as set in config/licenses
         */
        fun newInstance(licenseHash: String, @StringRes title: Int) = LicenseTextFragment().apply {
            arguments = Bundle().apply {
                putString(KEY_LICENSE_HASH, licenseHash)
                putInt(KEY_TITLE, title)
            }
        }
    }

    override fun getTitle() = getString(requireArguments().getInt(KEY_TITLE))

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val licenseHash = requireArguments().getString(KEY_LICENSE_HASH)
        return ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                MelonTheme {
                    LicenseTextScreen(licenseHash)
                }
            }
        }
    }
}

@Composable
private fun LicenseTextScreen(licenseHash: String?) {
    // Null while the list is loading
    val libraries by produceLibraries(R.raw.aboutlibraries)
    val notFoundMessage = stringResource(R.string.about_license_not_found)
    // One item per paragraph, so that only the visible part of a long licence is laid out
    val paragraphs = remember(libraries, licenseHash, notFoundMessage) {
        val loadedLibraries = libraries ?: return@remember emptyList()
        val content = loadedLibraries.licenses.firstOrNull { it.hash == licenseHash }?.licenseContent
        content?.takeIf { it.isNotBlank() }?.split("\n\n") ?: listOf(notFoundMessage)
    }

    SelectionContainer {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = settingsContentPadding(left = 16.dp, top = 8.dp, right = 16.dp, bottom = 16.dp),
        ) {
            items(paragraphs) {
                Text(
                    modifier = Modifier.padding(vertical = 6.dp),
                    text = it,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    }
}
