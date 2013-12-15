#ifndef UNIQUELIBRARY_H
#define UNIQUELIBRARY_H

#include <QWidget>

#include <miamuniquelibrary_global.h>
#include <model/librarysqlmodel.h>

namespace Ui {
class UniqueLibrary;
}

class FlowLayout;

class AlbumForm;

class MIAMUNIQUELIBRARY_LIBRARY UniqueLibrary : public QWidget
{
	Q_OBJECT

private:
	Ui::UniqueLibrary *ui;
	FlowLayout *_flowLayout;
	LibrarySqlModel *_sqlModel;

	QMap<QString, AlbumForm*> _albums;

public:
	explicit UniqueLibrary(QWidget *parent = 0);

	void init(LibrarySqlModel *sql);

	void insertTrackFromRecord(const QSqlRecord &record);
	void insertTrackFromFile(const FileHelper &fh);

private:
	void insertTrack(const QString &absFilePath, const QString &artistAlbum, const QString &artist, const QString &album,
					 int discNumber, const QString &title, int year);

private slots:
	void reset();
};

#endif // UNIQUELIBRARY_H
