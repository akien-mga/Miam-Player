#include "mainwindow.h"

#include "dialogs/customizethemedialog.h"
#include "playlists/playlist.h"
#include "pluginmanager.h"
#include "settings.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtMultimedia/QAudioDeviceInfo>

#include <QtDebug>

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent), _db(new SqlDatabase(this))
{
	setupUi(this);

	this->setAcceptDrops(true);
	//this->setAttribute(Qt::WA_DeleteOnClose, true);
	this->setWindowIcon(QIcon(":/icons/mp_win32"));

	// Special behaviour for media buttons
	mediaButtons << skipBackwardButton << seekBackwardButton << playButton << stopButton
				 << seekForwardButton << skipForwardButton << playbackModeButton;

	// Init the audio module
	_mediaPlayer = QSharedPointer<MediaPlayer>(new MediaPlayer(this));
	tabPlaylists->setMainWindow(this);
	tabPlaylists->setMediaPlayer(_mediaPlayer);
	seekSlider->setMediaPlayer(_mediaPlayer);

	/// XXX
	_uniqueLibrary = new UniqueLibrary(this);
	stackedWidget->addWidget(_uniqueLibrary);
	_uniqueLibrary->hide();

	// Instantiate dialogs
	customizeThemeDialog = new CustomizeThemeDialog(this);
	customizeOptionsDialog = new CustomizeOptionsDialog(this);

	/// free memory
	playlistManager = new PlaylistManager(_db, tabPlaylists);
	dragDropDialog = new DragDropDialog(this);
	playbackModeWidgetFactory = new PlaybackModeWidgetFactory(this, playbackModeButton, tabPlaylists);
	_searchDialog = new SearchDialog(_db, this);
}

void MainWindow::dispatchDrop(QDropEvent *event)
{
	bool onlyFiles = dragDropDialog->setMimeData(event->mimeData());
	if (onlyFiles) {
		QList<TrackDAO> tracks;
		foreach (QString file, dragDropDialog->externalLocations()) {
			TrackDAO track;
			track.setUri(file);
			tracks.append(track);
		}
		tabPlaylists->insertItemsToPlaylist(-1, tracks);
	} else {
		QList<QDir> dirs;
		foreach (QString location, dragDropDialog->externalLocations()) {
			dirs << location;
		}
		switch (SettingsPrivate::getInstance()->dragDropAction()) {
		case SettingsPrivate::DD_OpenPopup:
			dragDropDialog->show();
			break;
		case SettingsPrivate::DD_AddToLibrary:
			customizeOptionsDialog->addMusicLocations(dirs);
			break;
		case SettingsPrivate::DD_AddToPlaylist:
			tabPlaylists->addExtFolders(dirs);
			break;
		}
	}
}

void MainWindow::init()
{
	// Link database and views
	library->init(_db);
	_uniqueLibrary->init(_db);
	tagEditor->init(_db);

	// Load playlists at startup if any, otherwise just add an empty one
	this->setupActions();

	bool isEmpty = SettingsPrivate::getInstance()->musicLocations().isEmpty();
	quickStart->setVisible(isEmpty);
	libraryHeader->setVisible(!isEmpty);
	/// XXX For each view
	library->setHidden(isEmpty);
	/// XXX
	actionScanLibrary->setDisabled(isEmpty);
	widgetSearchBar->setHidden(isEmpty);
	this->showTabPlaylists();
	if (isEmpty) {
		quickStart->searchMultimediaFiles();
	} else {
		_db->load();
	}

	Settings *settings = Settings::getInstance();
	this->restoreGeometry(settings->value("mainWindowGeometry").toByteArray());
	//splitter->restoreState(settings->value("splitterState").toByteArray());
	leftTabs->setCurrentIndex(settings->value("leftTabsIndex").toInt());

	playlistManager->init();

	// Load shortcuts
	customizeOptionsDialog->initShortcuts();
}

/** Plugins. */
void MainWindow::loadPlugins()
{
	PluginManager *pm = PluginManager::getInstance();
	pm->setMainWindow(this);
	int row = Settings::getInstance()->value("customizeOptionsDialogCurrentTab", 0).toInt();
	if (customizeOptionsDialog->listWidget->isRowHidden(5) && row == 5) {
		customizeOptionsDialog->listWidget->setCurrentRow(0);
	} else {
		customizeOptionsDialog->listWidget->setCurrentRow(row);
	}
}

