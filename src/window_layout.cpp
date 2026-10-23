#include "window.h"

QString Window::layoutSettingsKey(int index) const {
    if (!engine.settings.value(QStringLiteral("separateWindowSizes"), false).toBool())
        return QStringLiteral("sharedWindowGeometry");
    return index == 0 ? QStringLiteral("browserWindowGeometry") : QStringLiteral("toolsWindowGeometry");
}

void Window::saveTabLayout() {
    if (activeLayoutKey.isEmpty() || isMinimized())
        return;
    const auto geometry = saveGeometry();
    sessionLayouts.insert(activeLayoutKey, geometry);
    if (engine.settings.value(QStringLiteral("rememberLayout"), true).toBool())
        engine.settings.setValue(activeLayoutKey, geometry);
}

void Window::restoreTabLayout(int index) {
    activeLayoutKey = layoutSettingsKey(index);
    auto geometry = sessionLayouts.value(activeLayoutKey);
    if (geometry.isEmpty() && engine.settings.value(QStringLiteral("rememberLayout"), true).toBool())
        geometry = engine.settings.value(activeLayoutKey).toByteArray();
    if (!geometry.isEmpty() && restoreGeometry(geometry))
        return;
    setWindowState(windowState() & ~(Qt::WindowMaximized | Qt::WindowMinimized));
    const QRect available = screen()->availableGeometry();
    resize(QSize(1100, 760).boundedTo(available.size()));
    move(available.center() - rect().center());
    setWindowState(windowState() | Qt::WindowMaximized);
}
