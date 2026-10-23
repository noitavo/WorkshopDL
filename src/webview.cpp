#include "webview.h"
#include "storage.h"
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
struct BrowserView::Impl {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> core;
    QUrl address;
    bool initializing = false, loading = false, fit = true;
    double scale = 1;
    bool ole = false;
};
BrowserView::BrowserView(QWidget *parent) : QWidget(parent), d(std::make_unique<Impl>()) {
    d->ole = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
}
BrowserView::~BrowserView() {
    if (d->controller)
        d->controller->Close();
    d->core.Reset();
    d->controller.Reset();
    if (d->ole)
        CoUninitialize();
}
QUrl BrowserView::url() const {
    return d->address;
}
bool BrowserView::canGoBack() const {
    BOOL b = FALSE;
    if (d->core)
        d->core->get_CanGoBack(&b);
    return b;
}
bool BrowserView::canGoForward() const {
    BOOL b = FALSE;
    if (d->core)
        d->core->get_CanGoForward(&b);
    return b;
}
bool BrowserView::isLoading() const {
    return d->loading;
}
double BrowserView::zoom() const {
    return d->scale;
}
void BrowserView::setUrl(const QUrl &url) {
    d->address = url;
    if (d->core)
        d->core->Navigate(url.toString().toStdWString().c_str());
}
void BrowserView::goBack() {
    if (d->core)
        d->core->GoBack();
}
void BrowserView::goForward() {
    if (d->core)
        d->core->GoForward();
}
void BrowserView::reload() {
    if (d->core)
        d->core->Reload();
    else if (!d->initializing)
        initialize();
}
void BrowserView::stop() {
    if (d->core)
        d->core->Stop();
}
void BrowserView::deleteAllCookies() {
    ComPtr<ICoreWebView2_2> v;
    if (d->core && SUCCEEDED(d->core.As(&v))) {
        ComPtr<ICoreWebView2CookieManager> cookies;
        v->get_CookieManager(&cookies);
        if (cookies)
            cookies->DeleteAllCookies();
    }
}
void BrowserView::setFitWidth(bool on) {
    d->fit = on;
    bounds();
}
void BrowserView::setZoom(double value) {
    d->scale = qBound(0.5, value, 2.0);
    if (d->controller)
        d->controller->put_ZoomFactor(d->scale);
    emit zoomChanged(d->scale);
}
void BrowserView::bounds() {
    if (!d->controller)
        return;
    RECT rect{};
    GetClientRect(reinterpret_cast<HWND>(winId()), &rect);
    d->controller->put_Bounds(rect);
    d->controller->NotifyParentWindowPositionChanged();
    if (d->fit) {
        const auto host = d->address.host();
        const bool steam =
            host == QStringLiteral("steamcommunity.com") || host == QStringLiteral("www.steamcommunity.com");
        setZoom(steam ? qBound(0.65, double(width()) / 1050.0, 1.0) : 1.0);
    }
}
void BrowserView::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (!d->initializing)
        initialize();
    if (d->controller)
        d->controller->put_IsVisible(TRUE);
    bounds();
}
void BrowserView::hideEvent(QHideEvent *e) {
    if (d->controller)
        d->controller->put_IsVisible(FALSE);
    QWidget::hideEvent(e);
}
void BrowserView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    bounds();
}
void BrowserView::focusInEvent(QFocusEvent *event) {
    QWidget::focusInEvent(event);
    if (d->controller) {
        const auto reason =
            event->reason() == Qt::BacktabFocusReason ? COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS
            : event->reason() == Qt::TabFocusReason   ? COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT
                                                      : COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC;
        d->controller->MoveFocus(reason);
    }
}
void BrowserView::initialize() {
    d->initializing = true;
    QPointer<BrowserView> self(this);
    const auto profile = QDir::toNativeSeparators(dataRoot() + QStringLiteral("/WebView2")).toStdWString();
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, profile.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([self](HRESULT result,
                                                                                    ICoreWebView2Environment
                                                                                        *env) -> HRESULT {
            if (!self)
                return S_OK;
            if (FAILED(result) || !env) {
                self->d->initializing = false;
                emit self->loadingChanged({BrowserLoadInfo::LoadStatus::Failed, self->url(),
                                           QStringLiteral("WebView2 Runtime could not start.")});
                return S_OK;
            }
            const HRESULT created = env->CreateCoreWebView2Controller(
                reinterpret_cast<HWND>(self->winId()),
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [self](HRESULT error, ICoreWebView2Controller *controller) -> HRESULT {
                        if (!self) {
                            if (controller)
                                controller->Close();
                            return S_OK;
                        }
                        if (FAILED(error) || !controller) {
                            self->d->initializing = false;
                            emit self->loadingChanged({BrowserLoadInfo::LoadStatus::Failed, self->url(),
                                                       QStringLiteral("Cannot create WebView2 controller.")});
                            return S_OK;
                        }
                        auto &d = *self->d;
                        d.controller = controller;
                        controller->get_CoreWebView2(&d.core);
                        if (!d.core) {
                            controller->Close();
                            d.controller.Reset();
                            d.initializing = false;
                            emit self->loadingChanged(
                                {BrowserLoadInfo::LoadStatus::Failed, self->url(),
                                 QStringLiteral("WebView2 is unavailable. Reload to retry.")});
                            return S_OK;
                        }
                        EventRegistrationToken token;
                        controller->add_MoveFocusRequested(
                            Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
                                [self](ICoreWebView2Controller *,
                                       ICoreWebView2MoveFocusRequestedEventArgs *args) -> HRESULT {
                                    if (!self)
                                        return S_OK;
                                    COREWEBVIEW2_MOVE_FOCUS_REASON reason;
                                    args->get_Reason(&reason);
                                    args->put_Handled(self->focusNextPrevChild(
                                        reason != COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS));
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        d.core->add_NavigationStarting(
                            Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [self](ICoreWebView2 *,
                                       ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                                    if (!self)
                                        return S_OK;
                                    LPWSTR value = nullptr;
                                    args->get_Uri(&value);
                                    const QUrl url(QString::fromWCharArray(value ? value : L""));
                                    CoTaskMemFree(value);
                                    if (url.scheme() != QStringLiteral("https") &&
                                        url.scheme() != QStringLiteral("http") &&
                                        url != QUrl(QStringLiteral("about:blank"))) {
                                        args->put_Cancel(TRUE);
                                        return S_OK;
                                    }
                                    self->d->address = url;
                                    self->d->loading = true;
                                    self->bounds();
                                    emit self->urlChanged(url);
                                    emit self->loadingChanged(
                                        {BrowserLoadInfo::LoadStatus::Started, url, {}});
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        d.core->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [self](ICoreWebView2 *,
                                       ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                    if (!self)
                                        return S_OK;
                                    BOOL success;
                                    args->get_IsSuccess(&success);
                                    self->d->loading = false;
                                    emit self->loadingChanged(
                                        {success ? BrowserLoadInfo::LoadStatus::Succeeded
                                                 : BrowserLoadInfo::LoadStatus::Failed,
                                         self->url(),
                                         success
                                             ? QString()
                                             : QStringLiteral(
                                                   "Navigation failed. Check the connection and reload.")});
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        d.core->add_SourceChanged(
                            Callback<ICoreWebView2SourceChangedEventHandler>(
                                [self](ICoreWebView2 *core,
                                       ICoreWebView2SourceChangedEventArgs *) -> HRESULT {
                                    if (!self)
                                        return S_OK;
                                    LPWSTR value = nullptr;
                                    core->get_Source(&value);
                                    self->d->address = QUrl(QString::fromWCharArray(value ? value : L""));
                                    CoTaskMemFree(value);
                                    emit self->urlChanged(self->url());
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        d.core->add_DocumentTitleChanged(
                            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                [self](ICoreWebView2 *core, IUnknown *) -> HRESULT {
                                    if (!self)
                                        return S_OK;
                                    LPWSTR value = nullptr;
                                    core->get_DocumentTitle(&value);
                                    emit self->titleChanged(QString::fromWCharArray(value ? value : L""));
                                    CoTaskMemFree(value);
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        d.core->add_HistoryChanged(
                            Callback<ICoreWebView2HistoryChangedEventHandler>([self](ICoreWebView2 *,
                                                                                     IUnknown *) -> HRESULT {
                                if (self)
                                    emit self->urlChanged(self->url());
                                return S_OK;
                            }).Get(),
                            &token);
                        d.core->add_NewWindowRequested(
                            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [self](ICoreWebView2 *,
                                       ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                                    if (!self)
                                        return S_OK;
                                    LPWSTR value = nullptr;
                                    args->get_Uri(&value);
                                    const QUrl url(QString::fromWCharArray(value ? value : L""));
                                    CoTaskMemFree(value);
                                    args->put_Handled(TRUE);
                                    if (url.scheme() == QStringLiteral("https") ||
                                        url.scheme() == QStringLiteral("http"))
                                        self->setUrl(url);
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        d.controller->add_ZoomFactorChanged(
                            Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                                [self](ICoreWebView2Controller *c, IUnknown *) -> HRESULT {
                                    if (self) {
                                        double scale;
                                        c->get_ZoomFactor(&scale);
                                        if (qAbs(self->d->scale - scale) > 0.005 && self->d->fit) {
                                            self->d->fit = false;
                                            emit self->fitWidthChanged(false);
                                        }
                                        self->d->scale = scale;
                                        emit self->zoomChanged(scale);
                                    }
                                    return S_OK;
                                })
                                .Get(),
                            &token);
                        controller->put_IsVisible(self->isVisible());
                        self->setZoom(d.scale);
                        self->bounds();
                        if (!self->url().isEmpty())
                            self->setUrl(self->url());
                        return S_OK;
                    })
                    .Get());
            if (FAILED(created)) {
                self->d->initializing = false;
                emit self->loadingChanged({BrowserLoadInfo::LoadStatus::Failed, self->url(),
                                           QStringLiteral("Cannot create WebView2. Reload to retry.")});
            }
            return S_OK;
        }).Get());
    if (FAILED(hr)) {
        d->initializing = false;
        emit loadingChanged(
            {BrowserLoadInfo::LoadStatus::Failed, url(), QStringLiteral("WebView2 initialization failed.")});
    }
}
