#include "mediaplayer.h"

#include "settings.h"
#include "settingsprivate.h"
#include "model/sqldatabase.h"
#include <QGuiApplication>
#include <QMediaPlaylist>
#include <QWindow>

#include "remotemediaplayer.h"

#include "vlc-qt/Audio.h"
#include "vlc-qt/Common.h"
#include "vlc-qt/Instance.h"
#include "vlc-qt/Media.h"
#include "vlc-qt/MediaPlayer.h"

#include <QtDebug>

MediaPlayer::MediaPlayer(QObject *parent) :
	QObject(parent), _playlist(NULL), _state(QMediaPlayer::StoppedState), _media(NULL), _remotePlayer(NULL)
{
	_instance = new VlcInstance(VlcCommon::args(), this);
	_player = new VlcMediaPlayer(_instance);
	this->createLocalConnections();

	connect(this, &MediaPlayer::currentMediaChanged, this, [=] (const QString &uri) {
		QWindow *w = QGuiApplication::topLevelWindows().first();
		TrackDAO t = SqlDatabase::instance()->selectTrack(uri);
		if (t.artist().isEmpty()) {
			w->setTitle(t.title() + " - Miam Player");
		} else {
			w->setTitle(t.title() + " (" + t.artist() + ") - Miam Player");
		}
	});
}

void MediaPlayer::createLocalConnections()
{
	connect(_player, &VlcMediaPlayer::opening, this, [=]() {
		emit mediaStatusChanged(QMediaPlayer::LoadingMedia);
	});

	connect(_player, &VlcMediaPlayer::playing, this, [=]() {
		// Prevent multiple signals bug?
		qDebug() << "VlcMediaPlayer::playing ?";
		//if (_state != QMediaPlayer::PlayingState) {
		//	qDebug() << "VlcMediaPlayer::playing !";
			//emit mediaStatusChanged(QMediaPlayer::LoadedMedia);
			//_state = QMediaPlayer::PlayingState;
			//emit stateChanged(QMediaPlayer::PlayingState);
		//}
	});

	connect(_player, &VlcMediaPlayer::stopped, this, [=]() {
		qDebug() << "VlcMediaPlayer::stopped";
		emit mediaStatusChanged(QMediaPlayer::NoMedia);
		_state = QMediaPlayer::StoppedState;
		emit stateChanged(QMediaPlayer::StoppedState);
	});

	connect(_player, &VlcMediaPlayer::paused, this, [=]() {
		qDebug() << "VlcMediaPlayer::paused";
		_state = QMediaPlayer::PausedState;
		emit stateChanged(QMediaPlayer::PausedState);
	});

	connect(_player, &VlcMediaPlayer::buffering, this, [=](float buffer) {
		qDebug() << "VlcMediaPlayer::buffering" << buffer;
		if (buffer == 100) {
			qDebug() << "VlcMediaPlayer::Buffered" << buffer;
			_state = QMediaPlayer::PlayingState;
			emit mediaStatusChanged(QMediaPlayer::BufferedMedia);
			emit stateChanged(QMediaPlayer::PlayingState);
		} else {
			emit mediaStatusChanged(QMediaPlayer::BufferingMedia);
		}
	});

	connect(_player, &VlcMediaPlayer::end, this, [=]() {
		qDebug() << "VlcMediaPlayer::end";
		emit mediaStatusChanged(QMediaPlayer::EndOfMedia);
	});

	connect(_player, &VlcMediaPlayer::error, this, [=]() {
		qDebug() << "VlcMediaPlayer::error";
		emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
	});

	// VlcMediaPlayer::positionChanged is percent-based
	connect(_player, &VlcMediaPlayer::positionChanged, this, [=](float f) {
		//qDebug() << "VlcMediaPlayer::positionChanged";
		qint64 pos = _player->length() * f;
		emit positionChanged(pos, _player->length());
	});

	// Cannot use new signal/slot syntax because libvlc_media_t is not fully defined at compile time (just a forward declaration)
	connect(_player, SIGNAL(mediaChanged(libvlc_media_t*)), this, SLOT(convertMedia(libvlc_media_t*)));
}

void MediaPlayer::addRemotePlayer(RemoteMediaPlayer *remotePlayer)
{
	if (remotePlayer) {
		_remotePlayers.insert(remotePlayer->host(), remotePlayer);
	}
}

QMediaPlaylist * MediaPlayer::playlist()
{
	return _playlist;
}

void MediaPlayer::setPlaylist(QMediaPlaylist *playlist)
{
	if (_playlist) {
		_playlist->disconnect(this);
	}
	_playlist = playlist;
	/// FIXME?
	/*connect(_playlist, &QMediaPlaylist::currentIndexChanged, this, [=]() {
		qDebug() << Q_FUNC_INFO;
		if (_player->state() == Vlc::State::Paused || _player->state() == Vlc::State::Playing) {
			_player->blockSignals(true);
			_player->stop();
		} else {
			foreach (RemoteMediaPlayer *remotePlayer, _remotePlayers) {
				if (remotePlayer) {
					remotePlayer->blockSignals(true);
					remotePlayer->stop();
				}
			}
		}
		// _state = QMediaPlayer::StoppedState;
		//emit stateChanged(_state);
	});*/
}

