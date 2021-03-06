#include "customizethemedialog.h"

#include <QDesktopWidget>
#include <QFileDialog>
#include <QFontComboBox>
#include <QScrollBar>
#include <QStandardPaths>

#include <QGraphicsOpacityEffect>

#include "mainwindow.h"
#include "starrating.h"

#include <scrollbar.h>
#include <settings.h>
#include <settingsprivate.h>

CustomizeThemeDialog::CustomizeThemeDialog(QWidget *parent)
	: QDialog(parent)
	, _targetedColor(nullptr)
	, _animation(new QPropertyAnimation(this, "windowOpacity"))
	, _timer(new QTimer(this))
{
	setupUi(this);
	for (QSpinBox *spinBox : this->findChildren<QSpinBox*>()) {
		spinBox->setAttribute(Qt::WA_MacShowFocusRect, false);
	}
	listWidget->verticalScrollBar()->deleteLater();
	listWidget->setVerticalScrollBar(new ScrollBar(Qt::Vertical, this));
	listWidget->setAttribute(Qt::WA_MacShowFocusRect, false);

	this->setWindowFlags(Qt::Tool);
	this->setAttribute(Qt::WA_DeleteOnClose);

	buttonsListBox->setVisible(false);
	spinBoxLibrary->setMouseTracking(true);

	// Animates this Dialog
	_timer->setInterval(3000);
	_timer->setSingleShot(true);
	_animation->setDuration(200);
	_animation->setTargetObject(this);

	// Custom colors in the whole application
	auto settingsPrivate = SettingsPrivate::instance();
	bool customColors = settingsPrivate->isCustomColors();
	if (customColors) {
		enableCustomColorsRadioButton->setChecked(true);
	}

	// Override text color or not which is usually computed automatically
	if (settingsPrivate->isCustomTextColorOverriden()) {
		enableCustomTextColorsRadioButton->setChecked(true);
	}

	this->setupActions();

	SettingsPrivate *settings = SettingsPrivate::instance();
	this->restoreGeometry(settings->value("customizeThemeDialogGeometry").toByteArray());
	listWidget->setCurrentRow(settings->value("customizeThemeDialogCurrentTab").toInt());

	this->loadTheme();
}

void CustomizeThemeDialog::animate(qreal startValue, qreal stopValue)
{
	_animation->setStartValue(startValue);
	_animation->setEndValue(stopValue);
	_animation->start();
}

void CustomizeThemeDialog::fade()
{
	if (this->isVisible()) {
		if (!_timer->isActive()) {
			this->animate(1.0, 0.5);
		}
		_timer->start();
	}
}

