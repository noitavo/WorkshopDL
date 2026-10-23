#include "steamsetupdialog.h"
#include "engine.h"
#include "ui_steamsetupdialog.h"
#include <QPushButton>

SteamSetupDialog::SteamSetupDialog(Engine &engine, QWidget *parent)
    : QDialog(parent), ui(std::make_unique<Ui::SteamSetupDialog>()), engine(engine) {
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    ui->destination->setText(QDir::toNativeSeparators(QFileInfo(engine.steamExe).absolutePath()));
    ui->details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ui->error->hide();
    ui->detailsGroup->hide();
    connect(ui->detailsToggle, &QToolButton::toggled, this, [this](bool expanded) {
        ui->detailsGroup->setVisible(expanded);
        ui->detailsToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        adjustSize();
        centerOnScreen();
    });
    ui->buttons->button(QDialogButtonBox::Retry)->hide();
    adjustSize();
    connect(ui->buttons, &QDialogButtonBox::rejected, this, &SteamSetupDialog::reject);
    connect(ui->buttons->button(QDialogButtonBox::Retry), &QPushButton::clicked, this,
            &SteamSetupDialog::retry);
    connect(&engine, &Engine::setupProgress, this, &SteamSetupDialog::setStatus);
    connect(&engine, &Engine::setupDownloadProgress, this, [this](qint64 received, qint64 total) {
        if (cancelling)
            return;
        if (total > 0) {
            ui->progress->setRange(0, 100);
            ui->progress->setValue(int(qBound(0.0, 100.0 * received / total, 100.0)));
            ui->status->setText(
                tr("Downloading SteamCMD: %1 of %2").arg(sizeText(received), sizeText(total)));
        } else {
            ui->progress->setRange(0, 0);
            ui->status->setText(tr("Downloading SteamCMD: %1 received").arg(sizeText(received)));
        }
    });
    connect(&engine, &Engine::message, ui->details, &QPlainTextEdit::appendPlainText);
    connect(&engine, &Engine::setupFinished, this, [this](const QString &error, bool cancelled) {
        if (cancelled || cancelling) {
            QDialog::reject();
        } else if (error.isEmpty()) {
            QDialog::accept();
        } else {
            ui->status->setText(tr("Setup failed."));
            ui->error->setText(error);
            ui->error->show();
            ui->progress->setRange(0, 100);
            ui->progress->setValue(0);
            ui->buttons->button(QDialogButtonBox::Retry)->show();
            ui->buttons->button(QDialogButtonBox::Retry)->setDefault(true);
            ui->buttons->button(QDialogButtonBox::Cancel)->setEnabled(true);
            adjustSize();
            centerOnScreen();
        }
    });
}

SteamSetupDialog::~SteamSetupDialog() = default;

void SteamSetupDialog::setStatus(const QString &text) {
    if (cancelling)
        return;
    ui->status->setText(text);
    ui->details->appendPlainText(text);
    ui->destination->setText(QDir::toNativeSeparators(QFileInfo(engine.steamExe).absolutePath()));
    ui->progress->setRange(0, 0);
    ui->error->hide();
    ui->buttons->button(QDialogButtonBox::Retry)->hide();
}

void SteamSetupDialog::retry() {
    if (engine.busy)
        return;
    cancelling = false;
    setStatus(tr("Preparing SteamCMD…"));
    engine.prepareSteamcmd();
}

void SteamSetupDialog::reject() {
    if (!engine.busy) {
        QDialog::reject();
        return;
    }
    if (cancelling)
        return;
    cancelling = true;
    ui->status->setText(tr("Stopping SteamCMD…"));
    ui->buttons->button(QDialogButtonBox::Cancel)->setEnabled(false);
    ui->buttons->button(QDialogButtonBox::Retry)->hide();
    engine.cancel();
}

void SteamSetupDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (centered)
        return;
    centered = true;
    centerOnScreen();
}

void SteamSetupDialog::centerOnScreen() {
    const QScreen *target =
        parentWidget() ? parentWidget()->screen() : QGuiApplication::screenAt(QCursor::pos());
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;
    const QRect available = target->availableGeometry();
    const QPoint center = parentWidget() ? parentWidget()->frameGeometry().center() : available.center();
    const QSize frame = frameGeometry().size();
    move(qBound(available.left(), center.x() - frame.width() / 2,
                qMax(available.left(), available.right() - frame.width() + 1)),
         qBound(available.top(), center.y() - frame.height() / 2,
                qMax(available.top(), available.bottom() - frame.height() + 1)));
}
