import random
import colorsys

# ------------------------------------------------
# UI ---------------------------------------------
# ------------------------------------------------

options_name = "Set_Color_and_Keys"
options = ix.item_exists("project://" + options_name)

if options == None:

    options = ix.create_object(options_name, "ProjectItem", ix.get_item("project://"))

    mainColor = options.add_attribute("Start_Color", ix.api.OfAttr.TYPE_DOUBLE, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_COLOR, "Colors")
    offsetColor = options.add_attribute("End_Color", ix.api.OfAttr.TYPE_DOUBLE, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_COLOR, "Colors")
    keys = options.add_attribute("Keys", ix.api.OfAttr.TYPE_LONG, ix.api.OfAttr.CONTAINER_SINGLE, ix.api.OfAttr.VISUAL_HINT_DEFAULT, "Keys")

res = ix.application.inspect(options, ix.api.AppDialog.ok(), ix.api.AppDialog.STYLE_OK_CANCEL, "Settings")

# Retrieve values set in the UI -----------------

def getOptionStrin(Name):
    value = options.get_attribute(Name).get_string()
    return value

def getOptionDouble(Name):
    value = options.get_attribute(Name).get_double(0)
    return value

def getOptionColor(Name):
    r = options.get_attribute(Name).get_double(0)
    g = options.get_attribute(Name).get_double(1)
    b = options.get_attribute(Name).get_double(2)
    color = [r,g,b]
    return color


# --------------------------------------

def addGradientKey(attr, position, color):
    if isinstance(attr, str):
        attr = ix.item_exists(attr)
    if not attr:
        ix.log_warning("The specified attribute doesn't exists.")
        return False

    data = []
    if len(color) == 3:
        color.append(1)

    for i in range(len(color)):
        data.append(1.0)
        data.append(0.0)
        data.append(position)
        data.append(float(color[i]))

    ix.cmds.AddCurveValue([str(attr)], data)

    return True

# --------------------------------------

def randRange(min, max):
    return min + (max-min) * random.random()

# --------------------------------------

position = 0.0
mainColor = getOptionColor("Start_Color")
offsetColor = getOptionColor("End_Color")
keys = int(getOptionDouble("Keys"))

r = mainColor[0]
g = mainColor[1]
b = mainColor[2]
offsetR = offsetColor[0]
offsetG = offsetColor[1]
offsetB = offsetColor[2]

hsv = colorsys.rgb_to_hsv(r,g,b)
h = hsv[0]
s = hsv[1]
v = hsv[2]
offsetHSV = colorsys.rgb_to_hsv(offsetR,offsetG,offsetB)
offsetH = offsetHSV[0]
offsetS = offsetHSV[1]
offsetV = offsetHSV[2]


# --------------------------------------

interval = 1.0/float(keys)

gradient = ix.cmds.CreateObject("gradient", "TextureGradient")

for i in range(keys+1):
    newH = randRange(h,offsetH)
    newS = randRange(s,offsetS)
    newV = randRange(v,offsetV)
    color = list(colorsys.hsv_to_rgb(newH,newS,newV))

    addGradientKey(str(gradient) + ".output", position, color)

    position = position + interval

