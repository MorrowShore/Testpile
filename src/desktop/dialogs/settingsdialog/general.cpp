// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/dialogs/settingsdialog/general.h"
#include "cmake-config/config.h"
#include "desktop/settings.h"
#include "desktop/utils/widgetutils.h"
#include "libshared/util/qtcompat.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QString>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWidget>

namespace dialogs {
namespace settingsdialog {

General::General(desktop::settings::Settings &settings, QWidget *parent)
	: Page(parent)
{
	init(settings);
}

void General::setUp(desktop::settings::Settings &settings, QVBoxLayout *layout)
{
	QFormLayout *themeLanguageSection = utils::addFormSection(layout);
	initTheme(settings, themeLanguageSection);
	initLanguage(settings, themeLanguageSection);

	utils::addFormSeparator(layout);

	QFormLayout *canvasSection = utils::addFormSection(layout);
	initLogging(settings, canvasSection);
	utils::addFormSpacer(canvasSection);
	initUndo(settings, canvasSection);
#ifndef __EMSCRIPTEN__
	utils::addFormSpacer(canvasSection);
	initAutosave(settings, canvasSection);
#endif
	utils::addFormSpacer(canvasSection);
	initSnapshots(settings, canvasSection);

	utils::addFormSeparator(layout);

	initPerformance(settings, utils::addFormSection(layout));
}

void General::initAutosave(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *autosaveInterval = new QSpinBox;
	autosaveInterval->setRange(1, 999);
	settings.bindAutoSaveInterval(this, [=](int intervalMsec) {
		autosaveInterval->setValue(intervalMsec / 1000);
	});
	connect(
		autosaveInterval, QOverload<int>::of(&QSpinBox::valueChanged), this,
		[&](int intervalSec) {
			settings.setAutoSaveInterval(intervalSec * 1000);
		});

	auto *snapshotCountLayout = utils::encapsulate(
		tr("When enabled, save every %1 minutes"), autosaveInterval);
	snapshotCountLayout->setControlTypes(QSizePolicy::CheckBox);
	form->addRow(tr("Autosave:"), snapshotCountLayout);

	form->addRow(
		nullptr,
		utils::formNote(tr("Autosave can be enabled for the current file under "
						   "File ▸ Autosave.")));
}

void General::initLanguage(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *language = new QComboBox;
	language->addItem(tr("System"), QString());

	const QLocale localeC = QLocale::c();
	for(const auto *localeName : cmake_config::locales()) {
		QLocale locale(localeName);
		if(locale != localeC) {
			language->addItem(formatLanguage(locale), localeName);
		}
	}

	settings.bindLanguage(language, Qt::UserRole);
	form->addRow(tr("Language:"), language);
	form->addRow(
		nullptr, utils::formNote(
					 tr("Language changes apply after you restart Drawpile.")));
}

QString General::formatLanguage(const QLocale &locale)
{
	bool needsCountryDisambiguation = false;
	for(const char *localeName : cmake_config::locales()) {
		QLocale other(localeName);
		if(locale != other && locale.language() == other.language()) {
			needsCountryDisambiguation = true;
			break;
		}
	}

	if(needsCountryDisambiguation) {
		return tr("%1 (%2) / %3 (%4)")
			.arg(locale.nativeLanguageName())
			.arg(compat::nativeTerritoryName(locale))
			.arg(QLocale::languageToString(locale.language()))
			.arg(compat::territoryToString(locale));
	} else {
		return tr("%1 / %2")
			.arg(locale.nativeLanguageName())
			.arg(QLocale::languageToString(locale.language()));
	}
}

void General::initLogging(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *enableLogging = new QCheckBox(tr("Write debugging log to file"));
	settings.bindWriteLogFile(enableLogging);
	form->addRow(tr("Logging:"), enableLogging);
}

void General::initPerformance(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *renderSmooth =
		new QCheckBox(tr("Interpolate when view is zoomed or rotated"));
	settings.bindRenderSmooth(renderSmooth);
	form->addRow(tr("Canvas view:"), renderSmooth);

	auto *renderUpdateFull =
		new QCheckBox(tr("Prevent jitter at certain zoom and rotation levels"));
	settings.bindRenderUpdateFull(renderUpdateFull);
	form->addRow(nullptr, renderUpdateFull);

	form->addRow(
		nullptr,
		utils::formNote(tr(
			"Enabling these options may impact performance on some systems.")));
}

void General::initSnapshots(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *snapshotCount = new QSpinBox;
	snapshotCount->setRange(0, 99);
	settings.bindEngineSnapshotCount(snapshotCount);
	auto *snapshotCountLayout =
		utils::encapsulate(tr("Keep %1 canvas snapshots"), snapshotCount);
	snapshotCountLayout->setControlTypes(QSizePolicy::CheckBox);
	form->addRow(tr("Canvas snapshots:"), snapshotCountLayout);

	auto *snapshotInterval = new QSpinBox;
	snapshotInterval->setRange(1, 600);
	snapshotInterval->setSingleStep(5);
	settings.bindEngineSnapshotInterval(snapshotInterval);
	settings.bindEngineSnapshotCount(snapshotInterval, &QSpinBox::setEnabled);
	auto *snapshotIntervalLayout = utils::encapsulate(
		tr("Take one snapshot every %1 seconds"), snapshotInterval);
	snapshotIntervalLayout->setControlTypes(QSizePolicy::CheckBox);
	form->addRow(nullptr, snapshotIntervalLayout);

	form->addRow(
		nullptr,
		utils::formNote(
			tr("Snapshots can be restored from the Session ▸ Reset… menu.")));
}

void General::initTheme(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *style = new QComboBox;
	style->addItem(tr("System"), QString());
	const auto styleNames = QStyleFactory::keys();
	for(const auto &styleName : styleNames) {
		// Qt5 does not give a proper name for the macOS style
		if(styleName == "macintosh") {
			style->addItem("macOS", styleName);
		} else {
			style->addItem(styleName, styleName);
		}
	}
	settings.bindThemeStyle(style, Qt::UserRole);
	form->addRow(tr("Style:"), style);

	auto *theme = new QComboBox;
	theme->addItems(
		{tr("System"), tr("Light"), tr("Dark"), tr("Krita Bright"),
		 tr("Krita Dark"), tr("Krita Darker"), tr("Qt Fusion"),
		 tr("Hotdog Stand")});
	settings.bindThemePalette(theme, std::nullopt);
	form->addRow(tr("Color scheme:"), theme);
}

void General::initUndo(desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *undoLimit = new QSpinBox;
	undoLimit->setRange(1, 255);
	settings.bindEngineUndoDepth(undoLimit);
	auto *undoLimitLayout =
		utils::encapsulate(tr("Use %1 undo levels by default"), undoLimit);
	undoLimitLayout->setControlTypes(QSizePolicy::CheckBox);
	form->addRow(tr("Session history:"), undoLimitLayout);
}

} // namespace settingsdialog
} // namespace dialogs
