#include <obs-module.h>
#include <obs-frontend-api.h>

#include <graphics/vec2.h>
#include <graphics/matrix4.h>

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-scroll", "en-US")

namespace {

constexpr double kZoomPerWheelStep = 1.10;
constexpr double kPreviewEdgePixels = 10.0;
constexpr float kMinimumScale = 1.0f;
constexpr float kMaximumScale = 100.0f;

struct CanvasPoint {
	double x = 0.0;
	double y = 0.0;
};

static QWidget *main_window()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

static QWidget *preview_widget()
{
	QWidget *window = main_window();
	return window ? window->findChild<QWidget *>(QStringLiteral("preview")) : nullptr;
}

static double scale_from_label(QWidget *window)
{
	QLabel *label = window->findChild<QLabel *>(QStringLiteral("previewScalePercent"));
	if (!label) {
		return 1.0;
	}

	static const QRegularExpression percentPattern(QStringLiteral(R"(([0-9]+(?:\.[0-9]+)?)\s*%)"));
	const QRegularExpressionMatch match = percentPattern.match(label->text());
	if (!match.hasMatch()) {
		return 1.0;
	}

	bool ok = false;
	const double percent = match.captured(1).toDouble(&ok);
	return ok && percent > 0.0 ? percent / 100.0 : 1.0;
}

/*
 * Convert a Qt preview-widget coordinate into the OBS base-canvas coordinate
 * used by obs_sceneitem_*().  OBS stores preview offsets and scale in device
 * pixels, while Qt mouse coordinates are in logical pixels, hence the DPR
 * conversion here.
 */
static bool canvas_point_from_preview(QWidget *preview, const QPoint &previewPoint, CanvasPoint &canvas)
{
	obs_video_info videoInfo = {};
	if (!obs_get_video_info(&videoInfo) || videoInfo.base_width == 0 || videoInfo.base_height == 0) {
		return false;
	}

	const double dpr = std::max(1.0, preview->devicePixelRatioF());
	const double previewWidthPixels = double(preview->width()) * dpr;
	const double previewHeightPixels = double(preview->height()) * dpr;
	const double canvasAreaWidthPixels = previewWidthPixels - kPreviewEdgePixels * 2.0;
	const double canvasAreaHeightPixels = previewHeightPixels - kPreviewEdgePixels * 2.0;
	if (canvasAreaWidthPixels <= 0.0 || canvasAreaHeightPixels <= 0.0) {
		return false;
	}

	QComboBox *scalingMode = main_window()->findChild<QComboBox *>(QStringLiteral("previewScalingMode"));
	const bool fixedScaling = scalingMode && scalingMode->currentIndex() > 0;

	double scaleInPixels = 0.0;
	double offsetXPixels = 0.0;
	double offsetYPixels = 0.0;

	if (!fixedScaling) {
		// Match OBSBasic::ResizePreview: fit inside the 10px device-pixel edge.
		scaleInPixels = std::min(canvasAreaWidthPixels / double(videoInfo.base_width),
						 canvasAreaHeightPixels / double(videoInfo.base_height));
		offsetXPixels = kPreviewEdgePixels +
				 (canvasAreaWidthPixels - double(videoInfo.base_width) * scaleInPixels) / 2.0;
		offsetYPixels = kPreviewEdgePixels +
				 (canvasAreaHeightPixels - double(videoInfo.base_height) * scaleInPixels) / 2.0;
	} else {
		// "Canvas"/"Output" scaling: the percentage is shown by OBS in this label.
		// OBS stores both the scale and scrollbar offsets in device pixels.
		scaleInPixels = scale_from_label(main_window());

		QScrollBar *xScrollBar = main_window()->findChild<QScrollBar *>(QStringLiteral("previewXScrollBar"));
		QScrollBar *yScrollBar = main_window()->findChild<QScrollBar *>(QStringLiteral("previewYScrollBar"));
		const double scrollX = xScrollBar ? -double(xScrollBar->value()) : 0.0;
		const double scrollY = yScrollBar ? -double(yScrollBar->value()) : 0.0;

		offsetXPixels = kPreviewEdgePixels +
				 (canvasAreaWidthPixels - double(videoInfo.base_width) * scaleInPixels) / 2.0 + scrollX;
		offsetYPixels = kPreviewEdgePixels +
				 (canvasAreaHeightPixels - double(videoInfo.base_height) * scaleInPixels) / 2.0 + scrollY;
	}

	if (scaleInPixels <= 0.0) {
		return false;
	}

	canvas.x = (double(previewPoint.x()) * dpr - offsetXPixels) / scaleInPixels;
	canvas.y = (double(previewPoint.y()) * dpr - offsetYPixels) / scaleInPixels;
	return true;
}

static float clamp_scale(float value)
{
	const float sign = value < 0.0f ? -1.0f : 1.0f;
	const float magnitude = std::clamp(std::abs(value), kMinimumScale, kMaximumScale);
	return sign * magnitude;
}

struct SelectedItems {
	std::vector<obs_sceneitem_t *> items;
};

static bool collect_selected_items(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	SelectedItems *selected = static_cast<SelectedItems *>(param);
	if (obs_sceneitem_selected(item)) {
		if (obs_sceneitem_locked(item)) {
			return true;
		}

		obs_source_t *source = obs_sceneitem_get_source(item);
		if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
			obs_sceneitem_addref(item);
			selected->items.push_back(item);
		}
		return true;
	}

