/*
 * OBS_wheelzoom
 * Copyright (C) 2026 kashi38aro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-wheelzoom", "en-US")

namespace {

constexpr double kDefaultZoomPerWheelStep = 1.05;
constexpr double kMinimumZoomPerWheelStep = 1.001;
constexpr double kMaximumZoomPerWheelStep = 2.0;
constexpr double kMinimumZoom = 1.0;
constexpr double kMaximumZoom = 100.0;
constexpr double kPreviewEdgePixels = 10.0;
constexpr char kZoomFilterId[] = "obs_wheelzoom_filter";
constexpr char kLegacyZoomFilterId[] = "obs_zoom_scroll_filter";
constexpr char kZoomFilterName[] = "OBS_wheelzoom";
constexpr char kLegacyZoomFilterName[] = "OBS_scrollzoom";
constexpr char kOlderLegacyZoomFilterName[] = "OBS Zoom Scroll";
constexpr char kZoomFilterDisplayName[] = "OBS_wheelzoom";
constexpr int kZoomStateVersion = 2;

struct CanvasPoint {
	double x = 0.0;
	double y = 0.0;
};

struct ZoomFilterState {
	double zoom = kMinimumZoom;
	double offsetX = 0.0;
	double offsetY = 0.0;
};

struct ZoomFilterData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *param_offset = nullptr;
	gs_eparam_t *param_zoom = nullptr;
	gs_eparam_t *param_image = nullptr;
	gs_samplerstate_t *sampler = nullptr;
	ZoomFilterState state;
};

struct PluginSettings {
	int modifier = int(Qt::ControlModifier);
	double zoomPerWheelStep = kDefaultZoomPerWheelStep;
};

PluginSettings pluginSettings;
QAction *settingsAction = nullptr;

static QWidget *main_window()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

static QSettings settings_store()
{
	return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("OBS_wheelzoom"),
				QStringLiteral("OBS_wheelzoom"));
}

static bool is_supported_modifier(int modifier)
{
	return modifier == int(Qt::NoModifier) || modifier == int(Qt::ControlModifier) ||
	       modifier == int(Qt::ShiftModifier) || modifier == int(Qt::AltModifier) ||
	       modifier == int(Qt::MetaModifier);
}

static void load_plugin_settings()
{
	QSettings settings = settings_store();
	const int savedModifier = settings.value(QStringLiteral("modifier"), int(Qt::ControlModifier)).toInt();
	pluginSettings.modifier = is_supported_modifier(savedModifier) ? savedModifier : int(Qt::ControlModifier);

	const double savedStep = settings.value(QStringLiteral("zoom_per_wheel_step"), kDefaultZoomPerWheelStep).toDouble();
	pluginSettings.zoomPerWheelStep =
		std::clamp(savedStep, kMinimumZoomPerWheelStep, kMaximumZoomPerWheelStep);
}

static void save_plugin_settings()
{
	QSettings settings = settings_store();
	settings.setValue(QStringLiteral("modifier"), pluginSettings.modifier);
	settings.setValue(QStringLiteral("zoom_per_wheel_step"), pluginSettings.zoomPerWheelStep);
	settings.sync();
}

class PluginSettingsDialog final : public QDialog {
public:
	explicit PluginSettingsDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(QStringLiteral("OBS_wheelzoom"));
		setModal(true);

		QFormLayout *layout = new QFormLayout(this);
		modifierCombo = new QComboBox(this);
		modifierCombo->addItem(QStringLiteral("Ctrl"), int(Qt::ControlModifier));
		modifierCombo->addItem(QStringLiteral("Shift"), int(Qt::ShiftModifier));
		modifierCombo->addItem(QStringLiteral("Alt"), int(Qt::AltModifier));
		modifierCombo->addItem(QStringLiteral("Meta / Command"), int(Qt::MetaModifier));
		modifierCombo->addItem(QStringLiteral("なし"), int(Qt::NoModifier));
		for (int index = 0; index < modifierCombo->count(); ++index) {
			if (modifierCombo->itemData(index).toInt() == pluginSettings.modifier) {
				modifierCombo->setCurrentIndex(index);
				break;
			}
		}

		zoomStepSpin = new QDoubleSpinBox(this);
		zoomStepSpin->setRange(kMinimumZoomPerWheelStep, kMaximumZoomPerWheelStep);
		zoomStepSpin->setSingleStep(0.01);
		zoomStepSpin->setDecimals(3);
		zoomStepSpin->setValue(pluginSettings.zoomPerWheelStep);
		zoomStepSpin->setSuffix(QStringLiteral(" x"));

		layout->addRow(QStringLiteral("ズームキー"), modifierCombo);
		layout->addRow(QStringLiteral("1スクロールあたりの倍率"), zoomStepSpin);

		QDialogButtonBox *buttons =
			new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		layout->addRow(buttons);
		connect(buttons, &QDialogButtonBox::accepted, this, &PluginSettingsDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &PluginSettingsDialog::reject);
		setMinimumWidth(360);
	}

protected:
	void accept() override
	{
		pluginSettings.modifier = modifierCombo->currentData().toInt();
		pluginSettings.zoomPerWheelStep = zoomStepSpin->value();
		save_plugin_settings();
		QDialog::accept();
	}

private:
	QComboBox *modifierCombo = nullptr;
	QDoubleSpinBox *zoomStepSpin = nullptr;
};

static void open_settings_dialog()
{
	QWidget *window = main_window();
	if (!window) {
		return;
	}

	PluginSettingsDialog dialog(window);
	dialog.exec();
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
		scaleInPixels = std::min(canvasAreaWidthPixels / double(videoInfo.base_width),
						 canvasAreaHeightPixels / double(videoInfo.base_height));
		offsetXPixels = kPreviewEdgePixels +
				 (canvasAreaWidthPixels - double(videoInfo.base_width) * scaleInPixels) / 2.0;
		offsetYPixels = kPreviewEdgePixels +
				 (canvasAreaHeightPixels - double(videoInfo.base_height) * scaleInPixels) / 2.0;
	} else {
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

static double clamp_zoom(double zoom)
{
	return std::clamp(zoom, kMinimumZoom, kMaximumZoom);
}

static double clamp_offset(double offset, double zoom)
{
	const double maximumOffset = 1.0 - 1.0 / zoom;
	return std::clamp(offset, 0.0, maximumOffset);
}

static const char *zoom_filter_get_name(void *)
{
	return kZoomFilterDisplayName;
}

static void zoom_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "state_version", kZoomStateVersion);
	obs_data_set_default_double(settings, "zoom", kMinimumZoom);
	obs_data_set_default_double(settings, "offset_x", 0.0);
	obs_data_set_default_double(settings, "offset_y", 0.0);
}

static void zoom_filter_update(void *data, obs_data_t *settings)
{
	ZoomFilterData *filter = static_cast<ZoomFilterData *>(data);
	if (obs_data_get_int(settings, "state_version") < kZoomStateVersion) {
		/* Older builds used a reversed offset range and could persist a broken
		 * top-left zoom. Reset that state once when the corrected filter loads. */
		obs_data_set_int(settings, "state_version", kZoomStateVersion);
		obs_data_set_double(settings, "zoom", kMinimumZoom);
		obs_data_set_double(settings, "offset_x", 0.0);
		obs_data_set_double(settings, "offset_y", 0.0);
	}
	filter->state.zoom = clamp_zoom(obs_data_get_double(settings, "zoom"));
	filter->state.offsetX = clamp_offset(obs_data_get_double(settings, "offset_x"), filter->state.zoom);
	filter->state.offsetY = clamp_offset(obs_data_get_double(settings, "offset_y"), filter->state.zoom);
}

