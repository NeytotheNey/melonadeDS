package me.magnum.melonds.ui.settings.fragments

import android.content.ActivityNotFoundException
import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.OpenInNew
import androidx.compose.material.icons.outlined.Code
import androidx.compose.material.icons.outlined.Description
import androidx.compose.material.icons.outlined.Gavel
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.fragment.app.Fragment
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.R
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.SettingsActivity
import me.magnum.melonds.ui.settings.settingsContentPadding
import me.magnum.melonds.ui.theme.MelonTheme

private const val SOURCE_CODE_URL = "https://github.com/NeytotheNey/melonadeDS"
private const val UPSTREAM_SOURCE_CODE_URL = "https://github.com/rafaelvcaetano/melonDS-android"

/**
 * Settings → About: version, credits, the app's licence, links to the source code and the open source licences. The GPL-3 and the
 * licences of the bundled code need these to be shown in the app.
 */
class AboutFragment : Fragment(), PreferenceFragmentTitleProvider {

    private companion object {
        // Hash of the GPL-3 entry in config/licenses, which holds the text of the root LICENSE file
        const val GPL_LICENSE_HASH = "GPL-3.0-only"
    }

    // About is only shown in SettingsActivity
    private val settingsActivity get() = requireActivity() as SettingsActivity

    override fun getTitle() = getString(R.string.settings_about)

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                MelonTheme {
                    AboutScreen(
                        onOpenGplClick = { settingsActivity.showPage(LicenseTextFragment.newInstance(GPL_LICENSE_HASH, R.string.about_gpl)) },
                        onOpenSourceLicensesClick = { settingsActivity.showPage(OpenSourceLicensesFragment()) },
                        onSourceCodeClick = { openUrl(SOURCE_CODE_URL) },
                        onUpstreamSourceCodeClick = { openUrl(UPSTREAM_SOURCE_CODE_URL) },
                    )
                }
            }
        }
    }

    private fun openUrl(url: String) {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, url.toUri()))
        } catch (_: ActivityNotFoundException) {
            Toast.makeText(requireContext(), R.string.about_link_open_failed, Toast.LENGTH_SHORT).show()
        }
    }
}

