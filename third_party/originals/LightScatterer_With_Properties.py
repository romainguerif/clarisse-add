### Light Scatterer ###
### Select an abc point cloud and run the script ###

sel = ix.selection[0]
ptc = sel.get_module()

# Point Cloud Property Listing -------------------------------

propList = []
propCount = 0
if ptc.get_properties() is not None:
    propCount = ptc.get_properties().get_property_count()

for i in range(propCount):
    propName = ptc.get_properties().get_property(i).get_name()
    propList.append(propName)

if "P" in propList:
    propList.remove("P")
if ".pointIds" in propList:
    propList.remove(".pointIds")

# ------------------------------------------------------------

def addPreset(Attribute, PropertyList):

    Attribute.add_preset("(none)", "")
    Attribute.set_string(Attribute.get_preset_value(0))

    for property in PropertyList:
        Attribute.add_preset(property, property)

# ------------------------------------------------------------

class LightInfo:
    def __init__(self):
        self.initialize()

    def initialize(self):
        self.color = ix.api.GMathVec3f()
        self.color[0] = self.color[1] = self.color[2] = 1.0
        self.position = ix.api.GMathVec3f()
        self.rotation = ix.api.GMathVec3f()
        self.scale = ix.api.GMathVec3f()
        self.scale[0] = self.scale[1] = self.scale[2] = 1.0
        self.exposure = 0.0

class PtcInfo:
    def __init__(self):
        self.ptc = None
        self.color = None
        self.position = None
        self.rotation = None
        self.scale = None
        self.exposure = None
        self.INVALID_PROPERTY = -1 + 2**32

    def initialize(self, ptc, color, rotation, scale, exposure):
        self.ptc = ptc
        self.color = None
        self.position = ptc.get_geometry().get_point_cloud()
        self.rotation = None
        self.scale = None
        self.exposure = None
        # find properties
        properties = self.ptc.get_properties()
        if properties:
            index = properties.get_property_index(color)
            if index != self.INVALID_PROPERTY:
                self.color = properties.get_property(index)

            index = properties.get_property_index(rotation)
            if index != self.INVALID_PROPERTY:
                self.rotation = properties.get_property(index)
#                self.rotation.load_data(0)

            index = properties.get_property_index(scale)
            if index != self.INVALID_PROPERTY:
                self.scale = properties.get_property(index)

            index = properties.get_property_index(exposure)
            if index != self.INVALID_PROPERTY:
                self.exposure = properties.get_property(index)

    def quat_to_euler(self, Quaternion):
        quat = ix.api.GMathQuat(Quaternion[3], ix.api.GMathVec3d(Quaternion[0],Quaternion[1],Quaternion[2]))
        rotMatrix = ix.api.GMathMatrix4x4d()
        quat.get_rotation(rotMatrix)
        rotation = ix.api.GMathVec3d()

        rotMatrix.compute_euler_angles(rotation, ix.api.GMATH_ROTATION_ORDER_ZXY)

        return rotation

    def set_light_info(self, index, light_info):
        if index < self.get_point_count():

            #get positions
            light_info.position = self.position.get_position(index)

            #get color
            if self.color != None:
                light_info.color = self.color.get_values_property(0).get_float(i)

            #get quaternion and convert to rotation
            if self.rotation != None:
                quaternion = self.rotation.get_values_property(0).get_float(i)
                light_info.rotation = self.quat_to_euler(quaternion)

            #get scale
            if self.scale != None:
                light_info.scale = self.scale.get_values_property(0).get_float(i)

            #get exposure
            if self.exposure != None:
                light_info.exposure = self.exposure.get_values_property(0).get_float(i)[0]

    def get_point_count(self):
        return self.ptc.get_point_count()

# Create Lights with myLight properties values --------

lightList= []

def createLight(Property, Type):
    if Type == "":
        Type = "LightPhysicalSphere"
    lgt = ix.cmds.CreateObject(Type + " ", Type, "Global", ctx)
    ix.cmds.SetValues([str(lgt) + ".translate"], [str(Property.position[0]), str(Property.position[1]), str(Property.position[2])])
    ix.cmds.SetValues([str(lgt) + ".rotate"], [str(Property.rotation[0]), str(Property.rotation[1]), str(Property.rotation[2])])
    ix.cmds.SetValues([str(lgt) + ".color"], [str(Property.color[0]), str(Property.color[1]), str(Property.color[2])])
    ix.cmds.SetValues([str(lgt) + ".scale"], [str(Property.scale[0]), str(Property.scale[1]), "1"])
    ix.cmds.SetValues([str(lgt) + ".exposure"], [str(Property.exposure)])
    lightList.append(str(lgt))