static void *zoom_filter_create(obs_data_t *settings, obs_source_t *context)
{
	ZoomFilterData *filter = new ZoomFilterData;
	filter->context = context;

	char *effectPath = obs_module_file("wheelzoom.effect");
	char *effectError = nullptr;
	gs_sampler_info samplerInfo = {};
	samplerInfo.filter = GS_FILTER_LINEAR;
	samplerInfo.address_u = GS_ADDRESS_CLAMP;
	samplerInfo.address_v = GS_ADDRESS_CLAMP;

	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effectPath, &effectError);
	filter->sampler = gs_samplerstate_create(&samplerInfo);
	obs_leave_graphics();
	if (effectError) {
		blog(LOG_ERROR, "obs-wheelzoom: effect compile error: %s", effectError);
		bfree(effectError);
	}
	if (!effectPath) {
		blog(LOG_ERROR, "obs-wheelzoom: effect path is unavailable");
	} else {
		blog(LOG_DEBUG, "obs-wheelzoom: loading effect: %s", effectPath);
	}
	bfree(effectPath);

	if (!filter->effect || !filter->sampler) {
		blog(LOG_ERROR, "obs-wheelzoom: failed to create filter resources (effect=%p, sampler=%p)",
		     filter->effect, filter->sampler);
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		gs_samplerstate_destroy(filter->sampler);
		obs_leave_graphics();
		delete filter;
		return nullptr;
	}

	filter->param_offset = gs_effect_get_param_by_name(filter->effect, "uv_offset");
	filter->param_zoom = gs_effect_get_param_by_name(filter->effect, "zoom");
	filter->param_image = gs_effect_get_param_by_name(filter->effect, "image");
	zoom_filter_update(filter, settings);
	return filter;
}

