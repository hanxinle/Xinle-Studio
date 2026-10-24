#include "player.h"

#include <QDebug>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QVector>

Player::Player(QWidget *parent) : QWidget(parent) {
    ui.setupUi(this);

    setWindowTitle(QStringLiteral("Xinle Studio"));
    resize(960, 640);

    // 加载特效配置。
    QString effectsPath = QStringLiteral(":/Player/effects/effects.json");
    if (!m_effectRegistry.load(effectsPath)) {
        qWarning() << "[Player] failed to load effects registry:"
                   << m_effectRegistry.lastError();
    }

    // 主布局。
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // OpenGL 视频显示区。
    m_glWidget = new GLWidget(this);
    mainLayout->addWidget(m_glWidget, 1);

    // 控制按钮与进度条。
    auto *controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(8);

    m_openBtn = new QPushButton(QStringLiteral("打开文件"), this);
    m_playBtn = new QPushButton(QStringLiteral("播放"), this);
    m_playBtn->setEnabled(false);

    m_progressSlider = new QSlider(Qt::Horizontal, this);
    m_progressSlider->setRange(0, 0);
    m_progressSlider->setEnabled(false);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    m_timeLabel->setMinimumWidth(110);

    controlLayout->addWidget(m_openBtn);
    controlLayout->addWidget(m_playBtn);
    controlLayout->addWidget(m_progressSlider, 1);
    controlLayout->addWidget(m_timeLabel);

    mainLayout->addLayout(controlLayout);

    // 特效控制。
    auto *effectLayout = new QHBoxLayout();
    effectLayout->setSpacing(8);

    effectLayout->addWidget(new QLabel(QStringLiteral("特效:"), this));
    m_effectList = new QListWidget(this);
    m_effectList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_effectList->setDragEnabled(true);
    m_effectList->setAcceptDrops(true);
    m_effectList->setDragDropMode(QAbstractItemView::InternalMove);
    effectLayout->addWidget(m_effectList, 1);

    mainLayout->addLayout(effectLayout);

    // 解码器。
    m_decoder = new VideoDecoder(this);
    connect(m_decoder, &VideoDecoder::decodeError, this,
            &Player::onDecodeError);
    // 循环播放回到开头时在主线程重置声卡(QAudioSink 属于主线程)。
    connect(m_decoder, &VideoDecoder::audioResetRequested, this,
            [this] { m_decoder->resetAudioOutput(); });

    // 视频显示定时器. 固定 10ms, 由解码器按音频主时钟决定取哪帧。
    m_videoTimer = new QTimer(this);
    m_videoTimer->setTimerType(Qt::PreciseTimer);
    m_videoTimer->setInterval(10);
    connect(m_videoTimer, &QTimer::timeout, this, &Player::onVideoTimer);

    connect(m_openBtn, &QPushButton::clicked, this, &Player::onOpenFile);
    connect(m_playBtn, &QPushButton::clicked, this, &Player::onPlayPause);
    connect(m_effectList->model(), &QAbstractItemModel::rowsMoved, this,
            &Player::onEffectOrderChanged);
    connect(m_progressSlider, &QSlider::sliderPressed, this,
            &Player::onProgressPressed);
    connect(m_progressSlider, &QSlider::sliderReleased, this,
            &Player::onProgressReleased);
    connect(m_progressSlider, &QSlider::sliderMoved, this,
            &Player::onProgressMoved);

    // 初始化特效列表（每个特效自带复选框与滑块）。
    initEffectControls();
}

Player::~Player() {
    if (m_videoTimer) {
        m_videoTimer->stop();
    }
    if (m_decoder) {
        m_decoder->close();
    }
}

void Player::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开视频文件"), QString(),
        QStringLiteral("视频文件 (*.mp4 *.avi *.mkv *.mov);;所有文件 (*.*)"));

    if (filePath.isEmpty()) {
        return;
    }

    m_videoTimer->stop();
    if (m_decoder->isRunning()) {
        m_decoder->close();
    }

    if (!m_decoder->open(filePath)) {
        m_playBtn->setEnabled(false);
        m_progressSlider->setEnabled(false);
        m_progressSlider->setRange(0, 0);
        m_timeLabel->setText(QStringLiteral("00:00 / 00:00"));
        return;
    }

    int durationMs = static_cast<int>(m_decoder->duration() * 1000.0);
    m_progressSlider->setRange(0, qMax(0, durationMs));
    m_progressSlider->setValue(0);
    m_progressSlider->setEnabled(durationMs > 0);
    updateTimeLabel(0.0, m_decoder->duration());

    m_videoTimer->start();

    // 解出第一帧作为封面, 不用点播放即可预览。
    QByteArray coverData;
    int coverWidth = 0;
    int coverHeight = 0;
    if (m_decoder->grabFirstFrame(coverData, coverWidth, coverHeight)) {
        m_glWidget->setVideoFrame(coverData, coverWidth, coverHeight);
    }

    // 重新应用当前选中的特效链。
    buildEffectChain();

    m_playBtn->setEnabled(true);
    m_playBtn->setText(QStringLiteral("播放"));
}

