@echo off
setlocal
set "QT_BIN=%~1"
set "APP_EXE=%~2"
set "BUILD_MODE=%~3"
if not exist "%QT_BIN%\windeployqt.exe" exit /b 1
if not exist "%APP_EXE%" exit /b 1
if /I not "%BUILD_MODE%"=="debug" if /I not "%BUILD_MODE%"=="release" exit /b 1
set "PATH=%QT_BIN%;%PATH%"
"%QT_BIN%\windeployqt.exe" --%BUILD_MODE% --no-translations --compiler-runtime "%APP_EXE%" || exit /b 1
set "LICENSE_DIR=%~dp2licenses"
if not exist "%LICENSE_DIR%" mkdir "%LICENSE_DIR%"
if exist "%QT_BIN%\..\..\..\Licenses\LICENSE" copy /Y "%QT_BIN%\..\..\..\Licenses\LICENSE" "%LICENSE_DIR%\Qt.txt" >nul || exit /b 1
copy /Y "%~dp0assets\material\LICENSE.txt" "%LICENSE_DIR%\Material-Icons.txt" >nul || exit /b 1
copy /Y "%~dp0LICENSE" "%LICENSE_DIR%\WorkshopDL.txt" >nul || exit /b 1
copy /Y "%~dp0third_party\webview2\LICENSE.txt" "%LICENSE_DIR%\WebView2.txt" >nul || exit /b 1
copy /Y "%~dp0third_party\webview2\NOTICE.txt" "%LICENSE_DIR%\WebView2-Notice.txt" >nul || exit /b 1
exit /b 0
