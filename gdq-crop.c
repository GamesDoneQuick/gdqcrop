#include <util/dstr.h>
#include <util/platform.h>
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/math-defs.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>



#define S_RESOLUTION                    "resolution"
#define S_SAMPLING                      "sampling"

#define T_RESOLUTION                    obs_module_text("Resolution")
#define T_NONE                          obs_module_text("None")

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("gdq-crop", "en-US")

struct Preset {
	char name[255];
	double left;
	double right;
	double top;
	double bottom;
} presets[255];
int preset_count = 0;


struct crop_filter_data {
	obs_source_t                   *context;

	gs_effect_t                    *effect;
	gs_eparam_t                    *param_mul;
	gs_eparam_t                    *param_add;

	int                            left;
	int                            right;
	int                            top;
	int                            bottom;
	uint32_t                       width;
	uint32_t                       height;

	struct vec2                    mul_val;
	struct vec2                    add_val;



	gs_eparam_t                     *image_param;
	gs_eparam_t                     *dimension_param;
	gs_eparam_t                     *undistort_factor_param;
	struct vec2                     dimension_i;
	double                          undistort_factor;
	int                             cx_in;
	int                             cy_in;
	int                             cx_out;
	int                             cy_out;
	enum obs_scale_type             sampling;
	gs_samplerstate_t               *point_sampler;
	bool                            aspect_ratio_only;
	bool                            target_valid;
	bool                            valid;
};

static const double downscale_vals[] = {
	1.0,
	1.25,
	(1.0 / 0.75),
	1.5,
	(1.0 / 0.6),
	1.75,
	2.0,
	2.25,
	2.5,
	2.75,
	3.0
};

#define NUM_DOWNSCALES (sizeof(downscale_vals) / sizeof(double))

static const char *aspects[] = {
	"16:9",
	"16:10",
	"4:3",
	"1:1"
};

#define NUM_ASPECTS (sizeof(aspects) / sizeof(const char *))



static const char *crop_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("GDQ Crop");
}


static void *crop_filter_create(obs_data_t *settings, obs_source_t *context)
{
	struct crop_filter_data *filter = bzalloc(sizeof(*filter));
	char *effect_path = obs_module_file("crop_lanczos_scale.effect");

	filter->context = context;
	struct gs_sampler_info sampler_info = { 0 };

	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	filter->point_sampler = gs_samplerstate_create(&sampler_info);
	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		bfree(filter);
		return NULL;
	}

	filter->param_mul = gs_effect_get_param_by_name(filter->effect,
		"mul_val");
	filter->param_add = gs_effect_get_param_by_name(filter->effect,
		"add_val");

	obs_source_update(context, settings);
	return filter;
}


static void crop_filter_destroy(void *data)
{
	struct crop_filter_data *filter = data;

	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	gs_samplerstate_destroy(filter->point_sampler);
	obs_leave_graphics();

	bfree(filter);
}


static void crop_filter_update(void *data, obs_data_t *settings)
{
	struct crop_filter_data *filter = data;

	filter->left = (int)obs_data_get_int(settings, "left");
	filter->top = (int)obs_data_get_int(settings, "top");
	filter->right = (int)obs_data_get_int(settings, "right");
	filter->bottom = (int)obs_data_get_int(settings, "bottom");

	if ((filter->left == 0) &&
		(filter->top == 0) &&
		(filter->right == 0) &&
		(filter->bottom == 0)) {
		obs_data_set_string(settings, "console", "None");
	}
	else {
		obs_data_set_string(settings, "console", "Custom");
	}


	const char *res_str = obs_data_get_string(settings, S_RESOLUTION);
	const char *sampling = obs_data_get_string(settings, S_SAMPLING);

	filter->valid = true;

	int ret = sscanf(res_str, "%dx%d", &filter->cx_in, &filter->cy_in);
	if (ret == 2) {
		filter->aspect_ratio_only = false;
	}
	else {
		ret = sscanf(res_str, "%d:%d", &filter->cx_in, &filter->cy_in);
		if (ret != 2) {
			filter->valid = false;
			return;
		}

		filter->aspect_ratio_only = true;
	}

}


