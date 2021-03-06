#include "mainwindow.h"

#include <mediabuttons/mediabutton.h>
#include <abstractviewplaylists.h>
#include <musicsearchengine.h>
#include <quickstart.h>
#include <settings.h>
#include <settingsprivate.h>

#include "dialogs/customizethemedialog.h"
#include "dialogs/dragdropdialog.h"
#include "dialogs/equalizerdalog.h"
#include "views/viewloader.h"
#include "pluginmanager.h"

#include <QDesktopWidget>
#include <QDesktopServices>
#include <QShortcut>
#include <QWindow>

#include <QtDebug>

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, _mediaPlayer(new MediaPlayer(this))
	, _pluginManager(new PluginManager(this))
	, _remoteControl(nullptr)
	, _currentView(nullptr)
	, _tagEditor(nullptr)
	, _mini(nullptr)
	, _shortcutSkipBackward(new QxtGlobalShortcut(QKeySequence(Qt::Key_MediaPrevious), this))
	, _shortcutStop(new QxtGlobalShortcut(QKeySequence(Qt::Key_MediaStop), this))
	, _shortcutPlayPause(new QxtGlobalShortcut(QKeySequence(Qt::Key_MediaPlay), this))
	, _shortcutSkipForward(new QxtGlobalShortcut(QKeySequence(Qt::Key_MediaNext), this))
{
	setupUi(this);
	actionPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	actionStop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
	QActionGroup *playbackActionGroup = new QActionGroup(this);
	playbackActionGroup->setObjectName("playbackActionGroup");
	playbackActionGroup->addAction(actionPlaybackSequential);
	playbackActionGroup->addAction(actionPlaybackRandom);
	playbackActionGroup->addAction(actionPlaybackLoop);
	playbackActionGroup->addAction(actionPlaybackCurrentItemOnce);
	playbackActionGroup->addAction(actionPlaybackCurrentItemInLoop);

	this->setAcceptDrops(true);
#ifndef Q_OS_MAC
	this->setWindowIcon(QIcon(":/icons/mp_win32"));
#else
	actionHideMenuBar->setVisible(false);
#endif

	// Fonts
	auto settings = SettingsPrivate::instance();
	this->updateFonts(settings->font(SettingsPrivate::FF_Menu));

	menubar->setVisible(!settings->value("isMenuHidden", false).toBool());

	connect(settings, &SettingsPrivate::languageAboutToChange, this, [=](const QString &newLanguage) {
		QApplication::removeTranslator(&_translator);
		_translator.load(":/translations/core_" + newLanguage);
		QApplication::installTranslator(&_translator);
	});

	// Init language
	_translator.load(":/translations/core_" + settings->language());
	QApplication::installTranslator(&_translator);
}

void MainWindow::activateLastView()
{
	bool isEmpty = SettingsPrivate::instance()->musicLocations().isEmpty();
	actionScanLibrary->setDisabled(isEmpty);
	if (isEmpty) {
		initQuickStart();
	} else {
		// Find the last active view and connect database to it
		SettingsPrivate *settingsPrivate = SettingsPrivate::instance();
		QString actionViewName = settingsPrivate->value("lastActiveView", "actionViewPlaylists").toString();
		QAction *action = this->findChild<QAction*>(actionViewName);
		if (action) {
			if (action->isCheckable()) {
				action->setChecked(true);
			}
			this->activateView(action);
		} else {
			// If no action was triggered, despite an entry in settings, it means some plugin was activated once, but now we couldn't find it
			actionViewPlaylists->trigger();
		}
	}
}