/** Load theme at startup. */
void CustomizeThemeDialog::loadTheme()
{
	SettingsPrivate *settingsPrivate = SettingsPrivate::instance();
	customizeThemeCheckBox->setChecked(settingsPrivate->isButtonThemeCustomized());

	Settings *settings = Settings::instance();
	sizeButtonsSpinBox->setValue(settings->buttonsSize());

	// Select the right drop-down item according to the theme
	int i = 0;
	while (settings->theme() != themeComboBox->itemText(i).toLower()) {
		i++;
	}
	themeComboBox->setCurrentIndex(i);

	// Buttons
	QList<QPushButton*> mediaPlayerButtons = buttonsListBox->findChildren<QPushButton*>();
	for (QPushButton *mediaPlayerButton : mediaPlayerButtons) {
		QString mediaButton = mediaPlayerButton->objectName() + "Button";
		QCheckBox *checkbox = this->findChild<QCheckBox*>(mediaPlayerButton->objectName() + "CheckBox");
		if (checkbox) {
			checkbox->setChecked(settings->isMediaButtonVisible(mediaButton));
		}
		// Replace icon of the button in this dialog too
		if (settingsPrivate->hasCustomIcon(mediaButton)) {
			QString customIcon = settingsPrivate->customIcon(mediaButton);
			mediaPlayerButton->setIcon(QIcon(customIcon));
		}

	}

	// Extended Search Area
	settingsPrivate->isExtendedSearchVisible() ? radioButtonShowExtendedSearch->setChecked(true) : radioButtonHideExtendedSearch->setChecked(true);

	// Volume bar
	radioButtonShowVolume->setChecked(settings->isVolumeBarTextAlwaysVisible());
	spinBoxHideVolumeLabel->setValue(settingsPrivate->volumeBarHideAfter());

	// Fonts
	fontComboBoxPlaylist->setCurrentFont(settingsPrivate->font(SettingsPrivate::FF_Playlist));
	fontComboBoxLibrary->setCurrentFont(settingsPrivate->font(SettingsPrivate::FF_Library));
	fontComboBoxMenus->setCurrentFont(settingsPrivate->font(SettingsPrivate::FF_Menu));
	spinBoxPlaylist->setValue(settingsPrivate->fontSize(SettingsPrivate::FF_Playlist));
	spinBoxLibrary->blockSignals(true);
	spinBoxLibrary->setValue(settingsPrivate->fontSize(SettingsPrivate::FF_Library));
	spinBoxLibrary->blockSignals(false);
	spinBoxMenus->setValue(settingsPrivate->fontSize(SettingsPrivate::FF_Menu));

	// Colors
	// Alternate background colors in playlists
	if (settingsPrivate->colorsAlternateBG()) {
		enableAlternateBGRadioButton->setChecked(true);
	} else {
		disableAlternateBGRadioButton->setChecked(true);
	}

	bool customColors = settingsPrivate->isCustomColors();
	this->toggleCustomColors(customColors);
	// Custom text color can be enabled only if custom colors are enabled first!
	this->toggleCustomTextColors(customColors && settingsPrivate->isCustomTextColorOverriden());

	// Covers
	settings->isCoverBelowTracksEnabled() ? radioButtonEnableBigCover->setChecked(true) : radioButtonDisableBigCover->setChecked(true);
	spinBoxBigCoverOpacity->setValue(settings->coverBelowTracksOpacity() * 100);

	// Tabs
	radioButtonTabsRect->setChecked(settingsPrivate->isRectTabs());
	overlapTabsSpinBox->setValue(settingsPrivate->tabsOverlappingLength());

	// Articles
	radioButtonEnableArticles->blockSignals(true);
	bool isFiltered = settingsPrivate->isLibraryFilteredByArticles();
	radioButtonEnableArticles->setChecked(isFiltered);
	std::list<QWidget*> enabled = { articlesLineEdit, labelReorderArtistsArticle, labelReorderArtistsArticleExample, radioButtonEnableReorderArtistsArticle, radioButtonDisableReorderArtistsArticle };
	for (QWidget *w : enabled) {
		w->setEnabled(isFiltered);
	}
	radioButtonEnableReorderArtistsArticle->setChecked(settingsPrivate->isReorderArtistsArticle());
	radioButtonEnableArticles->blockSignals(false);

	// Star delegate
	if (settings->libraryHasStars()) {
		radioButtonEnableStarDelegate->setChecked(true);
	} else {
		radioButtonDisableStarDelegate->setChecked(true);
	}
	if (settings->isShowNeverScored()) {
		radioButtonShowNeverScoredTracks->setChecked(true);
	} else {
		radioButtonHideNeverScoredTracks->setChecked(true);
	}
}