	if (!obs_sceneitem_is_group(item) || obs_sceneitem_locked(item)) {
		return true;
	}

	obs_sceneitem_group_enum_items(item, collect_selected_items, param);
	return true;
}

static std::string save_transform_states(obs_scene_t *scene)
{
	obs_data_t *states = obs_scene_save_transform_states(scene, false);
	if (!states) {
		return {};
	}

	const char *json = obs_data_get_json(states);
	const std::string result = json ? json : "";
	obs_data_release(states);
	return result;
}

static void apply_transform_states(const char *states)
{
	if (states && *states) {
		obs_scene_load_transform_states(states);
	}
}

static obs_scene_t *current_edit_scene(obs_source_t **sceneSource)
{
	*sceneSource = obs_frontend_get_current_preview_scene();
	if (!*sceneSource) {
		*sceneSource = obs_frontend_get_current_scene();
	}
	return *sceneSource ? obs_scene_from_source(*sceneSource) : nullptr;
}

static bool zoom_selected_items(const CanvasPoint &anchor, double factor)
{
	obs_source_t *sceneSource = nullptr;
	obs_scene_t *scene = current_edit_scene(&sceneSource);
	if (!scene) {
		if (sceneSource) {
			obs_source_release(sceneSource);
		}
		return false;
	}

	SelectedItems selected;
	obs_scene_enum_items(scene, collect_selected_items, &selected);
	if (selected.items.empty()) {
		if (sceneSource) {
			obs_source_release(sceneSource);
		}
		return false;
	}

	const std::string undoStates = save_transform_states(scene);

	for (obs_sceneitem_t *item : selected.items) {
		vec2 position;
		vec2 scale;
		obs_sceneitem_get_pos(item, &position);
		obs_sceneitem_get_scale(item, &scale);

		CanvasPoint itemAnchor = anchor;
		obs_sceneitem_t *group = obs_sceneitem_get_group(scene, item);
		if (group && obs_sceneitem_locked(group)) {
			obs_sceneitem_release(item);
			continue;
		}
		if (group) {
			matrix4 groupTransform;
			matrix4 inverseGroupTransform;
			obs_sceneitem_get_draw_transform(group, &groupTransform);
			if (matrix4_inv(&inverseGroupTransform, &groupTransform)) {
				vec3 canvasAnchor;
				vec3 localAnchor;
				vec3_set(&canvasAnchor, float(anchor.x), float(anchor.y), 0.0f);
				vec3_transform(&localAnchor, &canvasAnchor, &inverseGroupTransform);
				itemAnchor.x = localAnchor.x;
				itemAnchor.y = localAnchor.y;
			}
		}

		const float nextScaleX = clamp_scale(float(double(scale.x) * factor));
		const float nextScaleY = clamp_scale(float(double(scale.y) * factor));
		const double effectiveFactorX = std::abs(scale.x) > 0.000001f ? double(nextScaleX) / double(scale.x) : 1.0;
		const double effectiveFactorY = std::abs(scale.y) > 0.000001f ? double(nextScaleY) / double(scale.y) : 1.0;

		vec2 nextPosition;
		nextPosition.x = float(itemAnchor.x + (double(position.x) - itemAnchor.x) * effectiveFactorX);
		nextPosition.y = float(itemAnchor.y + (double(position.y) - itemAnchor.y) * effectiveFactorY);

		obs_sceneitem_defer_update_begin(item);
		vec2 nextScale;
		nextScale.x = nextScaleX;
		nextScale.y = nextScaleY;
		obs_sceneitem_set_scale(item, &nextScale);
		obs_sceneitem_set_pos(item, &nextPosition);
		obs_sceneitem_defer_update_end(item);

		obs_sceneitem_release(item);
	}

	const std::string redoStates = save_transform_states(scene);
	if (!undoStates.empty() && !redoStates.empty() && undoStates != redoStates) {
		obs_frontend_add_undo_redo_action("Zoom selected source", apply_transform_states, apply_transform_states,
								 undoStates.c_str(), redoStates.c_str(), true);
	}

	if (sceneSource) {
		obs_source_release(sceneSource);
	}
	return true;
}

