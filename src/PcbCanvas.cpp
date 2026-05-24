#include "PcbCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QVector4D>
#include <QWheelEvent>

#include "model/HitTest.h"
#include "render/LayerColors.h"

namespace {

constexpr auto kFlatVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr auto kFlatFragSrc = R"(
#version 330 core
uniform vec4 u_color;
out vec4 frag_color;
void main() {
    frag_color = u_color;
}
)";

constexpr auto kVcolVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
out vec4 v_color;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
}
)";

constexpr auto kVcolFragSrc = R"(
#version 330 core
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

int render_priority(int ord) {
    if (ord == 0) return 1000;
    if (ord == 31) return 500;
    return 100 - ord;
}

QVector4D toQVec(const std::array<float, 4>& c) {
    return {c[0], c[1], c[2], c[3]};
}

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PcbCanvas::setBoard(const sikit::model::Board* board) {
    board_ = board;
    pending_meshes_.clear();
    layer_visible_.clear();
    clearImpedanceOverlay();

    if (board_) {
        pending_meshes_ = sikit::render::build_all_meshes(*board_);
        meshes_dirty_ = true;
        fitToBoard();
    }
    update();
}

void PcbCanvas::setLayerVisibility(int ordinal, bool visible) {
    layer_visible_[ordinal] = visible;
    update();
}

void PcbCanvas::fitToBoard() {
    if (!board_) return;
    bool have_any = false;
    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    auto include = [&](double x, double y) {
        if (!have_any) { lo_x = hi_x = x; lo_y = hi_y = y; have_any = true; }
        else {
            if (x < lo_x) lo_x = x;
            if (x > hi_x) hi_x = x;
            if (y < lo_y) lo_y = y;
            if (y > hi_y) hi_y = y;
        }
    };
    for (const auto& s : board_->segments) {
        include(s.start.x, s.start.y);
        include(s.end.x, s.end.y);
    }
    for (const auto& p : board_->pads) include(p.at.x, p.at.y);
    for (const auto& z : board_->zones) {
        for (const auto& pt : z.outline.outline) include(pt.x, pt.y);
        for (const auto& fp : z.filled)
            for (const auto& pt : fp.outline) include(pt.x, pt.y);
    }
    if (have_any) {
        camera_.fit_to_bounds({lo_x, lo_y}, {hi_x, hi_y},
                               width(), height(), 0.10);
        update();
    }
}

void PcbCanvas::clearImpedanceOverlay() {
    pending_overlay_verts_.clear();
    pending_overlay_indices_.clear();
    overlay_dirty_ = true;
    update();
}

void PcbCanvas::setImpedanceOverlay(
    const std::vector<sikit::analysis::SegmentImpedance>& results,
    double target_z0) {

    pending_overlay_verts_.clear();
    pending_overlay_indices_.clear();

    if (!board_ || results.empty()) {
        overlay_dirty_ = true;
        update();
        return;
    }

    // Each segment → rectangle quad (4 vertices, 6 indices), inflated slightly
    // beyond the trace width so it visibly outlines the original trace fill.
    constexpr double kInflateFactor = 1.6;
    const double inflate = kInflateFactor;

    for (const auto& r : results) {
        if (r.segment_index >= board_->segments.size()) continue;
        const auto& s = board_->segments[r.segment_index];

        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0 || s.width <= 0.0) continue;

        const double nx = -dy / len;
        const double ny =  dx / len;
        const double hw = 0.5 * s.width * inflate;

        const float ax1 = static_cast<float>(s.start.x + nx * hw);
        const float ay1 = static_cast<float>(s.start.y + ny * hw);
        const float ax2 = static_cast<float>(s.start.x - nx * hw);
        const float ay2 = static_cast<float>(s.start.y - ny * hw);
        const float bx1 = static_cast<float>(s.end.x   - nx * hw);
        const float by1 = static_cast<float>(s.end.y   - ny * hw);
        const float bx2 = static_cast<float>(s.end.x   + nx * hw);
        const float by2 = static_cast<float>(s.end.y   + ny * hw);

        const auto c = sikit::analysis::color_for_error(r.z0, target_z0);

        const auto base = static_cast<std::uint32_t>(pending_overlay_verts_.size() / 6);
        // 4 vertices, each (x, y, r, g, b, a).
        auto push = [&](float x, float y) {
            pending_overlay_verts_.insert(pending_overlay_verts_.end(),
                                          {x, y, c.r, c.g, c.b, c.a});
        };
        push(ax1, ay1);
        push(ax2, ay2);
        push(bx1, by1);
        push(bx2, by2);

        pending_overlay_indices_.insert(pending_overlay_indices_.end(),
            {base + 0, base + 1, base + 2,
             base + 0, base + 2, base + 3});
    }

    overlay_dirty_ = true;
    update();
}

void PcbCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kFlatVertSrc);
    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragSrc);
    flat_prog_.link();

    vcol_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kVcolVertSrc);
    vcol_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kVcolFragSrc);
    vcol_prog_.link();

    grid_vao_.create();
    grid_vbo_.create();
    buildGrid();

    board_vao_.create();
    board_vbo_.create();
    board_ibo_.create();
    board_vao_.bind();
    board_vbo_.bind();
    board_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    overlay_vao_.create();
    overlay_vbo_.create();
    overlay_ibo_.create();
    overlay_vao_.bind();
    overlay_vbo_.bind();
    overlay_ibo_.bind();
    // Stride = 6 floats: (x, y, r, g, b, a).
    vcol_prog_.enableAttributeArray(0);
    vcol_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 6 * sizeof(float));
    vcol_prog_.enableAttributeArray(1);
    vcol_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 4,
                                   6 * sizeof(float));
    overlay_vao_.release();
    overlay_vbo_.release();
    overlay_ibo_.release();
}

void PcbCanvas::buildGrid() {
    std::vector<float> verts;
    const float lo = -0.5f, hi = 0.5f;
    const float step = 0.010f;
    for (float v = lo; v <= hi + 1e-6f; v += step) {
        verts.insert(verts.end(), {v, lo, v, hi});
        verts.insert(verts.end(), {lo, v, hi, v});
    }
    grid_vertex_count_ = static_cast<int>(verts.size() / 2);

    grid_vao_.bind();
    grid_vbo_.bind();
    grid_vbo_.allocate(verts.data(),
                       static_cast<int>(verts.size() * sizeof(float)));
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    grid_vbo_.release();
    grid_vao_.release();
}

void PcbCanvas::uploadBoardMeshes() {
    std::sort(pending_meshes_.begin(), pending_meshes_.end(),
              [](const auto& a, const auto& b) {
                  return render_priority(a.layer_ordinal) <
                         render_priority(b.layer_ordinal);
              });

    std::vector<float> all_verts;
    std::vector<std::uint32_t> all_indices;
    layer_ranges_.clear();
    layer_ranges_.reserve(pending_meshes_.size());

    std::uint32_t vbase = 0;
    int ibase = 0;
    for (const auto& m : pending_meshes_) {
        LayerRange r;
        r.ordinal = m.layer_ordinal;
        r.index_start = ibase;
        r.index_count = static_cast<int>(m.indices.size());
        layer_ranges_.push_back(r);

        all_verts.insert(all_verts.end(), m.vertices.begin(), m.vertices.end());
        for (auto idx : m.indices) all_indices.push_back(vbase + idx);
        vbase += static_cast<std::uint32_t>(m.vertex_count());
        ibase += static_cast<int>(m.indices.size());
    }

    board_vao_.bind();
    board_vbo_.bind();
    board_vbo_.allocate(all_verts.data(),
                        static_cast<int>(all_verts.size() * sizeof(float)));
    board_ibo_.bind();
    board_ibo_.allocate(all_indices.data(),
                        static_cast<int>(all_indices.size() * sizeof(std::uint32_t)));
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    meshes_dirty_ = false;
}

