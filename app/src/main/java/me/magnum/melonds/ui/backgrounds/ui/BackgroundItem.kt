package me.magnum.melonds.ui.backgrounds.ui

import android.net.Uri
import androidx.compose.animation.AnimatedContentScope
import androidx.compose.animation.ExperimentalSharedTransitionApi
import androidx.compose.animation.SharedTransitionScope
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil.compose.rememberAsyncImagePainter
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.Background
import me.magnum.melonds.domain.model.BuiltInBackgrounds
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.theme.MelonTheme

@OptIn(ExperimentalSharedTransitionApi::class)
@Composable
fun BackgroundItem(
    background: Background,
    isSelected: Boolean,
    sharedTransitionScope: SharedTransitionScope,
    animatedContentScope: AnimatedContentScope,
    onClick: () -> Unit,
    onPreviewClick: () -> Unit,
    onDeleteClick: () -> Unit,
) {
    BackgroundItemBase(
        backgroundId = background.id?.toString(),
        backgroundName = background.name,
        backgroundImage = rememberAsyncImagePainter(background),
        isSelected = isSelected,
        showOptions = true,
        showDeleteOption = !BuiltInBackgrounds.isBuiltIn(background.id),
        sharedTransitionScope = sharedTransitionScope,
        animatedContentScope = animatedContentScope,
        onClick = onClick,
        onPreviewClick = onPreviewClick,
        onDeleteClick = onDeleteClick,
    )
}

@OptIn(ExperimentalSharedTransitionApi::class)
@Composable
fun NoneBackgroundItem(
    isSelected: Boolean,
    onClick: () -> Unit,
) {
    BackgroundItemBase(
        backgroundId = null,
        backgroundName = stringResource(R.string.none),
        backgroundImage = painterResource(R.drawable.ic_block),
        isSelected = isSelected,
        showOptions = false,
        showDeleteOption = false,
        sharedTransitionScope = null,
        animatedContentScope = null,
        onClick = onClick,
        onPreviewClick = { },
        onDeleteClick = { },
    )
}

@OptIn(ExperimentalSharedTransitionApi::class)
@Composable
private fun BackgroundItemBase(
    backgroundId: String?,
    backgroundName: String,
    backgroundImage: Painter,
    isSelected: Boolean,
    showOptions: Boolean,
    showDeleteOption: Boolean,
    sharedTransitionScope: SharedTransitionScope?,
    animatedContentScope: AnimatedContentScope?,
    onClick: () -> Unit,
    onPreviewClick: () -> Unit,
    onDeleteClick: () -> Unit,
) {
    var isOptionsPopupVisible by remember { mutableStateOf(false) }

    Card(
        modifier = if (isSelected) {
            Modifier.border(
                width = 4.dp,
                color = MaterialTheme.colorScheme.primary,
                shape = MaterialTheme.shapes.medium,
            )
        } else {
            Modifier
        },
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
        onClick = onClick,
    ) {
        Column(
            modifier = Modifier.padding(4.dp).width(IntrinsicSize.Min),
        ) {
            if (LocalInspectionMode.current) {
                Box(Modifier.fillMaxWidth().aspectRatio(1.0f).padding(start = 4.dp, top = 4.dp, end = 4.dp).background(Color.Gray))
            } else {
                if (sharedTransitionScope != null && animatedContentScope != null) {
                    with(sharedTransitionScope) {
                        Image(
                            modifier = Modifier
                                .padding(start = 4.dp, top = 4.dp, end = 4.dp)
                                .sharedElement(
                                    sharedContentState = sharedTransitionScope.rememberSharedContentState(backgroundId.orEmpty()),
                                    animatedVisibilityScope = animatedContentScope,
                                )
                                .fillMaxWidth().aspectRatio(1.0f),
                            painter = backgroundImage,
                            contentDescription = null,
                        )
                    }
                } else {
                    Image(
                        modifier = Modifier.padding(start = 4.dp, top = 4.dp, end = 4.dp).fillMaxWidth().aspectRatio(1.0f),
                        painter = backgroundImage,
                        contentDescription = null,
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth().padding(start = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    modifier = Modifier.weight(1f),
                    text = backgroundName,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (showOptions) {
                    IconButton(
                        onClick = { isOptionsPopupVisible = true }
                    ) {
                        Icon(
                            painter = rememberVectorPainter(Icons.Filled.MoreVert),
                            contentDescription = stringResource(R.string.options),
                        )

                        DropdownMenu(
                            expanded = isOptionsPopupVisible,
                            onDismissRequest = { isOptionsPopupVisible = false },
                        ) {
                            DropdownMenuItem(
                                text = { Text(stringResource(R.string.preview)) },
                                onClick = {
                                    isOptionsPopupVisible = false
                                    onPreviewClick()
                                },
                            )
                            if (showDeleteOption) {
                                DropdownMenuItem(
                                    text = { Text(stringResource(R.string.delete)) },
                                    onClick = {
                                        isOptionsPopupVisible = false
                                        onDeleteClick()
                                    },
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalSharedTransitionApi::class)
@MelonPreviewSet
@Composable
private fun BackgroundItemPreview() {
    MelonTheme {
        BackgroundItemBase(
            backgroundId = "",
            backgroundName = "Background",
            backgroundImage = rememberAsyncImagePainter(Uri.EMPTY),
            isSelected = false,
            showOptions = true,
            showDeleteOption = true,
            sharedTransitionScope = null,
            animatedContentScope = null,
            onClick = { },
            onPreviewClick = { },
            onDeleteClick = { },
        )
    }
}

@OptIn(ExperimentalSharedTransitionApi::class)
@MelonPreviewSet
@Composable
private fun BackgroundItemSelectedPreview() {
    MelonTheme {
        BackgroundItemBase(
            backgroundId = "",
            backgroundName = "Background",
            backgroundImage = rememberAsyncImagePainter(Uri.EMPTY),
            isSelected = true,
            showOptions = true,
            showDeleteOption = true,
            sharedTransitionScope = null,
            animatedContentScope = null,
            onClick = { },
            onPreviewClick = { },
            onDeleteClick = { },
        )
    }
}