void MainWindow::dispatchDrop(QDropEvent *event)
{
	/** Popup shown to one when tracks are dropped from another application to MiamPlayer. */
	DragDropDialog dragDropDialog;

	SettingsPrivate *settings = SettingsPrivate::instance();

	AbstractViewPlaylists *viewPlaylists = nullptr;

	// Drag & Drop actions
	connect(&dragDropDialog, &DragDropDialog::aboutToAddExtFoldersToLibrary, settings, &SettingsPrivate::addMusicLocations);
	if (_currentView && _currentView->viewProperty(Settings::VP_PlaylistFeature)) {
		viewPlaylists = static_cast<AbstractViewPlaylists*>(_currentView);
		connect(&dragDropDialog, &DragDropDialog::aboutToAddExtFoldersToPlaylist, viewPlaylists, &AbstractViewPlaylists::addExtFolders);
	}

	bool onlyFiles = dragDropDialog.setMimeData(event->mimeData());
	if (onlyFiles) {
		QStringList tracks;
		for (QString file : dragDropDialog.externalLocations()) {
			tracks << "file://" + file;
		}
		tracks.sort(Qt::CaseInsensitive);
		QList<QUrl> urls;
		for (QString t : tracks) {
#if defined (Q_OS_WIN)
			urls << QUrl::fromLocalFile(t);
#else
			urls << QUrl(t);
#endif
		}
		if (viewPlaylists) {
			viewPlaylists->addToPlaylist(urls);
			if (!dragDropDialog.playlistLocations().isEmpty()) {
				viewPlaylists->openPlaylists(dragDropDialog.playlistLocations());
			}
		}
	} else {
		QList<QDir> dirs;
		for (QString location : dragDropDialog.externalLocations()) {
			dirs << location;
		}
		switch (SettingsPrivate::instance()->dragDropAction()) {
		case SettingsPrivate::DD_OpenPopup:
			dragDropDialog.exec();
			break;
		case SettingsPrivate::DD_AddToLibrary:
			settings->addMusicLocations(dirs);
			break;
		case SettingsPrivate::DD_AddToPlaylist:
			if (viewPlaylists) {
				viewPlaylists->addExtFolders(dirs);
			}
			break;
		}
	}
}

void MainWindow::init()
{
	this->toggleMenuBar(SettingsPrivate::instance()->value("isMenuHidden").toBool());
	this->setupActions();
}

void MainWindow::loadPlugins()
{
	_pluginManager->init();
}

/** Set up all actions and behaviour. */
void MainWindow::setupActions()
{
	auto settingsPrivate = SettingsPrivate::instance();

	// Adds a group where view mode are mutually exclusive
	QActionGroup *viewModeGroup = new QActionGroup(this);
	viewModeGroup->setObjectName("viewModeGroup");
	connect(viewModeGroup, &QActionGroup::triggered, this, &MainWindow::activateView);
	actionViewPlaylists->setActionGroup(viewModeGroup);
	actionViewUniqueLibrary->setActionGroup(viewModeGroup);
	connect(actionMiniPlayer, &QAction::triggered, this, &MainWindow::switchToMiniPlayer);
	connect(actionViewTagEditor, &QAction::triggered, this, &MainWindow::showTagEditor);

	// Link user interface
	// Actions from the menu
	QShortcut *exit = this->findChild<QShortcut*>("actionExit");
	//actionExit->setShortcut(exit->key());
	connect(exit, &QShortcut::activated, this, [=]() {
		QAction *a = this->findChild<QAction*>(exit->objectName());
		a->trigger();
	});
	connect(actionExit, &QAction::triggered, this, [=]() {
		QCloseEvent event;
		this->closeEvent(&event);
		qApp->quit();
	});
	connect(actionShowCustomize, &QAction::triggered, this, [=]() {
		CustomizeThemeDialog *customizeThemeDialog = new CustomizeThemeDialog;
		customizeThemeDialog->show();
	});
	connect(actionShowOptions, &QAction::triggered, this, &MainWindow::createCustomizeOptionsDialog);
	connect(actionAboutQt, &QAction::triggered, &QApplication::aboutQt);
	connect(actionHideMenuBar, &QAction::triggered, this, &MainWindow::toggleMenuBar);
	connect(actionScanLibrary, &QAction::triggered, this, [=]() {
		this->syncLibrary(QStringList(), settingsPrivate->musicLocations());
	});
	connect(actionShowHelp, &QAction::triggered, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://github.com/MBach/Miam-Player/wiki"));
	});

	// Load music
	connect(settingsPrivate, &SettingsPrivate::musicLocationsHaveChanged, this, &MainWindow::syncLibrary);

	connect(actionShowEqualizer, &QAction::triggered, this, [=]() {
		EqualizerDialog *equalizerDialog = new EqualizerDialog(_mediaPlayer, this);
		equalizerDialog->show();
		equalizerDialog->activateWindow();
	});

	connect(actionMute, &QAction::triggered, _mediaPlayer, &MediaPlayer::toggleMute);

	connect(settingsPrivate, &SettingsPrivate::monitorFileSystemChanged, this, [=](bool b) {
		if (b) {
			MusicSearchEngine *worker = new MusicSearchEngine;
			QThread *thread = new QThread;
			worker->moveToThread(thread);
			connect(thread, &QThread::started, worker, &MusicSearchEngine::watchForChanges);
			connect(thread, &QThread::finished, thread, &QThread::deleteLater);
			thread->start();

			if (_currentView && _currentView->viewProperty(Settings::VP_HasAreaForRescan)) {
				_currentView->setMusicSearchEngine(worker);
			}
        }
	});

	connect(settingsPrivate, &SettingsPrivate::fontHasChanged, this, [=](SettingsPrivate::FontFamily ff) {
		if (ff == SettingsPrivate::FF_Menu) {
			this->updateFonts(settingsPrivate->font(ff));
		}
	});

	connect(settingsPrivate, &SettingsPrivate::remoteControlChanged, this, [=](bool enabled, uint port) {
		if (enabled) {
			if (_remoteControl) {
				_remoteControl->changeServerPort(port);
			} else {
				_remoteControl = new RemoteControl(_currentView, port, this);
				_remoteControl->startServer();
			}
		} else {
			_remoteControl->deleteLater();
		}
	});
}