void MainWindow::moveSearchDialog()
{
	QPoint globalMW = this->mapToGlobal(QPoint(0, 0));
	QPoint globalSB = searchBar->mapToGlobal(searchBar->rect().topRight());
	_searchDialog->move(globalSB - globalMW);
	_searchDialog->setVisible(true);
}

/** Set up all actions and behaviour. */
void MainWindow::setupActions()
{
	// Load music
	connect(customizeOptionsDialog, &CustomizeOptionsDialog::musicLocationsHaveChanged, [=](bool libraryIsEmpty) {
		quickStart->setVisible(libraryIsEmpty);
		library->setVisible(!libraryIsEmpty);
		libraryHeader->setVisible(!libraryIsEmpty);
		actionScanLibrary->setDisabled(libraryIsEmpty);
		widgetSearchBar->setVisible(!libraryIsEmpty);
		if (libraryIsEmpty) {
			// Delete table tracks if such a previous one was found
			if (_db->open()) {
				_db->exec("DROP TABLE tracks");
				qDebug() << Q_FUNC_INFO;
				_db->close();
			}
			quickStart->searchMultimediaFiles();
		} else {
			_db->rebuild();
		}
	});

	connect(_db, &SqlDatabase::aboutToLoad, libraryHeader, &LibraryHeader::resetSortOrder);

	// Adds a group where view mode are mutually exclusive
	QActionGroup *viewModeGroup = new QActionGroup(this);
	actionViewPlaylists->setActionGroup(viewModeGroup);
	actionViewUniqueLibrary->setActionGroup(viewModeGroup);
	actionViewTagEditor->setActionGroup(viewModeGroup);

	connect(actionViewPlaylists, &QAction::triggered, this, [=]() {
		stackedWidget->setCurrentIndex(0);
		stackedWidgetRight->setCurrentIndex(0);
	});
	connect(actionViewUniqueLibrary, &QAction::triggered, this, [=]() {
		stackedWidget->setCurrentIndex(1);
	});
	connect(actionViewTagEditor, &QAction::triggered, this, [=]() {
		stackedWidget->setCurrentIndex(0);
		stackedWidgetRight->setCurrentIndex(1);
		actionViewTagEditor->setChecked(true);
	});

	QActionGroup *actionPlaybackGroup = new QActionGroup(this);
	foreach(QAction *actionPlayBack, findChildren<QAction*>(QRegExp("actionPlayback*", Qt::CaseSensitive, QRegExp::Wildcard))) {
		actionPlaybackGroup->addAction(actionPlayBack);
		connect(actionPlayBack, &QAction::triggered, this, [=]() {
			const QMetaObject &mo = QMediaPlaylist::staticMetaObject;
			QMetaEnum metaEnum = mo.enumerator(mo.indexOfEnumerator("PlaybackMode"));
			QString enu = actionPlayBack->property("PlaybackMode").toString();
			playbackModeWidgetFactory->setPlaybackMode((QMediaPlaylist::PlaybackMode)metaEnum.keyToValue(enu.toStdString().data()));
		});
	}

	// Link user interface
	// Actions from the menu
	connect(actionOpenFiles, &QAction::triggered, this, &MainWindow::openFiles);
	connect(actionOpenFolder, &QAction::triggered, this, &MainWindow::openFolder);
	connect(actionExit, &QAction::triggered, &QApplication::quit);
	connect(actionAddPlaylist, &QAction::triggered, tabPlaylists, &TabPlaylist::addPlaylist);
	connect(actionDeleteCurrentPlaylist, &QAction::triggered, tabPlaylists, &TabPlaylist::removeCurrentPlaylist);
	connect(actionShowCustomize, &QAction::triggered, customizeThemeDialog, &CustomizeThemeDialog::open);
	connect(actionShowOptions, &QAction::triggered, customizeOptionsDialog, &CustomizeOptionsDialog::open);
	connect(actionAboutQt, &QAction::triggered, &QApplication::aboutQt);
	connect(actionScanLibrary, &QAction::triggered, this, [=]() {
		searchBar->clear();
		_db->rebuild();
	});
	connect(actionShowHelp, &QAction::triggered, this, [=]() {
		QDesktopServices::openUrl(QUrl("http://miam-player.org/wiki/index.php"));
	});

	// Quick Start
	connect(quickStart->commandLinkButtonLibrary, &QAbstractButton::clicked, customizeOptionsDialog, &CustomizeOptionsDialog::open);

	// Select only folders that are checked by one
	connect(quickStart->quickStartApplyButton, &QDialogButtonBox::clicked, [=] (QAbstractButton *) {
		QStringList newLocations;
		for (int i = 1; i < quickStart->quickStartTableWidget->rowCount(); i++) {
			if (quickStart->quickStartTableWidget->item(i, 0)->checkState() == Qt::Checked) {
				QString musicLocation = quickStart->quickStartTableWidget->item(i, 1)->data(Qt::UserRole).toString();
				musicLocation = QDir::toNativeSeparators(musicLocation);
				customizeOptionsDialog->addMusicLocation(musicLocation);
				newLocations.append(musicLocation);
			}
		}
		SettingsPrivate::getInstance()->setMusicLocations(newLocations);
		quickStart->hide();
		library->setHidden(false);
		libraryHeader->setHidden(false);
		widgetSearchBar->setHidden(false);
		actionScanLibrary->setEnabled(true);
		_db->rebuild();
	});

	foreach (TreeView *tab, this->findChildren<TreeView*>()) {
		connect(tab, &TreeView::aboutToInsertToPlaylist, tabPlaylists, &TabPlaylist::insertItemsToPlaylist);
		connect(tab, &TreeView::sendToTagEditor, this, [=](const QModelIndexList , const QList<TrackDAO> &tracks) {
			this->showTagEditor();
			tagEditor->addItemsToEditor(tracks);
		});
	}

	// Send one folder to the music locations
	connect(filesystem, &FileSystemTreeView::aboutToAddMusicLocations, customizeOptionsDialog, &CustomizeOptionsDialog::addMusicLocations);

	// Send music to the tag editor
	connect(tagEditor, &TagEditor::aboutToCloseTagEditor, this, &MainWindow::showTabPlaylists);
	connect(tabPlaylists, &TabPlaylist::aboutToSendToTagEditor, [=](const QList<QUrl> &tracks) {
		this->showTagEditor();
		tagEditor->addItemsToEditor(tracks);
	});

	// Media buttons and their shortcuts
	/// XXX: can this be factorized with meta object system?
	connect(actionSkipBackward, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::skipBackward);
	connect(skipBackwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::skipBackward);
	connect(actionSeekBackward, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::seekBackward);
	connect(seekBackwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::seekBackward);
	connect(actionPlay, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::play);
	connect(playButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::play);
	connect(actionStop, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::stop);
	connect(stopButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::stop);
	connect(actionSeekForward, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::seekForward);
	connect(seekForwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::seekForward);
	connect(actionSkipForward, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::skipForward);
	connect(skipForwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::skipForward);
	connect(playbackModeButton, &MediaButton::mediaButtonChanged, playbackModeWidgetFactory, &PlaybackModeWidgetFactory::update);

	// Sliders
	connect(_mediaPlayer.data(), &MediaPlayer::positionChanged, [=] (qint64 pos, qint64 duration) {
		if (duration > 0) {
			seekSlider->setValue(1000 * pos / duration);
			timeLabel->setTime(pos, duration);
		}
	});

	// Volume bar
	connect(volumeSlider, &QSlider::valueChanged, _mediaPlayer.data(), &MediaPlayer::setVolume);
	volumeSlider->setValue(Settings::getInstance()->volume());

	// Filter the library when user is typing some text to find artist, album or tracks
	SettingsPrivate *settings = SettingsPrivate::getInstance();
	connect(searchBar, &QLineEdit::textEdited, library, &LibraryTreeView::filterLibrary);
	connect(searchBar, &QLineEdit::textEdited, this, [=](const QString &text) {
		if (settings->isExtendedSearchVisible()) {
			if (text.isEmpty()) {
				_searchDialog->clear();
			} else {
				_searchDialog->setSearchExpression(text);
				this->moveSearchDialog();
				searchBar->setFocus();
			}
		}
	});
	connect(searchBar, &LibraryFilterLineEdit::focusIn, this, [=] () {
		if (!_searchDialog->isVisible() && settings->isExtendedSearchVisible()) {
			this->moveSearchDialog();
		}
	});

	// Core
	connect(_mediaPlayer.data(), &MediaPlayer::stateChanged, this, &MainWindow::mediaPlayerStateHasChanged);

	// Playback
	connect(tabPlaylists, &TabPlaylist::updatePlaybackModeButton, playbackModeWidgetFactory, &PlaybackModeWidgetFactory::update);
	connect(actionRemoveSelectedTracks, &QAction::triggered, tabPlaylists, &TabPlaylist::removeSelectedTracks);
	connect(actionMoveTracksUp, &QAction::triggered, tabPlaylists, &TabPlaylist::moveTracksUp);
	connect(actionMoveTracksDown, &QAction::triggered, tabPlaylists, &TabPlaylist::moveTracksDown);
	connect(actionOpenPlaylistManager, &QAction::triggered, playlistManager, &PlaylistManager::open);
	connect(actionMute, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::toggleMute);
	connect(actionIncreaseVolume, &QAction::triggered, this, [=]() {
		volumeSlider->setValue(volumeSlider->value() + 5);
	});
	connect(actionDecreaseVolume, &QAction::triggered, this, [=]() {
		volumeSlider->setValue(volumeSlider->value() - 5);
	});

	connect(filesystem, &FileSystemTreeView::folderChanged, addressBar, &AddressBar::init);
	connect(addressBar, &AddressBar::aboutToChangePath, filesystem, &FileSystemTreeView::reloadWithNewPath);

	// Drag & Drop actions
	connect(dragDropDialog, &DragDropDialog::aboutToAddExtFoldersToLibrary, customizeOptionsDialog, &CustomizeOptionsDialog::addMusicLocations);
	connect(dragDropDialog, &DragDropDialog::aboutToAddExtFoldersToPlaylist, tabPlaylists, &TabPlaylist::addExtFolders);

	// Playback modes
	connect(playbackModeButton, &QPushButton::clicked, playbackModeWidgetFactory, &PlaybackModeWidgetFactory::togglePlaybackModes);

	connect(menuPlayback, &QMenu::aboutToShow, this, [=](){
		QMediaPlaylist::PlaybackMode mode = tabPlaylists->currentPlayList()->mediaPlaylist()->playbackMode();
		const QMetaObject &mo = QMediaPlaylist::staticMetaObject;
		QMetaEnum metaEnum = mo.enumerator(mo.indexOfEnumerator("PlaybackMode"));
		QAction *action = findChild<QAction*>(QString("actionPlayback").append(metaEnum.valueToKey(mode)));
		action->setChecked(true);
	});

	// Lambda function to reduce duplicate code
	auto updateActions = [this] (bool b) {
		actionRemoveSelectedTracks->setEnabled(b);
		actionMoveTracksUp->setEnabled(b);
		actionMoveTracksDown->setEnabled(b);
	};

	connect(menuPlaylist, &QMenu::aboutToShow, this, [=]() {
		bool b = tabPlaylists->currentPlayList()->selectionModel()->hasSelection();
		updateActions(b);
		if (b) {
			int selectedRows = tabPlaylists->currentPlayList()->selectionModel()->selectedRows().count();
			actionRemoveSelectedTracks->setText(tr("&Remove selected tracks", "Number of tracks to remove", selectedRows));
			actionMoveTracksUp->setText(tr("Move selected tracks &up", "Move upward", selectedRows));
			actionMoveTracksDown->setText(tr("Move selected tracks &down", "Move downward", selectedRows));
		}
	});
	connect(tabPlaylists, &TabPlaylist::selectionChanged, this, [=](bool isEmpty) {
		updateActions(!isEmpty);
	});

	connect(libraryHeader, &LibraryHeader::aboutToChangeSortOrder, library, &LibraryTreeView::changeSortOrder);
	connect(libraryHeader, &LibraryHeader::aboutToChangeHierarchyOrder, library, &LibraryTreeView::changeHierarchyOrder);

	connect(qApp, &QApplication::aboutToQuit, this, [=] {
		delete PluginManager::getInstance();
	});

	// Shortcuts
	connect(customizeOptionsDialog, &CustomizeOptionsDialog::aboutToBindShortcut, this, &MainWindow::bindShortcut);

	// Splitter
	connect(splitter, &QSplitter::splitterMoved, this, [=]() {
		if (_searchDialog->isVisible()) {
			this->moveSearchDialog();
		}
	});
}