void Player::onPlayPause() {
    if (!m_decoder) {
        return;
    }

    if (m_decoder->isPlaying()) {
        m_decoder->pause();
        m_videoTimer->stop();
        m_playBtn->setText(QStringLiteral("播放"));
    } else {
        m_decoder->play();
        if (!m_videoTimer->isActive()) {
            m_videoTimer->start();
        }
        m_playBtn->setText(QStringLiteral("暂停"));
    }
}

void Player::onVideoTimer() {
    QByteArray rgbaData;
    int width = 0;
    int height = 0;
    // 以音频主时钟决定显示哪一帧(太早等, 太晚丢)。
    if (m_decoder->getDisplayFrame(m_decoder->masterClock(), rgbaData, width,
                                   height)) {
        m_glWidget->setVideoFrame(rgbaData, width, height);
    }

    if (!m_seeking && m_decoder->duration() > 0.0) {
        // 进度条跟随主时钟, 比视频帧 pts 更平滑。
        double position = m_decoder->masterClock();
        int positionMs = static_cast<int>(position * 1000.0);
        m_progressSlider->setValue(
            qBound(0, positionMs, m_progressSlider->maximum()));
        updateTimeLabel(position, m_decoder->duration());
    }
}

void Player::onDecodeError(const QString message) {
    qWarning() << "[Player] decode error:" << message;
}