static bool res_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	bool aspect_ratio_only;
	uint32_t width, height;
	uint32_t res_x, res_y;
	uint32_t newWidth, newHeight;
	struct crop_filter_data *filter = obs_properties_get_param(props);

	obs_source_t *target = obs_filter_get_target(filter->context);	
	if (!target) {
		width = 0;
		height = 0;
	}
	else {
		width = obs_source_get_base_width(target);
		height = obs_source_get_base_height(target);
	}

	if (!height || !height) {
		obs_property_int_set_limits(obs_properties_get(props, "left"), 0, 0, 1);
		obs_property_int_set_limits(obs_properties_get(props, "top"), 0, 0, 1);
		obs_property_int_set_limits(obs_properties_get(props, "right"), 0, 0, 1);
		obs_property_int_set_limits(obs_properties_get(props, "bottom"), 0, 0, 1);
		UNUSED_PARAMETER(p);
		return true;
	}

	newWidth = width;
	newHeight = height;

	const char *res_str = obs_data_get_string(settings, S_RESOLUTION);
	int ret = sscanf(res_str, "%dx%d", &res_x, &res_y);
	if (ret == 2) {
		aspect_ratio_only = false;
	}
	else {
		ret = sscanf(res_str, "%d:%d", &res_x, &res_y);
		if (ret != 2) {
			obs_property_int_set_limits(obs_properties_get(props, "left"), 0, 0, 1);
			obs_property_int_set_limits(obs_properties_get(props, "top"), 0, 0, 1);
			obs_property_int_set_limits(obs_properties_get(props, "right"), 0, 0, 1);
			obs_property_int_set_limits(obs_properties_get(props, "bottom"), 0, 0, 1);
			UNUSED_PARAMETER(p);
			return true;
		}

		aspect_ratio_only = true;
	}

	double old_aspect = (double)width / (double)height;
	double new_aspect = (double)res_x / (double)res_y;

	if (aspect_ratio_only) {
		if (fabs(old_aspect - new_aspect) > EPSILON) {
			if (new_aspect > old_aspect) {
				newWidth = (int)(height * new_aspect);
				newHeight = height;
			}
			else {
				newWidth = width;
				newHeight = (int)(width / new_aspect);
			}
		}
	}
	else {
		newWidth = res_x;
		newHeight = res_y;
	}

	obs_property_t* slider_prop;
	double perCrop;

	slider_prop = obs_properties_get(props, "left");
	perCrop = (double)obs_data_get_int(settings, "left") / (double)obs_property_int_max(slider_prop);
	obs_property_int_set_limits(slider_prop, 0, newWidth, 1);
	obs_data_set_int(settings, "left", (int)round(perCrop * newWidth));

	slider_prop = obs_properties_get(props, "right");
	perCrop = (double)obs_data_get_int(settings, "right") / (double)obs_property_int_max(slider_prop);
	obs_property_int_set_limits(slider_prop, 0, newWidth, 1);
	obs_data_set_int(settings, "right", (int)round(perCrop * newWidth));

	slider_prop = obs_properties_get(props, "top");
	perCrop = (double)obs_data_get_int(settings, "top") / (double)obs_property_int_max(slider_prop);
	obs_property_int_set_limits(slider_prop, 0, newHeight, 1);
	obs_data_set_int(settings, "top", (int)round(perCrop * newHeight));

	slider_prop = obs_properties_get(props, "bottom");
	perCrop = (double)obs_data_get_int(settings, "bottom") / (double)obs_property_int_max(slider_prop);
	obs_property_int_set_limits(slider_prop, 0, newHeight, 1);
	obs_data_set_int(settings, "bottom", (int)round(perCrop * newHeight));

	UNUSED_PARAMETER(p);
	return true;
}


static bool console_type_modified(obs_properties_t *props, obs_property_t *p,
	obs_data_t *settings)
{
	const char *console_type = obs_data_get_string(settings, "console_type");
	if (strcmp(console_type, "4:3") == 0) {
		obs_data_set_string(settings, S_RESOLUTION, "4:3");
		obs_property_set_visible(obs_properties_get(props, S_RESOLUTION), false);
	}
	else if (strcmp(console_type, "16:9") == 0) {
		obs_data_set_string(settings, S_RESOLUTION, "16:9");
		obs_property_set_visible(obs_properties_get(props, S_RESOLUTION), false);
	}
	else {
		obs_property_set_visible(obs_properties_get(props, S_RESOLUTION), true);
	}

	res_modified(props, p, settings);

	UNUSED_PARAMETER(p);
	return true;
}