/** Update fonts for menu and context menus. */
void MainWindow::updateFonts(const QFont &font)
{
	menuBar()->setFont(font);
	foreach (QAction *action, findChildren<QAction*>()) {
		action->setFont(font);
	}
}

QMessageBox::StandardButton MainWindow::showWarning(const QString &target, int count)
{
	QMessageBox::StandardButton ret = QMessageBox::Ok;
	/// XXX: extract magic number (to where?)
	if (count > 300) {
		QMessageBox msgBox;
		QString totalFiles = tr("There are more than 300 files to add to the %1 (%2 to add).");
		msgBox.setText(totalFiles.arg(target).arg(count));
		msgBox.setInformativeText(tr("Are you sure you want to continue? This might take some time."));
		msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
		msgBox.setDefaultButton(QMessageBox::Ok);
		ret = (QMessageBox::StandardButton) msgBox.exec();
	}
	return ret;
}

/** Redefined to be able to retransltate User Interface at runtime. */
void MainWindow::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		this->retranslateUi(this);
		customizeOptionsDialog->retranslateUi(customizeOptionsDialog);
		customizeThemeDialog->retranslateUi(customizeThemeDialog);
		quickStart->retranslateUi(quickStart);
		playlistManager->retranslateUi(playlistManager);
		tagEditor->retranslateUi(tagEditor);
		dragDropDialog->retranslateUi(dragDropDialog);

		// (need to be tested with Arabic language)
		if (tr("LTR") == "RTL") {
			QApplication::setLayoutDirection(Qt::RightToLeft);
		}
	} else {
		QMainWindow::changeEvent(event);
	}
}