/** Update fonts for menu and context menus. */
void MainWindow::updateFonts(const QFont &font)
{
#ifndef Q_OS_OSX
	menuBar()->setFont(font);
	for (QAction *action : findChildren<QAction*>()) {
		action->setFont(font);
	}
#else
	Q_UNUSED(font)
#endif
}

/** Redefined to be able to retransltate User Interface at runtime. */
void MainWindow::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		this->retranslateUi(this);

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
	auto settingsPrivate = SettingsPrivate::instance();
	if (_currentView && _currentView->viewProperty(Settings::VP_PlaylistFeature) && settingsPrivate->playbackKeepPlaylists()) {
		if (AbstractViewPlaylists *v = static_cast<AbstractViewPlaylists*>(_currentView)) {
			v->saveCurrentPlaylists();
		}
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->mimeData()->hasFormat("playlist/x-tableview-item") || event->mimeData()->hasFormat("treeview/x-treeview-item")) {
		// Display a forbid cursor when one has started a drag from a playlist
		// Accepted drops are other playlists or the tabbar
		event->ignore();
	} else {
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent *event)
{
	// Ignore Drag & Drop if the source is a part of this player
	if (event->source() != nullptr) {
		return;
	}
	this->dispatchDrop(event);
}

bool MainWindow::event(QEvent *e)
{
	bool b = QMainWindow::event(e);
	if (e->type() == QEvent::KeyPress) {
		if (!this->menuBar()->isVisible()) {
			QKeyEvent *keyEvent = static_cast<QKeyEvent*>(e);
			if (keyEvent->key() == Qt::Key_Alt) {
				this->setProperty("altKey", true);
				this->toggleMenuBar(false);
				// Reactivate shortcuts on the menuBar

				/*QMapIterator<QString, QVariant> it(Settings::instance()->shortcuts());
				while (it.hasNext()) {
					it.next();
					this->bindShortcut(it.key(), it.value().value<QKeySequence>());
				}*/

			} else {
				this->setProperty("altKey", false);
			}
		}
	} else if (e->type() == QEvent::KeyRelease) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent*>(e);
		if (this->property("altKey").toBool() && keyEvent->key() == Qt::Key_Alt) {
			this->menuBar()->show();
			this->menuBar()->setFocus();
			this->setProperty("altKey", false);
			actionHideMenuBar->setChecked(false);
			SettingsPrivate::instance()->setValue("isMenuHidden", false);
		}
	}
	return b;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == _tagEditor && QEvent::Close) {
		actionViewTagEditor->setChecked(false);
	} else if (event->type() == QEvent::Drop) {
		QDropEvent *de = static_cast<QDropEvent*>(event);
		this->dispatchDrop(de);
	}
	//qDebug() << Q_FUNC_INFO << watched;
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::initQuickStart()
{
	// Clean any existing view first
	if (centralWidget()) {
		delete takeCentralWidget();
		_currentView = nullptr;
	}

	// Replace central widget with QuickStart
	QuickStart *quickStart = new QuickStart(this);
	quickStart->searchMultimediaFiles();
	connect(quickStart->commandLinkButtonLibrary, &QAbstractButton::clicked, this, &MainWindow::createCustomizeOptionsDialog);
	connect(quickStart, &QuickStart::destroyed, this, [=]() {
		int w = qApp->desktop()->screenGeometry().width() / 2;
		int h = qApp->desktop()->screenGeometry().height() / 2;
		this->move(w - frameGeometry().width() / 2, h - frameGeometry().height() / 2);
	});
	this->setCentralWidget(quickStart);
	this->menuBar()->hide();
	this->resize(400, 500);
}