void CustomizeThemeDialog::setupActions()
{
	SettingsPrivate *settingsPrivate = SettingsPrivate::instance();

	// Select button theme and size
	connect(themeComboBox, static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentIndexChanged), this, &CustomizeThemeDialog::setThemeNameAndDialogButtons);
	connect(customizeThemeCheckBox, &QCheckBox::toggled, this, [=](bool b) {
		settingsPrivate->setButtonThemeCustomized(b);
		if (!b) {
			// Restore all buttons when unchecked
			for (QCheckBox *button : customizeButtonsScrollArea->findChildren<QCheckBox*>()) {
				if (!button->isChecked()) {
					button->toggle();
				}
			}
			for (QPushButton *pushButton : customizeButtonsScrollArea->findChildren<QPushButton*>()) {
				settingsPrivate->setCustomIcon(pushButton->objectName() + "Button", "");
			}
		}
	});
	Settings *settings = Settings::instance();
	connect(sizeButtonsSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), settings, &Settings::setButtonsSize);

	// Hide buttons or not
	QList<QCheckBox*> mediaPlayerButtons = buttonsListBox->findChildren<QCheckBox*>();
	for (QCheckBox *mediaPlayerButton : mediaPlayerButtons) {
		connect(mediaPlayerButton, &QCheckBox::toggled, this, [=](bool value) {
			settings->setMediaButtonVisible(mediaPlayerButton->objectName().replace("CheckBox", "Button"), value);
		});
	}

	// Connect a file dialog to every button if one wants to customize everything
	for (QPushButton *pushButton : customizeButtonsScrollArea->findChildren<QPushButton*>()) {
		connect(pushButton, &QPushButton::clicked, this, &CustomizeThemeDialog::openChooseIconDialog);
	}

	// Extended Search Area
	connect(radioButtonShowExtendedSearch, &QRadioButton::toggled, settingsPrivate, &SettingsPrivate::setExtendedSearchVisible);

	// Volume bar
	connect(radioButtonShowVolume, &QRadioButton::toggled, settings, &Settings::setVolumeBarTextAlwaysVisible);
	connect(spinBoxHideVolumeLabel, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), settingsPrivate, &SettingsPrivate::setVolumeBarHideAfter);

	// Fonts
	connect(fontComboBoxPlaylist, &QFontComboBox::currentFontChanged, [=](const QFont &font) {
		settingsPrivate->setFont(SettingsPrivate::FF_Playlist, font);
		this->fade();
	});
	connect(fontComboBoxLibrary, &QFontComboBox::currentFontChanged, [=](const QFont &font) {
		settingsPrivate->setFont(SettingsPrivate::FF_Library, font);
		this->fade();
	});
	connect(fontComboBoxMenus, &QFontComboBox::currentFontChanged, [=](const QFont &font) {
		settingsPrivate->setFont(SettingsPrivate::FF_Menu, font);
		this->fade();
	});

	// And fonts size
	connect(spinBoxPlaylist, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int i) {
		settingsPrivate->setFontPointSize(SettingsPrivate::FF_Playlist, i);
		this->fade();
	});
	connect(spinBoxLibrary, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int i) {
		settingsPrivate->setFontPointSize(SettingsPrivate::FF_Library, i);
		QFont lowerFont = settingsPrivate->font(SettingsPrivate::FF_Library);
		lowerFont.setPointSize(lowerFont.pointSizeF() * 0.7);
		this->fade();
	});
	connect(spinBoxMenus, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int i) {
		settingsPrivate->setFontPointSize(SettingsPrivate::FF_Menu, i);
		this->fade();
	});

	// Timer
	connect(_timer, &QTimer::timeout, [=]() { this->animate(0.5, 1.0); });

	// Colors
	connect(enableAlternateBGRadioButton, &QRadioButton::toggled, settingsPrivate, &SettingsPrivate::setColorsAlternateBG);
	for (QToolButton *b : groupBoxCustomColors->findChildren<QToolButton*>()) {
		connect(b, &QToolButton::clicked, this, &CustomizeThemeDialog::showColorDialog);
	}
	connect(enableCustomColorsRadioButton, &QCheckBox::toggled, this, [=](bool b) {
		settingsPrivate->setCustomColors(b);
		this->toggleCustomColors(b);
	});
	connect(enableCustomTextColorsRadioButton, &QCheckBox::toggled, this, [=](bool b) {
		settingsPrivate->setCustomTextColorOverride(b);
		this->toggleCustomTextColors(b);
	});

	// Change cover size
	connect(spinBoxCoverSizeLibraryTree, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int cs) {
		settings->setCoverSizeLibraryTree(cs);
		this->fade();
	});

	// Change cover size
	connect(spinBoxCoverSizeUniqueLibrary, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int cs) {
		settings->setCoverSizeLibraryTree(cs);
		this->fade();
	});

	// Change big cover opacity
	connect(radioButtonEnableBigCover, &QRadioButton::toggled, [=](bool b) {
		settings->setCoverBelowTracksEnabled(b);
		labelBigCoverOpacity->setEnabled(b);
		spinBoxBigCoverOpacity->setEnabled(b);
		this->fade();
	});
	connect(spinBoxBigCoverOpacity, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int v) {
		settings->setCoverBelowTracksOpacity(v);
		this->fade();
	});

	// Filter library
	connect(radioButtonEnableArticles, &QRadioButton::toggled, this, [=](bool b) {
		settingsPrivate->setIsLibraryFilteredByArticles(b);
		// Don't reorder the library if one hasn't typed an article yet
		if (!settingsPrivate->libraryFilteredByArticles().isEmpty()) {
			/// FIXME
			//_mainWindow->library->model()->rebuildSeparators();
		}
	});
	connect(articlesLineEdit, &CustomizeThemeTagLineEdit::taglistHasChanged, this, [=](const QStringList &articles) {
		settingsPrivate->setLibraryFilteredByArticles(articles);
		/// FIXME
		//_mainWindow->library->model()->rebuildSeparators();
	});
	connect(radioButtonEnableReorderArtistsArticle, &QRadioButton::toggled, this, [=](bool b) {
		settingsPrivate->setReorderArtistsArticle(b);
		this->fade();
		/// FIXME
		//_mainWindow->library->viewport()->repaint();
	});

	// Tabs
	connect(radioButtonTabsRect, &QRadioButton::toggled, [=](bool b) {
		settingsPrivate->setTabsRect(b);
		this->fade();
		/// FIXME
		//_mainWindow->tabPlaylists->tabBar()->update();
		//_mainWindow->tabPlaylists->cornerWidget(Qt::TopRightCorner)->update();
	});
	connect(overlapTabsSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int v) {
		settingsPrivate->setTabsOverlappingLength(v);
		this->fade();
		/// FIXME
		//_mainWindow->tabPlaylists->tabBar()->update();
	});

	// Star delegates
	connect(radioButtonEnableStarDelegate, &QRadioButton::toggled, this, [=](bool b) {
		settings->setStarsInLibrary(b);
		labelLibraryDelegates->setEnabled(b);
		radioButtonShowNeverScoredTracks->setEnabled(b);
		radioButtonHideNeverScoredTracks->setEnabled(b);
	});

	connect(radioButtonShowNeverScoredTracks, &QRadioButton::toggled, settings, &Settings::setShowNeverScored);
}

