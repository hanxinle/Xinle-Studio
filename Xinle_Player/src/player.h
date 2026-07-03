#pragma once

#include "effectregistry.h"
#include "glwidget.h"
#include "ui_player.h"
#include "videodecoder.h"

#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class Player : public QWidget {
    Q_OBJECT

public:
    explicit Player(QWidget *parent = nullptr);
    ~Player();

private slots:
    void onOpenFile();
    void onPlayPause();
    void onVideoTimer();
    void onDecodeError(const QString message);
    void onEffectCheckChanged(Qt::CheckState state);
    void onEffectSliderChanged(int value);
    void onEffectOrderChanged();
    void onProgressPressed();
    void onProgressReleased();
    void onProgressMoved(int value);

private:
    struct EffectControl {
        int registryIndex = -1;
        QListWidgetItem *item = nullptr;
        QCheckBox *checkBox = nullptr;
        QSlider *slider = nullptr;
        QString paramName;  // 第一个 float 参数名，为空表示无 float 参数。
    };

    void initEffectControls();
    void buildEffectChain();
    void setupMaskEffect(QMap<QString, QVariant> &params);
    void updateEffectParam(int row, int sliderValue);
    int chainIndexForListRow(int row) const;
    QString formatTime(double seconds) const;
    void updateTimeLabel(double position, double duration) const;

    Ui::PlayerClass ui;

    GLWidget *m_glWidget = nullptr;
    VideoDecoder *m_decoder = nullptr;
    QTimer *m_videoTimer = nullptr;

    EffectRegistry m_effectRegistry;

    QPushButton *m_openBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QListWidget *m_effectList = nullptr;
    QSlider *m_progressSlider = nullptr;
    QLabel *m_timeLabel = nullptr;

    QVector<EffectControl> m_effectControls;
    QVector<EffectInstance> m_effectChain;
    bool m_seeking = false;
};