void PcbCanvas::uploadOverlay() {
    overlay_index_count_ = static_cast<int>(pending_overlay_indices_.size());

    overlay_vao_.bind();
    overlay_vbo_.bind();
    overlay_vbo_.allocate(pending_overlay_verts_.data(),
                          static_cast<int>(pending_overlay_verts_.size() *
                                           sizeof(float)));
    overlay_ibo_.bind();
    overlay_ibo_.allocate(pending_overlay_indices_.data(),
                          static_cast<int>(pending_overlay_indices_.size() *
                                           sizeof(std::uint32_t)));
    vcol_prog_.enableAttributeArray(0);
    vcol_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 6 * sizeof(float));
    vcol_prog_.enableAttributeArray(1);
    vcol_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 4,
                                   6 * sizeof(float));
    overlay_vao_.release();
    overlay_vbo_.release();
    overlay_ibo_.release();

    overlay_dirty_ = false;
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    if (meshes_dirty_) uploadBoardMeshes();
    if (overlay_dirty_) uploadOverlay();

    glClear(GL_COLOR_BUFFER_BIT);

    const auto m = camera_.ortho_matrix(width(), height());
    const QMatrix4x4 proj(
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]);

    flat_prog_.bind();
    flat_prog_.setUniformValue("u_proj", proj);

    flat_prog_.setUniformValue("u_color", QVector4D(0.22f, 0.22f, 0.28f, 1.0f));
    grid_vao_.bind();
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    grid_vao_.release();

    if (!layer_ranges_.empty()) {
        board_vao_.bind();
        for (const auto& r : layer_ranges_) {
            auto vis_it = layer_visible_.find(r.ordinal);
            const bool visible = (vis_it == layer_visible_.end()) || vis_it->second;
            if (!visible) continue;
            flat_prog_.setUniformValue("u_color",
                                       toQVec(sikit::render::layer_color(r.ordinal)));
            glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(r.index_start *
                                                            sizeof(std::uint32_t))));
        }
        board_vao_.release();
    }
    flat_prog_.release();

    // Impedance-error overlay, drawn last (on top of the board fills).
    if (overlay_index_count_ > 0) {
        vcol_prog_.bind();
        vcol_prog_.setUniformValue("u_proj", proj);
        overlay_vao_.bind();
        glDrawElements(GL_TRIANGLES, overlay_index_count_, GL_UNSIGNED_INT, nullptr);
        overlay_vao_.release();
        vcol_prog_.release();
    }
}

void PcbCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = true;
        last_mouse_ = e->pos();
    }
}

void PcbCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->pos() - last_mouse_;
        camera_.pan_pixels(d.x(), d.y());
        last_mouse_ = e->pos();
        update();
    }

    if (board_) {
        const auto world = camera_.screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        const double tol = 4.0 / camera_.pixels_per_meter;
        const auto hit = sikit::hittest::at_point(*board_, world, tol);

        QString info;
        if (hit.kind != sikit::hittest::Hit::Kind::None) {
            const auto* net = board_->find_net(hit.net_id);
            const auto* layer = board_->find_layer(hit.layer_ordinal);
            const QString net_name = (net && !net->name.empty())
                ? QString::fromStdString(net->name)
                : QString("(unnamed)");
            const QString layer_name = layer
                ? QString::fromStdString(layer->name)
                : QString("?");
            info = QString("%1   net %2 (%3)   layer %4")
                       .arg(sikit::hittest::name(hit.kind))
                       .arg(net_name)
                       .arg(hit.net_id)
                       .arg(layer_name);
        }
        emit hoverInfo(info);
    }
}

void PcbCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = false;
    }
}

void PcbCanvas::wheelEvent(QWheelEvent* e) {
    const double factor = (e->angleDelta().y() > 0) ? 1.20 : 1.0 / 1.20;
    const QPointF pos = e->position();
    camera_.zoom_at(pos.x(), pos.y(), factor, width(), height());
    update();
}