static void zoom_filter_destroy(void *data)
{
	ZoomFilterData *filter = static_cast<ZoomFilterData *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	gs_samplerstate_destroy(filter->sampler);
	obs_leave_graphics();
	delete filter;
}

static void zoom_filter_render(void *data, gs_effect_t *)
{
	ZoomFilterData *filter = static_cast<ZoomFilterData *>(data);
	obs_source_t *target = obs_filter_get_target(filter->context);
	if (!target || !filter->effect || !filter->sampler) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	const uint32_t width = obs_source_get_base_width(target);
	const uint32_t height = obs_source_get_base_height(target);
	if (width == 0 || height == 0) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		return;
	}

	vec2 offset;
	vec2_set(&offset, float(filter->state.offsetX), float(filter->state.offsetY));
	gs_effect_set_vec2(filter->param_offset, &offset);
	gs_effect_set_float(filter->param_zoom, float(filter->state.zoom));
	gs_effect_set_next_sampler(filter->param_image, filter->sampler);
	obs_source_process_filter_end(filter->context, filter->effect, width, height);
}

static uint32_t zoom_filter_width(void *data)
{
	ZoomFilterData *filter = static_cast<ZoomFilterData *>(data);
	obs_source_t *target = obs_filter_get_target(filter->context);
	return target ? obs_source_get_base_width(target) : 0;
}

static uint32_t zoom_filter_height(void *data)
{
	ZoomFilterData *filter = static_cast<ZoomFilterData *>(data);
	obs_source_t *target = obs_filter_get_target(filter->context);
	return target ? obs_source_get_base_height(target) : 0;
}

static obs_properties_t *zoom_filter_properties(void *)
{
	return obs_properties_create();
}

static obs_source_info zoom_filter_info = {};
static obs_source_info legacy_zoom_filter_info = {};

static void initialize_zoom_filter_info()
{
	zoom_filter_info.id = kZoomFilterId;
	zoom_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	zoom_filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	zoom_filter_info.get_name = zoom_filter_get_name;
	zoom_filter_info.create = zoom_filter_create;
	zoom_filter_info.destroy = zoom_filter_destroy;
	zoom_filter_info.update = zoom_filter_update;
	zoom_filter_info.get_defaults = zoom_filter_defaults;
	zoom_filter_info.get_properties = zoom_filter_properties;
	zoom_filter_info.video_render = zoom_filter_render;
	zoom_filter_info.get_width = zoom_filter_width;
	zoom_filter_info.get_height = zoom_filter_height;
	legacy_zoom_filter_info = zoom_filter_info;
	legacy_zoom_filter_info.id = kLegacyZoomFilterId;
}

