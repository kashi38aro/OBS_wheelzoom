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
#include <unordered_map>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-scroll", "en-US")

namespace {

constexpr double kZoomPerWheelStep = 1.05;
constexpr double kPreviewEdgePixels = 10.0;
constexpr float kMinimumScale = 0.001f;
constexpr float kMaximumScale = 100.0f;

struct CanvasPoint {
	double x = 0.0;
	double y = 0.0;
};

struct ZoomState {
	vec2 frameSize;
	obs_sceneitem_crop baseCrop = {};
	uint32_t sourceWidth = 0;
	uint32_t sourceHeight = 0;
	float minimumScaleX = kMinimumScale;
	float minimumScaleY = kMinimumScale;
	uint32_t boundsAlignment = OBS_ALIGN_CENTER;
};

std::unordered_map<obs_sceneitem_t *, ZoomState> zoomStates;

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

static float clamp_scale(float value, float minimumScale)
{
	const float sign = value < 0.0f ? -1.0f : 1.0f;
	const float magnitude = std::clamp(std::abs(value), minimumScale, std::max(kMaximumScale, minimumScale));
	return sign * magnitude;
}

static uint32_t cropped_dimension(uint32_t sourceDimension, int cropStart, int cropEnd)
{
	const int64_t remaining = int64_t(sourceDimension) - int64_t(cropStart) - int64_t(cropEnd);
	return uint32_t(std::max<int64_t>(1, remaining));
}

static bool configure_fixed_frame(obs_sceneitem_t *item, ZoomState &state)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) {
		return false;
	}

	state.sourceWidth = obs_source_get_width(source);
	state.sourceHeight = obs_source_get_height(source);
	if (state.sourceWidth == 0 || state.sourceHeight == 0) {
		return false;
	}

	obs_sceneitem_get_crop(item, &state.baseCrop);
	obs_sceneitem_get_box_scale(item, &state.frameSize);
	state.frameSize.x = std::abs(state.frameSize.x);
	state.frameSize.y = std::abs(state.frameSize.y);
	if (state.frameSize.x <= 0.0f || state.frameSize.y <= 0.0f) {
		return false;
	}

	const uint32_t baseWidth = cropped_dimension(state.sourceWidth, state.baseCrop.left, state.baseCrop.right);
	const uint32_t baseHeight = cropped_dimension(state.sourceHeight, state.baseCrop.top, state.baseCrop.bottom);
	state.minimumScaleX = std::max(kMinimumScale, state.frameSize.x / float(baseWidth));
	state.minimumScaleY = std::max(kMinimumScale, state.frameSize.y / float(baseHeight));
	state.boundsAlignment = obs_sceneitem_get_bounds_alignment(item);

	obs_transform_info info = {};
	obs_sceneitem_get_info2(item, &info);
	info.bounds = state.frameSize;
	info.bounds_type = OBS_BOUNDS_SCALE_OUTER;
	info.bounds_alignment = state.boundsAlignment;
	info.crop_to_bounds = true;
	obs_sceneitem_set_info2(item, &info);
	return true;
}

static void restore_fixed_frame(obs_sceneitem_t *item, const ZoomState &state)
{
	obs_transform_info info = {};
	obs_sceneitem_get_info2(item, &info);
	info.bounds = state.frameSize;
	info.bounds_type = OBS_BOUNDS_SCALE_OUTER;
	info.bounds_alignment = state.boundsAlignment;
	info.crop_to_bounds = true;
	obs_sceneitem_set_info2(item, &info);
}

static bool frame_anchor_ratio(obs_sceneitem_t *item, const CanvasPoint &itemSpaceAnchor, const ZoomState &state,
				       double &ratioX, double &ratioY)
{
	matrix4 boxTransform;
	matrix4 inverseBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	if (!matrix4_inv(&inverseBoxTransform, &boxTransform)) {
		return false;
	}

	vec3 canvasPoint;
	vec3 framePoint;
	vec3_set(&canvasPoint, float(itemSpaceAnchor.x), float(itemSpaceAnchor.y), 0.0f);
	vec3_transform(&framePoint, &canvasPoint, &inverseBoxTransform);

	ratioX = std::clamp(double(framePoint.x) / double(state.frameSize.x), 0.0, 1.0);
	ratioY = std::clamp(double(framePoint.y) / double(state.frameSize.y), 0.0, 1.0);
	return true;
}

