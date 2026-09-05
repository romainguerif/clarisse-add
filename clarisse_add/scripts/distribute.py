import random
import math

v_spacing = 30

class DistributeGui(ix.api.GuiWindow):
    def __init__(self, title, x, y, w, h):
        super(DistributeGui, self).__init__(ix.application.get_event_window(), x, y, w, h)
        self.set_title(title)
        self.panel = ix.api.GuiPanel(self, 0, 0, self.get_width(), self.get_height())
        self.panel.set_constraints(ix.api.GuiWidget.CONSTRAINT_LEFT, ix.api.GuiWidget.CONSTRAINT_TOP,
                                   ix.api.GuiWidget.CONSTRAINT_RIGHT, ix.api.GuiWidget.CONSTRAINT_BOTTOM)

        offset_y = 10
        self.shape_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Shape:")
        self.shape_gui_list = ix.api.GuiListButton(self.panel, 100, offset_y, 180, 22)
        self.shape_gui_list.add_item("Line")
        self.shape_gui_list.add_item("Square")
        self.shape_gui_list.add_item("Rectangle")
        self.shape_gui_list.add_item("Circle")

        offset_y += v_spacing
        # Line Settings
        self.relative_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Relative:")
        self.relative_x_check_box = ix.api.GuiCheckbox(self.panel, 100, offset_y, "")
        self.relative_x_check_box.set_value(True)
        self.relative_x_check_box.set_tooltip("Relative to the X bounding box of the previous object. Works only with Line.")
        self.relative_y_check_box = ix.api.GuiCheckbox(self.panel, 170, offset_y, "")
        self.relative_y_check_box.set_tooltip("Relative to the Y bounding box of the previous object. Works only with Line.")
        self.relative_z_check_box = ix.api.GuiCheckbox(self.panel, 240, offset_y, "")
        self.relative_z_check_box.set_tooltip("Relative to the Z bounding box of the previous object. Works only with Line.")
        self.relative_checkboxes = [self.relative_x_check_box, self.relative_y_check_box, self.relative_z_check_box]
        self.unit_label = ix.api.GuiLabel(self.panel, 10, offset_y + v_spacing, 380, 22, "Units:")
        self.unit_m_check_box = ix.api.GuiCheckButton(self.panel, 100, offset_y + v_spacing, 50, 22, "meters")
        self.unit_m_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.unit_m_check_box.set_value(True)
        self.unit_percentage_check_box = ix.api.GuiCheckButton(self.panel, 149, offset_y  + v_spacing, 32, 22, "⅒")
        self.unit_percentage_check_box.set_tooltip("Like percentage but zero to one.")
        self.unit_percentage_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.line_widgets = self.relative_checkboxes + [self.relative_label, self.unit_m_check_box,
                                                        self.unit_percentage_check_box, self.unit_label]        


        # Square Settings
        self.axis_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Axis:")
        self.x_check_box = ix.api.GuiCheckButton(self.panel, 100, offset_y, 60, 22, "X")
        self.x_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.y_check_box = ix.api.GuiCheckButton(self.panel, 170, offset_y, 60, 22, "Y")
        self.y_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.y_check_box.set_value(True)
        self.z_check_box = ix.api.GuiCheckButton(self.panel, 240, offset_y, 60, 22, "Z")
        self.z_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.axis_checkboxes = [self.x_check_box, self.y_check_box, self.z_check_box]
        self.square_widgets = [self.axis_label] + self.axis_checkboxes[:]
        for square_widget in self.square_widgets:
            square_widget.hide()

        # Rectangle Settings
        offset_y += v_spacing
        self.rows_cols_label = ix.api.GuiLabel(self.panel, 10, offset_y, 100, 22, "Direction:")
        self.rows_cols_label.hide()
        self.rows_cols_gui_list = ix.api.GuiListButton(self.panel, 100, offset_y, 180, 22)
        self.rows_cols_gui_list.add_item("Rows")
        self.rows_cols_gui_list.add_item("Columns")
        self.rows_cols_gui_list.hide()

        self.rows_cols_num_field = ix.api.GuiNumberField(self.panel, 290, offset_y, 60, "")
        self.rows_cols_num_field.set_increment(1)
        self.rows_cols_num_field.set_value(2)
        self.rows_cols_num_field.set_slider_range(1, 100)
        self.rows_cols_num_field.set_range(1, 10000)
        self.rows_cols_num_field.enable_slider_range(True)
        self.rows_cols_num_field.hide()
        self.rectangle_widgets = [self.rows_cols_label, self.rows_cols_num_field,
                                  self.rows_cols_gui_list] + self.square_widgets[:]
        self.rectangle_widgets = [self.rows_cols_label, self.rows_cols_num_field, self.rows_cols_gui_list] + self.square_widgets[:]

        # Circle Settings
        self.circle_radius_label = ix.api.GuiLabel(self.panel, 10, offset_y, 100, 22, "Radius:")
        self.circle_radius_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.circle_radius_num_field.set_increment(.25)
        self.circle_radius_num_field.set_value(1)
        self.circle_radius_num_field.set_slider_range(0, 100)
        self.circle_radius_num_field.set_range(0, 10000)
        self.circle_radius_num_field.enable_slider_range(True)
        self.circle_radius_num_field.set_enable(False)
        self.circle_auto_radius_check_box = ix.api.GuiCheckButton(self.panel, 170, offset_y, 60, 20, "Auto")
        self.circle_auto_radius_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.circle_auto_radius_check_box.set_value(True)

        offset_y += v_spacing
        self.circle_bbox_axis_label = ix.api.GuiLabel(self.panel, 10, offset_y, 100, 22, "BBox Axis:")
        self.circle_bbox_axis_gui_list = ix.api.GuiListButton(self.panel, 100, offset_y, 60, 22)
        self.circle_bbox_axis_gui_list.add_item("X")
        self.circle_bbox_axis_gui_list.add_item("Y")
        self.circle_bbox_axis_gui_list.add_item("Z")

        self.circle_spacing_label = ix.api.GuiLabel(self.panel, 170, offset_y, 100, 22, "Spacing:")
        self.circle_spacing_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.circle_spacing_num_field.set_increment(.1)
        self.circle_spacing_num_field.set_value(0)
        self.circle_spacing_num_field.set_slider_range(0, 100)
        self.circle_spacing_num_field.set_range(0, 10000)
        self.circle_spacing_num_field.enable_slider_range(True)

        self.circle_widgets = [self.circle_radius_label, self.circle_radius_num_field, self.circle_auto_radius_check_box,
                               self.circle_bbox_axis_gui_list, self.circle_bbox_axis_label, self.circle_spacing_label,
                               self.circle_spacing_num_field] + self.square_widgets[:]
        for circle_widget in self.circle_widgets:
            circle_widget.hide()
        
        offset_y += v_spacing
        self.affect_position_check_box = ix.api.GuiCheckButton(self.panel, 10, offset_y, 22, 22, "")
        self.affect_position_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.affect_position_check_box.set_value(True)
        self.affect_position_label = ix.api.GuiLabel(self.panel, 40, offset_y, 348, 22, "Position")
        self.affect_position_label.set_text_color(ix.api.GMathVec3uc(192, 192, 192))

        offset_y += v_spacing
        self.translate_offset_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Offset:")
        self.translate_offset_x_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.translate_offset_x_num_field.set_increment(0.1)
        self.translate_offset_x_num_field.set_value(0)
        self.translate_offset_y_num_field = ix.api.GuiNumberField(self.panel, 170, offset_y, 60, "")
        self.translate_offset_y_num_field.set_increment(0.1)
        self.translate_offset_y_num_field.set_value(0)
        self.translate_offset_z_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.translate_offset_z_num_field.set_increment(0.1)
        self.translate_offset_z_num_field.set_value(0)
        self.translate_offset_num_fields = [self.translate_offset_x_num_field, self.translate_offset_y_num_field, self.translate_offset_z_num_field]

        offset_y += v_spacing
        self.random_trans_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Variation:")

        self.random_trans_x_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.random_trans_x_num_field.set_increment(0.1)

        self.random_trans_y_num_field = ix.api.GuiNumberField(self.panel, 170, offset_y, 60, "")
        self.random_trans_y_num_field.set_increment(0.1)

        self.random_trans_z_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.random_trans_z_num_field.set_increment(0.1)

        offset_y += v_spacing
        offset_y += v_spacing
        self.affect_rotation_check_box = ix.api.GuiCheckButton(self.panel, 10, offset_y, 22, 22, "")
        self.affect_rotation_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.affect_rotation_check_box.set_value(True)
        self.affect_rotation_label = ix.api.GuiLabel(self.panel, 40, offset_y, 348, 22, "Rotation")
        self.affect_rotation_label.set_text_color(ix.api.GMathVec3uc(192, 192, 192))

        offset_y += v_spacing
        self.rotate_offset_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Offset:")
        self.rotate_offset_x_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.rotate_offset_x_num_field.set_increment(0.5)
        self.rotate_offset_x_num_field.set_value(0)
        self.rotate_offset_y_num_field = ix.api.GuiNumberField(self.panel, 170, offset_y, 60, "")
        self.rotate_offset_y_num_field.set_increment(0.5)
        self.rotate_offset_y_num_field.set_value(0)
        self.rotate_offset_z_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.rotate_offset_z_num_field.set_increment(0.5)
        self.rotate_offset_z_num_field.set_value(0)
        self.rotate_offset_num_fields = [self.rotate_offset_x_num_field, self.rotate_offset_y_num_field, self.rotate_offset_z_num_field]
        offset_y += v_spacing
        self.random_rot_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Variation:")

        self.random_rot_x_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.random_rot_x_num_field.set_increment(1)

        self.random_rot_y_num_field = ix.api.GuiNumberField(self.panel, 170, offset_y, 60, "")
        self.random_rot_y_num_field.set_increment(1)

        self.random_rot_z_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.random_rot_z_num_field.set_increment(1)

        offset_y += v_spacing
        offset_y += v_spacing
        self.affect_scale_check_box = ix.api.GuiCheckButton(self.panel, 10, offset_y, 22, 22, "")
        self.affect_scale_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.affect_scale_check_box.set_value(True)

        self.affect_scale_label = ix.api.GuiLabel(self.panel, 40, offset_y, 348, 22, "Scale")
        self.affect_scale_label.set_text_color(ix.api.GMathVec3uc(192, 192, 192))

        offset_y += v_spacing
        self.scale_offset_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Offset:")
        self.scale_offset_x_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.scale_offset_x_num_field.set_increment(0.1)
        self.scale_offset_x_num_field.set_value(0)
        self.scale_offset_y_num_field = ix.api.GuiNumberField(self.panel, 170, offset_y, 60, "")
        self.scale_offset_y_num_field.set_increment(0.1)
        self.scale_offset_y_num_field.set_value(0)
        self.scale_offset_z_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.scale_offset_z_num_field.set_increment(0.1)
        self.scale_offset_z_num_field.set_value(0)
        self.scale_offset_num_fields = [self.scale_offset_x_num_field, self.scale_offset_y_num_field, self.scale_offset_z_num_field]
        offset_y += v_spacing
        self.random_scale_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Variation:")

        self.random_scale_uniform_check_box = ix.api.GuiCheckButton(self.panel, 310, offset_y, 80, 22, "Uniform")
        self.random_scale_uniform_check_box.set_value(True)
        self.random_scale_uniform_check_box.set_style(ix.api.GuiPushButton.STYLE_COUNT)
        self.random_scale_x_num_field = ix.api.GuiNumberField(self.panel, 100, offset_y, 60, "")
        self.random_scale_x_num_field.set_increment(0.05)

        self.random_scale_y_num_field = ix.api.GuiNumberField(self.panel, 170, offset_y, 60, "")
        self.random_scale_y_num_field.set_increment(0.05)
        self.random_scale_y_num_field.set_enable(False)

        self.random_scale_z_num_field = ix.api.GuiNumberField(self.panel, 240, offset_y, 60, "")
        self.random_scale_z_num_field.set_increment(0.05)
        self.random_scale_z_num_field.set_enable(False)

        offset_y += v_spacing
        offset_y += v_spacing
        self.sort_label = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "Sort by:")

        self.sort_gui_list = ix.api.GuiListButton(self.panel, 100, offset_y, 200, 22)
        self.sort_gui_list.add_item("Selection Order")
        self.sort_gui_list.add_item("Random")
        self.sort_gui_list.add_item("Name")
        self.sort_gui_list.add_item("Volume")
        self.sort_gui_list.add_item("X - size")
        self.sort_gui_list.add_item("Y - size")
        self.sort_gui_list.add_item("Z - size")
        self.sort_gui_list.set_item_style(ix.api.GuiListButton.ITEM_STYLE_NONE)
        self.reverse_check_box = ix.api.GuiCheckbox(self.panel, 310, offset_y + 3, "Reverse")

        offset_y += v_spacing
        self.separator_label3 = ix.api.GuiLabel(self.panel, 10, offset_y, 380, 22, "[ INSTANCING: ]")
        self.separator_label3.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

        offset_y += v_spacing
        self.group_check_box = ix.api.GuiCheckbox(self.panel, 10, offset_y, "Group")
        self.group_check_box.set_value(True)
        self.combine_check_box = ix.api.GuiCheckbox(self.panel, 170, offset_y, "Combine")
        self.combine_check_box.set_value(True)
        offset_y += v_spacing
        self.localize_check_box = ix.api.GuiCheckbox(self.panel, 10, offset_y, "Localize Transforms")
        self.localize_check_box.set_value(True)

        offset_y += v_spacing * 2
        self.close_button = ix.api.GuiPushButton(self.panel, 10, offset_y, 80, 22, "Close")
        self.close_button.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.run_button = ix.api.GuiPushButton(self.panel, 100, offset_y, 100, 22, "Distribute")
        self.run_button.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)
        self.run_instantiate_button = ix.api.GuiPushButton(self.panel, 210, offset_y, 180, 22, "Distribute + Instantiate")
        self.run_instantiate_button.set_style(ix.api.GuiPushButton.STYLE_FLAT_OUTLINED)

        self.connect(self.shape_gui_list, "EVT_ID_LIST_BUTTON_SELECT", self.on_event)
        for checkbox in self.axis_checkboxes:
            self.connect(checkbox, "EVT_ID_CHECK_BUTTON_CLICK", self.on_event)
        self.connect(self.circle_auto_radius_check_box, "EVT_ID_CHECK_BUTTON_CLICK", self.on_event)
        self.connect(self.reverse_check_box, "EVT_ID_CHECKBOX_CLICK", self.on_event)
        self.connect(self.unit_m_check_box, "EVT_ID_CHECK_BUTTON_CLICK", self.on_event)
        self.connect(self.unit_percentage_check_box, "EVT_ID_CHECK_BUTTON_CLICK", self.on_event)
        self.connect(self.random_scale_uniform_check_box, "EVT_ID_CHECK_BUTTON_CLICK", self.on_event)
        self.connect(self.close_button, "EVT_ID_PUSH_BUTTON_CLICK", self.on_event)
        self.connect(self.run_button, "EVT_ID_PUSH_BUTTON_CLICK", self.on_event)
        self.connect(self.run_instantiate_button, "EVT_ID_PUSH_BUTTON_CLICK", self.on_event)
        
    def get_transformed_bbox(self, geo, no_rot=False):
        """Get the transformed Bbox3d of the specified geometry."""
        if not geo or not (geo.is_kindof("Geometry") or geo.is_kindof("GeometryBundle")):
            ix.log_warning("Please select a Geometry or GeometryBundle.")
            return None

        bbox = geo.get_module().get_bbox()
        global_matrix = geo.get_module().get_global_matrix()
        if no_rot:
            global_matrix.set_rotation(ix.api.GMathVec3d(0,0,0))

        transformed_bbox = ix.api.GMathBbox3d()
        bbox.transform_bbox_and_get_bbox(global_matrix, transformed_bbox)
        return transformed_bbox
    def transform(self, geo, index, row=0, col=0, previous_geo=None, bboxes=[], circumference=1, geometry_count=0):
        spacing = [self.translate_offset_x_num_field.get_value(),
                   self.translate_offset_y_num_field.get_value(),
                   self.translate_offset_z_num_field.get_value()]
                   
        rotate_offset = [self.rotate_offset_x_num_field.get_value(),
                         self.rotate_offset_y_num_field.get_value(),
                         self.rotate_offset_z_num_field.get_value()]

        scale_offset = [self.scale_offset_x_num_field.get_value(),
                        self.scale_offset_y_num_field.get_value(),
                        self.scale_offset_z_num_field.get_value()]

        random_trans_x = self.random_trans_x_num_field.get_value() * ((random.random() * 2) - 1)
        random_trans_y = self.random_trans_y_num_field.get_value() * ((random.random() * 2) - 1)
        random_trans_z = self.random_trans_z_num_field.get_value() * ((random.random() * 2) - 1)
        random_trans = [random_trans_x, random_trans_y, random_trans_z]

        random_rot_x = self.random_rot_x_num_field.get_value() * ((random.random() * 2) - 1)
        random_rot_y = self.random_rot_y_num_field.get_value() * ((random.random() * 2) - 1)
        random_rot_z = self.random_rot_z_num_field.get_value() * ((random.random() * 2) - 1)
        random_rot = [random_rot_x, random_rot_y, random_rot_z]

        random_scale_x = self.random_scale_x_num_field.get_value() * ((random.random() * 2) - 1)
        if self.random_scale_uniform_check_box.get_value():
            random_scale_y = random_scale_x
            random_scale_z = random_scale_x
        else:
            random_scale_y = self.random_scale_y_num_field.get_value() * ((random.random() * 2) - 1)
            random_scale_z = self.random_scale_z_num_field.get_value() * ((random.random() * 2) - 1)
        random_scale = [random_scale_x, random_scale_y, random_scale_z]
        shape = self.shape_gui_list.get_selected_item_name()
        if shape == "Line":
            attributes = []
            values = []
            for i in range(0, 3):
                geo_bbox = self.get_transformed_bbox(geo)
                width = geo_bbox.get_sizes()[i]
                if previous_geo:
                    offset = previous_geo.attrs.translate[i] + previous_geo.attrs.translate_offset[i]
                    prev_geo_bbox = self.get_transformed_bbox(previous_geo)
                    previous_width = prev_geo_bbox.get_sizes()[i]
                    if self.unit_percentage_check_box.get_value():
                        offset += ((width / 2) + (previous_width / 2)) * spacing[i]
                    else:
                        offset += spacing[i]
                    if self.relative_checkboxes[i].get_value():
                        offset += (previous_width / 2) * previous_geo.attrs.scale[i] * previous_geo.attrs.scale_offset[i]
                else:
                    offset = 0
                if self.relative_checkboxes[i].get_value():
                    offset += (geo.attrs.translate_offset[i] * -1) + ((width / 2) * geo.attrs.scale[i] * geo.attrs.scale_offset[i] * abs(1 - random_scale[i]))
                offset += random_trans[i]
                if self.affect_position_check_box.get_value():
                    attributes.append(str(geo) + ".translate[" + str(i) + "]")
                    values.append(str(offset))
                if self.affect_rotation_check_box.get_value():
                    attributes.append(str(geo) + ".rotate[" + str(i) + "]")
                    values.append(str(rotate_offset[i] + random_rot[i]))
                if self.affect_scale_check_box.get_value():
                    attributes.append(str(geo) + ".scale[" + str(i) + "]")
                    values.append(str(abs(scale_offset[i] + 1-random_scale[i])))
            ix.cmds.SetValues(attributes, values)
        elif shape in ['Square', 'Rectangle', 'Circle']:
            attributes = []
            values = []
            other_axis = [0, 1, 2]
            axis = 0
            if self.y_check_box.get_value():
                axis = 1
            elif self.z_check_box.get_value():
                axis = 2
            other_axis.remove(axis)
            for i in range(0, 3):
                if shape == "Circle":
                    if self.circle_auto_radius_check_box.get_value():
                        circumference_pos = 0
                        for bbox_i, bbox in enumerate(bboxes):
                            if bbox_i >= index:
                                continue
                            circumference_pos += bbox
                        perc = circumference_pos / circumference
                        radius = circumference / (2 * math.pi)
                    else:
                        perc = float(index+1) / float(geometry_count)
                        radius = self.circle_radius_num_field.get_value()
                    rad = math.radians(perc * 360)
                    if i == axis:
                        offset = spacing[i]
                    elif i == other_axis[0]:
                        offset = (math.sin(rad) * radius) + spacing[i]
                    else:
                        offset = (math.cos(rad) * radius) + spacing[i]
                    attributes.append(str(geo) + ".translate[" + str(i) + "]")
                    values.append(str(offset))
                    if self.affect_rotation_check_box.get_value():
                        attributes.append(str(geo) + ".rotate[" + str(i) + "]")
                        if i == axis:
                            values.append(str(rotate_offset[i] + (perc * 360 + random_rot[i])))
                        else:
                            values.append(str(rotate_offset[i] + random_rot[i]))
                    if self.affect_scale_check_box.get_value():
                        attributes.append(str(geo) + ".scale[" + str(i) + "]")
                        values.append(str(abs(scale_offset[i] + 1-random_scale[i])))
                else:
                    if self.affect_position_check_box.get_value():
                        offset = 0
                        if axis == i:
                            offset = index * spacing[i]
                        elif i == other_axis[0]:
                            offset = spacing[i] * row
                        elif i == other_axis[1]:
                            offset = spacing[i] * col
                        offset += random_trans[i]
                        attributes.append(str(geo) + ".translate[" + str(i) + "]")
                        values.append(str(offset))
                    if self.affect_rotation_check_box.get_value():
                        attributes.append(str(geo) + ".rotate[" + str(i) + "]")
                        values.append(str(rotate_offset[i] + random_rot[i]))
                    if self.affect_scale_check_box.get_value():
                        attributes.append(str(geo) + ".scale[" + str(i) + "]")
                        values.append(str(abs(scale_offset[i] + 1-random_scale[i])))
            ix.cmds.SetValues(attributes, values)

    def run(self, instantiate=False):
        # Finding geometry
        geometry = []
        for selection in ix.selection:
            if selection.is_context():
                filter = str(selection) + "/*"
                ctx_geometry = ix.api.OfObjectVector()
                types = ix.api.CoreStringVector()
                types.add('Geometry')
                types.add('SceneObjectCombiner')
                ix.application.get_matching_objects(ctx_geometry, filter, ix.application.get_factory().get_root(), types)
                for ctx_geo in ctx_geometry:
                    geometry.append(ctx_geo)
            else:
                if selection.is_kindof("Geometry"):
                    geometry.append(selection)
                elif selection.is_kindof("SceneObjectCombiner"):
                    geometry.append(selection)
        final_selection = geometry
        if not geometry:
            return None
        ix.begin_command_batch("Distribute")
        # Instancing
        if instantiate:
            ctx = ix.api.IOHelpers.pick_context(ix.application, "Specify Target Context")
            if not ctx:
                ix.end_command_batch()
                return None
            if (not ctx.is_editable()) or ctx.is_content_locked() or ctx.is_remote():
                ix.log_warning("Cannot write to context, because it's locked.")
                ix.end_command_batch()
                return False
            instances_names = ix.cmds.CreateInstancesTo([str(geo) for geo in geometry], ctx)
            ix.application.check_for_events()
            instances = []
            for i, geo in enumerate(instances_names):
                instances.append(ix.get_item(geo))
            final_selection = instances
            if self.localize_check_box.get_value():
                for instance in instances:
                    ix.cmds.LocalizeAttributes([str(instance) + ".translate",
                                                str(instance) + ".rotate",
                                                str(instance) + ".scale"], True)
            if self.group_check_box.get_value():
                grp = ix.cmds.GroupItems([str(instance) for instance in instances], str(ctx))
                if self.combine_check_box.get_value():
                    combiner = ix.cmds.CombineItems([str(grp)], str(ctx))
                    final_selection = [combiner]
            if self.combine_check_box.get_value() and not self.group_check_box.get_value():
                combiner = ix.cmds.CombineItems([str(instance) for instance in instances], str(ctx))
                final_selection = [combiner]
            geometry = [instance for instance in instances]
            if not self.combine_check_box.get_value() and not self.group_check_box.get_value():
                final_selection = geometry

        # Sorting geometry
        sort_method = self.sort_gui_list.get_selected_item_name()
        if sort_method == 'Selection Order':
            pass
        elif sort_method == "Name":
            def get_name(elem):
                return elem.get_contextual_name()
            geometry.sort(key=get_name)
        elif sort_method == "Random":
            random.shuffle(geometry)
        elif sort_method == "Volume":
            def get_volume(elem):
                scale = elem.attrs.scale[0] * elem.attrs.scale[1] * elem.attrs.scale[2] * elem.attrs.scale_offset[0] * elem.attrs.scale_offset[1] * elem.attrs.scale_offset[2]
                return elem.get_module().get_bbox().compute_volume() * scale
            geometry.sort(key=get_volume)
        elif sort_method == "X - size":
            def get_x(elem):
                return abs(elem.get_module().get_bbox().get_min()[0] - elem.get_module().get_bbox().get_max()[0]) * elem.attrs.scale[0] * elem.attrs.scale_offset[0]
            geometry.sort(key=get_x)
        elif sort_method == "Y - size":
            def get_y(elem):
                return abs(elem.get_module().get_bbox().get_min()[1] - elem.get_module().get_bbox().get_max()[1]) * elem.attrs.scale[1] * elem.attrs.scale_offset[1]
            geometry.sort(key=get_y)
        elif sort_method == "Z - size":
            def get_z(elem):
                return abs(elem.get_module().get_bbox().get_min()[2] - elem.get_module().get_bbox().get_max()[2]) * elem.attrs.scale[2] * elem.attrs.scale_offset[2]
            geometry.sort(key=get_z)

        if self.reverse_check_box.get_value():
            geometry.reverse()

        # Placement
        geometry_count = len(geometry)
        shape = self.shape_gui_list.get_selected_item_name()
        if shape == "Line":
            previous_geo = None
            for i, geo in enumerate(geometry):
                self.transform(geo, i, previous_geo=previous_geo)
                previous_geo = geo
        elif shape in ["Square", "Rectangle"]:
            if shape == "Square":
                rows = int(math.ceil(math.sqrt(geometry_count)))
                cols = rows
            elif shape == "Rectangle":
                if self.rows_cols_gui_list.get_selected_item_name() == "Rows":
                    rows = int(self.rows_cols_num_field.get_value())
                    cols = int(math.ceil(geometry_count / float(rows)))
                else:
                    cols = int(self.rows_cols_num_field.get_value())
                    rows = int(math.ceil(geometry_count / float(cols)))
            current_index = 0
            for row in range(0, rows):
                for col in range(0, cols):
                    if current_index >= geometry_count:
                        break
                    geo = geometry[current_index]
                    self.transform(geo, current_index, row=row, col=col)
                    current_index += 1
        elif shape == "Circle":
            auto_radius = self.circle_auto_radius_check_box.get_value()
            bboxes = []
            circumference = 0
            if auto_radius:
                bbox_direction = self.circle_bbox_axis_gui_list.get_selected_item_index()
                for geo in geometry:
                    geo_bbox = self.get_transformed_bbox(geo, True)
                    width = geo_bbox.get_sizes()[bbox_direction]
                    # geo_rot.set_vec3d(ix.api.GMathVec3d(0,0,0))
                    # ix.application.check_for_events()
                    circumference_offset = width + self.circle_spacing_num_field.get_value()
                    circumference += circumference_offset
                    bboxes.append(circumference_offset)
            else:
                circumference = self.circle_radius_num_field.get_value()
            previous_geo = None
            for i, geo in enumerate(geometry):
                self.transform(geo, i, previous_geo=previous_geo, circumference=circumference, bboxes=bboxes, geometry_count=geometry_count)
                previous_geo = geo

        ix.application.check_for_events()
        ix.selection.deselect_all()
        for final_selection_item in final_selection:
            ix.selection.add(final_selection_item)
        ix.end_command_batch()

    def on_event(self, sender, evtid):
        if sender == self.close_button:
            sender.get_window().hide()
        elif sender == self.shape_gui_list:
            shape = self.shape_gui_list.get_selected_item_name()
            # Line widgets
            if shape == "Line":
                for line_widget in self.line_widgets:
                    line_widget.show()
            else:
                for line_widget in self.line_widgets:
                    line_widget.hide()
            # Square widgets
            if shape == "Square":
                for square_widget in self.square_widgets:
                    square_widget.show()
            else:
                for square_widget in self.square_widgets:
                    square_widget.hide()
            # Circle widgets
            if shape == "Circle":
                for circle_widget in self.circle_widgets:
                    circle_widget.show()
            else:
                for circle_widget in self.circle_widgets:
                    if shape in ["Square", "Rectangle"] and circle_widget in self.square_widgets:
                        continue
                    circle_widget.hide()
            # Rectangle widgets
            if shape == "Rectangle":
                for rectangle_widget in self.rectangle_widgets:
                    rectangle_widget.show()
            else:
                for rectangle_widget in self.rectangle_widgets:
                    if shape in ["Square", "Circle"] and rectangle_widget in self.square_widgets:
                        continue
                    rectangle_widget.hide()
        elif sender == self.circle_auto_radius_check_box:
            self.circle_radius_num_field.set_enable(not self.circle_auto_radius_check_box.get_value())
            self.circle_spacing_num_field.set_enable(self.circle_auto_radius_check_box.get_value())
        elif sender in self.axis_checkboxes:
            for checkbox in self.axis_checkboxes:
                if checkbox != sender:
                    checkbox.set_value(False)
            sender.set_value(True)
        elif sender == self.random_scale_uniform_check_box:
            self.random_scale_y_num_field.set_enable(not self.random_scale_uniform_check_box.get_value())
            self.random_scale_z_num_field.set_enable(not self.random_scale_uniform_check_box.get_value())
        elif sender == self.unit_m_check_box:
            self.unit_percentage_check_box.set_value(not self.unit_m_check_box.get_value())
        elif sender == self.unit_percentage_check_box:
            self.unit_m_check_box.set_value(not self.unit_percentage_check_box.get_value())
        elif sender in [self.run_button, self.run_instantiate_button]:
            self.run(sender == self.run_instantiate_button)


window = DistributeGui("Distribute Objects", 900, 450, 400, 670)
window.show()
while window.is_shown():
    ix.application.check_for_events()
window.destroy()
