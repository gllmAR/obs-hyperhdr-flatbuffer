// SPDX-License-Identifier: GPL-2.0-or-later
// Tools dialog for the main output: persistent enable switch, stream
// parameters, endpoint, and live sink status (FR-7, FR-9).

#include "hhd-dialog.h"

#include "hhd_glue.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#define T_(s) obs_module_text(s)

namespace hhd_ui {
namespace {

QPointer<MainOutputDialog> g_dialog;

const char* stateText(hhd::SinkState state) {
    switch (state) {
    case hhd::SinkState::Idle:
        return T_("Dialog.State.Idle");
    case hhd::SinkState::Connecting:
        return T_("Dialog.State.Connecting");
    case hhd::SinkState::Registered:
        return T_("Dialog.State.Registered");
    case hhd::SinkState::Streaming:
        return T_("Dialog.State.Streaming");
    case hhd::SinkState::Backoff:
        return T_("Dialog.State.Backoff");
    case hhd::SinkState::Stopped:
        return T_("Dialog.State.Stopped");
    }
    return "?";
}

}  // namespace

MainOutputDialog::MainOutputDialog(QWidget* parent)
    : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint |
                          Qt::WindowCloseButtonHint) {
    setWindowTitle(T_("Dialog.Title"));
    setMinimumWidth(420);

    auto* mainBox = new QVBoxLayout(this);

    enabled_ = new QCheckBox(T_("Dialog.Enable"), this);
    mainBox->addWidget(enabled_);

    auto* streamGroup = new QGroupBox(T_("Dialog.StreamGroup"), this);
    auto* streamForm = new QFormLayout(streamGroup);
    origin_ = new QLineEdit(streamGroup);
    priority_ = new QSpinBox(streamGroup);
    priority_->setRange(0, 254);
    width_ = new QSpinBox(streamGroup);
    width_->setRange(8, 256);
    height_ = new QSpinBox(streamGroup);
    height_->setRange(8, 256);
    fps_ = new QDoubleSpinBox(streamGroup);
    fps_->setRange(0.0, 60.0);
    fps_->setDecimals(1);
    fps_->setSingleStep(1.0);
    flip_ = new QCheckBox(T_("Dialog.Flip"), streamGroup);
    streamForm->addRow(T_("Dialog.Origin"), origin_);
    streamForm->addRow(T_("Dialog.Priority"), priority_);
    streamForm->addRow(T_("Dialog.Width"), width_);
    streamForm->addRow(T_("Dialog.Height"), height_);
    streamForm->addRow(T_("Dialog.MaxFps"), fps_);
    streamForm->addRow(QString(), flip_);
    mainBox->addWidget(streamGroup);

    auto* endpointGroup = new QGroupBox(T_("Dialog.Endpoint"), this);
    auto* endpointForm = new QFormLayout(endpointGroup);
    preferDomainSocket_ = new QCheckBox(T_("Dialog.PreferDomainSocket"), endpointGroup);
    domainSocketPath_ = new QLineEdit(endpointGroup);
    host_ = new QLineEdit(endpointGroup);
    port_ = new QSpinBox(endpointGroup);
    port_->setRange(1, 65535);
    endpointForm->addRow(QString(), preferDomainSocket_);
    endpointForm->addRow(T_("Dialog.DomainSocketPath"), domainSocketPath_);
    endpointForm->addRow(T_("Dialog.Host"), host_);
    endpointForm->addRow(T_("Dialog.Port"), port_);
    mainBox->addWidget(endpointGroup);

    status_ = new QLabel(this);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setWordWrap(true);
    mainBox->addWidget(status_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply |
                                 QDialogButtonBox::Cancel,
                             this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &MainOutputDialog::apply);
    mainBox->addWidget(buttons);

    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, this, &MainOutputDialog::refreshStatus);
}

void MainOutputDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    refreshStatus();
    statusTimer_->start();
}

void MainOutputDialog::hideEvent(QHideEvent* event) {
    statusTimer_->stop();
    QDialog::hideEvent(event);
}

void MainOutputDialog::loadFromSettings() {
    const hhd_settings_data& s = hhd_settings();
    enabled_->setChecked(s.main.enabled);
    origin_->setText(QString::fromStdString(s.main.origin));
    priority_->setValue(s.main.priority);
    width_->setValue(s.main.width);
    height_->setValue(s.main.height);
    fps_->setValue(s.main.max_fps);
    flip_->setChecked(s.main.flip_vertical);
    preferDomainSocket_->setChecked(s.endpoint.preferDomainSocket);
    domainSocketPath_->setText(QString::fromStdString(s.endpoint.domainSocketPath));
    host_->setText(QString::fromStdString(s.endpoint.host));
    port_->setValue(s.endpoint.port);
    refreshStatus();
}

void MainOutputDialog::apply() {
    hhd_settings_data& s = hhd_settings();
    s.main.enabled = enabled_->isChecked();
    s.main.origin = origin_->text().trimmed().toStdString();
    s.main.priority = priority_->value();
    s.main.width = width_->value();
    s.main.height = height_->value();
    s.main.max_fps = fps_->value();
    s.main.flip_vertical = flip_->isChecked();
    s.endpoint.preferDomainSocket = preferDomainSocket_->isChecked();
    s.endpoint.domainSocketPath = domainSocketPath_->text().trimmed().toStdString();
    s.endpoint.host = host_->text().trimmed().toStdString();
    if (s.endpoint.host.empty()) s.endpoint.host = "127.0.0.1";
    s.endpoint.port = port_->value();

    hhd_settings_save();
    // Starts or stops the output on an enabled change; restarts it when the
    // parameters changed while running.
    hhd_main_output_apply();
    refreshStatus();
}

void MainOutputDialog::refreshStatus() {
    hhd::SinkState state;
    std::string error;
    QString text;
    if (hhd_main_output_sink_status(&state, &error)) {
        text = QString(T_("Dialog.StatusActive")).arg(stateText(state));
        if (!error.empty()) text += QString("\n%1").arg(error.c_str());
    } else {
        text = T_("Dialog.StatusInactive");
    }
    status_->setText(text);
}

void showMainOutputDialog() {
    if (!g_dialog) {
        obs_frontend_push_ui_translation(obs_module_get_string);
        g_dialog =
            new MainOutputDialog(static_cast<QWidget*>(obs_frontend_get_main_window()));
        obs_frontend_pop_ui_translation();
    }
    g_dialog->loadFromSettings();
    g_dialog->show();
    g_dialog->raise();
    g_dialog->activateWindow();
}

}  // namespace hhd_ui
