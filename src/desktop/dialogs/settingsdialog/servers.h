// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_DIALOGS_SETTINGSDIALOG_SERVERS_H
#define DESKTOP_DIALOGS_SETTINGSDIALOG_SERVERS_H
#include <QModelIndexList>
#include <QWidget>

class CertificateStoreModel;
class QDir;
class QItemSelectionModel;
class QListView;
class QModelIndex;
class QSslCertificate;
class QVBoxLayout;

namespace desktop {
namespace settings {
class Settings;
}
}

namespace sessionlisting {
class ListServerModel;
}

namespace dialogs {
namespace settingsdialog {

class Servers final : public QWidget {
	Q_OBJECT
public:
	Servers(
		desktop::settings::Settings &settings, bool singleSession,
		QWidget *parent = nullptr);

private:
	void initListingServers(
		desktop::settings::Settings &settings, QVBoxLayout *layout);

	void addListServer(sessionlisting::ListServerModel *model);

	void moveListServer(
		sessionlisting::ListServerModel *model,
		QItemSelectionModel *selectionModel, int offset);

#ifndef __EMSCRIPTEN__
	void initKnownHosts(QVBoxLayout *layout);

	void importCertificates(CertificateStoreModel *model);

	void pinCertificates(
		CertificateStoreModel *model, const QModelIndexList &indexes, bool pin);

	void
	viewCertificate(CertificateStoreModel *model, const QModelIndex &index);
#endif
};

} // namespace settingsdialog
} // namespace dialogs

#endif