static obs_source_t *get_or_create_zoom_filter(obs_source_t *source)
{
	obs_source_t *filter = obs_source_get_filter_by_name(source, kZoomFilterName);
	if (filter) {
		return filter;
	}

	/* Keep using filters created by versions before the display-name change. */
	filter = obs_source_get_filter_by_name(source, kLegacyZoomFilterName);
	if (filter) {
		return filter;
	}

	filter = obs_source_get_filter_by_name(source, kOlderLegacyZoomFilterName);
	if (filter) {
		return filter;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "state_version", kZoomStateVersion);
	obs_data_set_double(settings, "zoom", kMinimumZoom);
	obs_data_set_double(settings, "offset_x", 0.0);
	obs_data_set_double(settings, "offset_y", 0.0);
	obs_source_t *created = obs_source_create_private(kZoomFilterId, kZoomFilterName, settings);
	obs_data_release(settings);
	if (!created) {
		return nullptr;
	}

	obs_source_filter_add(source, created);
	obs_source_release(created);
	return obs_source_get_filter_by_name(source, kZoomFilterName);
}

static bool get_zoom_filter_state(obs_source_t *source, ZoomFilterState &state)
{
	obs_source_t *filter = get_or_create_zoom_filter(source);
	if (!filter) {
		return false;
	}

	obs_data_t *settings = obs_source_get_settings(filter);
	state.zoom = clamp_zoom(obs_data_get_double(settings, "zoom"));
	state.offsetX = clamp_offset(obs_data_get_double(settings, "offset_x"), state.zoom);
	state.offsetY = clamp_offset(obs_data_get_double(settings, "offset_y"), state.zoom);
	obs_data_release(settings);
	obs_source_release(filter);
	return true;
}

static bool set_zoom_filter_state(obs_source_t *source, const ZoomFilterState &state)
{
	obs_source_t *filter = get_or_create_zoom_filter(source);
	if (!filter) {
		return false;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "state_version", kZoomStateVersion);
	obs_data_set_double(settings, "zoom", clamp_zoom(state.zoom));
	obs_data_set_double(settings, "offset_x", clamp_offset(state.offsetX, state.zoom));
	obs_data_set_double(settings, "offset_y", clamp_offset(state.offsetY, state.zoom));
	obs_source_update(filter, settings);
	obs_data_release(settings);
	obs_source_release(filter);
	return true;
}

static bool source_uv_from_anchor(obs_scene_t *scene, obs_sceneitem_t *item, const CanvasPoint &anchor, vec2 &uv)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	const uint32_t sourceWidth = source ? obs_source_get_width(source) : 0;
	const uint32_t sourceHeight = source ? obs_source_get_height(source) : 0;
	if (!source || sourceWidth == 0 || sourceHeight == 0) {
		return false;
	}

	CanvasPoint itemAnchor = anchor;
	obs_sceneitem_t *group = obs_sceneitem_get_group(scene, item);
	if (group) {
		matrix4 groupTransform;
		matrix4 inverseGroupTransform;
		obs_sceneitem_get_draw_transform(group, &groupTransform);
		if (!matrix4_inv(&inverseGroupTransform, &groupTransform)) {
			return false;
		}

		vec3 canvasPoint;
		vec3 localPoint;
		vec3_set(&canvasPoint, float(anchor.x), float(anchor.y), 0.0f);
		vec3_transform(&localPoint, &canvasPoint, &inverseGroupTransform);
		itemAnchor.x = localPoint.x;
		itemAnchor.y = localPoint.y;
	}

	matrix4 boxTransform;
	matrix4 inverseBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	if (!matrix4_inv(&inverseBoxTransform, &boxTransform)) {
		return false;
	}

	vec3 canvasPoint;
	vec3 boxPoint;
	vec3_set(&canvasPoint, float(itemAnchor.x), float(itemAnchor.y), 0.0f);
	vec3_transform(&boxPoint, &canvasPoint, &inverseBoxTransform);

	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	const uint32_t visibleWidth = uint32_t(std::max<int64_t>(1, int64_t(sourceWidth) - crop.left - crop.right));
	const uint32_t visibleHeight = uint32_t(std::max<int64_t>(1, int64_t(sourceHeight) - crop.top - crop.bottom));
	uv.x = float(std::clamp((double(crop.left) + double(boxPoint.x) * visibleWidth) / sourceWidth, 0.0, 1.0));
	uv.y = float(std::clamp((double(crop.top) + double(boxPoint.y) * visibleHeight) / sourceHeight, 0.0, 1.0));
	return true;
}