# Bake lights values over frames -----------------------

def bakeLight(lightList, myLight):

        lgto = ix.get_item(lightList[i])
        lgtStr = str(lgto)
        ix.cmds.SetKey([lgtStr + ".translate[0]", lgtStr + ".translate[1]", lgtStr + ".translate[2]"], T, [myLight.position[0], myLight.position[1], myLight.position[2]], 0)
        ix.cmds.SetKey([lgtStr + ".rotate[0]", lgtStr + ".rotate[1]", lgtStr + ".rotate[2]"], T, [myLight.rotation[0], myLight.rotation[1], myLight.rotation[2]], 0)
        ix.cmds.SetKey([lgtStr + ".color[0]", lgtStr + ".color[1]", lgtStr + ".color[2]"], T, [myLight.color[0], myLight.color[1], myLight.color[2]], 0)
        ix.cmds.SetKey([lgtStr + ".scale[0]", lgtStr + ".scale[1]", lgtStr + ".scale[2]"], T, [myLight.scale[0], myLight.scale[1], 1.0], 0)
        ix.cmds.SetKey([lgtStr + ".exposure[0]"], T, [myLight.exposure], 0)



# UI Creation and populate -----------------------------

options_name = "LightScattererOptions"
options = ix.item_exists("project://default" + options_name)
typeList = ['LightPhysicalSphere','LightPhysicalCylinder','LightPhysicalPlane','LightPhysicalSpot','LightPhysicalDistant']

if options == None:

    options = ix.create_object(options_name, "ProjectItem", ix.get_item("project://default"))
    options.set_private(True)
    options.set_static(True)

    colorOption = options.add_attribute("Color", ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Attributes Settings")
    addPreset(colorOption, propList)
    orientOption = options.add_attribute("Orient", ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Attributes Settings")
    addPreset(orientOption, propList)
    scaleOption = options.add_attribute("Scale", ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Attributes Settings")
    addPreset(scaleOption, propList)
    exposureOption = options.add_attribute("Exposure", ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Attributes Settings")
    addPreset(exposureOption, propList)

    lightTypeOption = options.add_attribute("Light_Type", ix.api.OfAttr.TYPE_STRING, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Lights")
    addPreset(lightTypeOption, typeList)

    animateCheckbox = options.add_attribute("Bake_Animation", ix.api.OfAttr.TYPE_BOOL, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Animation Baking")
    animateFirstFrame = options.add_attribute("First_Frame", ix.api.OfAttr.TYPE_DOUBLE, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Animation Baking")
    animateLastFrame = options.add_attribute("Last_Frame", ix.api.OfAttr.TYPE_DOUBLE, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_FOLDER, "Animation Baking")

res = ix.application.inspect(options, ix.api.AppDialog.ok(), ix.api.AppDialog.STYLE_OK_CANCEL, "Light Scatterer Options")



# Retrieve values set in the UI -----------------

def getOptionString(Name):
    value = options.get_attribute(Name).get_string()
    return value

def getOptionDouble(Name):
    value = options.get_attribute(Name).get_double()
    return value

colorAttr = getOptionString("Color")
orientAttr = getOptionString("Orient")
scaleAttr = getOptionString("Scale")
exposureAttr = getOptionString("Exposure")
lightType = getOptionString("Light_Type")
animated = getOptionString("Bake_Animation")
firstFrame = getOptionDouble("First_Frame")
lastFrame = getOptionDouble("Last_Frame")


### Execute --------------------------------------

if res.get_value() == 1:

    ctx = ix.cmds.CreateContext("Scattered_Lights", "", "project:/")

    myLight = LightInfo()
    myPTC = PtcInfo()

    myPTC.initialize(ptc, colorAttr, orientAttr, scaleAttr, exposureAttr)

    for i in range(int(myPTC.get_point_count())):
        myLight.initialize()
        myPTC.set_light_info(i, myLight) #copying property values from PTC to myLight
        createLight(myLight, lightType)

    if animated == "1":

        ix.cmds.SetCurrentFrame(firstFrame)

        while firstFrame <= lastFrame:

            for i in range(int(myPTC.get_point_count())):

                myPTC.initialize(ptc, colorAttr, orientAttr, scaleAttr, exposureAttr)
                myLight.initialize()
                myPTC.set_light_info(i, myLight)

                T = ix.application.get_factory().get_vars().get("T").get_double()

                bakeLight(lightList, myLight)

            firstFrame = firstFrame + 1
            ix.cmds.SetCurrentFrame(firstFrame)
            ix.application.check_for_events()