void MainWindow::closeEvent(QCloseEvent *)
{
	SettingsPrivate *settings = SettingsPrivate::getInstance();
	settings->setValue("mainWindowGeometry", saveGeometry());
	//settings->setValue("splitterState", splitter->saveState());
	settings->setValue("leftTabsIndex", leftTabs->currentIndex());
	Settings::getInstance()->setVolume(volumeSlider->value());
	settings->sync();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
	event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
	// Ignore Drag & Drop if the source is a part of this player
	if (event->source() != NULL) {
		return;
	}
	this->dispatchDrop(event);
}

bool MainWindow::event(QEvent *e)
{
	bool b = QMainWindow::event(e);
	// Init the address bar. It's really important to have the exact width on screen
	if (e->type() == QEvent::Show) {
		if (!filesystem->isVisible()) {
			addressBar->setMinimumWidth(leftTabs->width());
		}
		addressBar->init(QDir(QStandardPaths::standardLocations(QStandardPaths::MusicLocation).first()));
		customizeThemeDialog->loadTheme();
	}
	return b;
}

void MainWindow::moveEvent(QMoveEvent *event)
{
	playbackModeWidgetFactory->move();
	QMainWindow::moveEvent(event);
}

void MainWindow::processArgs(const QStringList &args)
{
	// First arg is the location of the application
	if (args.count() > 2) {
		QString command = args.at(1);
		QList<TrackDAO> tracks;

		auto convertArgs = [&args, &tracks] () -> void {
			for (int i = 2; i < args.count(); i++) {
				TrackDAO track;
				track.setUri(args.at(i));
				tracks.append(track);
			}
		};

		if (command == "-f") {			// Append to playlist
			convertArgs();
			tabPlaylists->insertItemsToPlaylist(-1, tracks);
		} else if (command == "-n") {	// New playlist
			convertArgs();
			Playlist *p = tabPlaylists->addPlaylist();
			if (p) {
				tabPlaylists->insertItemsToPlaylist(-1, tracks);
			}
		} else if (command == "-t") {	// Tag Editor
			convertArgs();
			tagEditor->addItemsToEditor(tracks);
			showTagEditor();
		} else if (command == "-l") {	// Library

		}
	}
}