struct SelectedItems {
	std::vector<obs_sceneitem_t *> items;
};

static bool collect_selected_items(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	SelectedItems *selected = static_cast<SelectedItems *>(param);
	if (!obs_sceneitem_visible(item)) {
		return true;
	}

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

static void release_selected_items(SelectedItems &selected)
{
	for (obs_sceneitem_t *item : selected.items) {
		obs_sceneitem_release(item);
	}
	selected.items.clear();
}

struct FilterSnapshot {
	std::string sourceUuid;
	ZoomFilterState state;
};

static std::string serialize_filter_states(const std::vector<FilterSnapshot> &snapshots)
{
	obs_data_t *wrapper = obs_data_create();
	obs_data_array_t *items = obs_data_array_create();
	for (const FilterSnapshot &snapshot : snapshots) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "source_uuid", snapshot.sourceUuid.c_str());
		obs_data_set_double(item, "zoom", snapshot.state.zoom);
		obs_data_set_double(item, "offset_x", snapshot.state.offsetX);
		obs_data_set_double(item, "offset_y", snapshot.state.offsetY);
		obs_data_array_push_back(items, item);
		obs_data_release(item);
	}
	obs_data_set_array(wrapper, "items", items);
	obs_data_array_release(items);
	const char *json = obs_data_get_json(wrapper);
	const std::string result = json ? json : "";
	obs_data_release(wrapper);
	return result;
}

static void apply_filter_states(const char *json)
{
	if (!json || !*json) {
		return;
	}

	obs_data_t *wrapper = obs_data_create_from_json(json);
	if (!wrapper) {
		return;
	}
	obs_data_array_t *items = obs_data_get_array(wrapper, "items");
	if (items) {
		for (size_t index = 0; index < obs_data_array_count(items); ++index) {
			obs_data_t *item = obs_data_array_item(items, index);
			const char *uuid = obs_data_get_string(item, "source_uuid");
			obs_source_t *source = obs_get_source_by_uuid(uuid);
			if (source) {
				ZoomFilterState state;
				state.zoom = obs_data_get_double(item, "zoom");
				state.offsetX = obs_data_get_double(item, "offset_x");
				state.offsetY = obs_data_get_double(item, "offset_y");
				set_zoom_filter_state(source, state);
				obs_source_release(source);
			}
			obs_data_release(item);
		}
		obs_data_array_release(items);
	}
	obs_data_release(wrapper);
}