void MediaPlayer::setVolume(int v)
{
	Settings::instance()->setVolume(v);
	if (_player && _player->audio() && (_player->state() == Vlc::State::Playing || _player->state() == Vlc::State::Paused)) {
		_player->audio()->setVolume(v);
	}
	foreach (RemoteMediaPlayer *remotePlayer, _remotePlayers) {
		if (remotePlayer) {
			remotePlayer->setVolume(v);
		}
	}
}

qint64 MediaPlayer::duration()
{
	/// XXX: what if remote playing?
	if (_remotePlayer != NULL) {
		qDebug() << Q_FUNC_INFO << "not yet implemented for remote players";
		return 1;
	} else {
		return _player->length();
	}
}

float MediaPlayer::position() const
{
	if (_remotePlayer != NULL) {
		//return _remotePlayer->position();
		qDebug() << Q_FUNC_INFO << "not yet implemented for remote players";
		return 0.0;
	} else {
		return _player->position();
	}
}

QMediaPlayer::State MediaPlayer::state() const
{
	return _state;
}

void MediaPlayer::setState(QMediaPlayer::State state)
{
	_state = state;
	emit stateChanged(_state);
}

void MediaPlayer::setMute(bool b) const
{
	b ? _player->audio()->setTrack(-1) : _player->audio()->setTrack(0);
}

void MediaPlayer::setTime(int t) const
{
	QMediaContent mc = _playlist->media(_playlist->currentIndex());
	if (mc.canonicalUrl().isLocalFile()) {
		_player->setTime(t);
	} else if (_remotePlayer != NULL) {
		/// TODO
		_remotePlayer->setTime(t);
	} else {

	}
}

void MediaPlayer::seek(float pos)
{
	if (pos == 1.0) {
		pos -= 0.001f;
	}
	QMediaContent mc = _playlist->media(_playlist->currentIndex());
	if (mc.canonicalUrl().isLocalFile()) {
		_player->setPosition(pos);
	} else if (RemoteMediaPlayer *remotePlayer = this->remoteMediaPlayer(mc.canonicalUrl())) {
		remotePlayer->seek(pos);
	}
}

int MediaPlayer::volume() const
{
	return _player->audio()->volume();
}

/** Seek backward in the current playing track for a small amount of time. */
void MediaPlayer::seekBackward()
{
	if (state() == QMediaPlayer::PlayingState || state() == QMediaPlayer::PausedState) {
		int currentPos = _player->position() * _player->length();
		int time = currentPos - SettingsPrivate::instance()->playbackSeekTime();
		if (time < 0) {
			this->seek(0.0);
		} else {
			this->seek(time / (float)_player->length());
		}
	}
}

/** Seek forward in the current playing track for a small amount of time. */
void MediaPlayer::seekForward()
{
	if (state() == QMediaPlayer::PlayingState || state() == QMediaPlayer::PausedState) {
		int currentPos = _player->position() * _player->length();
		int time = currentPos + SettingsPrivate::instance()->playbackSeekTime();
		if (time > _player->length()) {
			skipForward();
		} else {
			this->seek(time / (float)_player->length());
		}
	}
}

void MediaPlayer::skipBackward()
{
	if (!_playlist || (_playlist && _playlist->playbackMode() == QMediaPlaylist::Sequential && _playlist->previousIndex() < 0)) {
		return;
	}

	QMediaContent previousMedia = _playlist->media(_playlist->previousIndex());
	QMediaContent currentMedia = _playlist->media(_playlist->currentIndex());
	if (currentMedia.canonicalUrl().isLocalFile() && !previousMedia.canonicalUrl().isLocalFile()) {
		qDebug() << Q_FUNC_INFO << "current is Local, previous is Remote, disconnecting local!";
		_player->blockSignals(true);
		_player->stop();
	} else if (!currentMedia.canonicalUrl().isLocalFile() && previousMedia.canonicalUrl().isLocalFile()) {
		qDebug() << Q_FUNC_INFO << "previous is Local, current is Remote, disconnecting remote!";
		RemoteMediaPlayer *remotePlayer = this->remoteMediaPlayer(currentMedia.canonicalUrl());
		remotePlayer->blockSignals(true);
		remotePlayer->stop();
	}

	_playlist->previous();
	this->play();
}

