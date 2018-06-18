#include <obs-module.h>
#include <graphics/vec2.h>
#include <stdio.h>

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("gdq-crop", "en-US")

#define S_RESOLUTION                    "resolution"
#define T_RESOLUTION                    "Input Source Resolution"


static const char *aspects[] = {
	"PC or HD Consoles [16:9]",
	"SD Consoles [4:3]",
	"override [Do not use!]"
};

#define NUM_ASPECTS (sizeof(aspects) / sizeof(const char *))


struct Preset {
	char name[255];
	int left;
	int right;
	int top;
	int bottom;
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
};


static const char *crop_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("GDQ Crop");
}


static void *crop_filter_create(obs_data_t *settings, obs_source_t *context)
{
	struct crop_filter_data *filter = bzalloc(sizeof(*filter));
	char *effect_path = obs_module_file("crop_filter.effect");

	filter->context = context;

	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
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
}



void modifyScaleFilter(obs_source_t *parent, obs_source_t *child, void *param) {
	const char* resolutionName = param;
	const char* res;

	if (strcmp(resolutionName, "PC or HD Consoles [16:9]") == 0)
		res = "16:9";
	else if (strcmp(resolutionName, "SD Consoles [4:3]") == 0)
		res = "4:3";
	else if (strcmp(resolutionName, "override [Do not use!]") == 0)
		return;

	const char* name = obs_source_get_name(child);
	if (strcmp(name, "GDQ Scale") == 0) {
		obs_data_t* filtersettings = obs_source_get_settings(child);
		obs_data_set_string(filtersettings, S_RESOLUTION, res);
		obs_source_update(child, filtersettings);
		obs_data_release(filtersettings);
	}
}


static bool resolution_modified(obs_properties_t *props, obs_property_t *p,
obs_data_t *settings)
{
	struct crop_filter_data* filter = obs_properties_get_param(props);
	obs_source_t* parentSource = obs_filter_get_parent(filter->context);
	obs_source_enum_filters(parentSource, modifyScaleFilter, obs_data_get_string(settings, obs_property_name(p)));

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
				obs_data_set_int(settings, "right", presets[i].right);
				obs_data_set_int(settings, "left", presets[i].left);
				obs_data_set_int(settings, "top", presets[i].top);
				obs_data_set_int(settings, "bottom", presets[i].bottom);
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
	presets[newPresetIndex].left = obs_data_get_int(settings, "left");
	presets[newPresetIndex].right = obs_data_get_int(settings, "right");
	presets[newPresetIndex].top = obs_data_get_int(settings, "top");
	presets[newPresetIndex].bottom = obs_data_get_int(settings, "bottom");

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
		fprintf(file, "%s\n\tleft:%d, right:%d, top:%d, bottom:%d\n",
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
	uint32_t width;
	uint32_t height;

	if (!target) {
		width = 0;
		height = 0;
	}
	else {
		width = obs_source_get_base_width(target);
		height = obs_source_get_base_height(target);
	}

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	obs_properties_set_param(props, filter, NULL);

	p = obs_properties_add_list(props, S_RESOLUTION, T_RESOLUTION,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	for (size_t i = 0; i < NUM_ASPECTS; i++)
		obs_property_list_add_string(p, aspects[i], aspects[i]);
	obs_property_set_modified_callback(p, resolution_modified);

	p = obs_properties_add_list(props, "console", "Cropping Preset",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "Custom", "Custom");
	obs_property_list_add_string(p, "None", "None");
	for (int i = 0; i < preset_count; i++) {
		obs_property_list_add_string(p, presets[i].name, presets[i].name);
	}
	obs_property_set_modified_callback(p, console_modified);

	obs_properties_add_float_slider(props, "left", obs_module_text("Crop.Left"), 0, width, 1);
	obs_properties_add_float_slider(props, "top", obs_module_text("Crop.Top"), 0, height, 1);
	obs_properties_add_float_slider(props, "right", obs_module_text("Crop.Right"), 0, width, 1);
	obs_properties_add_float_slider(props, "bottom", obs_module_text("Crop.Bottom"), 0, height, 1);

	obs_properties_add_text(props, "newconsole", "New Preset Name", OBS_TEXT_DEFAULT);
	obs_properties_add_button(props, "newbutton", "Save New Preset", new_console_clicked);

	UNUSED_PARAMETER(data);
	return props;
}


static void crop_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "console", "None");
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
		width = obs_source_get_base_width(target);
		height = obs_source_get_base_height(target);
	}


	total = filter->left + filter->right;
	filter->width = total > width ? 0 : (width - total);

	total = filter->top + filter->bottom;
	filter->height = total > height ? 0 : (height - total);

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

	vec2_zero(&filter->mul_val);
	vec2_zero(&filter->add_val);
	calc_crop_dimensions(filter, &filter->mul_val, &filter->add_val);

	UNUSED_PARAMETER(seconds);
}


static void crop_filter_render(void *data, gs_effect_t *effect)
{
	struct crop_filter_data *filter = data;

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
		OBS_NO_DIRECT_RENDERING))
		return;

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
	.id = "crop_console_filter",
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

			fscanf(file, "\tleft:%d, right:%d, top:%d, bottom:%d\n",
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