static bool console_modified(obs_properties_t *props, obs_property_t *p,
	obs_data_t *settings)
{
	const char *console = obs_data_get_string(settings, "console");
	if (strcmp(console, "None") == 0) {
		obs_data_set_int(settings, "right", 0);
		obs_data_set_int(settings, "left", 0);
		obs_data_set_int(settings, "top", 0);
		obs_data_set_int(settings, "bottom", 0);
	}
	else {
		for (int i = 0; i < preset_count; i++) {
			if (strcmp(console, presets[i].name) == 0) {
				obs_data_set_int(settings, "right", (int)round(presets[i].right * (double)obs_property_int_max(obs_properties_get(props, "right"))));
				obs_data_set_int(settings, "left", (int)round(presets[i].left * (double)obs_property_int_max(obs_properties_get(props, "left"))));
				obs_data_set_int(settings, "top", (int)round(presets[i].top * (double)obs_property_int_max(obs_properties_get(props, "top"))));
				obs_data_set_int(settings, "bottom", (int)round(presets[i].bottom * (double)obs_property_int_max(obs_properties_get(props, "bottom"))));
				break;
			}
		}
	}

	UNUSED_PARAMETER(p);
	return true;
}


static bool new_console_clicked(obs_properties_t *props, obs_property_t *p,
	void *data)
{
	struct crop_filter_data* filter = data;
	obs_data_t* settings = obs_source_get_settings(filter->context);

	// Do not save if no name set
	const char* newconsole = obs_data_get_string(settings, "newconsole");
	if (newconsole == NULL || strlen(newconsole) == 0) {
		return false;
	}

	// Reserved preset names
	if ((strcmp(newconsole, "Custom") == 0) ||
		(strcmp(newconsole, "None") == 0)) {
		return false;
	}

	//Check if preset already exists
	int newPresetIndex = preset_count;
	bool presetFound = false;
	for (int i = 0; i < preset_count; i++) {
		if (strcmp(newconsole, presets[i].name) == 0) {
			newPresetIndex = i;
			presetFound = true;
			break;
		}
	}
	
	// Update preset struct
	strcpy(presets[newPresetIndex].name, newconsole);
	presets[newPresetIndex].left = (double)obs_data_get_int(settings, "left") / (double)obs_property_int_max(obs_properties_get(props, "left"));
	presets[newPresetIndex].right = (double)obs_data_get_int(settings, "right") / (double)obs_property_int_max(obs_properties_get(props, "right"));
	presets[newPresetIndex].top = (double)obs_data_get_int(settings, "top") / (double)obs_property_int_max(obs_properties_get(props, "top"));
	presets[newPresetIndex].bottom = (double)obs_data_get_int(settings, "bottom") / (double)obs_property_int_max(obs_properties_get(props, "bottom"));

	// Add new preset to list
	if (!presetFound) {
		obs_property_t* listp = obs_properties_get(props, "console");
		obs_property_list_add_string(listp, presets[newPresetIndex].name, presets[newPresetIndex].name);
	
		preset_count++;
	}
	
	// Update UI
	obs_data_set_string(settings, "console", presets[newPresetIndex].name);
	obs_data_set_string(settings, "newconsole", "");

	// Save new config file
	FILE* file = fopen("gdq-crop.cfg", "w");
	for (int i = 0; i < preset_count; i++) {
		fprintf(file, "%s\n\tleft:%0.7f, right:%0.7f, top:%0.7f, bottom:%0.7f\n",
			presets[i].name,
			presets[i].left,
			presets[i].right,
			presets[i].top,
			presets[i].bottom);
	}
	fclose(file);


	UNUSED_PARAMETER(p);
	return true;
}