void CustomizeThemeDialog::toggleCustomColorsGridLayout(QGridLayout *gridLayout, bool enabled)
{
	for (int i = 0; i < gridLayout->rowCount(); i++) {
		for (int j = 0; j < gridLayout->columnCount(); j++) {
			QLayoutItem *item = gridLayout->itemAtPosition(i, j);
			if (item && item->widget()) {
				item->widget()->setEnabled(enabled);
			}
		}
	}
}

void CustomizeThemeDialog::toggleCustomColorsReflector(Reflector *one, Reflector *two, bool enabled)
{
	SettingsPrivate *settings = SettingsPrivate::instance();
	QPalette palette = settings->customPalette();
	QColor colorOne = palette.color(one->colorRole());
	QColor colorTwo = palette.color(two->colorRole());
	if (enabled) {
		one->setColor(colorOne);
		two->setColor(colorTwo);
		QApplication::setPalette(settings->customPalette());
	} else {
		int gray = qGray(colorOne.rgb());
		one->setColor(QColor(gray, gray, gray));
		gray = qGray(colorTwo.rgb());
		two->setColor(QColor(gray, gray, gray));
	}
}

/** Automatically centers the parent window when closing this dialog. */
void CustomizeThemeDialog::closeEvent(QCloseEvent *e)
{
	SettingsPrivate *settings = SettingsPrivate::instance();
	settings->setValue("customizeThemeDialogGeometry", saveGeometry());
	settings->setValue("customizeThemeDialogCurrentTab", listWidget->currentRow());

	QDialog::closeEvent(e);
}

void CustomizeThemeDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);

	/// XXX: why should I show the dialog before adding tags to have the exact and right size?
	/// Is it impossible to compute real size even if dialog is hidden?
	// Add grammatical articles
	for (QString article : SettingsPrivate::instance()->libraryFilteredByArticles()) {
		articlesLineEdit->addTag(article);
	}
	this->activateWindow();
}

/** Redefined to initialize favorites from settings. */
int CustomizeThemeDialog::exec()
{
	// Change the label that talks about star delegates
	bool starDelegateState = Settings::instance()->libraryHasStars();
	labelLibraryDelegates->setEnabled(starDelegateState);
	radioButtonShowNeverScoredTracks->setEnabled(starDelegateState);
	radioButtonHideNeverScoredTracks->setEnabled(starDelegateState);

	SettingsPrivate *settings = SettingsPrivate::instance();
	if (settings->value("customizeThemeDialogGeometry").isNull()) {
		int w = qApp->desktop()->screenGeometry().width() / 2;
		int h = qApp->desktop()->screenGeometry().height() / 2;
		this->move(w - frameGeometry().width() / 2, h - frameGeometry().height() / 2);
	}
	return QDialog::exec();
}