@Composable
private fun AboutScreen(
    onOpenGplClick: () -> Unit,
    onOpenSourceLicensesClick: () -> Unit,
    onSourceCodeClick: () -> Unit,
    onUpstreamSourceCodeClick: () -> Unit,
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = settingsContentPadding(left = 16.dp, top = 8.dp, right = 16.dp, bottom = 16.dp),
    ) {
        item {
            AppHeader()
        }
        item {
            AboutGroup(title = stringResource(R.string.about_credits)) {
                AboutCard(position = CardPosition.SINGLE) {
                    Text(
                        modifier = Modifier.padding(horizontal = 20.dp, vertical = 16.dp),
                        text = stringResource(R.string.about_credits_text),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        item {
            AboutGroup(title = stringResource(R.string.about_licenses)) {
                // The GPL-3's "Appropriate Legal Notices": copyright, no warranty, and where to read the licence (the next row)
                AboutCard(position = CardPosition.FIRST) {
                    Text(
                        modifier = Modifier.padding(horizontal = 20.dp, vertical = 16.dp),
                        text = stringResource(R.string.about_legal_notice),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                AboutRow(
                    position = CardPosition.MIDDLE,
                    icon = Icons.Outlined.Gavel,
                    title = stringResource(R.string.about_gpl),
                    summary = stringResource(R.string.about_gpl_summary),
                    onClick = onOpenGplClick,
                )
                AboutRow(
                    position = CardPosition.LAST,
                    icon = Icons.Outlined.Description,
                    title = stringResource(R.string.about_open_source_licenses),
                    summary = stringResource(R.string.about_open_source_licenses_summary),
                    onClick = onOpenSourceLicensesClick,
                )
            }
        }
        item {
            AboutGroup(title = stringResource(R.string.about_source_code)) {
                AboutRow(
                    position = CardPosition.FIRST,
                    icon = Icons.Outlined.Code,
                    title = stringResource(R.string.about_source_code_melonadeds),
                    summary = SOURCE_CODE_URL.removePrefix("https://"),
                    isLink = true,
                    onClick = onSourceCodeClick,
                )
                AboutRow(
                    position = CardPosition.LAST,
                    icon = Icons.Outlined.Code,
                    title = stringResource(R.string.about_source_code_upstream),
                    summary = UPSTREAM_SOURCE_CODE_URL.removePrefix("https://"),
                    isLink = true,
                    onClick = onUpstreamSourceCodeClick,
                )
            }
        }
    }
}

@Composable
private fun AppHeader() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        AppIcon()
        Spacer(Modifier.size(8.dp))
        Text(
            text = stringResource(R.string.app_name),
            style = MaterialTheme.typography.headlineSmall,
        )
        Text(
            text = stringResource(R.string.about_version, BuildConfig.VERSION_NAME),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
    }
}

/**
 * The launcher icon. The mipmap is an adaptive icon, which Compose can't load, so this draws its layers: the foreground is 108 dp with
 * the visible part in the middle 72 dp, so it's drawn 1.5 times bigger than the circle that clips it, like a launcher does.
 */
@Composable
private fun AppIcon() {
    val iconSize = 80.dp
    Box(
        modifier = Modifier
            .size(iconSize)
            .clip(CircleShape)
            .background(colorResource(R.color.launcherBackground)),
        contentAlignment = Alignment.Center,
    ) {
        Image(
            modifier = Modifier.requiredSize(iconSize * 1.5f),
            painter = painterResource(R.drawable.ic_launcher_foreground),
            contentDescription = null,
        )
    }
}

/**
 * Where a card sits in its group. Like the settings rows (see PreferenceCardDecoration), the outer corners of a group are large and the
 * corners between cards are small.
 */
private enum class CardPosition {
    FIRST,
    MIDDLE,
    LAST,
    SINGLE,
}

@Composable
private fun AboutGroup(title: String, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(dimensionResource(R.dimen.preference_card_gap))) {
        Text(
            modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 6.dp),
            text = title,
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.primary,
        )
        content()
    }
}

@Composable
private fun AboutCard(position: CardPosition, onClick: (() -> Unit)? = null, content: @Composable () -> Unit) {
    val outer = dimensionResource(R.dimen.preference_card_corner_radius_outer)
    val inner = dimensionResource(R.dimen.preference_card_corner_radius_inner)
    val shape = when (position) {
        CardPosition.FIRST -> RoundedCornerShape(topStart = outer, topEnd = outer, bottomStart = inner, bottomEnd = inner)
        CardPosition.MIDDLE -> RoundedCornerShape(inner)
        CardPosition.LAST -> RoundedCornerShape(topStart = inner, topEnd = inner, bottomStart = outer, bottomEnd = outer)
        CardPosition.SINGLE -> RoundedCornerShape(outer)
    }
    val color = MaterialTheme.colorScheme.surfaceContainer
    if (onClick != null) {
        Surface(modifier = Modifier.fillMaxWidth(), shape = shape, color = color, onClick = onClick, content = content)
    } else {
        Surface(modifier = Modifier.fillMaxWidth(), shape = shape, color = color, content = content)
    }
}

/**
 * A row laid out like the settings rows (preference_card.xml): an icon, a title and a summary. Rows that open a web page get an
 * "open in new" icon at the end.
 */
@Composable
private fun AboutRow(
    position: CardPosition,
    icon: ImageVector,
    title: String,
    summary: String,
    isLink: Boolean = false,
    onClick: () -> Unit,
) {
    AboutCard(position = position, onClick = onClick) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 64.dp)
                .padding(start = 20.dp, end = 16.dp, top = 12.dp, bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.width(16.dp))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                )
                Text(
                    text = summary,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (isLink) {
                Spacer(Modifier.width(16.dp))
                Icon(
                    modifier = Modifier.size(20.dp),
                    imageVector = Icons.AutoMirrored.Outlined.OpenInNew,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
