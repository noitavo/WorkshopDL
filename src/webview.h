#pragma once
#include <QtWidgets>
#include <memory>
struct BrowserLoadInfo {
    enum class LoadStatus { Started, Succeeded, Failed };
    LoadStatus state;
    QUrl address;
    QString error;
    LoadStatus status() const {
        return state;
    }
    QUrl url() const {
        return address;
    }
    QString errorString() const {
        return error;
    }
};
class BrowserView final : public QWidget {
    Q_OBJECT
  public:
    explicit BrowserView(QWidget *parent = nullptr);
    ~BrowserView() override;
    QUrl url() const;
    bool canGoBack() const;
    bool canGoForward() const;
    bool isLoading() const;
    void setUrl(const QUrl &url);
    void goBack();
    void goForward();
    void reload();
    void stop();
    void deleteAllCookies();
    void setFitWidth(bool enabled);
    void setZoom(double value);
    double zoom() const;
  signals:
    void urlChanged(const QUrl &url);
    void titleChanged(const QString &title);
    void loadingChanged(const BrowserLoadInfo &info);
    void zoomChanged(double zoom);
    void fitWidthChanged(bool enabled);

  protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void focusInEvent(QFocusEvent *) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void initialize();
    void bounds();
};
