#include "window.h"
#include "steamsetupdialog.h"
#include <QStyleFactory>

static_assert(sizeof(void *) == 8, "WorkshopDL++ requires an x64 build.");

int main(int argc, char **argv) {

    QApplication app(argc, argv);
    const QColor windowsAccent = app.palette().color(QPalette::Accent);
    QSettings appearance(dataRoot() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    const QString style =
        appearance.value(QStringLiteral("appearance/style"), QStringLiteral("Fusion")).toString();
    if (!QApplication::setStyle(style))
        QApplication::setStyle(QStringLiteral("Fusion"));
    const QString colors =
        appearance.value(QStringLiteral("appearance/colors"), QStringLiteral("dark")).toString();
    const auto scheme = colors == QStringLiteral("dark")    ? Qt::ColorScheme::Dark
                        : colors == QStringLiteral("light") ? Qt::ColorScheme::Light
                                                            : Qt::ColorScheme::Unknown;
    app.styleHints()->setColorScheme(scheme);
    QPalette palette = app.palette();
    if (scheme == Qt::ColorScheme::Dark) {
        palette.setColor(QPalette::Window, QColor(45, 45, 45));
        palette.setColor(QPalette::WindowText, QColor(225, 225, 225));
        palette.setColor(QPalette::Base, QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase, QColor(38, 38, 38));
        palette.setColor(QPalette::Text, QColor(225, 225, 225));
        palette.setColor(QPalette::Button, QColor(55, 55, 55));
        palette.setColor(QPalette::ButtonText, QColor(225, 225, 225));
        palette.setColor(QPalette::ToolTipBase, QColor(55, 55, 55));
        palette.setColor(QPalette::ToolTipText, QColor(225, 225, 225));
        palette.setColor(QPalette::Highlight, QColor(42, 116, 170));
        palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        palette.setColor(QPalette::Accent, QColor(90, 170, 225));
        palette.setColor(QPalette::Link, QColor(90, 170, 225));
        palette.setColor(QPalette::PlaceholderText, QColor(155, 155, 155));
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            palette.setColor(QPalette::Disabled, role, QColor(125, 125, 125));
        palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(65, 65, 65));
    } else if (scheme == Qt::ColorScheme::Light) {
        const std::unique_ptr<QStyle> lightStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        palette = lightStyle->standardPalette();
    }
    QColor accent(appearance.value(QStringLiteral("appearance/accent")).toString());
    if (!accent.isValid())
        accent = windowsAccent;
    if (accent.isValid()) {
        palette.setColor(QPalette::Highlight, accent);
        palette.setColor(QPalette::Accent, accent);
        palette.setColor(QPalette::HighlightedText, accent.lightnessF() > 0.6 ? Qt::black : Qt::white);
    }
    if (scheme != Qt::ColorScheme::Unknown || accent.isValid())
        app.setPalette(palette);
    app.setOrganizationName(QStringLiteral("WorkshopDL"));
    app.setApplicationName(QStringLiteral("WorkshopDL++"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/workshopdl.png")));
    QTemporaryFile storageProbe(dataRoot() + QStringLiteral("/.write-check-XXXXXX"));
    if (!storageProbe.open()) {
        QMessageBox::critical(nullptr, QStringLiteral("Application folder is read-only"),
                              QStringLiteral("Move the complete WorkshopDL++ folder to a writable location. "
                                             "The application stores its data beside the executable."));
        return 1;
    }
    storageProbe.close();
    storageProbe.remove();
    QLockFile lock(dataRoot() + QStringLiteral("/app.lock"));
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, QStringLiteral("WorkshopDL++"),
                                 QStringLiteral("WorkshopDL++ is already running."));
        return 0;
    }
    ItemModel queue(nullptr, 10000), library;
    Engine engine(&queue, &library);
    std::unique_ptr<Window> window;
    std::unique_ptr<SteamSetupDialog> setup;
    const auto showMainWindow = [&] {
        setup.reset();
        window = std::make_unique<Window>(queue, library, engine);
        window->showMaximized();
        app.setQuitOnLastWindowClosed(true);
    };
    if (engine.needsSteamcmdSetup()) {
        app.setQuitOnLastWindowClosed(false);
        setup = std::make_unique<SteamSetupDialog>(engine);
        QObject::connect(setup.get(), &QDialog::accepted, &app,
                         [&] { QTimer::singleShot(0, &app, showMainWindow); });
        QObject::connect(setup.get(), &QDialog::rejected, &app, &QCoreApplication::quit);
        setup->open();
        QTimer::singleShot(0, &engine, &Engine::prepareSteamcmd);
    } else {
        showMainWindow();
    }
    return app.exec();
}