static obs_properties_t *crop_filter_properties(void *data)
{
	struct crop_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width, height;
	uint32_t newWidth, newHeight;

	if (!target) {
		width = 0;
		height = 0;
	}
	else {
		width = obs_source_get_base_width(target);
		height = obs_source_get_base_height(target);
	}

	newWidth = width;
	newHeight = height;

	double old_aspect = (double)width / (double)height;
	double new_aspect = (double)filter->cx_in / (double)filter->cy_in;

	if (filter->aspect_ratio_only) {
		if (fabs(old_aspect - new_aspect) > EPSILON) {
			if (new_aspect > old_aspect) {
				newWidth = (int)(height * new_aspect);
				newHeight = height;
			}
			else {
				newWidth = width;
				newHeight = (int)(width / new_aspect);
			}
		}
	}
	else {
		newWidth = filter->cx_in;
		newHeight = filter->cy_in;
	}


	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	obs_properties_set_param(props, filter, NULL);

	p = obs_properties_add_list(props, "console_type", "Input Source Aspect",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "SD Console [4:3]", "4:3");
	obs_property_list_add_string(p, "PC/HD Console [16:9]", "16:9");
	obs_property_list_add_string(p, "override [Do not use]", "override");
	obs_property_set_modified_callback(p, console_type_modified);

	struct obs_video_info ovi;
	uint32_t cx;
	uint32_t cy;

	struct {
		int cx;
		int cy;
	} downscales[NUM_DOWNSCALES];

	/* ----------------- */

	obs_get_video_info(&ovi);
	cx = ovi.base_width;
	cy = ovi.base_height;

	for (size_t i = 0; i < NUM_DOWNSCALES; i++) {
		downscales[i].cx = (int)((double)cx / downscale_vals[i]);
		downscales[i].cy = (int)((double)cy / downscale_vals[i]);
	}

	/* ----------------- */

	p = obs_properties_add_list(props, S_RESOLUTION, T_RESOLUTION,
		OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p, T_NONE, T_NONE);

	for (size_t i = 0; i < NUM_ASPECTS; i++)
		obs_property_list_add_string(p, aspects[i], aspects[i]);

	for (size_t i = 0; i < NUM_DOWNSCALES; i++) {
		char str[32];
		snprintf(str, 32, "%dx%d", downscales[i].cx, downscales[i].cy);
		obs_property_list_add_string(p, str, str);
	}
	obs_property_set_modified_callback(p, res_modified);
	obs_property_set_visible(p, false);

	p = obs_properties_add_list(props, "console", "Cropping Preset",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "Custom", "Custom");
	obs_property_list_add_string(p, "None", "None");
	for (int i = 0; i < preset_count; i++) {
		obs_property_list_add_string(p, presets[i].name, presets[i].name);
	}
	obs_property_set_modified_callback(p, console_modified);

	obs_properties_add_int_slider(props, "left", obs_module_text("Crop.Left"), 0, newWidth, 1);
	obs_properties_add_int_slider(props, "top", obs_module_text("Crop.Top"), 0, newHeight, 1);
	obs_properties_add_int_slider(props, "right", obs_module_text("Crop.Right"), 0, newWidth, 1);
	obs_properties_add_int_slider(props, "bottom", obs_module_text("Crop.Bottom"), 0, newHeight, 1);

	obs_properties_add_text(props, "newconsole", "New Preset Name", OBS_TEXT_DEFAULT);
	obs_properties_add_button(props, "newbutton", "Save New Preset", new_console_clicked);
	
	UNUSED_PARAMETER(data);
	return props;
}


static void crop_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "console", "None");
	obs_data_set_default_string(settings, "console_type", "4:3");
	obs_data_set_default_string(settings, S_RESOLUTION, "4:3");
}


static void calc_crop_dimensions(struct crop_filter_data *filter,
	struct vec2 *mul_val, struct vec2 *add_val)
{
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width;
	uint32_t height;
	uint32_t total;

	if (!target) {
		width = 0;
		height = 0;
	}
	else {
		width = filter->cx_out;
		height = filter->cy_out;
	}

	total = filter->left + filter->right;
	filter->width = total > width ? 0 : (width - total);

	total = filter->top + filter->bottom;
	filter->height = total > height ? 0 : (height - total);

	vec2_zero(&filter->mul_val);
	vec2_zero(&filter->add_val);

	if (width && filter->width) {
		mul_val->x = (float)filter->width / (float)width;
		add_val->x = (float)filter->left / (float)width;
	}

	if (height && filter->height) {
		mul_val->y = (float)filter->height / (float)height;
		add_val->y = (float)filter->top / (float)height;
	}
}