void MediaPlayer::skipForward()
{
	if (!_playlist || (_playlist && _playlist->playbackMode() == QMediaPlaylist::Sequential && _playlist->nextIndex() < _playlist->currentIndex())) {
		_player->stop();
		return;
	}

	QMediaContent currentMedia = _playlist->media(_playlist->currentIndex());
	QMediaContent nextMedia = _playlist->media(_playlist->nextIndex());
	if (currentMedia.canonicalUrl().isLocalFile() && !nextMedia.canonicalUrl().isLocalFile()) {
		qDebug() << Q_FUNC_INFO << "current is Local, next is Remote, disconnecting local!";
		_player->blockSignals(true);
		_player->stop();
	} else if (!currentMedia.canonicalUrl().isLocalFile() && nextMedia.canonicalUrl().isLocalFile()) {
		qDebug() << Q_FUNC_INFO << "next is Local, current is Remote, disconnecting remote!";
		RemoteMediaPlayer *remotePlayer = this->remoteMediaPlayer(currentMedia.canonicalUrl());
		remotePlayer->blockSignals(true);
		remotePlayer->stop();
	}

	_playlist->next();
	this->play();
}

/** Pause current playing track. */
void MediaPlayer::pause()
{
	if (!_playlist) {
		return;
	}
	QMediaContent mc = _playlist->media(_playlist->currentIndex());
	if (mc.canonicalUrl().isLocalFile()) {
		_player->pause();
	} else if (RemoteMediaPlayer *remotePlayer = this->remoteMediaPlayer(mc.canonicalUrl())) {
		remotePlayer->pause();
	}
}

void MediaPlayer::disconnectPlayers(bool isLocal)
{
	if (isLocal) {
		_player->disconnect();
	} else {
		QMapIterator<QString, RemoteMediaPlayer*> it(_remotePlayers);
		while (it.hasNext()) {
			it.next().value()->disconnect();
		}
	}
}

/** Play current track in the playlist. */
void MediaPlayer::play()
{
	// Check if it's possible to play tracks first
	if (!_playlist) {
		return;
	}
	QMediaContent mc = _playlist->media(_playlist->currentIndex());
	if (mc.isNull()) {
		return;
	}

	// Everything is splitted in 2: local actions and remote actions
	// Is it the good way to proceed?
	if (mc.canonicalUrl().isLocalFile()) {
		_player->blockSignals(false);
		if (_state == QMediaPlayer::PausedState) {
			_player->resume();
			_state = QMediaPlayer::PlayingState;
			emit stateChanged(_state);
		} else {
			QString file = mc.canonicalUrl().toLocalFile();
			if (_media) {
				_media->disconnect();
				delete _media;
			}
			_media = new VlcMedia(file, true, _instance);
			_player->audio()->setVolume(Settings::instance()->volume());
			_player->open(_media);
		}
	} else if (RemoteMediaPlayer *remotePlayer = this->remoteMediaPlayer(mc.canonicalUrl())) {
		remotePlayer->blockSignals(false);
		remotePlayer->setVolume(Settings::instance()->volume());
		if (_state == QMediaPlayer::PausedState) {
			remotePlayer->resume();
			_state = QMediaPlayer::PlayingState;
		} else {
			remotePlayer->play(mc.canonicalUrl());
		}
	}
}

/** Stop current track in the playlist. */
void MediaPlayer::stop()
{
	_player->stop();
	foreach (RemoteMediaPlayer *remotePlayer, _remotePlayers) {
		if (remotePlayer) {
			remotePlayer->stop();
		}
	}
}

/** Activate or desactive audio output. */
void MediaPlayer::toggleMute() const
{
	if (_player->audio()->track() == 0) {
		_player->audio()->setTrack(-1);
	} else {
		_player->audio()->setTrack(0);
	}
}

RemoteMediaPlayer * MediaPlayer::remoteMediaPlayer(const QUrl &track, bool autoConnect)
{
	// Disconnect all existing remote players (just in case)
	QMapIterator<QString, RemoteMediaPlayer*> it(_remotePlayers);
	while (it.hasNext()) {
		it.next().value()->disconnect();
	}

	// Reconnect the good one
	RemoteMediaPlayer *p = _remotePlayers.value(track.host());
	if (!p) {
		return NULL;
	}

	// Auto connect after disconnected. Useful when switching from local to remote and vice-versa
	if (autoConnect) {
		connect(p, &RemoteMediaPlayer::paused, this, [=]() {
			_state = QMediaPlayer::PausedState;
			emit stateChanged(_state);
		});
		connect(p, &RemoteMediaPlayer::positionChanged, this, &MediaPlayer::positionChanged);
		connect(p, &RemoteMediaPlayer::started, this, [=](int) {
			_state = QMediaPlayer::PlayingState;
			emit stateChanged(_state);
			emit currentMediaChanged(track.toString());
		});
		connect(p, &RemoteMediaPlayer::stopped, this, [=]() {
			_state = QMediaPlayer::StoppedState;
			emit stateChanged(_state);
		});
		connect(p, &RemoteMediaPlayer::trackHasEnded, this, [=]() {
			_state = QMediaPlayer::StoppedState;
			emit stateChanged(_state);
			emit mediaStatusChanged(QMediaPlayer::EndOfMedia);
		});
	}
	return p;
}

void MediaPlayer::convertMedia(libvlc_media_t *)
{
	emit currentMediaChanged("file://" + _player->currentMedia()->currentLocation());
}
