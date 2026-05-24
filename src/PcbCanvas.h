#pragma once

#include <unordered_map>
#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "model/Board.h"
#include "render/Camera2D.h"
#include "render/SegmentMesher.h"

class PcbCanvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);

    void setBoard(const sikit::model::Board* board);
    void setLayerVisibility(int ordinal, bool visible);
    void fitToBoard();

signals:
    void hoverInfo(const QString& info);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void buildGrid();
    void uploadBoardMeshes();

    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };

    sikit::render::Camera2D camera_;
    const sikit::model::Board* board_ = nullptr;

    // Flat-color shader: grid + per-layer board fills. Future SI overlays
    // (impedance color, eye diagram) will add their own programs.
    QOpenGLShaderProgram flat_prog_;

    QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject grid_vao_;
    int grid_vertex_count_ = 0;

    QOpenGLBuffer board_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer board_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject board_vao_;
    std::vector<LayerRange> layer_ranges_;
    std::vector<sikit::render::LayerMesh> pending_meshes_;
    bool meshes_dirty_ = false;

    std::unordered_map<int, bool> layer_visible_;

    bool panning_ = false;
    QPoint last_mouse_;
};
