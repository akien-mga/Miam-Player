#include "playbackmodewidget.h"
#include "settings.h"

/// FIXME: use Qt::Popup to avoir flicker effect when one clicks. The problem is to manage multiple popups on screen
/// It seems difficult to handle more than one popup
PlaybackModeWidget::PlaybackModeWidget(QMediaPlaylist::PlaybackMode mode, QPushButton *playbackModeButton) :
	QWidget(playbackModeButton, Qt::Tool | Qt::FramelessWindowHint), _mode(mode)
{
	_animation = new QPropertyAnimation(this, "pos");
	_animation->setDuration(200);
	this->setMouseTracking(true);

	QPushButton *button = new QPushButton(this);
	button->setMouseTracking(true);
	_playbackMode;
	switch (mode) {
	case QMediaPlaylist::CurrentItemOnce:
		_playbackMode = "itemOnce";
		break;
	case QMediaPlaylist::CurrentItemInLoop:
		_playbackMode = "itemLoop";
		break;
	case QMediaPlaylist::Sequential:
		_playbackMode = "sequential";
		break;
	case QMediaPlaylist::Loop:
		_playbackMode = "repeat";
		break;
	case QMediaPlaylist::Random:
		_playbackMode = "shuffle";
		break;
	}
	Settings *settings = Settings::getInstance();
	button->setIcon(QIcon(":/player/" + settings->theme() + "/" + _playbackMode));
	button->setIconSize(QSize(settings->buttonsSize(), settings->buttonsSize()));

	QVBoxLayout *vLayout = new QVBoxLayout(this);
	vLayout->setContentsMargins(0, 0, 0, 0);
	vLayout->addWidget(button);
	this->setLayout(vLayout);
}

void PlaybackModeWidget::adjustIcon()
{
	Settings *settings = Settings::getInstance();
	button()->setIcon(QIcon(":/player/" + settings->theme() + "/" + _playbackMode));
	button()->setIconSize(QSize(settings->buttonsSize(), settings->buttonsSize()));
}

void PlaybackModeWidget::animate(const QPoint &start, const QPoint &end)
{
	_animation->setStartValue(start);
	_animation->setEndValue(end);
	_animation->start();
}