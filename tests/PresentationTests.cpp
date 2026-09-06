#include "texturebuffer.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLShaderProgram>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void finishGpu(QOpenGLFunctions_3_3_Core *gl)
{
    auto fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    auto result = gl->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
    gl->glDeleteSync(fence);
    require(result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED, "Test GPU fence failed");
}
GLuint target(QOpenGLFunctions_3_3_Core *gl, GLuint &texture)
{
    GLuint fbo = 0;
    gl->glGenTextures(1, &texture);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Test FBO failed");
    return fbo;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext producer, consumer;
        producer.setFormat(format);
        require(producer.create(), "Producer context failed");
        consumer.setFormat(producer.format());
        consumer.setShareContext(&producer);
        require(consumer.create(), "Consumer context failed");
        QOffscreenSurface surface;
        surface.setFormat(producer.format());
        surface.create();
        require(producer.makeCurrent(&surface), "Producer makeCurrent failed");
        auto pg = producer.versionFunctions<QOpenGLFunctions_3_3_Core>();
        pg->initializeOpenGLFunctions();
        GLuint sourceTexture, sourceFbo = target(pg, sourceTexture);
        auto bridge = TextureBuffer::instance();
        bridge->createTexture(&producer);
        auto publish = [&](float red, quint64 version) {
            pg->glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
            pg->glClearColor(red, 0, 0, 1);
            pg->glClear(GL_COLOR_BUFFER_BIT);
            // Compositing is independent of path tracing; the ambient FBO can be different at publication.
            pg->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            bool submitted = bridge->updateTexture(&producer, 8, 8, 0, version, sourceFbo);
            finishGpu(pg);
            return submitted;
        };
        // Delay the UI until every slot is full. GPU completion alone must not make unread frames reusable.
        require(publish(.2f, 1), "First frame not published");
        require(publish(.4f, 1), "Second frame not published");
        require(publish(.6f, 1), "Third frame not published");
        require(!publish(.8f, 1), "Producer overwrote a frame before the UI consumed it");
        require(consumer.makeCurrent(&surface), "Consumer makeCurrent failed");
        auto cg = consumer.versionFunctions<QOpenGLFunctions_3_3_Core>();
        cg->initializeOpenGLFunctions();
        GLuint destinationTexture, destinationFbo = target(cg, destinationTexture), vao;
        cg->glGenVertexArrays(1, &vao);
        cg->glBindVertexArray(vao);
        cg->glViewport(0, 0, 8, 8);
        QOpenGLShaderProgram shader;
        require(shader.addShaderFromSourceCode(
                    QOpenGLShader::Vertex,
                    "#version 330 core\nvoid main(){vec2 "
                    "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"),
                "Test vertex shader failed");
        require(
            shader.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\nuniform sampler2D beauty;out vec4 color;void "
                                           "main(){color=texelFetch(beauty,ivec2(gl_FragCoord.xy),0);}"),
            "Test fragment shader failed");
        require(shader.link(), "Test display shader failed");
        auto display = [&](quint64 version, int expectedRed) {
            cg->glBindFramebuffer(GL_FRAMEBUFFER, destinationFbo);
            shader.bind();
            shader.setUniformValue("beauty", 0);
            require(bridge->drawTexture(&consumer, 3, version), "UI could not acquire a completed frame");
            unsigned char pixel[4] = {};
            cg->glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            require(std::abs(int(pixel[0]) - expectedRed) <= 1,
                    "UI did not display the newest completed frame");
            finishGpu(cg);
        };
        display(1, 153);
        require(producer.makeCurrent(&surface), "Producer resume failed");
        require(publish(.8f, 1), "UI consumption did not release a slot");
        require(consumer.makeCurrent(&surface), "Consumer resume failed");
        display(1, 204);
        require(producer.makeCurrent(&surface), "Producer version switch failed");
        require(publish(.1f, 1) && publish(.2f, 1), "Could not fill spare slots");
        require(publish(.9f, 2), "Obsolete scene frames blocked the new scene");
        require(consumer.makeCurrent(&surface), "Consumer version switch failed");
        display(2, 230);
        shader.release();
        cg->glDeleteVertexArrays(1, &vao);
        cg->glDeleteFramebuffers(1, &destinationFbo);
        cg->glDeleteTextures(1, &destinationTexture);
        require(producer.makeCurrent(&surface), "Producer cleanup failed");
        bridge->deleteTexture(&producer);
        pg->glDeleteFramebuffers(1, &sourceFbo);
        pg->glDeleteTextures(1, &sourceTexture);
        std::cout << "Presentation: delayed UI, unread frame preservation and scene version switch passed"
                  << std::endl;
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Presentation failure: " << error.what() << std::endl;
        return 1;
    }
}