static bool zoom_selected_items(const CanvasPoint &anchor, double factor)
{
	obs_source_t *previewSource = obs_frontend_get_current_preview_scene();
	obs_source_t *programSource = obs_frontend_get_current_scene();
	obs_scene_t *previewScene = previewSource ? obs_scene_from_source(previewSource) : nullptr;
	obs_scene_t *programScene = programSource ? obs_scene_from_source(programSource) : nullptr;

	SelectedItems selected;
	obs_scene_t *selectedScene = nullptr;
	if (previewScene) {
		obs_scene_enum_items(previewScene, collect_selected_items, &selected);
		if (!selected.items.empty()) {
			selectedScene = previewScene;
		}
	}
	if (selected.items.empty() && programScene && programScene != previewScene) {
		obs_scene_enum_items(programScene, collect_selected_items, &selected);
		if (!selected.items.empty()) {
			selectedScene = programScene;
		}
	}
	if (selected.items.empty()) {
		if (previewSource) {
			obs_source_release(previewSource);
		}
		if (programSource) {
			obs_source_release(programSource);
		}
		return false;
	}

	std::vector<std::string> processedSources;
	std::vector<FilterSnapshot> before;
	std::vector<FilterSnapshot> after;

	for (obs_sceneitem_t *item : selected.items) {
		if (!obs_sceneitem_visible(item)) {
			obs_sceneitem_release(item);
			continue;
		}

		obs_source_t *source = obs_sceneitem_get_source(item);
		const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
		if (!source || !uuid || std::find(processedSources.begin(), processedSources.end(), uuid) != processedSources.end()) {
			obs_sceneitem_release(item);
			continue;
		}

		ZoomFilterState current;
		vec2 sourceUv;
		if (!get_zoom_filter_state(source, current) || !source_uv_from_anchor(selectedScene, item, anchor, sourceUv)) {
			obs_sceneitem_release(item);
			continue;
		}

		processedSources.emplace_back(uuid);
		before.push_back({uuid, current});

		ZoomFilterState next = current;
		next.zoom = clamp_zoom(current.zoom * factor);
		if (std::abs(next.zoom - kMinimumZoom) < 0.000001) {
			next.zoom = kMinimumZoom;
			next.offsetX = 0.0;
			next.offsetY = 0.0;
		} else {
			const double sourceAtCursorX = double(sourceUv.x) / current.zoom + current.offsetX;
			const double sourceAtCursorY = double(sourceUv.y) / current.zoom + current.offsetY;
			next.offsetX = clamp_offset(sourceAtCursorX - double(sourceUv.x) / next.zoom, next.zoom);
			next.offsetY = clamp_offset(sourceAtCursorY - double(sourceUv.y) / next.zoom, next.zoom);
		}

		if (set_zoom_filter_state(source, next)) {
			after.push_back({uuid, next});
		}
		obs_sceneitem_release(item);
	}

	if (!before.empty() && before.size() == after.size()) {
		const std::string undoStates = serialize_filter_states(before);
		const std::string redoStates = serialize_filter_states(after);
		if (!undoStates.empty() && !redoStates.empty() && undoStates != redoStates) {
			obs_frontend_add_undo_redo_action("Zoom selected source", apply_filter_states, apply_filter_states,
								 undoStates.c_str(), redoStates.c_str(), true);
		}
	}

	if (previewSource) {
		obs_source_release(previewSource);
	}
	if (programSource) {
		obs_source_release(programSource);
	}
	return !before.empty();
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
		if (pluginSettings.modifier != int(Qt::NoModifier) &&
		    !wheel->modifiers().testFlag(static_cast<Qt::KeyboardModifier>(pluginSettings.modifier))) {
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

		const double factor = std::pow(pluginSettings.zoomPerWheelStep, double(delta) / 120.0);
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
	load_plugin_settings();
	initialize_zoom_filter_info();
	obs_register_source(&zoom_filter_info);
	obs_register_source(&legacy_zoom_filter_info);

	QWidget *window = main_window();
	if (!window || !qApp) {
		blog(LOG_WARNING, "obs-wheelzoom: OBS main window or Qt application is unavailable");
		return false;
	}

	settingsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("OBS_wheelzoom"));
	if (settingsAction) {
		QObject::connect(settingsAction, &QAction::triggered, qApp, []() { open_settings_dialog(); });
	}

	filter = new ZoomScrollFilter(nullptr);
	qApp->installEventFilter(filter);
	blog(LOG_INFO, "obs-wheelzoom loaded: Ctrl+wheel applies content zoom to selected sources");
	return true;
}

void obs_module_unload(void)
{
	if (settingsAction) {
		QObject::disconnect(settingsAction, nullptr, nullptr, nullptr);
		settingsAction = nullptr;
	}

	if (filter && qApp) {
		qApp->removeEventFilter(filter);
		delete filter;
		filter = nullptr;
	}
	blog(LOG_INFO, "obs-wheelzoom unloaded");
}