void CustomizeThemeDialog::openChooseIconDialog()
{
	QPushButton *button = qobject_cast<QPushButton *>(sender());
	SettingsPrivate *settings = SettingsPrivate::instance();

	// It's always more convenient when the dialog re-open at the same location
	QString openPath;
	QVariant variantOpenPath = settings->value("customIcons/lastOpenPath");
	if (variantOpenPath.isValid()) {
		openPath = variantOpenPath.toString();
	} else {
		openPath = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation).first();
	}

	QString path = QFileDialog::getOpenFileName(this, tr("Choose your custom icon"), openPath, tr("Pictures (*.jpg *.jpeg *.png)"));

	// Reset custom icon if path is empty (delete key in settings too)
	settings->setCustomIcon(button->objectName() + "Button", path);

	if (path.isEmpty()) {
		button->setIcon(QIcon(":/player/" + Settings::instance()->theme() + "/" + button->objectName()));
	} else {
		settings->setValue("customIcons/lastOpenPath", QFileInfo(path).absolutePath());
		button->setIcon(QIcon(path));
	}
}

/** Changes the current theme and updates this dialog too. */
void CustomizeThemeDialog::setThemeNameAndDialogButtons(QString newTheme)
{
	SettingsPrivate *settings = SettingsPrivate::instance();
	// Check for each button if there is a custom icon
	for (QPushButton *button : customizeButtonsScrollArea->findChildren<QPushButton*>()) {
		if (button) {
			// Keep the custom icon provided by one
			if (settings->hasCustomIcon(button->objectName())) {
				button->setIcon(QIcon(settings->customIcon(button->objectName())));
			} else {
				QIcon i(":/player/" + newTheme.replace(" ", "").toLower() + "/" + button->objectName());
				if (!i.isNull()) {
					button->setIcon(i);
				}
			}
		}
	}
	Settings::instance()->setThemeName(newTheme);
}

/** Shows a color dialog and hides this dialog temporarily.
 * Also, reorder the mainWindow and the color dialog to avoid overlapping, if possible. */
void CustomizeThemeDialog::showColorDialog()
{
	_targetedColor = findChild<Reflector*>(sender()->objectName().replace("ToolButton", "Widget"));
	if (_targetedColor) {
		QPalette::ColorRole cr = _targetedColor->colorRole();
		_targetedColor->setColor(QApplication::palette().color(cr));

		this->setAttribute(Qt::WA_DeleteOnClose, false);
		ColorDialog *colorDialog = new ColorDialog(this);
		colorDialog->setCurrentColor(_targetedColor->color());
		this->hide();
		int i = colorDialog->exec();
		if (i >= 0) {
			// Automatically adjusts Reflector Widgets for Text colors if one hasn't check the option
			if (!SettingsPrivate::instance()->isCustomTextColorOverriden()) {
				QPalette palette = QApplication::palette();
				if (cr == QPalette::Base) {
					fontColorWidget->setColor(palette.color(QPalette::Text));
				} else if (cr == QPalette::Highlight) {
					selectedFontColorWidget->setColor(palette.color(QPalette::HighlightedText));
				}
			}
			this->show();
			this->setAttribute(Qt::WA_DeleteOnClose);
		}
	}
}

void CustomizeThemeDialog::toggleCustomColors(bool enabled)
{
	this->toggleCustomColorsGridLayout(customColorsGridLayout, enabled);

	labelOverrideTextColor->setEnabled(enabled);
	enableCustomTextColorsRadioButton->setEnabled(enabled);
	disableCustomTextColorsRadioButton->setEnabled(enabled);

	this->toggleCustomColorsReflector(bgPrimaryColorWidget, selectedItemColorWidget, enabled);
	if (enabled) {
		this->toggleCustomTextColors(SettingsPrivate::instance()->isCustomTextColorOverriden());
	} else {
		this->toggleCustomTextColors(false);
	}
}

void CustomizeThemeDialog::toggleCustomTextColors(bool enabled)
{
	this->toggleCustomColorsGridLayout(customTextColorsGridLayout, enabled);
	this->toggleCustomColorsReflector(fontColorWidget, selectedFontColorWidget, enabled);
}
