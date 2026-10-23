QT += core gui widgets network concurrent xml svg
CONFIG += c++20
CONFIG -= debug_and_release
!versionAtLeast(QT_VERSION, 6.10.0):error("WorkshopDL++ requires Qt 6.10 or later.")
!win32:error("WorkshopDL++ requires Windows.")
!msvc:error("WorkshopDL++ requires the MSVC Qt kit.")
!equals(QT_ARCH, x86_64):error("WorkshopDL++ requires an x64 Qt kit.")
RESOURCES += $$PWD/assets/branding.qrc
DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
UI_DIR = $$OUT_PWD/ui
INCLUDEPATH += $$PWD/src
FORMS += \
    $$PWD/forms/aboutdialog.ui \
    $$PWD/forms/accountdialog.ui \
    $$PWD/forms/activationdialog.ui \
    $$PWD/forms/archivecontentsdialog.ui \
    $$PWD/forms/archivedialog.ui \
    $$PWD/forms/bookmarkdialog.ui \
    $$PWD/forms/bookmarksdialog.ui \
    $$PWD/forms/collectiondialog.ui \
    $$PWD/forms/dependenciesdialog.ui \
    $$PWD/forms/gameactivationdialog.ui \
    $$PWD/forms/gettingstarteddialog.ui \
    $$PWD/forms/historydialog.ui \
    $$PWD/forms/installdialog.ui \
    $$PWD/forms/logdialog.ui \
    $$PWD/forms/mainwindow.ui \
    $$PWD/forms/settingsdialog.ui \
    $$PWD/forms/statuspanel.ui \
    $$PWD/forms/steamsetupdialog.ui \
    $$PWD/forms/supportdialog.ui
SOURCES += \
    $$PWD/src/steamsetupdialog.cpp \
    $$PWD/src/storage.cpp \
    $$PWD/src/item_model.cpp \
    $$PWD/src/transfers.cpp \
    $$PWD/src/settings.cpp \
    $$PWD/src/engine.cpp \
    $$PWD/src/window.cpp \
    $$PWD/src/browser.cpp \
    $$PWD/src/dialogs.cpp \
    $$PWD/src/games.cpp \
    $$PWD/src/features.cpp \
    $$PWD/src/gma.cpp \
    $$PWD/src/webview.cpp \
    $$PWD/src/mods.cpp \
    $$PWD/src/metadata.cpp \
    $$PWD/src/menus.cpp \
    $$PWD/src/window_layout.cpp \
    $$PWD/src/queue_actions.cpp \
    $$PWD/src/library_filter.cpp \
    $$PWD/src/maintenance.cpp \
    $$PWD/src/icons.cpp \
    $$PWD/src/bookmarks.cpp
HEADERS += \
    $$PWD/src/item_model.h \
    $$PWD/src/storage.h \
    $$PWD/src/games.h \
    $$PWD/src/gma.h \
    $$PWD/src/engine.h \
    $$PWD/src/window.h \
    $$PWD/src/steamsetupdialog.h \
    $$PWD/src/webview.h \
    $$PWD/src/library_filter.h \
    $$PWD/src/icons.h
INCLUDEPATH += $$PWD/third_party/webview2
win32 {
    LIBS += -luser32 -ladvapi32 -lole32 -loleaut32 -lversion -lshlwapi $$PWD/third_party/webview2/WebView2LoaderStatic.lib
    QMAKE_CXXFLAGS += /utf-8 /MP4
}