void MainWindow::bindShortcut(const QString &objectName, const QKeySequence &keySequence)
{
	QAction *action = findChild<QAction*>("action" + objectName.left(1).toUpper() + objectName.mid(1));
	// Connect actions first
	if (action) {
		action->setShortcut(keySequence);
		// Some default shortcuts might interfer with other widgets, so we need to restrict where it applies
		if (action == actionIncreaseVolume || action == actionDecreaseVolume) {
			action->setShortcutContext(Qt::WidgetShortcut);
		}
	// Specific actions not defined in main menu
	} else if (objectName == "showTabLibrary" || objectName == "showTabFilesystem") {
		leftTabs->setShortcut(objectName, keySequence);
	} else if (objectName == "sendToCurrentPlaylist") {
		library->sendToCurrentPlaylist->setKey(keySequence);
	} else if (objectName == "sendToTagEditor") {
		library->openTagEditor->setKey(keySequence);
	} else if (objectName == "search") {
		searchBar->shortcut->setKey(keySequence);
	}
}

void MainWindow::mediaPlayerStateHasChanged(QMediaPlayer::State state)
{
	qDebug() << "MW ; MediaPlayer::stateChanged" << state;
	playButton->disconnect();
	actionPlay->disconnect();
	if (state == QMediaPlayer::PlayingState) {
		QString iconPath;
		if (SettingsPrivate::getInstance()->hasCustomIcon("pauseButton")) {
			iconPath = SettingsPrivate::getInstance()->customIcon("pauseButton");
		} else {
			iconPath = ":/player/" + Settings::getInstance()->theme() + "/pause";
		}
		playButton->setIcon(QIcon(iconPath));
		connect(playButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::pause);
		connect(actionPlay, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::pause);
		seekSlider->setEnabled(true);
	} else {
		playButton->setIcon(QIcon(":/player/" + Settings::getInstance()->theme() + "/play"));
		connect(playButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::play);
		connect(actionPlay, &QAction::triggered, _mediaPlayer.data(), &MediaPlayer::play);
		seekSlider->setDisabled(state == QMediaPlayer::StoppedState);
		if (state == QMediaPlayer::StoppedState) {
			seekSlider->setValue(0);
			timeLabel->setTime(0, 0);
		}
	}
	// Remove bold font when player has stopped
	tabPlaylists->currentPlayList()->viewport()->update();
	seekSlider->update();
}