static int crop_left_for_anchor(double sourcePoint, double ratio, uint32_t visibleSize, uint32_t sourceSize)
{
	const int maxCrop = std::max(0, int(sourceSize - visibleSize));
	const int requested = int(std::lround(sourcePoint - ratio * double(visibleSize)));
	return std::clamp(requested, 0, maxCrop);
}

static uint32_t visible_dimension_for_frame(float frameDimension, float scale, uint32_t sourceDimension)
{
	const double absoluteScale = std::max(std::abs(double(scale)), double(kMinimumScale));
	const int requested = int(std::lround(double(frameDimension) / absoluteScale));
	return uint32_t(std::clamp(requested, 1, int(sourceDimension)));
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

		auto stateIt = zoomStates.find(item);
		if (stateIt == zoomStates.end()) {
			ZoomState state;
			if (!configure_fixed_frame(item, state)) {
				obs_sceneitem_release(item);
				continue;
			}
			stateIt = zoomStates.emplace(item, state).first;
		} else {
			restore_fixed_frame(item, stateIt->second);
		}

		const ZoomState &state = stateIt->second;
		double ratioX = 0.0;
		double ratioY = 0.0;
		if (!frame_anchor_ratio(item, itemAnchor, state, ratioX, ratioY)) {
			obs_sceneitem_release(item);
			continue;
		}

		vec2 scale;
		obs_sceneitem_get_scale(item, &scale);
		obs_sceneitem_crop crop = {};
		obs_sceneitem_get_crop(item, &crop);

		const uint32_t currentWidth = cropped_dimension(state.sourceWidth, crop.left, crop.right);
		const uint32_t currentHeight = cropped_dimension(state.sourceHeight, crop.top, crop.bottom);
		const double sourcePointX = std::clamp(double(crop.left) + ratioX * double(currentWidth), 0.0,
									 double(state.sourceWidth));
		const double sourcePointY = std::clamp(double(crop.top) + ratioY * double(currentHeight), 0.0,
									 double(state.sourceHeight));

		const float nextScaleX = clamp_scale(float(double(scale.x) * factor), state.minimumScaleX);
		const float nextScaleY = clamp_scale(float(double(scale.y) * factor), state.minimumScaleY);
		const uint32_t nextWidth = visible_dimension_for_frame(state.frameSize.x, nextScaleX, state.sourceWidth);
		const uint32_t nextHeight = visible_dimension_for_frame(state.frameSize.y, nextScaleY, state.sourceHeight);

		obs_sceneitem_crop nextCrop = crop;
		if (std::abs(std::abs(nextScaleX) - state.minimumScaleX) <= 0.00001f) {
			nextCrop.left = state.baseCrop.left;
			nextCrop.right = state.baseCrop.right;
		} else {
			nextCrop.left = crop_left_for_anchor(sourcePointX, ratioX, nextWidth, state.sourceWidth);
			nextCrop.right = int(state.sourceWidth) - int(nextWidth) - nextCrop.left;
		}
		if (std::abs(std::abs(nextScaleY) - state.minimumScaleY) <= 0.00001f) {
			nextCrop.top = state.baseCrop.top;
			nextCrop.bottom = state.baseCrop.bottom;
		} else {
			nextCrop.top = crop_left_for_anchor(sourcePointY, ratioY, nextHeight, state.sourceHeight);
			nextCrop.bottom = int(state.sourceHeight) - int(nextHeight) - nextCrop.top;
		}

		obs_sceneitem_defer_update_begin(item);
		vec2 nextScale;
		nextScale.x = nextScaleX;
		nextScale.y = nextScaleY;
		obs_sceneitem_set_crop(item, &nextCrop);
		obs_sceneitem_set_scale(item, &nextScale);
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
	zoomStates.clear();
	blog(LOG_INFO, "obs-zoom-scroll unloaded");
}
