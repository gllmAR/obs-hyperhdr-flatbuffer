// SPDX-License-Identifier: GPL-2.0-or-later
// Tools dialog for the main output (SDD D-7). Qt Widgets, optional build.
#pragma once

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QShowEvent;
class QSpinBox;
class QTimer;

namespace hhd_ui {

class MainOutputDialog : public QDialog {
    Q_OBJECT

public:
    explicit MainOutputDialog(QWidget* parent);
    ~MainOutputDialog() override = default;

    // Reloads widgets from the current settings. Call each time the dialog opens.
    void loadFromSettings();

protected:
    // The status poll only runs while the dialog is visible.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void apply();
    void refreshStatus();

private:
    QCheckBox* enabled_;
    QLineEdit* origin_;
    QSpinBox* priority_;
    QSpinBox* width_;
    QSpinBox* height_;
    QDoubleSpinBox* fps_;
    QCheckBox* flip_;
    QCheckBox* preferDomainSocket_;
    QLineEdit* domainSocketPath_;
    QLineEdit* host_;
    QSpinBox* port_;
    QLabel* status_;
    QTimer* statusTimer_;
};

// Creates the dialog on first call, then shows and raises it.
void showMainOutputDialog();

}  // namespace hhd_ui
