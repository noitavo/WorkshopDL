TEMPLATE = app
TARGET = WorkshopDL
VERSION = 1.0.0
QMAKE_TARGET_PRODUCT = WorkshopDL++
QMAKE_TARGET_DESCRIPTION = Steam Workshop downloader
include(application.pri)
RC_ICONS = $$PWD/assets/workshopdl.ico
SOURCES += $$PWD/src/main.cpp
CONFIG(debug, debug|release): DEPLOY_MODE = debug
else: DEPLOY_MODE = release
QMAKE_POST_LINK = call $$shell_quote($$shell_path($$PWD/deploy.bat)) $$shell_quote($$shell_path($$[QT_INSTALL_BINS])) $$shell_quote($$shell_path($$DESTDIR/$${TARGET}.exe)) $$DEPLOY_MODE
