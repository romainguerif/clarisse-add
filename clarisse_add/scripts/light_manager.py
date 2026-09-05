import math
import os
import re
from collections import OrderedDict

# Horizontal space between the widgets
h_spacing = 10
# Vertical space between the widgets
v_spacing = 30
window_w = 1030
# Base window height without any lights
window_h = 150
# Auto localize modified attributes from instances
localize_attributes = True
# DNEG uses spectrum instead of color. Replace if needed.
color_attr = "color"
lpe_suffix = "_lpe"
all_images_txt = "All images"
all_lpe_labels_txt = "All LPE labels"
all_groups_txt = "All light groups"
filter_txt_null_label = "Light name and/or path"
candidates = ["Key", "Rim", "Shatner", "Fill", "Sun", "Bounce", "Umbrella", "Dish", "Disk", "Primary", "Secondary",
              "Projector", "Gobo", "Character", "Eyes"]
capitalize_candidates = False
capitalize_light_names = False
light_name_suffix = "_light"
light_classes = ['LightPhysicalSpot', 'LightPhysicalPlane', 'LightPhysicalDistant', 'LightPhysicalEnvironment',
                 'LightPhysicalSphere', 'LightPhysicalCylinder', 'LightPhysicalAmbient']
light_class_prefix = "LightPhysical"
init_focus_search_widget = True
prefs = ix.application.get_prefs(ix.api.AppPreferences.MODE_APPLICATION)

if not prefs.item_exists("light_manager", "lights_per_page"):
    prefs.add_long("light_manager", "lights_per_page", 25).set_hidden(False)
prefs.get_item("light_manager", "lights_per_page").set_hidden(False)

extensions = 'All Known Files...\t*.{exr,tif,tx,hdr,tga,png,jpg,jpeg,bmp,sgi,psd,pic}\n\
Open EXR \t*.{exr}\nTIFF \t*.{tif}\nTX \t*.{tx}\n\
HDR \t*.{hdr}\nTarga \t*.{tga}\nPNG \t*.{png}\nJPG \t*.{jpg}\n\
JPEG \t*.{jpeg}\nBMP \t*.{bmp}\nSGI \t*.{sgi}\nPSD \t*.{psd}\n\
PIC \t*.{pic}'


def check_context(ctx):
    """Tests if you can write to specified context."""
    if (not ctx.is_editable()) or ctx.is_content_locked() or ctx.is_remote():
        ix.log_warning("Cannot write to path \"{}\", because it's locked.".format(str(ctx)))
        return False
    return True


class QuickDialog(ix.api.GuiWindow):
    def __init__(self, title, x, y, w, h, widgets=None):
        super(QuickDialog, self).__init__(ix.application.get_event_window(), x, y, w, 10)
        self.panel = ix.api.GuiPanel(self, 0, 0, self.get_width(), self.get_height())
        self.set_title(title)
        self.panel.set_constraints(ix.api.GuiWidget.CONSTRAINT_LEFT, ix.api.GuiWidget.CONSTRAINT_TOP,
                                   ix.api.GuiWidget.CONSTRAINT_RIGHT, ix.api.GuiWidget.CONSTRAINT_BOTTOM)
        pos_y = 10
        widget_i = 0
        for widget_settings in widgets:
            widget_key = widget_settings['key']
            current_window_pos_x = self.get_x()
            current_window_pos_y = self.get_y()
            current_window_h = self.get_height()
            if widget_settings['type'] == "txt":
                setattr(self, widget_key + "_widget",
                        ix.api.GuiLineEdit(self.panel, h_spacing + 80, pos_y + (v_spacing * widget_i),
                                           (w - h_spacing * 2) - 80, 22))
                getattr(self, widget_key + "_widget").set_constraints(ix.api.GuiWidget.CONSTRAINT_LEFT,
                                                                      ix.api.GuiWidget.CONSTRAINT_TOP,
                                                                      ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
                getattr(self, widget_key + "_widget").set_text(widget_settings['value'])
            elif widget_settings['type'] == "bool":
                setattr(self, widget_key + "_widget",
                        ix.api.GuiCheckbox(self.panel, h_spacing + 80, pos_y + (v_spacing * widget_i), ""))
                if widget_settings.get('value'):
                    getattr(self, widget_key + "_widget").set_value(widget_settings['value'])
            else:
                continue
            setattr(self, widget_key + "_lbl",
                    ix.api.GuiLabel(self.panel, h_spacing, pos_y + (v_spacing * widget_i), 80, 22,
                                    widget_settings['label'] + ":"))
            widget_i += 1
            self.resize(current_window_pos_x, current_window_pos_y, self.get_width(), current_window_h + v_spacing)

        self.ok_btn = ix.api.GuiPushButton(self.panel, h_spacing, pos_y + (v_spacing * (widget_i + 1)),
                                           int(self.get_width() / 2 - h_spacing * 1.5), 22, "Accept")
        self.ok_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP, ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
        self.ok_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.ok_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.accept)
        self.cancel_btn = ix.api.GuiPushButton(self.panel, int(self.get_width() / 2 + h_spacing * 0.5),
                                               pos_y + (v_spacing * (widget_i + 1)),
                                               int(self.get_width() / 2 - h_spacing * 1.5), 22, "Cancel")
        self.cancel_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP, ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
        self.cancel_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        # Dummy widget to store if user pressed Accept button
        self.is_accepted = ix.api.GuiCheckbox(self.panel, 0, 0, "")
        self.is_accepted.hide()
        self.connect(self.cancel_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.close_window)
        current_window_pos_x = self.get_x()
        current_window_pos_y = self.get_y()
        current_window_h = self.get_height()
        self.resize(current_window_pos_x, current_window_pos_y, self.get_width(), current_window_h + v_spacing * 2)

    def accept(self, sender, evtid):
        self.is_accepted.set_value(True)
        self.hide()

    def close_window(self, sender, evtid):
        self.hide()


class ColorField(ix.api.GuiWidget):
    def __init__(self, parent, x, y, w, h, r, g, b, light=""):
        super(ColorField, self).__init__(parent, x, y, w, h)
        self.r = int(r * 255)
        self.g = int(g * 255)
        self.b = int(b * 255)
        self.add_custom_data(self, "{},{},{},{}".format(light, self.r, self.g, self.b))

    def process_event(self, evt_id):
        if not evt_id == "EVT_ID_MOUSE_UP":
            # not interested in the event
            return 0

        # create a temporary color dialog
        color_dialog = ix.api.GuiColorDialog(ix.application, self.get_x(), self.get_y())
        color_dialog.set_dialog_title("Light color")
        color_dialog.set_rgb_color(self.r, self.g, self.b)

        # show and wait
        color_dialog.show()
        while color_dialog.is_visible():
            ix.application.check_for_events()

        # get the new color
        # note that get_rgb_color returns the values as [0-1] floating point values, so you must convert to [0-255] integers
        rgb = color_dialog.get_rgb_color()
        # If not cancel
        data = ix.api.CoreString()
        self.get_custom_data(self, data)
        light = str(data).split(",")[0]
        if rgb[0] != self.r and rgb[1] != self.g and rgb[2] != self.b:
            self.r = int(rgb[0] * 255)
            self.g = int(rgb[1] * 255)
            self.b = int(rgb[2] * 255)
            self.set_custom_data(self, "{}, {},{},{}".format(light, rgb[0], rgb[1], rgb[2]))
            self.redraw()
            # this event has been used
            return 1
        return 0

    def draw(self, dc):
        dc.draw_rectf(self.get_x(), self.get_y(), self.get_width(), self.get_height(), self.r, self.g, self.b)


class CustomGuiWindow(ix.api.GuiWindow):
    def __init__(self, *args):
        super(CustomGuiWindow, self).__init__(*args)

    def process_event(self, evt_id):
        key = ix.api.Gui.get_last_key_pressed()
        if key == ix.api.Gui.KEY_ID_ESCAPE and not self.panel.children_has_focus(self.panel):
            self.hide()
            return 1
        return super(CustomGuiWindow, self).process_event(evt_id)