void MainWindow::createCustomizeOptionsDialog()
{
	CustomizeOptionsDialog *dialog = new CustomizeOptionsDialog(_pluginManager, this);
	connect(dialog, &CustomizeOptionsDialog::aboutToBindShortcut, this, &MainWindow::bindShortcut);
	if (_currentView && _currentView->viewProperty(Settings::VP_FileExplorerFeature)) {
		connect(dialog, &CustomizeOptionsDialog::aboutToBindShortcut, _currentView, &AbstractView::bindShortcut);
		connect(dialog, &CustomizeOptionsDialog::defaultLocationFileExplorerHasChanged, _currentView, &AbstractView::initFileExplorer);
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void MainWindow::processArgs(const QStringList &args)
{
	QCommandLineParser parser;
	parser.setApplicationDescription(QCoreApplication::tr("Command line helper for Miam-Player"));
	parser.addHelpOption();

	QCommandLineOption directoryOption(QStringList() << "d" << "directory", tr("Directory to open."), tr("dir"));
	QCommandLineOption createNewPlaylist(QStringList() << "n" << "new-playlist", tr("Medias are added into a new playlist."));
	QCommandLineOption sendToTagEditor(QStringList() << "t" << "tag-editor", tr("Medias are sent to tag editor."));
	QCommandLineOption addToLibrary(QStringList() << "l" << "library", tr("Directory is sent to library."));
	QCommandLineOption playPause(QStringList() << "p" << "play", tr("Play or pause track in active playlist."));
	QCommandLineOption stop(QStringList() << "s" << "stop", tr("Stop playback."));
	QCommandLineOption skipForward(QStringList() << "f" << "forward", tr("Play next track."));
	QCommandLineOption skipBackward(QStringList() << "b" << "backward", tr("Play previous track."));
	QCommandLineOption volume(QStringList() << "v" << "volume", tr("Set volume of the player."), tr("volume"));

	parser.addOption(directoryOption);
	parser.addPositionalArgument("files", "Files to open", "[files]");
	parser.addOption(createNewPlaylist);
	parser.addOption(sendToTagEditor);
	parser.addOption(addToLibrary);
	parser.addOption(playPause);
	parser.addOption(stop);
	parser.addOption(skipForward);
	parser.addOption(skipBackward);
	parser.addOption(volume);
	parser.process(args);

	QStringList positionalArgs = parser.positionalArguments();
	bool isDirectoryOption = parser.isSet(directoryOption);
	bool isCreateNewPlaylist = parser.isSet(createNewPlaylist);
	bool isSendToTagEditor = parser.isSet(sendToTagEditor);
	bool isAddToLibrary = parser.isSet(addToLibrary);

	// -d <dir> and -f <files...> options are exclusive
	// It could be possible to use them at the same time but it can be confusing. Directory takes precedence
	if (isDirectoryOption) {
		QFileInfo fileInfo(parser.value(directoryOption));
		if (!fileInfo.isDir()) {
			parser.showHelp();
		}
		if (isSendToTagEditor) {
			this->showTagEditor();
			_tagEditor->addDirectory(fileInfo.absoluteDir());
		} else if (isAddToLibrary) {
			SettingsPrivate::instance()->addMusicLocations(QList<QDir>() << QDir(fileInfo.absoluteFilePath()));
		} else {
			if (_currentView) {
				if (AbstractViewPlaylists *v = static_cast<AbstractViewPlaylists*>(_currentView)) {
					if (isCreateNewPlaylist) {
						v->addPlaylist();
					}
					v->openFolder(fileInfo.absoluteFilePath());
				}
			}
		}
	} else if (!positionalArgs.isEmpty()) {
		if (isSendToTagEditor) {
			this->showTagEditor();
			QList<QUrl> tracks;
			for (QString p : positionalArgs) {
				tracks << QUrl::fromLocalFile(p);
			}
			_tagEditor->addItemsToEditor(tracks);
		} else {
			if (_currentView) {
				if (AbstractViewPlaylists *v = static_cast<AbstractViewPlaylists*>(_currentView)) {
					if (isCreateNewPlaylist) {
						v->addPlaylist();
					}
					QList<QUrl> tracks;
					for (QString p : positionalArgs) {
						tracks << QUrl::fromLocalFile(p);
					}
					v->addToPlaylist(tracks);
				}
			}
		}
	} else if (parser.isSet(playPause)) {
		if (_mediaPlayer->state() == QMediaPlayer::PlayingState) {
			_mediaPlayer->pause();
		} else {
			_mediaPlayer->play();
		}
	} else if (parser.isSet(skipForward)) {
		_mediaPlayer->skipForward();
	} else if (parser.isSet(skipBackward)) {
		_mediaPlayer->skipBackward();
	} else if (parser.isSet(stop)) {
		_mediaPlayer->stop();
	} else if (parser.isSet(volume)) {
		bool ok = false;
		int vol = parser.value(volume).toInt(&ok);
		if (ok) {
			_mediaPlayer->setVolume((qreal)vol / 100.0);
		}
	}
}

void MainWindow::activateView(QAction *menuAction)
{
	// User a Helper to load views depending on which classes are attached to the QAction
	ViewLoader v(_mediaPlayer, _pluginManager, this);
	auto view = v.load(_currentView, menuAction->objectName());
	if (_currentView == view) {
		return;
	} else {
		_currentView = view;
	}

	if (!_currentView) {
		qDebug() << Q_FUNC_INFO << menuAction->objectName() << "couldn't load it's attached view";
		actionViewPlaylists->trigger();
		return;
	}
	_currentView->installEventFilter(this);

	if (_currentView->viewProperty(Settings::VP_CanSendTracksToEditor)) {
		connect(_currentView, &AbstractView::aboutToSendToTagEditor, this, [=](const QList<QUrl> &tracks) {
			actionViewTagEditor->trigger();
			_tagEditor->addItemsToEditor(tracks);
		});
	}

	// First, clean the view (can be a QuickStart instance)
	SettingsPrivate *settingsPrivate = SettingsPrivate::instance();
	if (_currentView->viewProperty(Settings::VP_OwnWindow)) {
		connect(_currentView->windowHandle(), &QWindow::visibleChanged, this, [=](bool b) {
			if (!b) {
				_currentView->hide();
				AbstractView *parent = _currentView->origin();
				_currentView->deleteLater();
				_currentView = parent;
				this->show();
			}
		});
		_currentView->show();
		_currentView->activateWindow();
		this->hide();
	} else {
		// Replace the main widget
		if (this->centralWidget()) {
			QWidget *w = this->takeCentralWidget();
			w->deleteLater();
		}
		QByteArray ba = settingsPrivate->lastActiveViewGeometry(menuAction->objectName());
		if (ba.isEmpty()) {
			this->resize(_currentView->sizeHint());
		} else {
			this->restoreGeometry(ba);
		}
		this->setCentralWidget(_currentView);
	}

	// Activate remote control server if toggled in settings
	if (settingsPrivate->isRemoteControlEnabled()) {
		_remoteControl = new RemoteControl(_currentView, settingsPrivate->remoteControlPort(), this);
		_remoteControl->startServer();
	}

	// Init default multimedia keys
	_shortcutSkipBackward->disconnect();
	_shortcutPlayPause->disconnect();
	_shortcutStop->disconnect();
	_shortcutSkipForward->disconnect();
	connect(_shortcutSkipBackward, &QxtGlobalShortcut::activated, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::skipBackward);
	connect(_shortcutPlayPause, &QxtGlobalShortcut::activated, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::togglePlayback);
	connect(_shortcutStop, &QxtGlobalShortcut::activated, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::stop);
	connect(_shortcutSkipForward, &QxtGlobalShortcut::activated, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::skipForward);

	// Link media buttons and their shortcuts
	menuPlayback->disconnect();
	connect(menuPlayback, &QMenu::aboutToShow, this, [=]() {

		bool isPlaying = (_mediaPlayer->state() == QMediaPlayer::PlayingState || _mediaPlayer->state() == QMediaPlayer::PausedState);
		actionSeekBackward->setEnabled(isPlaying);
		actionStop->setEnabled(isPlaying);
		actionStopAfterCurrent->setEnabled(isPlaying);
		actionStopAfterCurrent->setChecked(_mediaPlayer->isStopAfterCurrent());
		actionSeekForward->setEnabled(isPlaying);

		if (_currentView->viewProperty(Settings::VP_PlaylistFeature)) {
			bool notEmpty = false;
			if (_mediaPlayer->playlist()) {
				notEmpty = !_mediaPlayer->playlist()->isEmpty();

				QMediaPlaylist::PlaybackMode mode = _mediaPlayer->playlist()->playbackMode();
				const QMetaObject &mo = QMediaPlaylist::staticMetaObject;
				QMetaEnum metaEnum = mo.enumerator(mo.indexOfEnumerator("PlaybackMode"));
				QAction *action = findChild<QAction*>(QString("actionPlayback").append(metaEnum.valueToKey(mode)));
				action->setChecked(true);
			}
			actionSkipBackward->setEnabled(notEmpty);
			actionPlay->setEnabled(notEmpty);
			actionSkipForward->setEnabled(notEmpty);
		} else {
			qDebug() << Q_FUNC_INFO << "playback mode to set";
			if (_currentView->mediaPlayerControl()->isInShuffleState()) {
				actionPlaybackRandom->setChecked(true);
			} else {
				actionPlaybackSequential->setChecked(true);
			}
		}
	});

	QList<QAction*> multimediaActions = { actionSkipBackward, actionSeekBackward, actionPlay, actionStop, actionStopAfterCurrent, actionSeekForward, actionSkipForward };
	for (QAction *action : multimediaActions) {
		action->disconnect();
	}
	connect(actionSkipBackward, &QAction::triggered, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::skipBackward);
	connect(actionSeekBackward, &QAction::triggered, _mediaPlayer, &MediaPlayer::seekBackward);
	connect(actionPlay, &QAction::triggered, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::togglePlayback);
	connect(actionStop, &QAction::triggered, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::stop);
	connect(actionStopAfterCurrent, &QAction::triggered, _mediaPlayer, &MediaPlayer::stopAfterCurrent);
	connect(actionSeekForward, &QAction::triggered, _mediaPlayer, &MediaPlayer::seekForward);
	connect(actionSkipForward, &QAction::triggered, _currentView->mediaPlayerControl(), &AbstractMediaPlayerControl::skipForward);

	// Basically, a music player provides a playlist feature or it doesn't.
	// It implies a clean and separate way to display things, I suppose.
	bool b = _currentView->viewProperty(Settings::VP_PlaylistFeature);
	menuView->setEnabled(true);
	menuPlayback->setEnabled(true);
	menuPlaylist->setEnabled(true);
	menuPlaylist->menuAction()->setVisible(b);
	actionOpenFiles->setEnabled(b);
	actionOpenFolder->setEnabled(b);

	QActionGroup *playbackActionGroup = this->findChild<QActionGroup*>("playbackActionGroup");

	actionPlaybackLoop->setEnabled(b);
	actionPlaybackCurrentItemOnce->setEnabled(b);
	actionPlaybackCurrentItemInLoop->setEnabled(b);
	if (b) {
		AbstractViewPlaylists *viewPlaylists = static_cast<AbstractViewPlaylists*>(_currentView);
		connect(actionOpenFiles, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::openFiles);
		connect(actionOpenFolder, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::openFolderPopup);
		connect(actionAddPlaylist, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::addPlaylist);
		connect(actionDeleteCurrentPlaylist, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::removeCurrentPlaylist);
		connect(menuPlaylist, &QMenu::aboutToShow, this, [=]() {
			int selectedTracks = viewPlaylists->selectedTracksInCurrentPlaylist();
			bool b = selectedTracks > 0;
			actionRemoveSelectedTracks->setEnabled(b);
			actionMoveTracksUp->setEnabled(b);
			actionMoveTracksDown->setEnabled(b);
			if (selectedTracks > 1) {
				actionRemoveSelectedTracks->setText(tr("&Remove selected tracks", "Number of tracks to remove", selectedTracks));
				actionMoveTracksUp->setText(tr("Move selected tracks &up", "Move upward", selectedTracks));
				actionMoveTracksDown->setText(tr("Move selected tracks &down", "Move downward", selectedTracks));
			}
		});

		playbackActionGroup->disconnect();
		connect(playbackActionGroup, &QActionGroup::triggered, this, [=](QAction *action) {
			const QMetaObject &mo = QMediaPlaylist::staticMetaObject;
			QMetaEnum metaEnum = mo.enumerator(mo.indexOfEnumerator("PlaybackMode"));
			int mode = metaEnum.keyToValue(action->property("PlaybackMode").toString().toStdString().c_str());
			_mediaPlayer->playlist()->setPlaybackMode((QMediaPlaylist::PlaybackMode)mode);
		});

		// Playback
		connect(actionRemoveSelectedTracks, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::removeSelectedTracks);
		connect(actionMoveTracksUp, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::moveTracksUp);
		connect(actionMoveTracksDown, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::moveTracksDown);
		connect(actionOpenPlaylistManager, &QAction::triggered, viewPlaylists, &AbstractViewPlaylists::openPlaylistManager);
	} else {
		actionOpenFiles->disconnect();
		actionOpenFolder->disconnect();
		actionAddPlaylist->disconnect();
		actionDeleteCurrentPlaylist->disconnect();
		menuPlaylist->disconnect();
		playbackActionGroup->disconnect();
		actionRemoveSelectedTracks->disconnect();
		actionMoveTracksUp->disconnect();
		actionMoveTracksDown->disconnect();
		actionOpenPlaylistManager->disconnect();

		connect(playbackActionGroup, &QActionGroup::triggered, this, [=](QAction *action) {
			_currentView->mediaPlayerControl()->toggleShuffle(action != actionPlaybackSequential);
		});
	}

	connect(actionIncreaseVolume, &QAction::triggered, _currentView, &AbstractView::volumeSliderIncrease);
	connect(actionDecreaseVolume, &QAction::triggered, _currentView, &AbstractView::volumeSliderDecrease);

	connect(qApp, &QApplication::aboutToQuit, this, [=] {
		if (_currentView) {
			QActionGroup *actionGroup = this->findChild<QActionGroup*>("viewModeGroup");
			if (!_currentView->viewProperty(Settings::VP_OwnWindow)) {
				settingsPrivate->setLastActiveViewGeometry(actionGroup->checkedAction()->objectName(), this->saveGeometry());
			}
			settingsPrivate->sync();
		}
	});

	settingsPrivate->setValue("lastActiveView", menuAction->objectName());
	_currentView->loadModel();
}

void MainWindow::bindShortcut(const QString &objectName, const QKeySequence &keySequence)
{
	QAction *action = findChild<QAction*>("action" + objectName.left(1).toUpper() + objectName.mid(1));
	// Connect actions first
	if (action) {
		action->setShortcut(keySequence);
		// Some default shortcuts might interfer with other widgets, so we need to restrict where it applies
		if (action == actionRemoveSelectedTracks) {
			action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		}
	}
}

void MainWindow::showTagEditor()
{
	if (actionViewTagEditor->isChecked()) {
		/// XXX
		// Tag editor is opened, closing it
		if (_tagEditor) {
			_pluginManager->unregisterExtensionPoint(_tagEditor->extensionPoints().first);
			_tagEditor->deleteLater();
			_tagEditor = nullptr;
		}
		_tagEditor = new TagEditor;
		_pluginManager->registerExtensionPoint(_tagEditor->extensionPoints());
		_tagEditor->setOrigin(_currentView);
		_tagEditor->installEventFilter(this);
		_tagEditor->show();
		_tagEditor->activateWindow();

	} else {
		if (_tagEditor) {
			_pluginManager->unregisterExtensionPoint(_tagEditor->extensionPoints().first);
			_tagEditor->deleteLater();
			_tagEditor = nullptr;
		}
	}
}

void MainWindow::switchToMiniPlayer()
{
	if (_currentView) {
		if (!_mini) {
			_mini = new MiniModeWidget(this);
			connect(_mini, &MiniModeWidget::destroyed, this, [=]() { _mini = nullptr; });
		}
		_mini->show();
		this->hide();
	}
}

void MainWindow::syncLibrary(const QStringList &oldLocations, const QStringList &newLocations)
{
	if (!_currentView) {
		this->activateLastView();
	}
	if (newLocations.isEmpty()) {
		this->initQuickStart();
		return;
	}

	bool same = true;
	if (oldLocations.size() == newLocations.size()) {
		for (int i = 0; i < newLocations.size(); i++) {
			same = same && QString::compare(newLocations.at(i), oldLocations.at(i)) == 0;
		}

	} else {
		same = false;
	}

	if (!same) {
		SqlDatabase db;
		db.reset();
	}

	QThread *thread = new QThread;
	MusicSearchEngine *worker = new MusicSearchEngine;
	if (_currentView->viewProperty(Settings::VP_HasAreaForRescan)) {
		_currentView->setMusicSearchEngine(worker);
	}
	for (BasicPlugin *plugin : _pluginManager->loadedPlugins().values()) {
		if (plugin && plugin->canInteractWithSearchEngine()) {
			plugin->setMusicSearchEngine(worker);
		}
	}

	worker->moveToThread(thread);
	connect(thread, &QThread::started, worker, &MusicSearchEngine::doSearch);
	connect(worker, &MusicSearchEngine::aboutToSearch, this, [=]() {
		menuView->setEnabled(false);
		actionScanLibrary->setEnabled(false);
	});
	connect(thread, &QThread::finished, thread, &QThread::deleteLater);
	connect(worker, &MusicSearchEngine::searchHasEnded, this, [=]() {
		qDebug() << "MainWindow -> searchHasEnded";
		worker->deleteLater();
		thread->quit();
		menuView->setEnabled(true);
		actionScanLibrary->setEnabled(true);
		_currentView->loadModel();
	});

	thread->start();
}

void MainWindow::toggleMenuBar(bool checked)
{
	auto settings = Settings::instance();
	auto settingsPrivate = SettingsPrivate::instance();
	settingsPrivate->setValue("isMenuHidden", checked);
	menuBar()->setVisible(!checked);

	// Attach shortcuts to visible menu or "invisible" handler
	qDeleteAll(_menuShortcuts);
	_menuShortcuts.clear();

	QMapIterator<QString, QVariant> it(settings->shortcuts());
	while (it.hasNext()) {
		it.next();

		QKeySequence sequence = settingsPrivate->shortcut(it.key());
		QString actionName = "action" + it.key().left(1).toUpper() + it.key().mid(1);
		QAction *action = this->findChild<QAction*>(actionName);
		if (!action) {
			continue;
		}
		if (checked) {
			QShortcut *s = new QShortcut(sequence, this);
			s->setObjectName(actionName);

			connect(s, &QShortcut::activated, action, &QAction::trigger);
			_menuShortcuts.insert(s);
		} else {
			action->setShortcut(sequence);
		}
	}
}
