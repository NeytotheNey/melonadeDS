package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.platform.UriHandler
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.unit.dp
import androidx.fragment.app.Fragment
import com.mikepenz.aboutlibraries.ui.compose.android.produceLibraries
import com.mikepenz.aboutlibraries.ui.compose.m3.LibrariesContainer
import com.mikepenz.aboutlibraries.ui.compose.variant.LibraryDetailMode
import me.magnum.melonds.R
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.settingsContentPadding
import me.magnum.melonds.ui.theme.MelonTheme

/**
 * The third-party code in the app and its licences. The list is generated at build time by the AboutLibraries plugin from the Gradle
 * dependencies, plus the native code and shaders described in app/config/libraries. Tapping an entry shows its licence text in a sheet.
 */
class OpenSourceLicensesFragment : Fragment(), PreferenceFragmentTitleProvider {

    override fun getTitle() = getString(R.string.about_open_source_licenses)

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                MelonTheme {
                    OpenSourceLicensesScreen()
                }
            }
        }
    }
}

@Composable
private fun OpenSourceLicensesScreen() {
    val libraries by produceLibraries(R.raw.aboutlibraries)
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    // The link chips open their URL with LocalUriHandler, which throws when no app can open it
    val safeUriHandler = remember(context, uriHandler) {
        object : UriHandler {
            override fun openUri(uri: String) {
                try {
                    uriHandler.openUri(uri)
                } catch (_: IllegalArgumentException) {
                    Toast.makeText(context, R.string.about_link_open_failed, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    CompositionLocalProvider(LocalUriHandler provides safeUriHandler) {
        LibrariesContainer(
            libraries = libraries,
            modifier = Modifier.fillMaxSize(),
            contentPadding = settingsContentPadding(bottom = 16.dp),
            // The sheet shows the licence as plain text. The dialog parses it as HTML, which drops anything in angle brackets, like the
            // addresses in the GPL
            detailMode = LibraryDetailMode.Sheet,
        )
    }
}
