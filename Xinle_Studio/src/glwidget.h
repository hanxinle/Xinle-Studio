#pragma once

#include "effect.h"

#include <QByteArray>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QPixmap>
#include <QVector>

/**
 * @brief 特效实例描述。
 *
 * 由调用方维护特效链的顺序、参数与迭代次数，GLWidget 据此加载并渲染。
 */
struct EffectInstance {
    QString name;
    QString fragPath;
    QMap<QString, QVariant> params;
    int iterations = 1;
};

/**
 * @brief 基于现代 OpenGL 3.3 Core 的视频显示控件，支持 GLSL 特效链。
 *
 * 支持上传 RGBA 视频帧并在全屏四边形上渲染；可通过 setEffectChain 挂载多个 .frag 特效，
 * 特效按链式顺序依次作用，前一特效的输出作为后一特效的输入。
 */
class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget();

    // 设置当前要显示的视频帧（RGBA 数据）。主线程调用。
    void setVideoFrame(const QByteArray &rgbaData, int width, int height);

    // 设置整个特效链，会重新加载所有 fragment shader。
    void setEffectChain(const QVector<EffectInstance> &chain);
    void clearEffects();

    bool hasEffects() const {
        return !m_effectRuntimes.isEmpty();
    }

    // 更新指定位置特效的 uniform 参数。
    void setEffectParam(int index, const QString &name, const QVariant &value);
    void setEffectParams(int index, const QMap<QString, QVariant> &params);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    struct EffectRuntime {
        EffectInstance instance;
        Effect *effect = nullptr;
    };

    void initQuadGeometry();
    bool loadPassThroughShader();
    void uploadTexture();
    void updateMvp();
    void drawFullscreenQuad();

    void renderPassThrough();
    void renderWithEffects();
    void renderTextureToScreen(GLuint texture);
    void renderTextureToFbo(GLuint srcTexture, QOpenGLFramebufferObject *dstFbo,
                            Effect *effect, int iteration);

    void recreateFbos();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_videoTexture = 0;

    QOpenGLShaderProgram *m_passThroughProgram = nullptr;

    QVector<EffectRuntime> m_effectRuntimes;
    QOpenGLFramebufferObject *m_fbo[2] = {nullptr, nullptr};

    QByteArray m_frameData;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    bool m_frameDirty = false;
    bool m_fboSizeDirty = false;

    QMatrix4x4 m_mvp;
    QPixmap m_coverPixmap;
};