void MainWindow::openFiles()
{
	QString audioFiles = tr("Audio files");
	Settings *settings = Settings::getInstance();
	QString lastOpenedLocation;
	QString defaultMusicLocation = QStandardPaths::standardLocations(QStandardPaths::MusicLocation).first();
	if (settings->value("lastOpenedLocation").toString().isEmpty()) {
		lastOpenedLocation = defaultMusicLocation;
	} else {
		lastOpenedLocation = settings->value("lastOpenedLocation").toString();
	}
	QStringList files = QFileDialog::getOpenFileNames(this, tr("Choose some files to open"), lastOpenedLocation,
													 audioFiles.append(" (" + FileHelper::suffixes(true).join(" ") + ")"));
	if (files.isEmpty()) {
		settings->setValue("lastOpenedLocation", defaultMusicLocation);
	} else {
		QFileInfo fileInfo(files.first());
		settings->setValue("lastOpenedLocation", fileInfo.absolutePath());
		QList<TrackDAO> tracks;
		foreach (QString file, files) {
			TrackDAO track;
			track.setUri(file);
			tracks.append(track);
		}
		tabPlaylists->insertItemsToPlaylist(-1, tracks);
	}
}

void MainWindow::openFolder()
{
	Settings *settings = Settings::getInstance();
	QString lastOpenedLocation;
	QString defaultMusicLocation = QStandardPaths::standardLocations(QStandardPaths::MusicLocation).first();
	if (settings->value("lastOpenedLocation").toString().isEmpty()) {
		lastOpenedLocation = defaultMusicLocation;
	} else {
		lastOpenedLocation = settings->value("lastOpenedLocation").toString();
	}
	QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder to open"), lastOpenedLocation);
	if (dir.isEmpty()) {
		settings->setValue("lastOpenedLocation", defaultMusicLocation);
	} else {
		settings->setValue("lastOpenedLocation", dir);
		QDirIterator it(dir, QDirIterator::Subdirectories);
		QStringList suffixes = FileHelper::suffixes();
		QList<TrackDAO> tracks;
		while (it.hasNext()) {
			it.next();
			if (suffixes.contains(it.fileInfo().suffix())) {
				TrackDAO track;
				track.setUri(it.filePath());
				tracks.append(track);
			}
		}
		if (showWarning(tr("playlist"), tracks.count()) == QMessageBox::Ok) {
			tabPlaylists->insertItemsToPlaylist(-1, tracks);
		}
	}
}

void MainWindow::showTabPlaylists()
{
	if (!actionViewPlaylists->isChecked()) {
		actionViewPlaylists->setChecked(true);
	}
	stackedWidgetRight->setCurrentIndex(0);
}

void MainWindow::showTagEditor()
{
	if (!actionViewTagEditor->isChecked()) {
		actionViewTagEditor->setChecked(true);
	}
	stackedWidgetRight->setCurrentIndex(1);
}