static void crop_filter_tick(void *data, float seconds)
{
	struct crop_filter_data *filter = data;

	enum obs_base_effect type;
	obs_source_t *target;
	bool lower_than_2x;
	double cx_f;
	double cy_f;
	int cx;
	int cy;

	target = obs_filter_get_target(filter->context);
	filter->cx_out = 0;
	filter->cy_out = 0;

	filter->target_valid = !!target;
	if (!filter->target_valid)
		return;

	cx = obs_source_get_base_width(target);
	cy = obs_source_get_base_height(target);

	if (!cx || !cy) {
		filter->target_valid = false;
		return;
	}

	filter->cx_out = cx;
	filter->cy_out = cy;

	if (!filter->valid)
		return;

	/* ------------------------- */

	cx_f = (double)cx;
	cy_f = (double)cy;

	double old_aspect = cx_f / cy_f;
	double new_aspect =
		(double)filter->cx_in / (double)filter->cy_in;

	if (filter->aspect_ratio_only) {
		if (fabs(old_aspect - new_aspect) <= EPSILON) {
			filter->target_valid = false;
			return;
		}
		else {
			if (new_aspect > old_aspect) {
				filter->cx_out = (int)(cy_f * new_aspect);
				filter->cy_out = cy;
			}
			else {
				filter->cx_out = cx;
				filter->cy_out = (int)(cx_f / new_aspect);
			}
		}
	}
	else {
		filter->cx_out = filter->cx_in;
		filter->cy_out = filter->cy_in;
	}

	vec2_set(&filter->dimension_i,
		1.0f / (float)cx,
		1.0f / (float)cy);

	filter->undistort_factor = new_aspect / old_aspect;

	/* ------------------------- */

	calc_crop_dimensions(filter, &filter->mul_val, &filter->add_val);


	filter->image_param = gs_effect_get_param_by_name(filter->effect,
		"image");

	filter->dimension_param = gs_effect_get_param_by_name(
		filter->effect, "base_dimension_i");

	filter->undistort_factor_param = gs_effect_get_param_by_name(
		filter->effect, "undistort_factor");

	UNUSED_PARAMETER(seconds);

}


static void crop_filter_render(void *data, gs_effect_t *effect)
{
	struct crop_filter_data *filter = data;

	const char *technique = "DrawUndistort";

	if (!filter->valid || !filter->target_valid) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
		OBS_ALLOW_DIRECT_RENDERING))
		return;

	if (filter->dimension_param)
		gs_effect_set_vec2(filter->dimension_param,
		&filter->dimension_i);

	if (filter->undistort_factor_param)
		gs_effect_set_float(filter->undistort_factor_param,
		(float)filter->undistort_factor);

	if (filter->sampling == OBS_SCALE_POINT)
		gs_effect_set_next_sampler(filter->image_param,
		filter->point_sampler);

	gs_effect_set_vec2(filter->param_mul, &filter->mul_val);
	gs_effect_set_vec2(filter->param_add, &filter->add_val);


	obs_source_process_filter_end(filter->context, filter->effect,
		filter->width, filter->height);

	UNUSED_PARAMETER(effect);
}


static uint32_t crop_filter_width(void *data)
{
	struct crop_filter_data *crop = data;

	return crop->width;
}


static uint32_t crop_filter_height(void *data)
{
	struct crop_filter_data *crop = data;
	return crop->height;
}


struct obs_source_info gdq_crop_filter = {
	.id = "gdq_crop_console_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = crop_filter_get_name,
	.create = crop_filter_create,
	.destroy = crop_filter_destroy,
	.update = crop_filter_update,
	.get_properties = crop_filter_properties,
	.get_defaults = crop_filter_defaults,
	.video_tick = crop_filter_tick,
	.video_render = crop_filter_render,
	.get_width = crop_filter_width,
	.get_height = crop_filter_height
};

bool obs_module_load(void)
{
	FILE* file = fopen("gdq-crop.cfg", "r");

	if (file) {
		while (!feof(file) && preset_count < 255) {
			fgets(presets[preset_count].name, 255, file);
			int len = strlen(presets[preset_count].name);
			if (len == 0) {
				break;
			}
			if (presets[preset_count].name[len - 1] == '\n') {
				presets[preset_count].name[len - 1] = 0;
			}

			fscanf(file, "\tleft:%lf, right:%lf, top:%lf, bottom:%lf\n",
				&presets[preset_count].left,
				&presets[preset_count].right,
				&presets[preset_count].top,
				&presets[preset_count].bottom);
			preset_count++;
		};

		fclose(file);
	}

	obs_register_source(&gdq_crop_filter);

	return true;
}