void Player::initEffectControls() {
    m_effectControls.clear();

    for (int i = 0; i < m_effectRegistry.effects().size(); ++i) {
        const EffectDef &def = m_effectRegistry.effects()[i];
        if (def.frag.isEmpty()) {
            continue;
        }

        // 查找第一个 float 参数。
        const EffectParamDef *floatParam = nullptr;
        for (const EffectParamDef &param : def.params) {
            if (param.type == QStringLiteral("float")) {
                floatParam = &param;
                break;
            }
        }

        auto *item = new QListWidgetItem();
        item->setFlags(item->flags() | Qt::ItemIsEnabled |
                       Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setData(Qt::UserRole, i);

        auto *widget = new QWidget(m_effectList);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(8);

        auto *checkBox = new QCheckBox(def.name, widget);
        auto *slider = new QSlider(Qt::Horizontal, widget);
        if (floatParam) {
            slider->setRange(static_cast<int>(floatParam->min),
                              static_cast<int>(floatParam->max));
            slider->setValue(floatParam->defaultValue.toInt());
        }
        slider->setEnabled(false);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        layout->addWidget(checkBox);
        layout->addWidget(slider, 1);

        m_effectList->addItem(item);
        m_effectList->setItemWidget(item, widget);
        // 让 item 高度适应 widget。
        item->setSizeHint(widget->sizeHint());

        EffectControl ctrl;
        ctrl.registryIndex = i;
        ctrl.item = item;
        ctrl.checkBox = checkBox;
        ctrl.slider = slider;
        if (floatParam) {
            ctrl.paramName = floatParam->name;
        }
        m_effectControls.append(ctrl);

        connect(checkBox, &QCheckBox::checkStateChanged, this,
                &Player::onEffectCheckChanged);
        connect(slider, &QSlider::valueChanged, this,
                &Player::onEffectSliderChanged);
    }
}

void Player::onEffectCheckChanged(Qt::CheckState /*state*/) {
    auto *checkBox = qobject_cast<QCheckBox *>(sender());
    if (!checkBox) {
        return;
    }

    // 找到对应的 EffectControl。
    for (EffectControl &ctrl : m_effectControls) {
        if (ctrl.checkBox == checkBox) {
            ctrl.slider->setEnabled(checkBox->isChecked());
            break;
        }
    }

    buildEffectChain();
}

void Player::onEffectSliderChanged(int value) {
    auto *slider = qobject_cast<QSlider *>(sender());
    if (!slider) {
        return;
    }

    const EffectControl *ctrl = nullptr;
    for (const EffectControl &c : m_effectControls) {
        if (c.slider == slider) {
            ctrl = &c;
            break;
        }
    }
    if (!ctrl) {
        return;
    }

    int row = m_effectList->row(ctrl->item);
    if (row < 0) {
        return;
    }

    updateEffectParam(row, value);
}

void Player::onEffectOrderChanged() {
    buildEffectChain();
}

void Player::buildEffectChain() {
    m_effectChain.clear();

    for (int row = 0; row < m_effectList->count(); ++row) {
        const EffectControl *ctrl = nullptr;
        for (const EffectControl &c : m_effectControls) {
            if (c.item == m_effectList->item(row)) {
                ctrl = &c;
                break;
            }
        }
        if (!ctrl || !ctrl->checkBox->isChecked()) {
            continue;
        }

        int registryIndex = ctrl->registryIndex;
        if (registryIndex < 0 || registryIndex >= m_effectRegistry.effects().size()) {
            continue;
        }

        const EffectDef &def = m_effectRegistry.effects()[registryIndex];
        if (def.frag.isEmpty()) {
            continue;
        }

        EffectInstance instance;
        instance.name = def.name;
        instance.fragPath = def.frag;
        instance.iterations = qMax(1, def.iterations);

        for (const EffectParamDef &param : def.params) {
            instance.params[param.name] = param.defaultValue;
        }

        // 用滑块当前值覆盖第一个 float 参数默认值。
        if (!ctrl->paramName.isEmpty() && ctrl->slider) {
            instance.params[ctrl->paramName] =
                static_cast<float>(ctrl->slider->value());
        }

        // 遮罩特殊初始化。
        if (def.setup == QStringLiteral("mask")) {
            setupMaskEffect(instance.params);
            // 如果滑块有值，覆盖 mask_blur。
            if (!ctrl->paramName.isEmpty() && ctrl->slider) {
                instance.params[ctrl->paramName] =
                    static_cast<float>(ctrl->slider->value());
            }
        }

        m_effectChain.append(instance);
    }

    m_glWidget->setEffectChain(m_effectChain);
}

void Player::updateEffectParam(int row, int sliderValue) {
    if (row < 0 || row >= m_effectControls.size()) {
        return;
    }

    const EffectControl &ctrl = m_effectControls[row];
    if (!ctrl.checkBox->isChecked() || ctrl.paramName.isEmpty()) {
        return;
    }

    int chainIndex = chainIndexForListRow(row);
    if (chainIndex < 0 || chainIndex >= m_effectChain.size()) {
        return;
    }

    m_effectChain[chainIndex].params[ctrl.paramName] =
        static_cast<float>(sliderValue);
    m_glWidget->setEffectParam(chainIndex, ctrl.paramName,
                               static_cast<float>(sliderValue));

    // 遮罩的羽化也随滑块变化。
    const EffectDef &def = m_effectRegistry.effects()[ctrl.registryIndex];
    if (def.setup == QStringLiteral("mask")) {
        m_effectChain[chainIndex].params[QStringLiteral("mask_blur")] =
            static_cast<float>(sliderValue);
        m_glWidget->setEffectParam(chainIndex, QStringLiteral("mask_blur"),
                                   static_cast<float>(sliderValue));
    }
}

int Player::chainIndexForListRow(int row) const {
    int chainIndex = -1;
    for (int i = 0; i <= row; ++i) {
        QListWidgetItem *item = m_effectList->item(i);
        const EffectControl *ctrl = nullptr;
        for (const EffectControl &c : m_effectControls) {
            if (c.item == item) {
                ctrl = &c;
                break;
            }
        }
        if (ctrl && ctrl->checkBox->isChecked()) {
            ++chainIndex;
        }
    }
    return chainIndex;
}

void Player::onProgressPressed() {
    m_seeking = true;
}

void Player::onProgressReleased() {
    m_seeking = false;

    if (!m_decoder || m_decoder->duration() <= 0.0) {
        return;
    }

    double seconds = static_cast<double>(m_progressSlider->value()) / 1000.0;
    m_decoder->seek(seconds);
    // 清掉声卡里积压的旧音频, 否则 seek 后还会放几百毫秒旧声音。
    m_decoder->resetAudioOutput();
    updateTimeLabel(seconds, m_decoder->duration());
}

void Player::onProgressMoved(int value) {
    if (!m_decoder) {
        return;
    }

    double seconds = static_cast<double>(value) / 1000.0;
    updateTimeLabel(seconds, m_decoder->duration());
}

QString Player::formatTime(double seconds) const {
    int total = qMax(0, static_cast<int>(seconds + 0.5));
    int minutes = total / 60;
    int secs = total % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

void Player::updateTimeLabel(double position, double duration) const {
    m_timeLabel->setText(QStringLiteral("%1 / %2")
                              .arg(formatTime(position))
                              .arg(formatTime(duration)));
}

void Player::setupMaskEffect(QMap<QString, QVariant> &params) {
    // 示例：用一个三角形遮罩覆盖画面中心区域。
    float cx = 0.5f;
    float cy = 0.5f;
    float size = 0.3f;

    QVector<float> points;
    points << cx << cy - size;
    points << cx - size << cy + size;
    points << cx + size << cy + size;

    params[QStringLiteral("numPoints")] = points.size() / 2;
    params[QStringLiteral("isHasChromakey")] = false;

    QVariantList pointDataList;
    for (float v : points) {
        pointDataList.append(v);
    }
    params[QStringLiteral("pointData")] = pointDataList;

    // 如果 JSON 里没有默认 mask_blur，这里给一个。
    if (!params.contains(QStringLiteral("mask_blur"))) {
        params[QStringLiteral("mask_blur")] = 10.0f;
    }
}
