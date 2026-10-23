#pragma once

#include <QDialog>
#include <memory>

class Engine;
namespace Ui {
class SteamSetupDialog;
}

class SteamSetupDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit SteamSetupDialog(Engine &engine, QWidget *parent = nullptr);
    ~SteamSetupDialog() override;
    void setStatus(const QString &text);
    void reject() override;

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    std::unique_ptr<Ui::SteamSetupDialog> ui;
    Engine &engine;
    bool cancelling = false;
    bool centered = false;
    void centerOnScreen();
    void retry();
};
