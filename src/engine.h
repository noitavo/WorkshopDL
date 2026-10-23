#pragma once
#include "item_model.h"
#include "storage.h"
#include <QtNetwork>
#include <QtConcurrent>
#include <functional>

struct WorkshopEntry {
    QJsonObject details;
    QStringList children;
    QString error;
    bool collection = false;
};
using WorkshopEntries = QHash<QString, WorkshopEntry>;

class Engine : public QObject {
    Q_OBJECT
  public:
    explicit Engine(ItemModel *queue, ItemModel *library, QObject *parent = nullptr);
    QNetworkAccessManager network;
    QSettings settings;
    QString root, steamExe, username, password;
    bool busy = false, stopAfter = false, checkingUpdates = false;
    int resolving = 0;
    void resolve(const QStringList &ids);
    void start(const QStringList &only = {});
    void cancel();
    void retryCurrent();
    void submitInput(const QString &input);
    void persist();
    void bootstrap();
    void repairSteamcmd();
    void prepareSteamcmd();
    bool needsSteamcmdSetup() const;
    void setSteamcmdPath(const QString &path);
    void preview(const QStringList &ids, QObject *context,
                 std::function<void(QHash<QString, QJsonObject>, QString)> callback);
    void inspectWorkshop(const QStringList &ids, QObject *context,
                         std::function<void(WorkshopEntries)> callback);
    void checkLibrary();
    void reviewDependencies(const QStringList &ids, QObject *context,
                            std::function<void(QMap<QString, QString>, QString)> callback);
    QString diagnostics() const;
    QString stateLoadError() const;
  signals:
    void message(const QString &text);
    void activity(const QString &text);
    void busyChanged(bool busy);
    void authRequired(const QString &prompt);
    void collectionReady(const QString &id, const QString &title, const QStringList &children);
    void storageError(const QString &text);
    void itemSaved(const QString &id);
    void transferStatus(const QString &text);
    void setupProgress(const QString &text);
    void setupDownloadProgress(qint64 received, qint64 total);
    void setupFinished(const QString &error, bool cancelled);

  private:
    ItemModel *queue, *library;
    QProcess process;
    QTimer timeout;
    QTimer retryTimer;
    QString loadError;
    QString active, outputBuffer, lineBuffer, phase, setupZip;
    bool successSeen = false, cancelled = false, bootstrapping = false, authPrompted = false;
    bool steamReady = false, clientReady = false;
    QString steamcmdKey() const;
    bool steamcmdInstalled() const;
    bool passwordSubmitted = false, timedOut = false;
    bool retryRequested = false;
    QTimer transferTimer;
    QElapsedTimer attemptClock;
    qint64 lastOutput = 0, lastBytes = 0, lastSample = 0, transferBytes = -1;
    QString attemptMethod;
    bool sampling = false;
    quint64 attemptGeneration = 0;
    void sampleTransfer();
    void recordAttempt(const QString &result, const QString &error = {});
    QString downloadError() const;
    int setupAttempts = 0;
    void updateSteamcmd();
    void finishSetup(const QString &error = {});
    int attempts = 0;
    QHash<QString, int> retries;
    QSet<QString> pending;
    QStringList runIds;
    QStringList secrets;
    QPointer<QNetworkReply> setupReply;
    QPointer<QNetworkReply> directReply;
    bool triedDirect = false;
    QString directSource;
    int succeededCount = 0, failedCount = 0;
    void directDownload();
    void api(const QString &method, const QUrlQuery &form,
             std::function<void(QJsonObject, QString)> callback);
    void next();
    void launch();
    void readOutput();
    void handleAuthPrompt(const QString &text, bool partial = false);
    void handleLine(const QString &line);
    void finished(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &error, bool retry = false);
    void finishRun();
    void commit();
};