class ZoomScrollFilter final : public QObject {
public:
	explicit ZoomScrollFilter(QObject *parent) : QObject(parent) {}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		Q_UNUSED(watched);
		if (event->type() != QEvent::Wheel) {
			return QObject::eventFilter(watched, event);
		}

		QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
		if (!(wheel->modifiers() & Qt::ControlModifier)) {
			return QObject::eventFilter(watched, event);
		}

		QWidget *preview = preview_widget();
		if (!preview || !preview->isVisible() || !preview->isEnabled()) {
			return QObject::eventFilter(watched, event);
		}

		const QPoint previewPoint = preview->mapFromGlobal(QCursor::pos());
		if (!preview->rect().contains(previewPoint)) {
			return QObject::eventFilter(watched, event);
		}

		const int delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y() : wheel->pixelDelta().y();
		if (delta == 0) {
			return QObject::eventFilter(watched, event);
		}

		CanvasPoint anchor;
		if (!canvas_point_from_preview(preview, previewPoint, anchor)) {
			return QObject::eventFilter(watched, event);
		}

		const double factor = std::pow(kZoomPerWheelStep, double(delta) / 120.0);
		if (zoom_selected_items(anchor, factor)) {
			event->accept();
			return true;
		}

		return QObject::eventFilter(watched, event);
	}
};

ZoomScrollFilter *filter = nullptr;

} // namespace

bool obs_module_load(void)
{
	QWidget *window = main_window();
	if (!window || !qApp) {
		blog(LOG_WARNING, "obs-zoom-scroll: OBS main window or Qt application is unavailable");
		return false;
	}

	filter = new ZoomScrollFilter(nullptr);
	qApp->installEventFilter(filter);
	blog(LOG_INFO, "obs-zoom-scroll loaded: Ctrl+wheel zooms selected sources around the cursor");
	return true;
}

void obs_module_unload(void)
{
	if (filter && qApp) {
		qApp->removeEventFilter(filter);
		delete filter;
		filter = nullptr;
	}
	blog(LOG_INFO, "obs-zoom-scroll unloaded");
}