class LightManagerGui(CustomGuiWindow):
    def __init__(self, title, x, y, w, h, settings=None):
        if not settings:
            settings = {}
        self.settings = settings
        super(LightManagerGui, self).__init__(ix.application.get_event_window(), x, y, w, h)
        self.set_title(title)
        # Render and connect everything
        self.draw_widgets()
        # Store the sender and evtid in a list for the command batch
        self.last_event = []

    def draw_widgets(self):
        current_window_pos_x = self.get_x()
        current_window_pos_y = self.get_y()
        current_window_w = self.get_width()
        self.resize(current_window_pos_x, current_window_pos_y, current_window_w, window_h)

        self.panel = ix.api.GuiPanel(self, 0, 0, self.get_width(), self.get_height())
        self.panel.set_constraints(ix.api.GuiWidget.CONSTRAINT_LEFT, ix.api.GuiWidget.CONSTRAINT_TOP,
                                   ix.api.GuiWidget.CONSTRAINT_RIGHT, ix.api.GuiWidget.CONSTRAINT_BOTTOM)

        self.light_states = {}
        self.isolation_mode = False
        pos_y_filter_bar = 10

        self.filter_txt = ix.api.GuiLineEdit(self.panel, h_spacing, pos_y_filter_bar, 220, 22)
        self.filter_txt.set_text(self.settings.get("filter", ""))
        self.filter_txt.set_tooltip("Type a portion of the name or the path of the light(s).")
        self.filter_txt.set_null_label(filter_txt_null_label)
        self.filter_txt.enable_candidate_popup(True)
        for candidate in candidates:
            self.filter_txt.add_candidate_value(candidate if capitalize_candidates else candidate.lower())
        self.connect(self.filter_txt, "EVT_ID_WIDGET_FOCUS_OUT", self.submit_filter_txt)

        self.filter_image_list = ix.api.GuiListButton(self.panel, 240, pos_y_filter_bar, 120, 22)
        self.filter_image_list.set_tooltip("Specify which image to search for lights")
        self.filter_image_list.add_item(all_images_txt)
        self.filter_image_list.add_separator("")
        self.filter_image_list.set_item_style(ix.api.GuiListButton.ITEM_STYLE_CHECK)
        images = self.get_images()
        selected_image_index = None
        for image_i, image in enumerate(images):
            self.filter_image_list.add_item(str(image))
            if str(image) == self.settings.get('image'):
                selected_image_index = image_i + 1
        if selected_image_index is not None:
            self.filter_image_list.set_selected_item_by_index(selected_image_index)
        self.connect(self.filter_image_list, "EVT_ID_LIST_BUTTON_SELECT", self.change_image)

        self.filter_pick_image_btn = ix.api.GuiPushButton(self.panel, 370, pos_y_filter_bar, 20, 22, ">")
        self.filter_pick_image_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.filter_pick_image_btn.set_tooltip("Select image(s)")
        self.connect(self.filter_pick_image_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.select_images)

        self.filter_image_layer_list = ix.api.GuiListButton(self.panel, 400, pos_y_filter_bar, 100, 22)
        self.filter_image_layer_list.set_tooltip("Filter lights by image layer")
        self.filter_image_layer_list.set_undefined_label("None")
        self.filter_image_layer_list.add_separator("")
        self.connect(self.filter_image_layer_list, "EVT_ID_LIST_BUTTON_SELECT", self.submit_filters)

        self.filter_pick_image_layer_btn = ix.api.GuiPushButton(self.panel, 510, pos_y_filter_bar, 20, 22, ">")
        self.filter_pick_image_layer_btn.set_tooltip("Select image layer")
        self.filter_pick_image_layer_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.filter_pick_image_layer_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.select_image_layer)

        self.filter_group_list = ix.api.GuiListButton(self.panel, 540, pos_y_filter_bar, 110, 22)
        self.filter_group_list.set_tooltip("Filter lights by a certain group")
        self.filter_group_list.add_item(all_groups_txt)
        self.filter_group_list.add_separator("")
        groups = self.get_groups()
        for group in groups:
            self.filter_group_list.add_item(str(group))
            if str(group) == str(self.settings.get('group')):
                self.filter_group_list.set_selected_item_by_name(str(group))
        self.connect(self.filter_group_list, "EVT_ID_LIST_BUTTON_SELECT", self.submit_filters)
        self.filter_pick_group_btn = ix.api.GuiPushButton(self.panel, 660, pos_y_filter_bar, 20, 22, ">")
        self.filter_pick_group_btn.set_tooltip("Select group(s)")
        self.filter_pick_group_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.filter_pick_group_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.select_groups)

        self.filter_lpe_labels_list = ix.api.GuiListButton(self.panel, 690, pos_y_filter_bar, 100, 22)
        self.filter_lpe_labels_list.add_item("All LPE labels")
        self.filter_lpe_labels_list.set_tooltip("Find lights with a certain LPE label")
        self.filter_lpe_labels_list.add_separator("")
        labels = self.get_lpe_labels()
        for label in labels:
            self.filter_lpe_labels_list.add_item(str(label))
            if label == self.settings.get('lpe_label') and self.settings.get('lpe_label') in labels:
                self.filter_lpe_labels_list.set_selected_item_by_name(str(label))
        self.connect(self.filter_lpe_labels_list, "EVT_ID_LIST_BUTTON_SELECT", self.submit_filters)
        self.create_pass_btn = ix.api.GuiPushButton(self.panel, 800, pos_y_filter_bar, 80, 22,
                                                    "Create LPE")
        self.create_pass_btn.set_tooltip("Select an image layer and LPE label to quickly create a LPE pass")
        self.create_pass_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        if not (self.settings.get('image_layer') and self.settings.get('lpe_label')):
            self.create_pass_btn.disable()
        self.connect(self.create_pass_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.create_lpe_pass)
        # Three buttons in the top right
        self.refresh_btn = ix.api.GuiPushButton(self.panel, 900, pos_y_filter_bar, 120, 22, "Refresh")
        self.refresh_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP, ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
        self.refresh_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.refresh_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.submit_filters)

        self.filter_reset_btn = ix.api.GuiPushButton(self.panel, 900, pos_y_filter_bar + v_spacing, 120, 22, "Reset")
        self.filter_reset_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP, ix.api.GuiWidget.CONSTRAINT_RIGHT,
                                              -1)
        self.filter_reset_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.filter_reset_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.reset_filters)

        self.minify_btn = ix.api.GuiPushButton(self.panel, 900, pos_y_filter_bar + v_spacing * 2, 120, 22,
                                               "Lock Window")
        self.minify_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP, ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
        self.minify_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.minify_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.toggle_border)

        pos_y_filter_bar += v_spacing
        # Light filtering and creation
        self.filter_by_label = ix.api.GuiLabel(self.panel, h_spacing, pos_y_filter_bar, 80, 22, "Filter by:")
        self.filter_by_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
        for light_class_i, light_class in enumerate(light_classes):
            setattr(self, light_class + "_btn",
                    ix.api.GuiCheckButton(self.panel, light_class_i * 90 + h_spacing + 60, pos_y_filter_bar, 80, 22,
                                          light_class.replace(light_class_prefix, "")))
            getattr(self, light_class + "_btn").set_style(ix.api.GuiCheckButton.STYLE_COUNT)
            getattr(self, light_class + "_btn").set_tooltip(
                "Show lights of type: " + light_class.replace(light_class_prefix, ""))
            self.connect(getattr(self, light_class + "_btn"), "EVT_ID_CHECK_BUTTON_CLICK", self.submit_filters)
            if self.settings and self.settings.get('light_types'):
                getattr(self, light_class + "_btn").set_value(self.settings.get('light_types').get(light_class, False))

        self.hide_instances_check_box = ix.api.GuiCheckbox(self.panel, (light_class_i + 1) * 90 + h_spacing + 60, pos_y_filter_bar + 3, "Ignore Instances")
        self.hide_instances_check_box.set_value(self.settings.get('hide_instances', False))
        self.connect(self.hide_instances_check_box, "EVT_ID_CHECKBOX_CLICK", self.submit_filters)

        self.filter_bar_separator_label = ix.api.GuiLabel(self.panel, h_spacing, pos_y_filter_bar + 20, (light_class_i + 1) * 90 + h_spacing + 220, 22, "_"*300)
        self.filter_bar_separator_label.set_text_color(ix.api.GMathVec3uc(72, 72, 72))

        pos_y_action_bar = v_spacing * 3

        self.actions_label = ix.api.GuiLabel(self.panel, h_spacing, pos_y_action_bar, 80, 22, "Actions:")
        self.actions_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        self.select_btn = ix.api.GuiPushButton(self.panel, h_spacing + 60, pos_y_action_bar, 80, 22, "Select")
        self.select_btn.set_tooltip("Select the marked lights on the left")
        self.select_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.select_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.select_lights)
        self.group_btn = ix.api.GuiPushButton(self.panel, h_spacing + 90 * 1  + 60, pos_y_action_bar, 80, 22, "Group")
        self.group_btn.set_tooltip("Group marked lights on the left")
        self.group_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.group_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.group_lights)
        self.duplicate_btn = ix.api.GuiPushButton(self.panel, h_spacing + 90 * 2  + 60, pos_y_action_bar, 80, 22,
                                                  "Duplicate")
        self.duplicate_btn.set_tooltip("Duplicate the marked lights on the left")
        self.duplicate_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.duplicate_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.duplicate_lights)
        self.instantiate_btn = ix.api.GuiPushButton(self.panel, h_spacing + 90 * 3  + 60, pos_y_action_bar, 80, 22,
                                                    "Instantiate")
        self.instantiate_btn.set_tooltip("Instantiate the marked lights on the left")
        self.instantiate_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.instantiate_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.instantiate_lights)
        self.isolate_btn = ix.api.GuiCheckButton(self.panel, h_spacing + 90 * 4  + 60, pos_y_action_bar, 80, 22, "Isolate")
        self.isolate_btn.set_tooltip(
            "Isolate the marked items on the left. Press again to undo isolation to the previous state.")
        self.isolate_btn.set_style(ix.api.GuiCheckButton.STYLE_COUNT)
        self.connect(self.isolate_btn, "EVT_ID_CHECK_BUTTON_CLICK", self.isolate_lights)
        self.remove_btn = ix.api.GuiPushButton(self.panel, h_spacing + 90 * 5 + 60, pos_y_action_bar, 80, 22, "Remove")
        self.remove_btn.set_tooltip("Remove the marked lights on the left")
        self.remove_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.remove_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.remove_lights)

        if self.settings.get('image'):
            self.change_image(self.filter_image_list, None)
        else:
            self.filter_image_layer_list.remove_all()
            self.filter_image_layer_list.disable()

        self.light_type_creation_list = ix.api.GuiListButton(self.panel, h_spacing + 90 * 6 + 60, pos_y_action_bar, 170, 22)
        self.light_type_creation_list.add_item("Create Light By Type:")
        self.light_type_creation_list.add_separator("")
        self.light_type_creation_list.set_item_style(ix.api.GuiListButton.ITEM_STYLE_NONE)
        for light_class in light_classes:
            self.light_type_creation_list.add_item(light_class.replace("LightPhysical", ""))

        self.connect(self.light_type_creation_list, "EVT_ID_LIST_BUTTON_SELECT", self.create_light)

        self.sort_mode_label = ix.api.GuiLabel(self.panel, h_spacing + 90 * 6, pos_y_action_bar, 80, 22, "Sort By:")
        self.sort_mode_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        self.sort_mode_list = ix.api.GuiListButton(self.panel, h_spacing + 90 * 6 + 50, pos_y_action_bar, 120, 22)
        self.sort_mode_list.set_style(ix.api.GuiListButton.STYLE_FLAT)
        self.sort_mode_list.add_item("Default")
        self.sort_mode_list.add_item("Default Reversed")
        self.sort_mode_list.add_separator("")
        self.sort_mode_list.add_item("Type")
        self.sort_mode_list.add_item("Type Reverse")
        self.sort_mode_list.add_item("State")
        self.sort_mode_list.add_item("State Reverse")
        self.sort_mode_list.add_item("Name")
        self.sort_mode_list.add_item("Name Reverse")
        self.sort_mode_list.add_item("Exposure")
        self.sort_mode_list.add_item("Exposure Reverse")
        self.sort_mode_list.add_item("Instanced")
        self.sort_mode_list.add_item("Instanced Reverse")
        self.sort_mode_list.add_item("Creation Date")
        self.sort_mode_list.add_item("Creation Date Reverse")
        self.sort_mode_list.add_item("Modification Date")
        self.sort_mode_list.add_item("Modification Date Reverse")
        selected_sort_mode = self.settings.get("sort_mode", None)
        if not selected_sort_mode:
            if prefs.item_exists("light_manager", "sort_mode"):
                selected_sort_mode = prefs.get_string_value("light_manager", "sort_mode")
            else:
                selected_sort_mode = "Default"
        self.sort_mode_list.set_selected_item_by_name(selected_sort_mode)
        self.connect(self.sort_mode_list, "EVT_ID_LIST_BUTTON_SELECT", self.submit_filters)

        self.display_mode_label = ix.api.GuiLabel(self.panel, h_spacing + 90 * 6 + 180, pos_y_action_bar, 80, 22,
                                                  "Display:")
        self.display_mode_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        self.display_mode_list = ix.api.GuiListButton(self.panel, h_spacing + 90 * 6 + 230, pos_y_action_bar, 130, 22)
        self.display_mode_list.set_style(ix.api.GuiListButton.STYLE_FLAT)
        self.display_mode_list.add_item("Artistic Settings")
        self.display_mode_list.add_item("Technical Settings")
        self.display_mode_list.add_item("Attenuation Settings")
        selected_display_mode = self.settings.get("display_mode", None)
        if not selected_display_mode:
            if prefs.item_exists("light_manager", "display_mode"):
                selected_display_mode = prefs.get_string_value("light_manager", "display_mode")
            else:
                selected_display_mode = "Artistic Settings"
        self.display_mode_list.set_selected_item_by_name(selected_display_mode)
        self.connect(self.display_mode_list, "EVT_ID_LIST_BUTTON_SELECT", self.submit_filters)

        pos_y_lights = pos_y_action_bar + v_spacing

        pos_mark = h_spacing
        self.mark_btn = ix.api.GuiPushButton(self.panel, pos_mark, pos_y_lights, 22, 22, "#")
        self.mark_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.mark_btn.set_tooltip(
            "Mark all or none of the lights on the left. You can apply the above actions to marked lights.")
        self.connect(self.mark_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.mark_items)

        pos_enable = pos_mark + 30
        self.enable_btn = ix.api.GuiPushButton(self.panel, pos_enable, pos_y_lights, 30, 22, "On")
        self.enable_btn.set_tooltip("Enable/disable all the lights")
        self.enable_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.connect(self.enable_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.toggle_lights)

        pos_pick = pos_enable + 40
        self.pick_label = ix.api.GuiLabel(self.panel, pos_pick, pos_y_lights, 100, 22, "Pick")
        self.pick_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        pos_name = pos_pick + 30
        self.name_label = ix.api.GuiLabel(self.panel, pos_name, pos_y_lights, 100, 22, "Name")
        self.name_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        pos_color = pos_name + 120
        self.color_label = ix.api.GuiLabel(self.panel, pos_color, pos_y_lights, 100, 22, "Color")
        self.color_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        pos_texture = pos_color + 45
        self.texture_label = ix.api.GuiLabel(self.panel, pos_texture, pos_y_lights, 100, 22, "Texture")
        self.texture_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        pos_exposure = pos_texture + 45
        self.exposure_label = ix.api.GuiLabel(self.panel, pos_exposure, pos_y_lights, 100, 22, "Exposure")
        self.exposure_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        pos_type = pos_exposure + 80
        self.type_label = ix.api.GuiLabel(self.panel, pos_type, pos_y_lights, 100, 22, "Type")
        self.type_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        if self.display_mode_list.get_selected_item_name() == "Technical Settings":
            pos_samples = pos_type + 90
            self.samples_label = ix.api.GuiLabel(self.panel, pos_samples, pos_y_lights, 80, 22, "Samples")
            self.samples_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

            pos_lpe = pos_samples + 60
            self.lpe_label = ix.api.GuiLabel(self.panel, pos_lpe, pos_y_lights, 120, 22, "LPE Label")
            self.lpe_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

            pos_groups = pos_lpe + 110
            self.groups_label = ix.api.GuiLabel(self.panel, pos_groups, pos_y_lights, 100, 22, "Groups")
            self.groups_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
        elif self.display_mode_list.get_selected_item_name() == "Attenuation Settings":
            pos_attenuation_mode = pos_type + 90
            self.attenuation_mode_label = ix.api.GuiLabel(self.panel, pos_attenuation_mode, pos_y_lights, 120, 22,
                                                          "Attenuation")
            self.attenuation_mode_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

            pos_attenuation_near_start = pos_attenuation_mode + 110
            self.attenuation_near_start_label = ix.api.GuiLabel(self.panel, pos_attenuation_near_start, pos_y_lights,
                                                                80, 22, "Near Start")
            self.attenuation_near_start_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

            pos_attenuation_near_end = pos_attenuation_near_start + 110
            self.attenuation_near_end_label = ix.api.GuiLabel(self.panel, pos_attenuation_near_end, pos_y_lights, 80,
                                                              22, "Near End")
            self.attenuation_near_end_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

            pos_attenuation_far_start = pos_attenuation_near_end + 110
            self.attenuation_far_start_label = ix.api.GuiLabel(self.panel, pos_attenuation_far_start, pos_y_lights, 80,
                                                               22, "Far Start")
            self.attenuation_far_start_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

            pos_attenuation_far_end = pos_attenuation_far_start + 110
            self.attenuation_far_end_label = ix.api.GuiLabel(self.panel, pos_attenuation_far_end, pos_y_lights, 80, 22,
                                                             "Far End")
            self.attenuation_far_end_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
        else:
            pos_light_specific = pos_type + 90
            self.light_specific_settings_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y_lights, 150, 22,
                                                                 "Light Specific Settings")
            self.light_specific_settings_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        self.lights = {}
        filtered_lights = self.get_filtered_lights()
        pagination = self.get_paginated_lights(filtered_lights, self.settings.get('page', 1))
        lights = pagination.get('lights', [])
        pos_y = pos_y_lights + v_spacing
        if lights:
            for light in lights:
                current_window_pos_x = self.get_x()
                current_window_pos_y = self.get_y()
                current_window_h = self.get_height()
                self.resize(current_window_pos_x, current_window_pos_y, window_w, current_window_h + v_spacing)
                self.lights[str(light)] = []
                mark_check_box = ix.api.GuiCheckbox(self.panel, pos_mark, pos_y, "")
                if self.settings.get('marked_lights') and str(light) in self.settings.get('marked_lights'):
                    mark_check_box.set_value(True)
                mark_check_box.set_tooltip("By checking this checkbox you can mark the light and apply the above actions to it")
                check_box_enable = ix.api.GuiCheckButton(self.panel, pos_enable, pos_y, 22, 22, "")
                check_box_enable.set_value(light.is_enabled())
                check_box_enable.set_tooltip("Enable/Disable the light")
                self.connect(check_box_enable, "EVT_ID_CHECK_BUTTON_CLICK", self.toggle_light)
                pick_btn = ix.api.GuiPushButton(self.panel, pos_pick, pos_y, 20, 22, ">")
                pick_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
                pick_btn.set_tooltip("Select the light")
                self.connect(pick_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.select_light)
                light_name_txt = ix.api.GuiLineEdit(self.panel, pos_name, pos_y, 110, 22)
                light_name_txt.set_tooltip("The name of the light object")
                light_name_txt.set_text(light.get_contextual_name())
                if light.is_instance():
                    light_name_txt.set_use_custom_color_text(True)
                    light_name_txt.set_custom_color_text(250, 140, 10)
                light_name_txt.enable_candidate_popup (True)
                for candidate in candidates:
                    light_name_txt.add_candidate_value(candidate if capitalize_candidates else candidate.lower())

                self.connect(light_name_txt, "EVT_ID_WIDGET_FOCUS_OUT", self.rename_light)
                color = getattr(light.attrs, color_attr).attr.get_vec3d()
                light_clr = ColorField(self.panel, pos_color, pos_y, 40, 22, color[0], color[1], color[2],
                                       light=str(light))
                light_clr.set_tooltip("Change the color of the light. Works only in Clarisse 4SP8+")
                self.connect(light_clr, "EVT_ID_MOUSE_UP", self.change_light_color)
                light_texture_btn = ix.api.GuiPushButton(self.panel, pos_texture + 10, pos_y, 20, 22,
                                                         ">" if getattr(light.attrs,
                                                                        color_attr).attr.is_textured() else "+")
                light_texture_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
                light_texture_btn.set_tooltip("Create or select a texture for this light. + will create and > will select.")
                self.connect(light_texture_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.select_or_create_texture)
                exposure_num = ix.api.GuiNumberField(self.panel, pos_exposure, pos_y, 70, "")
                exposure_num.set_increment(0.0025)
                exposure_num.set_value(light.attrs.exposure.attr.get_double())
                exposure_num.set_tooltip("Changes the light exposure")
                self.connect(exposure_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_exposure)
                self.connect(exposure_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_exposure)

                # TODO: Light type switcher
                # light_type_list = ix.api.GuiListButton(self.panel, pos_type, pos_y, 100, 22)
                # for light_class in light_classes:
                #     light_type_list.add_item(light_class.replace("LightPhysical", ""))
                # light_type_list.set_selected_item_by_name(light.get_class_name().replace("LightPhysical", ""))
                light_type = light.get_class_name().replace("LightPhysical", "")
                if light.is_instance():
                    light_type += "*"
                light_type_label = ix.api.GuiLabel(self.panel, pos_type, pos_y, 150, 22, light_type)
                if light.is_instance():
                    light_type_label.set_tooltip("Instanced light")
                    light_type_label.set_text_color(ix.api.GMathVec3uc(250, 140, 10))
                else:
                    light_type_label.set_text_color(ix.api.GMathVec3uc(200, 200, 200))

                extra_widgets = []

                if self.display_mode_list.get_selected_item_name() == "Technical Settings":
                    samples_num = ix.api.GuiNumberField(self.panel, pos_samples, pos_y, 50, "")
                    samples_num.set_increment(1)
                    samples_num.set_float_precision(0)
                    samples_num.set_unit_type("sample")
                    samples_num.set_slider_range(0, 16384)
                    samples_num.set_value(light.attrs.sample_count.attr.get_long())
                    samples_num.enable_slider_range(True)
                    samples_num.set_tooltip("Define the samples of the light")
                    self.connect(samples_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_samples)

                    lpe_label_txt = ix.api.GuiLineEdit(self.panel, pos_lpe, pos_y, 100, 22)
                    lpe_label_txt.set_text(light.attrs.light_path_expression_label.attr.get_string())
                    lpe_label_txt.set_tooltip("Change the LPE label")
                    self.connect(lpe_label_txt, "EVT_ID_LINE_EDIT_CHANGED", self.change_label)

                    dependencies = self.get_light_dependencies(light)
                    light_groups_list = ix.api.GuiListButton(self.panel, pos_groups, pos_y, 100, 22)
                    light_groups_list.set_tooltip("Add or remove this light from a light group")
                    light_groups_list.add_item("None" if len(dependencies) == 0 else "{} {}".format(len(dependencies),
                                                                                                    "group" if len(
                                                                                                        dependencies) == 1 else "groups"))
                    light_groups_list.add_separator("")
                    for group_i, group in enumerate(groups):
                        light_groups_list.add_item(str(group))
                        if group in dependencies:
                            light_groups_list.set_item_hint(group_i + 1, True)
                        else:
                            light_groups_list.set_item_hint(group_i + 1, False)
                    self.connect(light_groups_list, "EVT_ID_LIST_BUTTON_SELECT", self.change_light_group)
                    extra_widgets = [samples_num, lpe_label_txt, light_groups_list]
                elif self.display_mode_list.get_selected_item_name() == "Attenuation Settings":
                    if light.get_attribute('attenuation_mode') and not light.get_attribute(
                            'attenuation_mode').is_hidden():
                        light_attenuation_mode_list = ix.api.GuiListButton(self.panel, pos_attenuation_mode, pos_y, 100,
                                                                           22)
                        light_attenuation_mode_list.set_tooltip("Change the attenuation mode")
                        light_attenuation_mode_list.add_item("None")
                        light_attenuation_mode_list.add_separator("")
                        attenuation_mode = light.attrs.attenuation_mode.attr.get_long()
                        attenuation_modes = ['Near Only', 'Far Only', 'Near & Far', 'Curve']
                        for mode_i, mode in enumerate(attenuation_modes):
                            light_attenuation_mode_list.add_item(mode)
                            if attenuation_mode == mode_i + 1:
                                light_attenuation_mode_list.set_selected_item_by_index(mode_i + 1)
                        self.connect(light_attenuation_mode_list, "EVT_ID_LIST_BUTTON_SELECT",
                                     self.change_attenuation_mode)
                        attenuation_near_start_num = ix.api.GuiNumberField(self.panel, pos_attenuation_near_start,
                                                                           pos_y, 90, "")
                        attenuation_near_start_num.set_increment(0.005)
                        attenuation_near_start_num.set_float_precision(5)
                        attenuation_near_start_num.set_slider_range(0, 999999)
                        attenuation_near_start_num.set_value(light.attrs.attenuation_near_start.attr.get_double())
                        attenuation_near_start_num.enable_slider_range(True)
                        attenuation_near_start_num.set_tooltip("Change the near start attenuation setting")
                        self.connect(attenuation_near_start_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_attenuation_near_start)
                        attenuation_near_end_num = ix.api.GuiNumberField(self.panel, pos_attenuation_near_end, pos_y,
                                                                         90, "")
                        attenuation_near_end_num.set_increment(0.005)
                        attenuation_near_end_num.set_float_precision(5)
                        attenuation_near_end_num.set_slider_range(0, 999999)
                        attenuation_near_end_num.set_value(light.attrs.attenuation_near_end.attr.get_double())
                        attenuation_near_end_num.enable_slider_range(True)
                        attenuation_near_end_num.set_tooltip("Change the near end attenuation setting")
                        self.connect(attenuation_near_end_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_attenuation_near_end)
                        if not attenuation_mode in [1, 3]:
                            attenuation_near_start_num.disable()
                            attenuation_near_end_num.disable()
                        attenuation_far_start_num = ix.api.GuiNumberField(self.panel, pos_attenuation_far_start, pos_y,
                                                                          90, "")
                        attenuation_far_start_num.set_increment(0.005)
                        attenuation_far_start_num.set_float_precision(5)
                        attenuation_far_start_num.set_slider_range(0, 999999)
                        attenuation_far_start_num.set_value(light.attrs.attenuation_far_start.attr.get_double())
                        attenuation_far_start_num.enable_slider_range(True)
                        attenuation_far_start_num.set_tooltip("Change the far start attenuation setting")
                        self.connect(attenuation_far_start_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_attenuation_far_start)
                        attenuation_far_end_num = ix.api.GuiNumberField(self.panel, pos_attenuation_far_end, pos_y, 90,
                                                                        "")
                        attenuation_far_end_num.set_increment(0.005)
                        attenuation_far_end_num.set_float_precision(5)
                        attenuation_far_end_num.set_slider_range(0, 999999)
                        attenuation_far_end_num.set_value(light.attrs.attenuation_far_end.attr.get_double())
                        attenuation_far_end_num.enable_slider_range(True)
                        attenuation_far_end_num.set_tooltip("Change the far end attenuation setting")
                        self.connect(attenuation_far_end_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_attenuation_far_end)
                        if not attenuation_mode in [2, 3]:
                            attenuation_far_start_num.disable()
                            attenuation_far_end_num.disable()
                        extra_widgets = [light_attenuation_mode_list, attenuation_near_start_num,
                                         attenuation_near_end_num,
                                         attenuation_far_start_num, attenuation_far_end_num]
                else:
                    # Light specific settings:
                    if light.get_class_name() == "LightPhysicalDistant":
                        shadow_angle_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y, 50, 22, "Angle:")
                        shadow_angle_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        shadow_angle_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 50, pos_y, 60, "")
                        shadow_angle_num.set_increment(0.1)
                        shadow_angle_num.set_slider_range(0, 200)
                        shadow_angle_num.enable_slider_range(True)
                        shadow_angle_num.set_value(light.attrs.angle.attr.get_double())
                        shadow_angle_num.set_tooltip("Change the shadow angle of the light. Low values will create sharper shadows and smaller speculars. High values will create softer shadows and bigger speculars.")
                        self.connect(shadow_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_distant_light_angle)
                        self.connect(shadow_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED",
                                     self.change_distant_light_angle)
                        extra_widgets.append(shadow_angle_label)
                        extra_widgets.append(shadow_angle_num)
                    elif light.get_class_name() == "LightPhysicalPlane":
                        cone_angle_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y, 50, 22, "Angle:")
                        cone_angle_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        cone_angle_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 50, pos_y, 60, "")
                        cone_angle_num.set_increment(0.1)
                        cone_angle_num.set_slider_range(0, 100)
                        cone_angle_num.enable_slider_range(True)
                        cone_angle_num.set_value(light.attrs.cone_angle.attr.get_double())
                        cone_angle_num.set_tooltip("Change the cone angle")
                        self.connect(cone_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_light_cone_angle)
                        self.connect(cone_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_light_cone_angle)

                        softness_label = ix.api.GuiLabel(self.panel, pos_light_specific + 120, pos_y, 50, 22,
                                                         "Softness:")
                        softness_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        cone_softness_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 180, pos_y, 60, "")
                        cone_softness_num.set_increment(0.1)
                        cone_softness_num.set_slider_range(0, 180)
                        cone_softness_num.enable_slider_range(True)
                        cone_softness_num.set_value(light.attrs.cone_softness.attr.get_double() * 100)
                        cone_softness_num.set_tooltip("Change cone softness")
                        self.connect(cone_softness_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_plane_light_softness)
                        self.connect(cone_softness_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED",
                                     self.change_plane_light_softness)

                        radius_label = ix.api.GuiLabel(self.panel, pos_light_specific + 250, pos_y, 50, 22, "Radius:")
                        radius_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        radius_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 300, pos_y, 60, "")
                        radius_num.set_increment(0.00025)
                        radius_num.set_slider_range(0, 20000000000)
                        radius_num.enable_slider_range(True)
                        radius_num.set_value(light.attrs.radius.attr.get_double())
                        radius_num.set_tooltip("Change the radius of the light")
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_light_radius)
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_light_radius)

                        light_shape_label = ix.api.GuiLabel(self.panel, pos_light_specific + 370, pos_y, 50, 22,
                                                            "Shape:")
                        light_shape_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        light_shape_list = ix.api.GuiListButton(self.panel, pos_light_specific + 420, pos_y, 60, 22)
                        light_shape_list.add_item("Square")
                        light_shape_list.add_item("Disk")
                        light_shape_list.set_selected_item_by_index(light.attrs.shape.attr.get_long())
                        light_shape_list.set_tooltip("Change the light shape")
                        self.connect(light_shape_list, "EVT_ID_LIST_BUTTON_SELECT", self.change_plane_light_shape)

                        extra_widgets.append(cone_angle_label)
                        extra_widgets.append(cone_angle_num)
                        extra_widgets.append(softness_label)
                        extra_widgets.append(cone_softness_num)
                        extra_widgets.append(light_shape_label)
                        extra_widgets.append(light_shape_list)
                        extra_widgets.append(radius_label)
                        extra_widgets.append(radius_num)
                    elif light.get_class_name() == "LightPhysicalSphere":
                        radius_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y, 50, 22, "Radius:")
                        radius_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        radius_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 50, pos_y, 60, "")
                        radius_num.set_increment(0.00025)
                        radius_num.set_slider_range(0, 20000000000)
                        radius_num.enable_slider_range(True)
                        radius_num.set_value(light.attrs.radius.attr.get_double())
                        radius_num.set_tooltip("Change the radius")
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_light_radius)
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_light_radius)

                        extra_widgets.append(radius_label)
                        extra_widgets.append(radius_num)
                    elif light.get_class_name() == "LightPhysicalSpot":
                        cone_angle_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y, 50, 22, "Angle:")
                        cone_angle_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        cone_angle_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 50, pos_y, 60, "")
                        cone_angle_num.set_increment(0.1)
                        cone_angle_num.set_slider_range(0, 100)
                        cone_angle_num.enable_slider_range(True)
                        cone_angle_num.set_value(light.attrs.cone_angle.attr.get_double())
                        cone_angle_num.set_tooltip("Change the cone angle")
                        self.connect(cone_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_light_cone_angle)
                        self.connect(cone_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_light_cone_angle)
                        falloff_angle_label = ix.api.GuiLabel(self.panel, pos_light_specific + 120, pos_y, 50, 22,
                                                              "Falloff:")
                        falloff_angle_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        falloff_angle_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 180, pos_y, 60, "")
                        falloff_angle_num.set_increment(0.1)
                        falloff_angle_num.set_slider_range(0, 100)
                        falloff_angle_num.enable_slider_range(True)
                        falloff_angle_num.set_value(light.attrs.falloff_angle.attr.get_double())
                        falloff_angle_num.set_tooltip("Change the falloff angle")
                        self.connect(falloff_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_spot_light_falloff_angle)
                        self.connect(falloff_angle_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED",
                                     self.change_spot_light_falloff_angle)
                        radius_label = ix.api.GuiLabel(self.panel, pos_light_specific + 250, pos_y, 50, 22, "Radius:")
                        radius_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        radius_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 300, pos_y, 60, "")
                        radius_num.set_increment(0.00025)
                        radius_num.set_slider_range(0, 20000000000)
                        radius_num.enable_slider_range(True)
                        radius_num.set_value(light.attrs.radius.attr.get_double())
                        radius_num.set_tooltip("Change the radius")
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_light_radius)
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_light_radius)
                        extra_widgets.append(cone_angle_label)
                        extra_widgets.append(cone_angle_num)
                        extra_widgets.append(falloff_angle_label)
                        extra_widgets.append(falloff_angle_num)
                        extra_widgets.append(radius_label)
                        extra_widgets.append(radius_num)
                    elif light.get_class_name() == "LightPhysicalCylinder":
                        radius_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y, 50, 22, "Radius:")
                        radius_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        radius_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 50, pos_y, 60, "")
                        radius_num.set_increment(0.00025)
                        radius_num.set_slider_range(0, 20000000000)
                        radius_num.enable_slider_range(True)
                        radius_num.set_value(light.attrs.radius.attr.get_double())
                        radius_num.set_tooltip("Change the radius")
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_light_radius)
                        self.connect(radius_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_light_radius)
                        length_label = ix.api.GuiLabel(self.panel, pos_light_specific + 120, pos_y, 50, 22, "Length:")
                        length_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        length_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 180, pos_y, 60, "")
                        length_num.set_increment(0.001)
                        length_num.set_slider_range(0, 20000000000)
                        length_num.enable_slider_range(True)
                        length_num.set_value(light.attrs.length.attr.get_double())
                        length_num.set_tooltip("Change the length")
                        self.connect(length_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING",
                                     self.change_cylinder_light_length)
                        self.connect(length_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED",
                                     self.change_cylinder_light_length)

                        extra_widgets.append(radius_label)
                        extra_widgets.append(radius_num)
                        extra_widgets.append(length_label)
                        extra_widgets.append(length_num)
                    elif light.get_class_name() == "LightPhysicalEnvironment":
                        rot_y_label = ix.api.GuiLabel(self.panel, pos_light_specific, pos_y, 50, 22, "Rot. Y:")
                        rot_y_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))
                        rot_y_num = ix.api.GuiNumberField(self.panel, pos_light_specific + 50, pos_y, 60, "")
                        rot_y_num.set_increment(0.05)
                        rot_y_num.set_value(light.attrs.rotate[1])
                        rot_y_num.set_tooltip("Change the Y rotation")
                        self.connect(rot_y_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGING", self.change_environment_light_rot)
                        self.connect(rot_y_num, "EVT_ID_NUMBER_FIELD_VALUE_CHANGED", self.change_environment_light_rot)
                        extra_widgets.append(rot_y_label)
                        extra_widgets.append(rot_y_num)
                # Add light widgets to dict
                widgets = [mark_check_box, check_box_enable, pick_btn, light_name_txt, light_clr,
                           exposure_num, light_texture_btn, light_type_label]
                for widget in widgets + extra_widgets:
                    # widget.light_full_name = str(light)
                    if not light.is_enabled() and not widget in [mark_check_box, check_box_enable]:
                        widget.disable()
                    self.lights[str(light)].append(widget)

                pos_y += v_spacing
        current_window_pos_x = self.get_x()
        current_window_pos_y = self.get_y()
        current_window_h = self.get_height()
        self.resize(current_window_pos_x, current_window_pos_y, window_w, current_window_h + v_spacing)

        # Pagination
        num_lights_found_txt = "Showing {} to {} of {} lights found".format(pagination.get('first_index') + 1,
                                                                            pagination.get('last_index'),
                                                                            len(filtered_lights))
        if len(lights) == 0:
            num_lights_found_txt = "No lights found"
        self.num_lights_found_label = ix.api.GuiLabel(self.panel, h_spacing, pos_y, 255, 22, num_lights_found_txt)
        self.num_lights_found_label.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        self.pagination_first_page_btn = ix.api.GuiPushButton(self.panel, window_w - 225, pos_y, 25, 22, "<<")
        self.pagination_first_page_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.pagination_first_page_btn.set_tooltip("Go to the first page")
        self.pagination_first_page_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP,
                                                       ix.api.GuiWidget.CONSTRAINT_RIGHT,
                                                       -1)
        self.connect(self.pagination_first_page_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.goto_first_page)

        self.pagination_prev_page_btn = ix.api.GuiPushButton(self.panel, window_w - 195, pos_y, 25, 22, "<")
        self.pagination_prev_page_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.pagination_prev_page_btn.set_tooltip("Go to the prev page")
        if pagination.get('page') == 1:
            self.pagination_prev_page_btn.disable()
            self.pagination_first_page_btn.disable()
        self.pagination_prev_page_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP,
                                                      ix.api.GuiWidget.CONSTRAINT_RIGHT,
                                                      -1)
        self.connect(self.pagination_prev_page_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.goto_prev_page)

        self.pagination_page_list = ix.api.GuiListButton(self.panel, window_w - 165, pos_y, 95, 22)
        self.pagination_page_list.set_tooltip("Jump to a specific page")
        for page_i in range(0, pagination.get('num_pages', 1)):
            self.pagination_page_list.add_item(str(page_i + 1))
        self.pagination_page_list.set_item_style(ix.api.GuiListButton.ITEM_STYLE_CHECK)
        self.pagination_page_list.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP,
                                                  ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
        self.pagination_page_list.set_selected_item_by_index(self.settings.get('page', 1) - 1)
        self.pagination_page_list.set_undefined_label("No pages")
        self.connect(self.pagination_page_list, 'EVT_ID_LIST_BUTTON_SELECT', self.goto_page)

        self.pagination_next_page_btn = ix.api.GuiPushButton(self.panel, window_w - 65, pos_y, 25, 22, ">")
        self.pagination_next_page_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.pagination_next_page_btn.set_tooltip("Go to the next page")
        self.pagination_next_page_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP,
                                                      ix.api.GuiWidget.CONSTRAINT_RIGHT, -1)
        self.connect(self.pagination_next_page_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.goto_next_page)

        self.pagination_last_page_btn = ix.api.GuiPushButton(self.panel, window_w - 35, pos_y, 25, 22, ">>")
        self.pagination_last_page_btn.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.pagination_last_page_btn.set_tooltip("Go to the last page")
        if pagination.get('page') == pagination.get('num_pages'):
            self.pagination_next_page_btn.disable()
            self.pagination_last_page_btn.disable()
        self.pagination_last_page_btn.set_constraints(-1, ix.api.GuiWidget.CONSTRAINT_TOP,
                                                      ix.api.GuiWidget.CONSTRAINT_RIGHT,
                                                      -1)
        self.connect(self.pagination_last_page_btn, "EVT_ID_PUSH_BUTTON_CLICK", self.goto_last_page)

        self.current_page_num = ix.api.GuiNumberField(self.panel, window_w - 0, pos_y, 10, "")
        self.current_page_num.set_increment(1)
        self.current_page_num.set_float_precision(0)
        self.current_page_num.set_slider_range(0, 10000)
        self.current_page_num.set_value(self.settings.get('page', 1))
        self.current_page_num.enable_slider_range(True)
        self.current_page_num.hide()


        # Move sort and display mode to the bottom
        self.sort_mode_label.set_position(400, pos_y)
        self.sort_mode_list.set_position(450, pos_y)
        self.display_mode_label.set_position(595, pos_y)
        self.display_mode_list.set_position(645, pos_y)

        # self.connect(ix.application.get_factory(), "EVT_ID_OF_ADD_OBJECT", self.light_crud_listener)
        # self.connect(ix.application.get_factory(), "EVT_ID_OF_OBJECT_RENAME", self.light_crud_listener)
        # self.connect(ix.application.get_command_manager(), "EVT_ID_COMMAND_MANAGER_UNDO", self.light_crud_listener)
        # self.connect(ix.application.get_command_manager(), "EVT_ID_COMMAND_MANAGER_REDO", self.light_crud_listener)
        # self.connect(ix.application.get_factory(), "EVT_ID_OF_REMOVE_OBJECT", self.light_crud_listener)

    def collect_settings(self):
        settings = {
            'display_mode': self.display_mode_list.get_selected_item_name(),
            'filter': self.filter_txt.get_text(),
            'image': str(self.get_selected_image()),
            'image_layer': str(self.get_selected_image_layer()) if self.get_selected_image() else None,
            'group': self.get_selected_group(),
            'lpe_label': self.get_selected_lpe_label(),
            'light_types': self.get_filter_light_types(),
            'sort_mode': self.sort_mode_list.get_selected_item_name(),
            'marked_lights': [str(light) for light in self.get_marked_lights()],
            'hide_instances': self.hide_instances_check_box.get_value(),
            'page': self.get_current_page(),
        }
        return settings

    def refresh_window(self, pass_settings=True, pass_marked=False, focus_search_widget=False):
        settings = self.collect_settings()
        if not pass_marked:
            settings.pop('marked_lights', None)
        if not pass_settings:
            settings = {'display_mode': settings.get('display_mode'), 'sort_mode': settings.get('sort_mode')}
        if not prefs.item_exists("light_manager", "display_mode"):
            prefs.add_string("light_manager", "display_mode", '').set_hidden(True)
        prefs.set_string_value("light_manager", "display_mode", settings.get('display_mode'))
        if not prefs.item_exists("light_manager", "sort_mode"):
            prefs.add_string("light_manager", "sort_mode", '').set_hidden(True)
        prefs.set_string_value("light_manager", "sort_mode", settings.get('sort_mode'))
        # No other way to delete a widget then using del
        del self.panel
        self.settings = settings
        self.disconnect_all()
        self.remove_all_connection()
        self.draw_widgets()
        child_widgets = self.get_children()
        for child_widget in child_widgets:
            child_widget.show()
        if focus_search_widget:
            self.filter_txt.set_focus()
            self.filter_txt.select_end()

    def submit_filter_txt(self, sender, evtid):
        # Since this gets called when the widgets loses focus we don't want it to renew when nothing changes
        focus_search_widget = sender == self.filter_txt

        if not self.settings and not self.filter_txt.get_text():
            return None
        if not self.settings.get('filter') == self.filter_txt.get_text():
            self.refresh_window(focus_search_widget=focus_search_widget)

    def submit_filters(self, sender, evtid):
        self.refresh_window()

    def reset_filters(self, sender, evtid):
        self.refresh_window(pass_settings=False)

    def light_crud_listener(self, sender, evtid):
        refresh = False
        if evtid in ["EVT_ID_OF_ADD_OBJECT", "EVT_ID_OF_REMOVE_OBJECT", "EVT_ID_OF_OBJECT_RENAME"]:
            event_obj = ix.application.get_factory().get_last_event_object()
            if event_obj.get_class_name() in light_classes:
                refresh = True
        if evtid in ['EVT_ID_COMMAND_MANAGER_UNDO', 'EVT_ID_COMMAND_MANAGER_REDO']:
            refresh = True
        if refresh:
            self.refresh_window(pass_marked=True)

    def get_images(self, filter="*"):
        images = ix.api.OfObjectVector()
        types = ix.api.CoreStringVector()
        types.add('Image')
        ix.application.get_matching_objects(images, filter, ix.application.get_factory().get_project(), types)
        return images

    def get_selected_image(self):
        if self.filter_image_list.get_item_count() > 1:
            image_name = self.filter_image_list.get_selected_item_name()
            if image_name != all_images_txt and image_name:
                image = ix.item_exists(image_name)
                return image
        return None

    def change_image(self, sender, evtid):
        image = self.get_selected_image()
        if image:
            self.filter_image_layer_list.enable()
            layers = self.get_image_layers(image)
            self.filter_image_layer_list.remove_all()
            if layers.get_count() > 0:
                for layer in layers:
                    if "ModuleLayer3d" in str(type(layer.get_object().get_module())):
                        self.filter_image_layer_list.add_item(str(layer.get_object()))
                        if self.settings.get('image_layer') == str(layer.get_object()):
                            self.filter_image_layer_list.set_selected_item_by_name(
                                str(self.settings.get('image_layer')))
        else:
            self.filter_image_layer_list.disable()
        self.filter_image_layer_list.redraw()
        # Check if user interaction was involved. One script calls this method no need to refresh.
        if evtid:
            self.refresh_window()

    def select_images(self, sender, evtid):
        image = self.get_selected_image()
        ix.selection.deselect_all()
        if image:
            ix.selection.add(image)
        else:
            images = self.get_images()
            for image in images:
                ix.selection.add(image)

    def get_image_layers(self, image):
        image_module = image.get_module()
        layers = image_module.get_layers()
        return layers

    def get_selected_image_layer(self):
        if self.filter_image_layer_list.get_item_count() > 0:
            image_layer_name = self.filter_image_layer_list.get_selected_item_name()
            if image_layer_name:
                image_layer = ix.item_exists(image_layer_name)
                return image_layer
        return None

    def select_image_layer(self, sender, evtid):
        image_layer = self.get_selected_image_layer()
        if image_layer:
            ix.selection.deselect_all()
            ix.selection.add(image_layer)

    def get_groups(self, filter="*", allow_empty=False):
        groups = ix.api.OfObjectVector()
        types = ix.api.CoreStringVector()
        types.add('Group')
        ix.application.get_matching_objects(groups, filter, ix.application.get_factory().get_project(), types)
        if allow_empty:
            return groups
        filtered_groups = []
        for group in groups:
            group_objects = ix.api.OfObjectArray()
            group.get_module().get_objects(group_objects)
            if group_objects:
                for group_object in group_objects:
                    if "Light" in group_object.get_class_name():
                        filtered_groups.append(group)
                        break
        return filtered_groups

    def get_selected_group(self):
        if self.filter_group_list.get_item_count() > 1:
            group_name = self.filter_group_list.get_selected_item_name()
            if group_name and group_name != all_groups_txt:
                group = ix.item_exists(group_name)
                return group
        return None

    def select_groups(self, sender, evtid):
        group = self.get_selected_group()
        ix.selection.deselect_all()
        if group:
            ix.selection.add(group)
        else:
            groups = self.get_groups()
            for group in groups:
                ix.selection.add(group)

    def get_lpe_labels(self):
        lights = ix.api.OfObjectVector()
        types = ix.api.CoreStringVector()
        fltr = "*"
        types.add('Light')
        ctx = ix.application.get_factory().get_project()
        ix.application.get_matching_objects(lights, fltr, ctx, types)
        labels = []
        for light in lights:
            light_label = light.attrs.light_path_expression_label.attr.get_string()
            if light_label not in labels:
                labels.append(light_label)
        return labels

    def get_selected_lpe_label(self):
        if self.filter_lpe_labels_list.get_item_count() > 1:
            lpe_label_name = self.filter_lpe_labels_list.get_selected_item_name()
            if lpe_label_name and lpe_label_name != all_lpe_labels_txt:
                return lpe_label_name
        return None

    def get_filter_light_types(self):
        light_types = {}
        for light_class in light_classes:
            light_types[light_class] = getattr(self, light_class + "_btn").get_value()
        return light_types

    def get_all_lights(self):
        lights = ix.api.OfObjectVector()
        types = ix.api.CoreStringVector()
        fltr = "*"
        types.add('Light')
        ctx = ix.application.get_factory().get_project()
        ix.application.get_matching_objects(lights, fltr, ctx, types)
        for light in lights:
            print(str(light))
        return [light for light in lights]

    def get_filtered_lights(self):
        lights = ix.api.OfObjectVector()
        types = ix.api.CoreStringVector()
        filter_input = self.filter_txt.get_text()
        if filter_input:
            fltr = "*" + filter_input.strip("*") + "*"
        else:
            fltr = "*"
        types.add('Light')
        ctx = ix.application.get_factory().get_project()
        ix.application.get_matching_objects(lights, fltr, ctx, types)
        for light in lights:
            print(str(light))
        image = self.get_selected_image()

        excluded_lights = []
        if image:
            image_layer = self.get_selected_image_layer()
            if image_layer:
                image_layer_lights_object = image_layer.attrs.lights.attr.get_object()
                # "Use Current Context" returns a None object
                if image_layer_lights_object is None:
                    ctx_lights = ix.api.OfObjectVector()
                    light_ctx = image.get_context()
                    ix.application.get_matching_objects(ctx_lights, "./*", light_ctx, types)
                    for light in lights:
                        exists_in_group = False
                        for ctx_light in ctx_lights:
                            if str(light) == str(ctx_light):
                                exists_in_group = True
                        if not exists_in_group:
                            excluded_lights.append(light)
                # Group
                elif image_layer_lights_object.get_class_name() == "Group":
                    image_group_objects = ix.api.OfObjectVector()
                    image_group_attr = image_layer_lights_object.get_attribute('references')
                    image_group_attr.get_serialized_values(image_group_objects)
                    for light in lights:
                        exists_in_group = False
                        for group_object in image_group_objects:
                            if str(light) == str(group_object):
                                exists_in_group = True
                        if not exists_in_group:
                            excluded_lights.append(light)
                # Specific Light
                elif "Light" in image_layer_lights_object.get_class_name():
                    for light in lights:
                        if str(light) != str(image_layer_lights_object):
                            excluded_lights.append(image_layer_lights_object)
        group = self.get_selected_group()
        if group:
            group_objects = ix.api.OfObjectVector()
            lights_group_attr = group.get_attribute('references')
            lights_group_attr.get_serialized_values(group_objects)
            for light in lights:
                exists_in_group = False
                for group_object in group_objects:
                    if str(light) == str(group_object):
                        exists_in_group = True
                if not exists_in_group:
                    excluded_lights.append(light)

        lpe_label = self.get_selected_lpe_label()
        if lpe_label:
            for light in lights:
                if light.attrs.light_path_expression_label.attr.get_string() != lpe_label:
                    excluded_lights.append(light)

        # Hide instances
        if self.hide_instances_check_box.get_value():
            for light in lights:
                if light.is_instance():
                    excluded_lights.append(light)

        light_types = self.get_filter_light_types()
        # Select all lights when there's no light type checkbox checked
        all_false = True
        if light_types:
            for light_type_state, light_type in light_types.items():
                if light_type:
                    all_false = False
                    break
        if not all_false:
            for light in lights:
                if not light_types.get(light.get_class_name()):
                    excluded_lights.append(light)
        filtered_lights = []
        for light in lights:
            removed = False
            for removed_light in excluded_lights:
                if str(light) == str(removed_light):
                    removed = True
            if not removed:
                filtered_lights.append(light)
        # Sorting
        sort_mode = self.sort_mode_list.get_selected_item_name()
        sorted_lights = []
        if "Default" in sort_mode:
            sorted_lights = filtered_lights
        elif "Type" in sort_mode:
            lights_by_classes_dict = OrderedDict()
            for light_class in light_classes:
                lights_by_classes_dict[light_class] = []
            for light in filtered_lights:
                for light_class in light_classes:
                    if light.get_class_name() == light_class:
                        lights_by_classes_dict[light_class].append(light)
            for light_class_name, lights_by_class in lights_by_classes_dict.items():
                sorted_lights += lights_by_class
        elif "State" in sort_mode:
            enabled_lights = []
            disabled_lights = []
            for light in filtered_lights:
                if light.is_enabled():
                    enabled_lights.append(light)
                else:
                    disabled_lights.append(light)
            sorted_lights += enabled_lights + disabled_lights
        elif "Instanced" in sort_mode:
            instanced_lights = []
            non_instanced_lights = []
            for light in filtered_lights:
                if light.is_instance():
                    instanced_lights.append(light)
                else:
                    non_instanced_lights.append(light)
            sorted_lights += instanced_lights + non_instanced_lights
        elif "Name" in sort_mode:
            sorted_lights = filtered_lights

            def get_name(elem):
                return elem.get_contextual_name()

            sorted_lights.sort(key=get_name)
        elif "Exposure" in sort_mode:
            sorted_lights = filtered_lights

            # Making the item famous
            def get_exposure(elem):
                return elem.attrs.exposure.attr.get_double()

            sorted_lights.sort(key=get_exposure)
            sorted_lights.reverse()
        elif "Creation Date" in sort_mode:
            sorted_lights = filtered_lights

            # Making the item famous
            def get_creation_date(elem):
                return elem.get_creation_date()

            sorted_lights.sort(key=get_creation_date)
            sorted_lights.reverse()
        elif "Modification Date" in sort_mode:
            sorted_lights = filtered_lights

            # Making the item famous
            def get_modified_date(elem):
                return elem.get_modified_date()

            sorted_lights.sort(key=get_modified_date)
            sorted_lights.reverse()
        if "Reverse" in sort_mode:
            sorted_lights.reverse()
        return sorted_lights

    def get_num_pages(self, lights, max_num):
        return int(math.ceil(float(len(lights)) / float(max_num)))

    def get_paginated_lights(self, lights, page=1):
        total = len(lights)
        max_num = prefs.get_item("light_manager", "lights_per_page").get_long_value()
        num_pages = self.get_num_pages(lights, max_num)
        first_index = max_num * (page - 1)
        last_index = max_num * page
        if last_index > total:
            last_index = total
        paginated_lights = lights[first_index:last_index]
        return {'page': page, 'total': total, 'num_pages': num_pages, 'lights': paginated_lights,
                'first_index': first_index, 'last_index': last_index}

    def get_current_page(self):
        return int(self.current_page_num.get_value())

    def goto_first_page(self, sender, evtid):
        self.current_page_num.set_value(1)
        self.refresh_window(pass_marked=True)

    def goto_prev_page(self, sender, evtid):
        self.current_page_num.set_value(self.current_page_num.get_value() + -1)
        self.refresh_window(pass_marked=True)

    def goto_next_page(self, sender, evtid):
        self.current_page_num.set_value(self.current_page_num.get_value() + 1)
        self.refresh_window(pass_marked=True)

    def goto_last_page(self, sender, evtid):
        self.current_page_num.set_value(self.pagination_page_list.get_item_count())
        self.refresh_window(pass_marked=True)

    def goto_page(self, sender, evtid):
        self.current_page_num.set_value(self.pagination_page_list.get_selected_item_index() + 1)
        self.refresh_window(pass_marked=True)

    def create_light(self, sender, evtid):
        light_type = sender.get_selected_item_name()
        sender.set_selected_item_by_index(0)
        if not light_class_prefix + light_type in light_classes:
            return None
        ctx = ix.application.get_working_context()
        if not check_context(ctx):
            return None
        self.start_command_batch(sender, evtid, override=True)
        light = None
        light_class = light_class_prefix + light_type
        if light_class == "LightPhysicalEnvironment":
            file = ix.api.GuiWidget.open_file(ix.application, '', 'Browse for an image', extensions)
            if file != "":
                ibl_name = os.path.splitext(os.path.split(file)[-1])[0]
                tx = ix.cmds.CreateObject(ibl_name + "_tx", 'TextureMapFile')
                tx.attrs.projection = 5
                tx.attrs.filename = file
                tx.attrs.interpolation_mode = 1
                tx.attrs.mipmap_filtering_mode = 1
                tx.attrs.color_space_auto_detect = False

                color_space = 'linear'
                for name in ix.api.ColorIO.get_color_space_names():
                    if 'Utility - Linear - sRGB' in name:
                        color_space = 'Utility - Linear - sRGB'

                tx.attrs.file_color_space = color_space
                tx.attrs.pre_multiplied = False
                light = ix.cmds.CreateObject(ibl_name + '_ibl', 'LightPhysicalEnvironment', str(ctx))
                ix.cmds.SetTexture([light.get_full_name() + ".color"], tx.get_full_name())
            else:
                ix.log_warning('Image Based Lighting setup has been aborted.')
        else:
            light_name = light_type
            if capitalize_light_names:
                light_name = light_name + light_name_suffix
            else:
                light_name = str(light_name + light_name_suffix).lower()
            pos_x = self.get_window().get_x()
            pos_y = self.get_window().get_y()
            widgets = [{'type': 'txt', 'value': light_name, 'label': 'Light Name', 'key': 'name'}, ]
            quick_dialog = QuickDialog("Enter the light name", pos_x, pos_y, 300, 100, widgets=widgets)
            quick_dialog.show_modal()
            quick_dialog.name_widget.set_focus()
            quick_dialog.name_widget.select_all()
            name = None
            while quick_dialog.is_shown():
                ix.application.check_for_events()
            if quick_dialog.is_accepted.get_value():
                # Collect values entered
                name = quick_dialog.name_widget.get_text().strip()
            del quick_dialog
            if name:
                light = ix.cmds.CreateObject(name, light_class, "Global", str(ctx))
                ix.application.check_for_events()
            else:
                return None
        grp = self.get_selected_group()
        if light and grp:
            ix.cmds.AddValues([str(grp) + ".inclusion_items"], [str(light)])
            ix.application.check_for_events()
            grp_module = grp.get_module()
            grp_module.set_is_dirty(True)
            grp_module.force_update_references()
        self.end_command_batch()
        # Refresh not needed if event listener gets fixed
        self.refresh_window()

    def get_light(self, sender, evtid):
        light = None
        for light_full_name, light_widgets in self.lights.items():
            for light_widget in light_widgets:
                if light_widget == sender:
                    light = ix.item_exists(light_full_name)
                    break
        return light

    def select_light(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            ix.selection.deselect_all()
            ix.selection.add(light)

    def select_lights(self, sender, evtid):
        lights = self.get_marked_lights()
        ix.selection.deselect_all()
        for light in lights:
            ix.selection.add(light)

    def group_lights(self, sender, evtid):
        lights = self.get_marked_lights()
        if not lights:
            return None
        self.filter_txt.lose_focus()
        ctx = ix.application.get_working_context()
        if not check_context(ctx):
            return None
        pos_x = self.get_window().get_x()
        pos_y = self.get_window().get_y()
        widgets = [{'type': 'txt', 'value': "light_group", 'label': 'Group Name', 'key': 'name'}, ]
        quick_dialog = QuickDialog("Enter the group name", pos_x, pos_y, 300, 100, widgets=widgets)
        quick_dialog.show_modal()
        quick_dialog.name_widget.set_focus()
        quick_dialog.name_widget.select_all()
        name = None
        while quick_dialog.is_shown():
            ix.application.check_for_events()
        if quick_dialog.is_accepted.get_value():
            # Collect values entered
            name = quick_dialog.name_widget.get_text().strip()
        del quick_dialog
        if name and ctx:
            self.start_command_batch(sender, evtid, override=True)
            grp = ix.cmds.CreateObject(name, "Group", "Global", str(ctx))
            ix.cmds.AddValues([str(grp) + ".inclusion_items"], [str(light) for light in lights])
            ix.application.check_for_events()
            grp_module = grp.get_module()
            grp_module.set_is_dirty(True)
            grp_module.force_update_references()
            self.end_command_batch()
            self.refresh_window(pass_marked=True)

    def instantiate_lights(self, sender, evtid):
        lights = self.get_marked_lights()
        if not lights:
            return None
        for light in lights:
            ctx = light.get_context()
            if not check_context(ctx):
                return None
        self.start_command_batch(sender, evtid, override=True)
        instances = ix.cmds.Instantiate([str(light) for light in lights])
        local_attribute_names = [str(instance) + ".translate" for instance in instances] + \
                                [str(instance) + ".rotate" for instance in instances] + \
                                [str(instance) + ".scale" for instance in instances]
        ix.cmds.LocalizeAttributes(local_attribute_names, True)
        self.end_command_batch()
        self.refresh_window(pass_marked=True)

    def duplicate_lights(self, sender, evtid):
        lights = self.get_marked_lights()
        if not lights:
            return None
        self.filter_txt.lose_focus()
        ix.selection.deselect_all()
        for light in lights:
            ix.selection.add(light)
            ctx = light.get_context()
            if not check_context(ctx):
                return None
        self.start_command_batch(sender, evtid, override=True)
        ix.application.copy()
        ix.api.SdkHelpers.paste(ix.application)
        ix.application.check_for_events()
        self.end_command_batch()
        self.refresh_window(pass_marked=True)

    def isolate_lights(self, sender, evtid):
        all_lights = self.get_all_lights()
        marked_lights = self.get_marked_lights()
        if not marked_lights and not self.isolation_mode:
            sender.set_value(False)
            return None
        self.start_command_batch(sender, evtid, override=True)
        if self.isolation_mode:
            stored_light_states = self.light_states
            for light in all_lights:
                # check if light is displayed in the manager
                light_widgets = self.lights.get(str(light))
                if str(light) in stored_light_states:
                    print (str(light))
                    ix.cmds.DisableItems([str(light)], not stored_light_states[str(light)])
                    if light_widgets:
                        light_widgets[1].set_value(stored_light_states[str(light)])
            self.isolation_mode = False
        else:
            self.isolation_mode = True
            self.store_light_states()
            for light in all_lights:
                # check if light is displayed in the manager
                light_widgets = self.lights.get(str(light))
                if str(light) in [str(marked_light) for marked_light in marked_lights]:
                    if light_widgets:
                        light_widgets[1].set_value(True)
                    ix.cmds.DisableItems([str(light)], False)
                else:
                    if light_widgets:
                        light_widgets[1].set_value(False)
                    ix.cmds.DisableItems([str(light)], True)
        self.end_command_batch()

    def store_light_states(self):
        lights = self.get_all_lights()
        light_states = {}
        for light in lights:
            light_states[str(light)] = light.is_enabled()
        self.light_states = light_states
        return light_states

    def get_marked_lights(self):
        lights = []
        for light_full_name, light_widgets in self.lights.items():
            mark_checkbox = light_widgets[0]
            if mark_checkbox.get_value():
                light = ix.item_exists(light_full_name)
                if light:
                    lights.append(light)
        return lights

    def toggle_lights(self, sender, evtid):
        mark_value = False
        for light_full_name, light_widgets in self.lights.items():
            mark_checkbox = light_widgets[1]
            if not mark_checkbox.get_value():
                mark_value = True
                break
        self.start_command_batch(sender, evtid, override=True)
        for light_full_name, light_widgets in self.lights.items():
            mark_checkbox = light_widgets[1]
            mark_checkbox.set_value(mark_value)
            light = self.get_light(mark_checkbox, None)
            ix.cmds.DisableItems([str(light)], not mark_value)
            for widget_i, widget in enumerate(light_widgets):
                if widget_i not in [0, 1]:
                    if mark_value:
                        if light.is_instance():
                            source = light.get_source()
                            if source.is_disabled():
                                mark_checkbox.set_value(False)
                                continue
                        widget.enable()
                    else:
                        widget.disable()
        self.end_command_batch()

    def toggle_light(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid, override=True)
            ix.cmds.DisableItems([str(light)], not sender.get_value())
            widgets = self.lights.get(str(light))
            for widget_i, widget in enumerate(widgets):
                if widget_i not in [0, 1]:
                    if sender.get_value():
                        if light.is_instance():
                            source = light.get_source()
                            if source.is_disabled():
                                sender.set_value(False)
                                continue
                        widget.enable()
                    else:
                        widget.disable()
            self.end_command_batch()

    def rename_light(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light and sender.get_text().strip():
            previous_full_name = str(light)
            previous_name = light.get_contextual_name()
            # Since this gets called when the widgets loses focus we don't want it to renew when nothing changes
            if previous_name == sender.get_text().strip():
                return None
            self.start_command_batch(sender, evtid, override=True)
            ix.cmds.RenameItem(str(light), sender.get_text().strip())
            self.lights[str(light)] = self.lights.pop(previous_full_name)
            self.end_command_batch()
        else:
            sender.set_text(light.get_contextual_name())

    def remove_lights(self, sender, evtid):
        lights = self.get_marked_lights()
        if lights:
            self.disconnect_all()
            self.start_command_batch(sender, evtid, override=True)
            ix.cmds.DeleteItems([str(light) for light in lights])
            self.end_command_batch()
            self.refresh_window()

    def create_lpe_pass(self, sender, evtid):
        image_layer = self.get_selected_image_layer()
        if not image_layer:
            return None
        lpe_label = self.get_selected_lpe_label()
        if not lpe_label:
            return None

        pos_x = self.get_window().get_x()
        pos_y = self.get_window().get_y()
        widgets = [{'type': 'txt', 'value': lpe_label + lpe_suffix, 'label': 'LPE Name', 'key': 'lpe_name'},
                   {'type': 'txt', 'value': lpe_label, 'label': 'Buffer Name', 'key': 'buffer_name'}]
        quick_dialog = QuickDialog("Enter LPE settings", pos_x, pos_y, 300, 100, widgets=widgets)
        quick_dialog.show_modal()
        quick_dialog.lpe_name_widget.set_focus()
        quick_dialog.lpe_name_widget.select_end()

        while quick_dialog.is_shown():
            ix.application.check_for_events()
        if quick_dialog.is_accepted.get_value():
            # Collect values entered
            lpe_name = quick_dialog.lpe_name_widget.get_text().strip()
            buffer_name = quick_dialog.buffer_name_widget.get_text().strip()
        else:
            return None
        del quick_dialog
        ctx = ix.api.IOHelpers.pick_context(ix.application, "Specify LPE context location")
        if not check_context(ctx):
            return None
        if lpe_name and buffer_name and ctx:
            self.start_command_batch(sender, evtid, override=True)
            lpe = ix.cmds.CreateObject(lpe_name, "LightPathExpression", "", str(ctx))
            ix.cmds.SetValues([str(lpe) + ".expression[0]"], ["C.*<L.'{}'>".format(lpe_label)])
            create_aov(buffer_name)
            ix.application.check_for_events()
            ix.cmds.SetValues([str(lpe) + ".output[0]"], [buffer_name])
            ix.cmds.AddValues([str(image_layer) + ".light_path_expressions"], [str(lpe)])

            aov_list = image_layer.get_attribute("selected_aov_list")
            enabled_aov_list = image_layer.get_attribute("enabled_aov_list")
            aov_blend_override_list = image_layer.get_attribute("aov_blend_override_list")

            ix.cmds.AddValues([str(aov_list)], [str(buffer_name)])
            ix.cmds.AddValues([str(enabled_aov_list)], ["1"])
            ix.cmds.AddValues([str(aov_blend_override_list)],
                              [str(ix.api.OfChannelManager.AOVBLENDINGMODE_INVALID)])

            self.end_command_batch()

    def start_command_batch(self, sender, evtid, override=False):
        if override:
            ix.enable_command_history()
            ix.begin_command_batch('LightManager')
        else:
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGING":
                if not self.last_event or not (self.last_event and self.last_event[0] == sender):
                    ix.enable_command_history()
                    ix.begin_command_batch('LightManager')
            elif evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.last_event = []
                return None
        self.last_event = [sender, evtid]

    def end_command_batch(self):
        ix.application.check_for_events()
        ix.end_command_batch()
        ix.enable_command_history()

    def localize_attribute(self, light, attr_name):
        if localize_attributes:
            attr = light.get_attribute(attr_name)
            if light.is_instance() and not attr.is_local():
                ix.cmds.LocalizeAttributes([str(attr)], True)
                ix.application.check_for_events()

    def change_exposure(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'exposure')
            ix.cmds.SetValue(str(light) + ".exposure", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_samples(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'sample_count')
            ix.cmds.SetValue(str(light) + ".sample_count", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_label(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid, override=True)
            self.localize_attribute(light, 'light_path_expression_label')
            ix.cmds.SetValue(str(light) + ".light_path_expression_label", [str(sender.get_text())])
            self.filter_lpe_labels_list.remove_all()
            self.filter_lpe_labels_list.add_item(all_lpe_labels_txt)
            self.filter_lpe_labels_list.add_separator("")
            labels = self.get_lpe_labels()
            for label in labels:
                self.filter_lpe_labels_list.add_item(label)
            self.end_command_batch()

    def select_or_create_texture(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if not light:
            return None
        if getattr(light.attrs, color_attr).attr.is_textured():
            ix.selection.deselect_all()
            ix.selection.add(getattr(light.attrs, color_attr).attr.get_texture())
        else:
            self.start_command_batch(sender, evtid, override=True)
            filters = ix.api.CoreStringVector()
            filters.add("Texture")
            tx = ix.api.IOHelpers.pick_item(ix.application, filters, "Pick or create your texture")
            if tx:
                self.localize_attribute(light, color_attr)
                ix.cmds.SetTexture([str(light) + "." + color_attr], str(tx))
            self.end_command_batch()
            if tx:
                self.refresh_window()

    def mark_items(self, sender, evtid):
        mark_value = False
        for light_full_name, light_widgets in self.lights.items():
            mark_checkbox = light_widgets[0]
            if not mark_checkbox.get_value():
                mark_value = True
                break
        for light_full_name, light_widgets in self.lights.items():
            mark_checkbox = light_widgets[0]
            mark_checkbox.set_value(mark_value)

    def toggle_border(self, sender, evtid):
        sender.get_window().set_border_drawn(not sender.get_window().is_border_drawn())
        if sender.get_window().is_border_drawn():
            sender.set_label("Lock Window")
        else:
            sender.set_label("Unlock Window")

    def get_light_dependencies(self, light, included=True, excluded=False):
        dependencies_object_set = ix.api.OfObjectSet()
        light.get_dependency_objects(dependencies_object_set)
        dependencies = []
        for obj in dependencies_object_set:
            if obj.is_kindof('Group') and not str(obj).endswith("__context_lights__") and not str(obj).endswith("__light_group__"):
                # Check if in included objects
                if included:
                    group_objects = ix.api.OfObjectVector()
                    lights_group_attr = obj.get_attribute('references')
                    lights_group_attr.get_serialized_values(group_objects)
                    found_included = False
                    for group_object in group_objects:
                        if str(light) == str(group_object):
                            found_included = True
                    if not found_included:
                        continue
                dependencies.append(obj)
        return dependencies

    def change_light_color(self, sender, evtid):
        data = ix.api.CoreString()
        sender.get_custom_data(sender, data)
        light_name, r, g, b = str(data).split(",")
        light = ix.get_item(str(light_name))
        if light:
            self.start_command_batch(sender, evtid, override=True)
            self.localize_attribute(light, color_attr)
            ix.cmds.SetValue(str(light) + "." + color_attr, [str(r), str(g), str(b)])
            self.end_command_batch()

    def change_light_group(self, sender, evtid):
        light = self.get_light(sender, evtid)
        selected_index = sender.get_selected_item_index()
        if selected_index == 0:
            return None
        if light:
            group_name = sender.get_selected_item_name()
            group = ix.item_exists(group_name)
            dependencies = self.get_light_dependencies(light)
            num_dependencies = len(dependencies)
            self.start_command_batch(sender, evtid, override=True)
            item_index = sender.get_selected_item_index()
            if group:
                if str(group) in [str(dependency) for dependency in dependencies]:
                    group_objects = ix.api.OfObjectVector()
                    group_inclusion_attr = group.get_attribute('inclusion_items')
                    group_inclusion_attr.get_serialized_values(group_objects)
                    for group_object in group_objects:
                        if str(light) == str(group_object):
                            group_inclusion_attr.remove_object(group_object)
                            ix.application.check_for_events()

                    group_ref_objects = ix.api.OfObjectVector()
                    group_ref_inclusion_attr = group.get_attribute('references')
                    group_ref_inclusion_attr.get_serialized_values(group_ref_objects)
                    in_ref_items = False
                    for group_ref_object in group_ref_objects:
                        if str(light) == str(group_ref_object):
                            in_ref_items = True
                    if in_ref_items:
                        ix.cmds.AddValues([group_name + ".exclusion_items"], [str(light)])
                        ix.application.check_for_events()
                    sender.set_item_hint(item_index, False)
                    num_dependencies -= 1
                else:
                    group_objects = ix.api.OfObjectVector()
                    group_exclusion_attr = group.get_attribute('exclusion_items')
                    group_exclusion_attr.get_serialized_values(group_objects)
                    for group_object in group_objects:
                        if str(light) == str(group_object):
                            group_exclusion_attr.remove_object(group_object)
                            ix.application.check_for_events()
                    group_ref_objects = ix.api.OfObjectVector()
                    group_ref_inclusion_attr = group.get_attribute('references')
                    group_ref_inclusion_attr.get_serialized_values(group_ref_objects)
                    in_ref_items = False
                    for group_ref_object in group_ref_objects:
                        if str(light) == str(group_ref_object):
                            in_ref_items = True
                    if not in_ref_items:
                        ix.cmds.AddValues([group_name + ".inclusion_items"], [str(light)])
                        ix.application.check_for_events()
                    sender.set_item_hint(item_index, True)
                    num_dependencies += 1
            sender.select_item_index(0)
            sender.set_item_name(0, "None" if num_dependencies == 0 else "{} {}".format(num_dependencies, "group" if num_dependencies == 1 else "groups"))
            self.end_command_batch()

    ### Light specific events ###
    # Common Settings
    def change_light_radius(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'radius')
            ix.cmds.SetValue(str(light) + ".radius", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_light_cone_angle(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'cone_angle')
            ix.cmds.SetValue(str(light) + ".cone_angle", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    # Distant Light
    def change_distant_light_angle(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'angle')
            ix.cmds.SetValue(str(light) + ".angle", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    # Plane Light
    def change_plane_light_softness(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'cone_softness')
            ix.cmds.SetValue(str(light) + ".cone_softness", [str(sender.get_value() / 100)])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_plane_light_shape(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'shape')
            ix.cmds.SetValue(str(light) + ".shape", [str(sender.get_selected_item_index())])
            self.end_command_batch()

    # Spot Light
    def change_spot_light_falloff_angle(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'falloff_angle')
            ix.cmds.SetValue(str(light) + ".falloff_angle", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    # Environment Light
    def change_environment_light_rot(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'rotate')
            ix.cmds.SetValue(str(light) + ".rotate[1]", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    # Cylinder Light
    def change_cylinder_light_length(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, 'length')
            ix.cmds.SetValue(str(light) + ".length", [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    # Attenuation
    def change_attenuation_mode(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            self.start_command_batch(sender, evtid, override=True)
            attribute_name = "attenuation_mode"
            if not light.get_attribute(attribute_name) or light.get_attribute(attribute_name).is_hidden():
                return None
            self.localize_attribute(light, attribute_name)
            ix.cmds.SetValue(str(light) + "." + attribute_name, [str(sender.get_selected_item_index())])
            self.end_command_batch()
            self.refresh_window()

    def change_attenuation_near_start(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            attribute_name = "attenuation_near_start"
            if not light.get_attribute(attribute_name) or light.get_attribute(attribute_name).is_locked():
                return None
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, attribute_name)
            ix.cmds.SetValue(str(light) + "." + attribute_name, [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_attenuation_near_end(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            attribute_name = "attenuation_near_end"
            if not light.get_attribute(attribute_name) or light.get_attribute(attribute_name).is_locked():
                return None
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, attribute_name)
            ix.cmds.SetValue(str(light) + "." + attribute_name, [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_attenuation_far_start(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            attribute_name = "attenuation_far_start"
            if not light.get_attribute(attribute_name) or light.get_attribute(attribute_name).is_locked():
                return None
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, attribute_name)
            ix.cmds.SetValue(str(light) + "." + attribute_name, [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()

    def change_attenuation_far_end(self, sender, evtid):
        light = self.get_light(sender, evtid)
        if light:
            attribute_name = "attenuation_far_end"
            if not light.get_attribute(attribute_name) or light.get_attribute(attribute_name).is_locked():
                return None
            self.start_command_batch(sender, evtid)
            self.localize_attribute(light, attribute_name)
            ix.cmds.SetValue(str(light) + "." + attribute_name, [str(sender.get_value())])
            if evtid == "EVT_ID_NUMBER_FIELD_VALUE_CHANGED":
                self.end_command_batch()


def create_aov(name, description="", group_separator=0):
    """
    Create a new channel layer.
    name            : name of the channel layer,
    description     : optional description
    group_separator : character defining name hierarchy, set to 0 to not use grouping
    """
    # Create an array that will contain the channels' name
    channels = ix.api.CoreStringArray(3)
    # Feed the array
    for i in range(3):
        channels[i] = ["red", "green", "blue"][i]
    # Grab the channel manager object
    cm = ix.application.get_channel_manager()
    # Create the layer (Name, channels, BlendingMode, FilteringMode, VisualHintMode, Description)
    index = cm.add_layer(str(name), channels, cm.AOVBLENDINGMODE_BLEND, cm.AOVFILTERINGMODE_USE_FILTER,
                         cm.AOVVISUALHINTMODE_COLOR, description, group_separator)
    # Return the index of the new layer, or the index of the existing one
    return index


def setup_window(pos_x=900, pos_y=450, settings={}):
    ix.enable_command_history()
    window = LightManagerGui("Light Manager", pos_x, pos_y, window_w, window_h, settings=settings)
    window.show()
    # # Give focus to filter text widget
    if init_focus_search_widget:
        window.filter_txt.set_focus()
        window.filter_txt.select_end()
    while window.is_shown():
        ix.application.check_for_events()
    del window
    # ix.disable_command_history()


setup_window()
