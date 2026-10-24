#pragma once

#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QThread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

/**
 * @brief 基于 FFmpeg 7.x 的音视频解码器。
 *
 * 音视频同步采用音频主时钟方案: 音频写入 QAudioSink 由声卡匀速消费,
 * 主时钟 = seek 基准 + QAudioSink::processedUSecs(); 视频帧带 pts 入队,
 * 由调用方按主时钟取帧显示(太早等待, 太晚丢弃)。无音频流时退化为定时器节奏逐帧显示。
 */
class VideoDecoder : public QThread {
    Q_OBJECT

public:
    explicit VideoDecoder(QObject *parent = nullptr);
    ~VideoDecoder();

    // 打开本地视频文件。
    bool open(const QString &filePath);

    // 关闭并释放资源。
    void close();

    // 播放控制。
    void play();
    void pause();
    void seek(double seconds);

    // 重置音频输出缓冲。必须在创建 QAudioSink 的线程（主线程）调用。
    void resetAudioOutput();

    // 当前是否正在播放。
    bool isPlaying() const;

    // 主时钟(秒): 有音频流时为音频播放位置, 否则为当前视频位置。
    double masterClock() const;

    // 按主时钟取一帧用于显示: 队头帧太早则返回 false 等待, 太晚则丢弃直到赶上。
    bool getDisplayFrame(double masterSec, QByteArray &rgbaData, int &width,
                         int &height);

    // 当前队列中的帧数。
    int frameQueueSize() const;

    // 视频基本信息。
    int width() const {
        return m_width;
    }
    int height() const {
        return m_height;
    }
    double duration() const {
        return m_duration;
    }
    double frameRate() const {
        return m_frameRate;
    }

    // 当前播放位置（秒）。
    double currentPosition() const {
        QMutexLocker lock(&m_stateMutex);
        return m_currentPosition;
    }

signals:
    void decodeError(const QString message);

    // 播放到文件末尾循环回开头, 请求主线程重置音频输出(processedUSecs 归零)。
    void audioResetRequested();

protected:
    void run() override;

private:
    struct VideoFrame {
        QByteArray rgbaData;
        int width = 0;
        int height = 0;
        double pts = 0.0;  // 显示时间戳(秒), 用于音视频同步。
    };

    bool decodeVideoPacket(const AVPacket *packet);
    bool decodeAudioPacket(const AVPacket *packet);
    void initAudioOutput();
    void stopAudioOutput();
    void flushAudioDecoder();
    void flushVideoDecoder();

    QString m_filePath;

    AVFormatContext *m_fmtCtx = nullptr;

    // 视频。
    AVCodecContext *m_videoCodecCtx = nullptr;
    const AVCodec *m_videoCodec = nullptr;
    AVFrame *m_videoFrame = nullptr;
    AVFrame *m_rgbaFrame = nullptr;
    SwsContext *m_swsCtx = nullptr;
    uint8_t *m_rgbaBuffer = nullptr;
    int m_videoStreamIndex = -1;
    AVRational m_videoTimeBase = {0, 1};

    int m_width = 0;
    int m_height = 0;
    double m_duration = 0.0;
    double m_frameRate = 0.0;

    // 音频。
    AVCodecContext *m_audioCodecCtx = nullptr;
    const AVCodec *m_audioCodec = nullptr;
    AVFrame *m_audioFrame = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStreamIndex = -1;
    int m_audioSampleRate = 48000;
    int m_audioChannels = 2;

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioIODevice = nullptr;

    // 视频帧队列。
    mutable QMutex m_frameQueueMutex;
    QQueue<VideoFrame> m_frameQueue;
    static constexpr int MAX_FRAME_QUEUE_SIZE = 60;

    // 音频设备锁，保护对 QAudioSink/QIODevice 的 start/stop/reset/suspend/resume/write 操作。
    mutable QMutex m_audioMutex;

    bool m_running = false;
    bool m_playing = false;
    bool m_seekRequested = false;
    double m_seekTarget = 0.0;
    double m_currentPosition = 0.0;
    double m_audioClockBase = 0.0;  // 音频主时钟基准(秒), seek/循环后 processedUSecs 从这里起算。
    mutable QMutex m_stateMutex;
};
